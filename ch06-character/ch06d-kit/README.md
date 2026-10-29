# Chapter 06D userspace test kit

This kit is prepared for `linux-system-programming/ch06-character` and the existing Yocto Poky Wrynose `meta-yocto-lab` layer.

Files:
- `thermal_event_test.c` — D01–D16 kernel-facing tests. Reuses the existing project `include/thermal_uapi.h`.
- `thermal-event-test_1.0.bb` — separate test application recipe. Does not modify `thermal-test` or `thermal-core` recipes.
- `build_06d_yocto.sh` — copies test sources to a separate recipe and builds/stages the ARM64 executable in the Windows share.

Copy the `.c` into `ch06-character/userspace/`, the `.bb` into `ch06-character/yocto-recipe/`, and the script into `ch06-character/userspace/`. Run the script from WSL using `bash userspace/build_06d_yocto.sh`. Do not execute the tests on WSL; run `thermal_event_test` as root on the Pi with the 06D module installed.

Important: This automated test **destructively consumes** the shared event queue. Do not run another event reader or producer while tests run. The program resets state and drains existing events before scenarios. The overflow test validates a relative eight-event tail; absolute seq 3–10 requires a freshly loaded 06D module and no previous producers. Test D16 checks integrity of two competing readers, not fair distribution.

Host syntax compilation is not a Yocto/ARM64 build and does not validate kernel runtime. The normal test app exit code is 0 only when all 16 test scenarios pass. Existing 06B/06C regression tests remain separate.
