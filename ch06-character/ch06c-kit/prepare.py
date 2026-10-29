#!/usr/bin/env python3
"""Apply CH06C to the existing CH06A project, with verification and backups.

No Git operations, kernel-image changes, rootfs/image changes, or rmmod/insmod.
"""
from __future__ import annotations

import re
import shutil
from pathlib import Path

KIT = Path(__file__).resolve().parent
PROJECT = KIT.parent
YOCTO = Path.home() / "study/rootfs/yocto/bitbake-builds/poky-wrynose"
RECIPE_ROOT = YOCTO / "layers/meta-yocto-lab/recipes-kernel"
DRIVER = PROJECT / "driver/thermal_core.c"
MAKEFILE = PROJECT / "driver/Makefile"
UAPI = PROJECT / "include/thermal_uapi.h"
APP = PROJECT / "userspace/thermal_ioctl_test.c"
CORE_BB = RECIPE_ROOT / "thermal-core/thermal-core_1.0.bb"
CORE_FILES = RECIPE_ROOT / "thermal-core/files"
APP_BB = RECIPE_ROOT / "thermal-ioctl-test/thermal-ioctl-test_1.0.bb"
APP_FILES = APP_BB.parent / "files"
BACKUPS = KIT / "backups"


def fail(message: str) -> None:
    raise SystemExit("[ERROR] " + message)


def backup_once(src: Path) -> None:
    BACKUPS.mkdir(parents=True, exist_ok=True)
    dst = BACKUPS / (src.name + ".pre06c")
    if not dst.exists():
        shutil.copy2(src, dst)
        print(f"[BACKUP] {src} -> {dst}")


def copy_new_or_matching(src: Path, dst: Path) -> None:
    data = src.read_bytes()
    if dst.exists() and dst.read_bytes() != data:
        fail(f"Existing file differs; not overwriting: {dst}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists():
        dst.write_bytes(data)
        print(f"[NEW] {dst}")


for path in (DRIVER, MAKEFILE, CORE_BB, KIT / "templates/thermal_uapi.h"):
    if not path.is_file():
        fail(f"Required file not found: {path}")

# The existing module recipe must still follow the 06A structure reported in the handoff.
bb = CORE_BB.read_text()
if not all(t in bb for t in ('file://thermal_core.c', 'file://Makefile', 'inherit module', '${UNPACKDIR}')):
    fail("Unexpected thermal-core recipe format; inspect it before changing anything")

src = DRIVER.read_text()
if 'static atomic_t thermal_state = ATOMIC_INIT(0);' not in src or 'simple_read_from_buffer' not in src:
    fail("The original 06A driver no longer matches expected essentials; not changing it")
if '.llseek = no_llseek' in src:
    fail("Obsolete no_llseek found in driver; not changing it")
if 'static long thermal_ioctl(' not in src:
    if '#include <linux/errno.h>' not in src:
        fail("Cannot locate expected include anchor in thermal_core.c")
    if 'static const struct file_operations thermal_fops = {' not in src:
        fail("Cannot locate file_operations anchor in thermal_core.c")
    write_op = re.compile(r'(?m)^([ \t]*)\.write[ \t]*=[ \t]*thermal_write,[ \t]*$')
    if len(write_op.findall(src)) != 1:
        fail("Expected exactly one '.write = thermal_write' initializer")

    backup_once(DRIVER)
    src = src.replace('#include <linux/errno.h>',
                      '#include <linux/errno.h>\n#include "thermal_uapi.h"', 1)
    src = src.replace('static const struct file_operations thermal_fops = {',
                      (KIT / 'templates/thermal_ioctl.inc').read_text()
                      + 'static const struct file_operations thermal_fops = {', 1)
    src = write_op.sub(lambda m: m.group(0) + '\n' + m.group(1)
                       + '.unlocked_ioctl = thermal_ioctl,', src, count=1)
    DRIVER.write_text(src)
    print("[PATCH] Added 06C ioctl handler to driver/thermal_core.c")
else:
    if ('#include "thermal_uapi.h"' not in src or
            '.unlocked_ioctl = thermal_ioctl' not in src):
        fail("Existing ioctl appears partially modified; inspect manually")
    print("[SKIP] 06C ioctl handler is already present")

make = MAKEFILE.read_text()
if 'ccflags-y += -I$(src)/../include' not in make:
    if 'obj-m += thermal_core.o' not in make:
        fail("Unexpected driver Makefile; cannot add UAPI include path")
    backup_once(MAKEFILE)
    make = make.replace('obj-m += thermal_core.o',
                        'ccflags-y += -I$(src)/../include\nobj-m += thermal_core.o', 1)
    MAKEFILE.write_text(make)
    print("[PATCH] Added local UAPI include path to driver/Makefile")

copy_new_or_matching(KIT / 'templates/thermal_uapi.h', UAPI)
copy_new_or_matching(KIT / 'templates/thermal_ioctl_test.c', APP)

# Recipe files are staging copies; they intentionally track the current source.
CORE_FILES.mkdir(parents=True, exist_ok=True)
for filename in ('thermal_core.c', 'Makefile'):
    shutil.copy2(PROJECT / 'driver' / filename, CORE_FILES / filename)
shutil.copy2(UAPI, CORE_FILES / 'thermal_uapi.h')
if 'file://thermal_uapi.h' not in bb:
    backup_once(CORE_BB)
    with CORE_BB.open('a') as f:
        f.write('\n# Chapter 06C: shared fixed-width ioctl API\n')
        f.write('SRC_URI:append = " file://thermal_uapi.h"\n')
    print('[PATCH] thermal-core recipe now includes thermal_uapi.h')

APP_FILES.mkdir(parents=True, exist_ok=True)
shutil.copy2(KIT / 'yocto-recipe/thermal-ioctl-test_1.0.bb', APP_BB)
shutil.copy2(APP, APP_FILES / 'thermal_ioctl_test.c')
shutil.copy2(UAPI, APP_FILES / 'thermal_uapi.h')
print('[STAGE] thermal-core + thermal-ioctl-test recipes prepared')
print('[OK] No images, existing 06B tests, or hello-drv were modified')
