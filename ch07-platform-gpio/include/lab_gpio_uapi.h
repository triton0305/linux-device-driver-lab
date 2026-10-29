/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef LAB_GPIO_UAPI_H
#define LAB_GPIO_UAPI_H
#include <linux/ioctl.h>
#include <linux/types.h>
#define LAB_GPIO_ABI_VERSION 1U
/* Private educational ABI, distinct from THERMAL_IOC_MAGIC. */
struct lab_gpio_state {
	__u32 abi_version;
	__u32 led; /* last successfully requested logical output, not optical feedback */
	__u32 button; /* sampled logical input: 1 pressed, 0 released */
	__u32 reserved;
};
struct lab_gpio_led {
	__u32 abi_version;
	__u32 led;
	__u32 reserved[2];
};
#define LAB_GPIO_IOC_OFF _IO('G', 0x00)
#define LAB_GPIO_IOC_GET _IOR('G', 0x01, struct lab_gpio_state)
#define LAB_GPIO_IOC_SET _IOW('G', 0x02, struct lab_gpio_led)
#endif
