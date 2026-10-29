/* Chapter 06C: the same ioctl numbers and layout are shared with userspace. */
static long thermal_ioctl(struct file *file, unsigned int cmd,
                          unsigned long arg)
{
    struct thermal_core_state req = { 0 };
    void __user *user_arg = (void __user *)arg;

    (void)file;

    switch (cmd) {
    case THERMAL_IOC_RESET:
        atomic_set(&thermal_state, 0);
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
        atomic_set(&thermal_state, (int)req.state);
        return 0;

    default:
        return -ENOTTY;
    }
}

