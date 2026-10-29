/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef THERMAL_UAPI_H
#define THERMAL_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Local educational UAPI; new incompatible layouts use new ioctl numbers. */
#define THERMAL_UAPI_VERSION 1U
#define THERMAL_IOC_MAGIC 'T'

/* Fixed-width, pointer-free 16-byte ABI shared with native ARM64 userspace. */
struct thermal_core_state {
    __u32 abi_version;
    __u32 state;         /* 0 = off, 1 = on */
    __u32 reserved[2];   /* SET: must be zero; GET: returned zero */
};

#define THERMAL_IOC_RESET     _IO(THERMAL_IOC_MAGIC, 0x00)
#define THERMAL_IOC_GET_STATE _IOR(THERMAL_IOC_MAGIC, 0x01, struct thermal_core_state)
#define THERMAL_IOC_SET_STATE _IOW(THERMAL_IOC_MAGIC, 0x02, struct thermal_core_state)

struct thermal_core_event {
    __u32 abi_version;
    __u32 state;
    __u64 sequence;
    __u64 timestamp_ns;
};

#endif /* THERMAL_UAPI_H */
