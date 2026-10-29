# Chapter 07 — Device Tree / Platform Driver / GPIO

Raspberry Pi 4B / Yocto ARM64 학습용 구현. Chapter 05/06 소스는 보존한다.
실제 결과는 [CH07_VERIFICATION.md](CH07_VERIFICATION.md), 설계·기존 코드 분석은
[CH07_IMPLEMENTATION.md](CH07_IMPLEMENTATION.md)에 기록한다.

## 현재 상태와 안전 조건

- ARM64 외부 모듈·앱 빌드 및 DT 오프라인 병합 완료. **HW VERIFIED 아님**.
- Overlay의 GPIO17/27은 **후보**이고 노드는 기본 `disabled`다.
- 실제 GPIO consumer/pinmux, 부팅 체인, 전기적 극성, 학습자 배선 확인 전에는
  Overlay를 복사·적용하거나 `status`를 활성화하지 않는다. 물리 핀 번호는 지정하지 않았다.
- IRQ, 커널 이벤트 큐, 자동 LED-버튼 연동은 없다. 앱이 100 ms 간격으로 입력을 읽는다.
- 전원 인가부터 probe 이전의 상태는 드라이버가 보장할 수 없다. 회로 검토 시 LED 전류 제한,
  입력의 확정된 외부 bias, 출력 비활성 상태를 확보해야 한다. 3.3 V GPIO에 5 V를 연결하지 않는다.

## 파일

| 경로 | 용도 |
|---|---|
| `driver/lab_gpio.c`, `driver/Makefile` | DT platform driver와 misc character interface |
| `include/lab_gpio_uapi.h` | LED 설정·버튼 샘플을 위한 고정 크기 ABI |
| `userspace/lab_gpio_test.c` | get/on/off/watch/selftest/hold |
| `dt/` | 비활성 Overlay 템플릿, 교육용 YAML binding |
| `scripts/build-existing.sh` | 기존 Yocto sysroot/toolchain/kernel artifacts만 재사용 |
| `scripts/inspect-target.sh` | 장비에서 수행할 읽기 전용 조사 |
| `yocto/` | 기존 meta-yocto-lab에 추가할 모듈·앱 recipe 원본 |

## 빌드 (WSL)

저장소 루트에서:

```sh
bash ch07-platform-gpio/scripts/build-existing.sh
```

기본 `YOCTO=$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose`.
다른 **기존** 위치이면 환경 변수로 지정한다. 준비된 thermal-core native sysroot와
thermal-event-test target sysroot, Pi 커널 artifacts가 없으면 중단한다.
새 toolchain/build tree 생성, kernel/DT 전체 rebuild, layer/config 변경은 하지 않는다.
출력은 `driver/lab_gpio.ko`, `build/lab_gpio_test`, `build/lab-gpio.dtbo`,
`build/merged-offline.dtb`다. 마지막 파일은 검사 전용이며 부팅 DTB로 설치하지 않는다.

선택적인 BitBake 패키징은 아래처럼 **새 recipe 디렉터리만** 만든다. 기존 경로가 있으면
먼저 비교하고 덮어쓰지 않는다. 이번 작업에서는 아래 단계는 실행하지 않았다.

```sh
# 저장소 루트에서, 기존 YOCTO 경로를 먼저 지정
P=$PWD/ch07-platform-gpio
L=$YOCTO/layers/meta-yocto-lab
# mkdir는 경로가 이미 있으면 실패하게 하여 기존 recipe 덮어쓰기 방지
mkdir "$L/recipes-kernel/lab-gpio"
mkdir -p "$L/recipes-kernel/lab-gpio/files/driver" "$L/recipes-kernel/lab-gpio/files/include"
cp "$P/yocto/lab-gpio_1.0.bb" "$L/recipes-kernel/lab-gpio/"
cp "$P/driver/lab_gpio.c" "$P/driver/Makefile" "$L/recipes-kernel/lab-gpio/files/driver/"
cp "$P/include/lab_gpio_uapi.h" "$L/recipes-kernel/lab-gpio/files/include/"
mkdir "$L/recipes-test/lab-gpio-test"
mkdir "$L/recipes-test/lab-gpio-test/files"
cp "$P/yocto/lab-gpio-test_1.0.bb" "$L/recipes-test/lab-gpio-test/"
cp "$P/userspace/lab_gpio_test.c" "$P/include/lab_gpio_uapi.h" "$L/recipes-test/lab-gpio-test/files/"
. "$YOCTO/layers/openembedded-core/oe-init-build-env" "$YOCTO/build"
bitbake lab-gpio lab-gpio-test
```

셸 오류 시 즉시 중단하도록 `set -e`가 활성화된 셸에서 수행한다.
`local.conf`, IMAGE_INSTALL, 기존 thermal recipe, autoload는 변경할 필요가 없다.

## 장비 조사와 Overlay 로딩 방식 결정

