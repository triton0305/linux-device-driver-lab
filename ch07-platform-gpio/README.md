# Chapter 07 — 8-Channel Platform GPIO Driver

**Raspberry Pi 4B에서 Device Tree와 Linux Platform Driver를 연결해 8개 LED·버튼 채널을 독립 제어합니다.**

하나의 `lab_gpio.ko`가 8개 Platform Device에 바인딩하고, 장치별 Character Interface를 제공합니다. 사용자 공간 C 앱은 버튼 입력을 읽고 디바운스를 적용해 해당 채널의 LED를 토글합니다.

**C · Linux 6.18.33-v8 · ARM64 · Device Tree · GPIO Descriptor · Yocto**

## 핵심 구현

| 구현 | 주요 내용 |
|---|---|
| 다중 장치 | 장치별 GPIO descriptor, 상태, mutex, 참조 카운트 분리 |
| 장치 수명 관리 | `kref`로 열린 FD의 수명 유지, remove 이후 I/O에 `ENODEV` 반환 |
| 자원 정리 | `devm_gpiod_get()`과 cleanup action을 이용한 GPIO 반환 및 출력 OFF 요청 |
| 공유 UAPI | `read/write/ioctl`, 고정 크기 구조체, ABI 버전·reserved 필드·입력 검사 |
| 8채널 앱 | 20 ms 샘플링 간격 설정, 3회 연속 입력 확인, 해제 후 재무장 |
| 실행 제어 | 백그라운드 실행, `flock` 기반 실행 조정, Unix Domain Socket을 통한 상태·종료 요청 |

## 실장비 검증 결과

2026-10-30 완료 현황.

| 항목 | 결과 |
|---|---|
| 8채널 구성 | 하나의 모듈로 8개 Platform Device 바인딩 및 8개 Character Device 독립 제어 |
| 실물 입출력 | 8채널 LED·버튼 동작 확인 |
| 통합 실행 | `all toggle start/stop` 동작 확인 |
| 재바인딩 | 기존 1채널에서 `unbind/rebind` 20회 확인 |

[검증 기록](CH07_VERIFICATION.md) · [구현 및 설계](CH07_IMPLEMENTATION.md)

## 실행

모듈과 8채널 Overlay가 적용된 Pi에서 실행합니다. 장치 접근과 `/run/lab-gpio` 생성 권한이 필요합니다.

```sh
./lab_gpio_test all toggle start
./lab_gpio_test all toggle status
./lab_gpio_test all toggle stop
./lab_gpio_test all off
```

`start`는 전체 LED를 OFF로 초기화하고 한 프로세스에서 8개 장치를 관리합니다. 버튼을 누르면 해당 LED가 한 번 토글되고, 버튼을 뗀 뒤 다음 누름을 받습니다. 그룹 구성원 하나에 `off` 또는 `toggle stop`을 요청해도 전체 그룹이 종료됩니다.

<details>
<summary><strong>개별 채널 명령</strong></summary>

D0 장치 이름은 `/dev/lab_gpio-lab-gpio`, D1–D7은 `/dev/lab_gpio-lab-gpio-d1`부터 `-d7`까지입니다.

```sh
./lab_gpio_test /dev/lab_gpio-lab-gpio get
./lab_gpio_test /dev/lab_gpio-lab-gpio on
./lab_gpio_test /dev/lab_gpio-lab-gpio off
./lab_gpio_test /dev/lab_gpio-lab-gpio watch
./lab_gpio_test /dev/lab_gpio-lab-gpio selftest
./lab_gpio_test /dev/lab_gpio-lab-gpio hold
```

- `watch`: 100 ms 간격으로 300회 읽고, 10회마다 상태를 출력합니다.
- `selftest`: LED를 구동하며 read/write/ioctl과 오류 처리를 검사합니다.
- `hold`: FD를 유지한 채 수동 unbind를 기다리고, 제거 후 기존 FD의 오류 반환을 검사합니다.
- `on`·`selftest`: 토글 컨트롤러 종료 후 실행합니다.
- `get`의 LED 값은 마지막으로 성공한 논리 출력 요청 값입니다.

</details>

## 소스 안내

| 경로 | 역할 |
|---|---|
| [driver/lab_gpio.c](driver/lab_gpio.c) | Platform Driver, GPIO 획득, misc device, 수명 관리 |
| [include/lab_gpio_uapi.h](include/lab_gpio_uapi.h) | LED 설정·버튼 조회용 공유 ABI |
| [userspace/lab_gpio_test.c](userspace/lab_gpio_test.c) | 개별 제어, selftest, 다중 채널 토글 앱 |
| [dt/lab-gpio-eight-overlay.dts](dt/lab-gpio-eight-overlay.dts) | 8개 장치의 GPIO·극성 정의 |
| [tests/](tests/) | DT 오프라인 검사와 mock 통합 테스트 |
| [scripts/](scripts/) | 기존 Yocto 환경을 이용한 빌드와 장비 조사 |
| [yocto/](yocto/) | 모듈·앱 BitBake recipe |

<details>
<summary><strong>빌드 및 Overlay 적용</strong></summary>

기존 WSL Yocto 환경의 toolchain, target sysroot, kernel artifacts를 재사용합니다.

```sh
# 저장소 루트에서 모듈·앱·단일 채널 Overlay 빌드
bash ch07-platform-gpio/scripts/build-existing.sh

# 앱과 8채널 Overlay 빌드 및 오프라인 DT 병합 검사
bash ch07-platform-gpio/scripts/build-eight-existing.sh
```

기본 경로는 `$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose`입니다. 다른 위치에서는 `YOCTO` 환경 변수로 지정합니다.

| 스크립트 | 주요 산출물 |
|---|---|
| `build-existing.sh` | `driver/lab_gpio.ko`, `build/lab_gpio_test`, `build/lab-gpio.dtbo`, `build/merged-offline.dtb` |
| `build-eight-existing.sh` | `build/lab_gpio_test`, `build/lab-gpio-eight-disabled.dtbo`, `build/lab-gpio-eight-validation-active.dtbo`, 각 variant의 `.merged.dtb` |

저장소 Overlay는 `disabled` 템플릿입니다. 8채널 스크립트는 `okay` 사본을 만들어 파일상 병합도 검사합니다. 대상 장비 적용은 다음 순서로 진행합니다.

1. `scripts/inspect-target.sh`로 커널 버전, live DT, GPIO consumer, pinmux와 부팅 구성을 확인합니다.
2. GPIO 배선·극성·입력 bias를 대조하고, 기존 부팅 설정과 Overlay를 백업합니다. Pi GPIO는 3.3 V를 사용하며 LED 전류 제한을 구성합니다.
3. 장비용 Overlay의 노드를 활성화하고 실제 부팅 경로에 적용합니다. 8채널 Overlay는 기존 단일 채널 Overlay를 교체합니다.
4. 부팅 후 live DT와 `/dev/lab_gpio-*`, 모듈의 vermagic, `dmesg`를 확인합니다.
5. 설정 복구 시 백업을 복원하고 재부팅합니다.

펌웨어·U-Boot 구성에 따라 커널에 전달되는 DT를 확인합니다. BitBake 패키징용 recipe는 `yocto/`에 있으며, 이미지 통합은 [yocto-rpi4-lab](https://github.com/triton0305/yocto-rpi4-lab)에서 관리합니다.

</details>
