// SPDX-License-Identifier: GPL-2.0-only
/* Chapter 06 misc/read/write validation pattern, adapted to per-device lifetime. */
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "lab_gpio_uapi.h"

struct lab_gpio {
	struct miscdevice misc;
	struct mutex lock;
	struct kref refs;
	struct gpio_desc *led, *button;
	bool dead;
	u32 led_state;
	char *name;
};

static void lab_free(struct kref *ref)
{
	struct lab_gpio *lab = container_of(ref, struct lab_gpio, refs);

	kfree(lab->name);
	kfree(lab);
}

static void lab_put(void *data)
{
	struct lab_gpio *lab = data;

	kref_put(&lab->refs, lab_free);
}

/* Runs before devm releases LED, including probe failures after acquisition. */
static void lab_safe_off(void *data)
{
	struct lab_gpio *lab = data;
	int ret = gpiod_set_value_cansleep(lab->led, 0);

	if (ret)
		pr_warn("lab_gpio: %s safe OFF failed: %d\n", lab->name, ret);
}

static int lab_open(struct inode *inode, struct file *file)
{
	struct lab_gpio *lab = container_of(file->private_data,
					 struct lab_gpio, misc);
	int ret = 0;

	/* misc_open serializes this callback with misc_deregister. */
	mutex_lock(&lab->lock);
	if (lab->dead) {
		ret = -ENODEV;
	} else {
		kref_get(&lab->refs);
		file->private_data = lab;
		nonseekable_open(inode, file);
	}
	mutex_unlock(&lab->lock);
	return ret;
}

static int lab_release(struct inode *inode, struct file *file)
{
	lab_put(file->private_data);
	return 0;
}

/* All descriptor use is locked against remove. */
static int lab_set_locked(struct lab_gpio *lab, u32 value)
{
	int ret = gpiod_set_value_cansleep(lab->led, value);

	if (!ret)
		lab->led_state = value;
	return ret;
}

static ssize_t lab_read(struct file *file, char __user *buf, size_t count,
			loff_t *pos)
{
	struct lab_gpio *lab = file->private_data;
	char data[2];
	ssize_t ret;

	mutex_lock(&lab->lock);
	if (lab->dead) {
		ret = -ENODEV;
	} else {
		data[0] = lab->led_state ? '1' : '0';
		data[1] = '\n';
		ret = simple_read_from_buffer(buf, count, pos, data, sizeof(data));
	}
	mutex_unlock(&lab->lock);
	return ret;
}

static ssize_t lab_write(struct file *file, const char __user *buf,
			size_t count, loff_t *pos)
{
	struct lab_gpio *lab = file->private_data;
	char cmd;
	ssize_t ret;

	mutex_lock(&lab->lock);
	if (lab->dead) {
		ret = -ENODEV;
	} else if (!count) {
		ret = 0;
	} else if (count != 1) {
		ret = -EINVAL;
	} else if (copy_from_user(&cmd, buf, 1)) {
		ret = -EFAULT;
	} else if (cmd != '0' && cmd != '1') {
		ret = -EINVAL;
	} else {
		ret = lab_set_locked(lab, cmd - '0');
		if (!ret)
			ret = 1;
	}
	mutex_unlock(&lab->lock);
	return ret;
}

static long lab_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct lab_gpio *lab = file->private_data;
	struct lab_gpio_state state = { .abi_version = LAB_GPIO_ABI_VERSION };
	struct lab_gpio_led req;
	void __user *user = (void __user *)arg;
	int ret = 0;

	mutex_lock(&lab->lock);
	if (lab->dead) {
		ret = -ENODEV;
		goto out;
	}
	switch (cmd) {
	case LAB_GPIO_IOC_OFF:
		ret = lab_set_locked(lab, 0);
		break;
	case LAB_GPIO_IOC_GET:
		ret = gpiod_get_value_cansleep(lab->button);
		if (ret < 0)
			break;
		state.button = !!ret;
		state.led = lab->led_state;
		ret = copy_to_user(user, &state, sizeof(state)) ? -EFAULT : 0;
		break;
	case LAB_GPIO_IOC_SET:
		if (copy_from_user(&req, user, sizeof(req))) {
			ret = -EFAULT;
			break;
		}
		if (req.abi_version != LAB_GPIO_ABI_VERSION || req.led > 1 ||
		    req.reserved[0] || req.reserved[1]) {
			ret = -EINVAL;
			break;
		}
		ret = lab_set_locked(lab, req.led);
		break;
	default:
		ret = -ENOTTY;
	}
