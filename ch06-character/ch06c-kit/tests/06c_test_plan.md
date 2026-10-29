# 06C test plan — pending real Raspberry Pi verification

Target: Pi 4B, Yocto, `6.18.33-v8` (must check actual `uname -r` before loading).

Expected native `thermal_ioctl_test` checks:
- ABI structure is 16 bytes
- RESET returns 0, GET returns version 1/state 0; legacy read confirms 0
- SET 1 returns 0; GET and legacy read confirm 1
- Invalid state 2, bad ABI version 2, nonzero reserved field -> -1/EINVAL; state unchanged
- Unknown ioctl and wrong encoded size -> -1/ENOTTY
- SET/GET protected page -> -1/EFAULT; state unchanged
- Legacy `write("0", 1)` -> 1, GET confirms state 0
- Final SET 0, GET 0
- `SUMMARY: all PASS, 0 FAIL` and exit code 0
- Rerun `thermal_test`: 19/19 PASS, exit 0 (regression check)

Strict gates: build PASS, transfer PASS, insmod PASS, new function PASS, old regression PASS are **distinct**. Capture Pi logs before marking done.