먼저 Pi에서 `sh inspect-target.sh > ch07-target-audit.txt`를 실행하고 결과를 검토한다.
실행 중인 `uname -r`, 모델, gpioinfo consumer, pinctrl mux/bias, debugfs 정보와 live DT를
대조한다. GPIO 번호는 전역 Linux 번호가 아니라 DT controller의 line offset이다.
부팅 설정의 `include`, 조건부 section, `os_prefix`, `overlay_prefix`, `device_tree`,
`kernel`, `arm_64bit` 및 실제 부팅 파티션을 확인한다. 설정 파일이 보인다는 이유만으로
현재 적용된 파일이라고 판단하지 않는다. 필요하면 SD 부팅 파티션과 boot 로그를 대조한다.

호스트 배포본 `bootfiles/config.txt`에는 `dtoverlay=vc4-kms-v3d`가 있다.
이것은 **호스트 이미지 근거**이며 현재 SD의 부팅 체인을 확정하지 않는다.

| 확인한 부팅 체인 | 적용 방식 검토 |
|---|---|
| Pi firmware가 DTB를 만들고 kernel에 전달 | 실제 config.txt에서 `dtoverlay=lab-gpio`; 실제 overlay_prefix 위치 사용 |
| firmware → U-Boot → kernel | U-Boot가 firmware DT를 전달하는지, 별도 FDT로 교체하는지 boot script/extlinux로 확인; 교체 시 firmware overlay가 사라질 수 있음 |
| 런타임 dtoverlay/configfs | kernel CONFIG_OF_OVERLAY/OF_CONFIGFS 외에 target 도구·mount·base symbols 확인 필요; 현재 자동 적용 대상 아님 |

기존 로컬 커널 config는 `CONFIG_OF_OVERLAY=y`, `CONFIG_OF_CONFIGFS=y`지만 target support
확정 근거로 쓰지 않는다. Chapter 07 첫 검증은 지원 확인 후 firmware 부팅 적용을 우선 검토한다.
펌웨어로 합쳐진 Overlay는 런타임 `dtoverlay -r`로 제거할 수 없으므로 설정 복구와 재부팅이 필요하다.
공식 근거: [Raspberry Pi Device Tree configuration](https://www.raspberrypi.com/documentation/computers/configuration.html).

## 적용 전 백업·복구 절차 (아직 실행 금지)

1. 위 조사, 후보 GPIO의 무점유 및 pinmux 확인, 회로·극성 검토, 학습자의 배선 확인을 마친다.
2. 확인한 부팅 파티션의 config.txt와 모든 include 파일, 기존 대상 dtbo(있다면),
   U-Boot script/extlinux(사용 시)를 날짜별 백업에 보관한다. SD를 호스트에서 수정할 수 있는
   복구 경로와 serial console을 준비한다. 파일 해시와 원래 경로를 남긴다.
3. 확인한 line offset·극성으로 Overlay 사본을 수정하고 `status = "okay"`로 바꾼다.
   재빌드·정식 binding 검사 후 **확인한** overlay 위치에만 설치한다.
   원본 disabled 템플릿과 기존 DTB는 보존한다. 실제 config의 맞는 조건부 section에서만 활성화한다.
4. 재부팅 후 live DT의 해당 노드와 GPIO properties를 확인한다. 그 다음 모듈을 로드한다.
5. 오류 시 앱을 종료하고 driver를 unbind/정상 unload한다. 설정과 기존 dtbo를 백업으로 복원하고
   재부팅한다. 부팅 불가 시 전원을 안전하게 차단하고 SD 부팅 파티션을 호스트에 연결해 복원한다.
   정상 baseline 부팅과 GPIO consumer 복구를 확인한다.

전원을 끄는 시점, 저항·배선·물리 핀의 실제 연결은 학습자 확인 후 결정한다.
강제 module unload, GPIO 소유자 강제 해제, 무검토 런타임 Overlay 제거는 사용하지 않는다.

## 승인된 장비 구성에서의 실행 예 (현재 BLOCKED)

```sh
uname -r                       # 빌드 vermagic과 비교
insmod ./lab_gpio.ko
ls -l /sys/bus/platform/drivers/lab-gpio/
ls -l /dev/lab_gpio-*
dmesg | tail -60
# 실제 생성된 이름을 사용: 이 템플릿은 보통 /dev/lab_gpio-lab-gpio
./lab_gpio_test /dev/lab_gpio-lab-gpio get
./lab_gpio_test /dev/lab_gpio-lab-gpio on
./lab_gpio_test /dev/lab_gpio-lab-gpio off
./lab_gpio_test /dev/lab_gpio-lab-gpio watch
./lab_gpio_test /dev/lab_gpio-lab-gpio selftest
```

비활성 템플릿은 device를 생성하지 않으므로 모듈 등록만 되고 probe되지 않는 것이 정상이다.
`selftest`는 LED를 짧게 구동하므로 회로 확인 후에만 실행한다.
`get`의 LED 값은 마지막 성공한 요청 상태이며 실제 발광 검증을 대신하지 않는다.
`on` 후 앱을 닫아도 출력은 유지된다. 종료 시 명시적 `off`를 수행한다.
자세한 unbind/rebind, 실패 주입, 반복 검증은 검증 문서의 절차를 따른다.