out:
	mutex_unlock(&lab->lock);
	return ret;
}

static const struct file_operations lab_fops = {
	.owner = THIS_MODULE,
	.open = lab_open,
	.release = lab_release,
	.read = lab_read,
	.write = lab_write,
	.unlocked_ioctl = lab_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
};

static int lab_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lab_gpio *lab;
	int ret;

	/* Not devm_kzalloc: open FDs may outlive platform removal. */
	lab = kzalloc(sizeof(*lab), GFP_KERNEL);
	if (!lab)
		return -ENOMEM;
	mutex_init(&lab->lock);
	kref_init(&lab->refs);
	lab->name = kasprintf(GFP_KERNEL, "lab_gpio-%s", dev_name(dev));
	if (!lab->name) {
		lab_put(lab);
		return -ENOMEM;
	}
	/* First action is released last, after GPIO cleanup. */
	ret = devm_add_action_or_reset(dev, lab_put, lab);
	if (ret)
		return ret;
	/* Acquire input first: missing/busy button cannot change LED direction. */
	lab->button = devm_gpiod_get(dev, "button", GPIOD_IN);
	if (IS_ERR(lab->button))
		return dev_err_probe(dev, PTR_ERR(lab->button), "button GPIO acquisition failed\n");
	lab->led = devm_gpiod_get(dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(lab->led))
		return dev_err_probe(dev, PTR_ERR(lab->led), "LED GPIO acquisition failed\n");
	ret = devm_add_action_or_reset(dev, lab_safe_off, lab);
	if (ret)
		return ret;
	lab->misc.minor = MISC_DYNAMIC_MINOR;
	lab->misc.name = lab->name;
	lab->misc.fops = &lab_fops;
	lab->misc.parent = dev;
	lab->misc.mode = 0600;
	platform_set_drvdata(pdev, lab);
	ret = misc_register(&lab->misc);
	if (ret)
		return dev_err_probe(dev, ret, "misc registration failed\n");
	dev_info(dev, "bound; GPIOs acquired; logical LED OFF; /dev/%s\n", lab->name);
	return 0;
}

static void lab_quiesce(struct lab_gpio *lab)
{
	mutex_lock(&lab->lock);
	lab->dead = true;
	lab_safe_off(lab);
	mutex_unlock(&lab->lock);
}

static void lab_remove(struct platform_device *pdev)
{
	struct lab_gpio *lab = platform_get_drvdata(pdev);

	lab_quiesce(lab);
	/* Never hold lab->lock across misc_deregister (open lock ordering). */
	misc_deregister(&lab->misc);
	dev_info(&pdev->dev, "unbound; old FDs return ENODEV; GPIO cleanup follows\n");
}

static void lab_shutdown(struct platform_device *pdev)
{
	lab_quiesce(platform_get_drvdata(pdev));
}

static const struct of_device_id lab_of_match[] = {
	{ .compatible = "yocto-lab,gpio-demo" },
	{ }
};
MODULE_DEVICE_TABLE(of, lab_of_match);

static struct platform_driver lab_driver = {
	.probe = lab_probe,
	.remove = lab_remove, /* Linux 6.18: void return */
	.shutdown = lab_shutdown,
	.driver = {
		.name = "lab-gpio",
		.of_match_table = lab_of_match,
	},
};
module_platform_driver(lab_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("triton0305");
MODULE_DESCRIPTION("Chapter 07 DT GPIO LED and sampled button character device");
