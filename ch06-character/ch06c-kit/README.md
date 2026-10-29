# Chapter 06C — ioctl, versioned UAPI, and regression checks

**Purpose:** Add `_IO/_IOR/_IOW` commands to the already-tested `/dev/thermal_core` while preserving all original `read/write` behavior and the 06B 19-test regression baseline.

## Package content

- `templates/thermal_uapi.h`: single shared 16-byte, fixed-width UAPI layout.
- `templates/thermal_ioctl.inc`: ioctl implementation inserted before `thermal_fops`.
- `templates/thermal_ioctl_test.c`: ioctl/errno/EFAULT + legacy-I/O checks, emits PASS/FAIL and nonzero exit code on failure.
- `prepare.py`: checks original anchors, backs up files to `ch06c-kit/backups`, updates driver plus Makefile, stages sources to existing `thermal-core` recipe, and creates a new `thermal-ioctl-test` recipe.
- `build_yocto.sh`: invokes prepare, `bitbake thermal-core thermal-ioctl-test`, checks ARM64 ELF and stages two artifacts to the existing Windows HTTP share.

### WSL Ubuntu (`kccistc`) — after saving the archive to Downloads

```sh
tar -xzf /mnt/c/Users/kccistc/Downloads/thermal-06c-kit.tar.gz -C ~/study/linux-system-programming/ch06-character
bash ~/study/linux-system-programming/ch06-character/ch06c-kit/build_yocto.sh
```

### Architecture / ABI contract

```c
struct thermal_core_state {
    __u32 abi_version;
    __u32 state;
    __u32 reserved[2];
};  // 16 bytes
```

- `THERMAL_IOC_RESET` (`_IO`): reset to 0, no arg.
- `THERMAL_IOC_GET_STATE` (`_IOR`): kernel copies `{version=1, state=0|1, reserved=0}` to user memory, returns 0.
- `THERMAL_IOC_SET_STATE` (`_IOW`): user passes version=1, state 0|1, reserved zeros; valid sets value and returns 0; bad input `EINVAL`.
- Unknown request / mismatched `_IOC_SIZE`: `ENOTTY`.
- Protected user page (`mmap(PROT_NONE)`): `EFAULT` for SET and GET; never dereference an invalid pointer in user code.
- Only native ARM64 tested here; 32-bit compat ioctl is out of scope. Version field is a validation field **not** a license to redefine the same ioctl layout incompatibly. Add new ioctl command numbers when the ABI layout evolves.

**Testing boundary:** This archive is source/build tooling. No ARM64 Target execution has been performed in this environment. Do not claim Chapter 06C PASS until the newly built module has been loaded on Raspberry Pi, ioctl tester passes, and 06B `thermal_test` is rerun successfully.

### Pi test order (after reviewing ARM64 build output)

1. Verify downloaded files and matching `modinfo -F vermagic` with `uname -r`.
2. `rmmod thermal_core` (only if loaded and no active FD), then `insmod /tmp/thermal_core_06c.ko`.
3. Run `/tmp/thermal_ioctl_test`, check `SUMMARY` and `echo $?`.
4. Run previously tested `/tmp/thermal_test` to recheck 06B (19/19).
5. `rmmod thermal_core` on cleanup if desired; do not change the boot image.

The existing 06A `.ko` in the Windows share is **not** overwritten. The 06B C source, `hello-drv`, `ch05-memory`, Yocto images, and boot setup are untouched. Git commit and push are intentionally omitted.
