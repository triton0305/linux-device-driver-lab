# Chapter 06B — Test plan

**Target:** Raspberry Pi 4B / Yocto ARM64 / Linux 6.18.33-v8  
**Device:** `/dev/thermal_core` (education-only virtual state device)

## Already observed on real hardware
- [x] `printf '12' | dd ...` rejected with `Invalid argument`; shell exit 1; state 0 unchanged.
- [x] `printf 'x' | dd ...` rejected with `Invalid argument`; shell exit 1; state 0 unchanged.

## Pending automated C tests (not yet verified on Pi)
- [ ] Success writes `'0'` and `'1'`: return 1; subsequent state matches.
- [ ] Write length 2: `write=-1`, `errno=EINVAL` and no state change.
- [ ] Write invalid value `x`: `write=-1`, `errno=EINVAL` and no state change.
- [ ] Write length 0: return 0, no state change.
- [ ] Read length 0: return 0, offset unchanged.
- [ ] Partial reads 1+1 byte; then EOF returns 0.
- [ ] Read with buffer larger than data: return 2 bytes.
- [ ] Protected user page write: `write=-1`, `errno=EFAULT`, no state change.
- [ ] Protected user page read: `read=-1`, `errno=EFAULT`.
- [ ] Test app returns 0 on all PASS, nonzero on FAIL.

## Evidence to save
- Yocto `bitbake thermal-test` task summary, ELF `Machine` line.
- Pi `uname -r`, `ls -l /dev/thermal_core`, app stdout and exit status.
- Distinguish **built**, **transferred**, **launched**, and **functional PASS**.
