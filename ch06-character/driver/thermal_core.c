
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include "thermal_uapi.h"

#define DEVICE_NAME "thermal_core"

static atomic_t thermal_state = ATOMIC_INIT(0);

#define THERMAL_EVENT_CAPACITY 8

static DEFINE_MUTEX(thermal_lock);
static DECLARE_WAIT_QUEUE_HEAD(thermal_waitq);

static struct thermal_core_event
    thermal_events[THERMAL_EVENT_CAPACITY];

static unsigned int thermal_head;
static unsigned int thermal_count;
static __u64 thermal_next_sequence = 1;


/*
 * Caller must hold thermal_lock.
 * Full queue policy: overwrite the oldest event.
 */
static void thermal_event_push_locked(unsigned int state)
{
    unsigned int index =
        (thermal_head + thermal_count) % THERMAL_EVENT_CAPACITY;
    struct thermal_core_event *event = &thermal_events[index];

    event->abi_version = THERMAL_UAPI_VERSION;
    event->state = state;
    event->sequence = thermal_next_sequence++;
    event->timestamp_ns = ktime_get_ns();

    if (thermal_count == THERMAL_EVENT_CAPACITY)
        thermal_head = (thermal_head + 1) % THERMAL_EVENT_CAPACITY;
    else
        thermal_count++;
}

/* Serialize state changes and event production. */
static void thermal_set_state(unsigned int new_state)
{
    bool changed = false;

    mutex_lock(&thermal_lock);

    if (atomic_read(&thermal_state) != (int)new_state) {
        atomic_set(&thermal_state, (int)new_state);
        thermal_event_push_locked(new_state);
        changed = true;
    }

    mutex_unlock(&thermal_lock);

    if (changed)
        wake_up_interruptible(&thermal_waitq);
}

static int thermal_open(struct inode *inode, struct file *file)
{
    return nonseekable_open(inode, file);
}

static int thermal_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t thermal_read(struct file *file,
                            char __user *buf,
                            size_t count,
                            loff_t *ppos)
{
    char data[2];

    data[0] = atomic_read(&thermal_state) ? '1' : '0';
    data[1] = '\n';

    return simple_read_from_buffer(buf, count, ppos,
                                   data, sizeof(data));
}

static ssize_t thermal_write(struct file *file,
                             const char __user *buf,
                             size_t count,
                             loff_t *ppos)
{
    char cmd;

    if (count == 0)
        return 0;

    if (count != 1)
        return -EINVAL;

    if (copy_from_user(&cmd, buf, 1))
        return -EFAULT;

    if (cmd != '0' && cmd != '1')
        return -EINVAL;

    thermal_set_state(cmd - '0');

    return 1;
}

/* Chapter 06C: the same ioctl numbers and layout are shared with userspace. */
static long thermal_ioctl(struct file *file, unsigned int cmd,
                          unsigned long arg)
{
    struct thermal_core_state req = { 0 };
    void __user *user_arg = (void __user *)arg;

    (void)file;

    switch (cmd) {
    case THERMAL_IOC_RESET:
        thermal_set_state(0);
        return 0;

    case THERMAL_IOC_GET_STATE:
        req.abi_version = THERMAL_UAPI_VERSION;
        req.state = (__u32)atomic_read(&thermal_state);
        if (copy_to_user(user_arg, &req, sizeof(req)))
            return -EFAULT;
        return 0;

    case THERMAL_IOC_SET_STATE:
        if (copy_from_user(&req, user_arg, sizeof(req)))
            return -EFAULT;
        if (req.abi_version != THERMAL_UAPI_VERSION ||
            req.state > 1 || req.reserved[0] || req.reserved[1])
            return -EINVAL;
        thermal_set_state(req.state);
        return 0;

    default:
        return -ENOTTY;
    }
}

/*
 * Shared destructive event stream.
 * Each successful read consumes exactly one 24-byte event.
 */
static ssize_t thermal_events_read(struct file *file,
                                   char __user *buf,
                                   size_t count,
                                   loff_t *ppos)
{
    struct thermal_core_event event;
    int ret;

    (void)ppos;

    if (count == 0)
        return 0;

    if (count < sizeof(event))
        return -EINVAL;

    for (;;) {
        ret = mutex_lock_interruptible(&thermal_lock);
        if (ret)
            return ret;

        if (thermal_count > 0)
            break;

        mutex_unlock(&thermal_lock);

        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        ret = wait_event_interruptible(
            thermal_waitq,
            READ_ONCE(thermal_count) > 0
        );
        if (ret)
            return ret;
    }

    event = thermal_events[thermal_head];

    /*
     * Do not remove an event when the userspace copy fails.
     * copy_to_user() may sleep, so use a mutex, not a spinlock.
     */
    if (copy_to_user(buf, &event, sizeof(event))) {
        mutex_unlock(&thermal_lock);
        return -EFAULT;
    }

    thermal_head =
        (thermal_head + 1) % THERMAL_EVENT_CAPACITY;
    thermal_count--;

    mutex_unlock(&thermal_lock);

    return sizeof(event);
}

static __poll_t thermal_events_poll(struct file *file,
                                    poll_table *wait)
{
    __poll_t mask = 0;

    poll_wait(file, &thermal_waitq, wait);

    mutex_lock(&thermal_lock);

    if (thermal_count > 0)
        mask = EPOLLIN | EPOLLRDNORM;

    mutex_unlock(&thermal_lock);

    return mask;
}

static const struct file_operations thermal_events_fops = {
    .owner   = THIS_MODULE,
    .open    = thermal_open,
    .release = thermal_release,
    .read    = thermal_events_read,
    .poll    = thermal_events_poll,
};

static struct miscdevice thermal_events_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "thermal_events",
    .fops  = &thermal_events_fops,
    .mode  = 0600,
};

static const struct file_operations thermal_fops = {
    .owner   = THIS_MODULE,
    .open    = thermal_open,
    .release = thermal_release,
    .read    = thermal_read,
    .write   = thermal_write,
    .unlocked_ioctl = thermal_ioctl,
};

static struct miscdevice thermal_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &thermal_fops,
    .mode  = 0600,
};

static int __init thermal_init(void)
{
    int ret;

    ret = misc_register(&thermal_device);
    if (ret)
        return ret;

    ret = misc_register(&thermal_events_device);
    if (ret) {
        misc_deregister(&thermal_device);
        return ret;
    }

    pr_info("thermal_core: registered\n");
    return 0;
}

static void __exit thermal_exit(void)
{
    misc_deregister(&thermal_events_device);
    misc_deregister(&thermal_device);
    pr_info("thermal_core: unregistered\n");
}

module_init(thermal_init);
module_exit(thermal_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("triton0305");
MODULE_DESCRIPTION("Virtual Thermal Control Character Driver");

