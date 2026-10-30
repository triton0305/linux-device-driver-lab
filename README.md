# Linux System Programming & Device Drivers

**Raspberry Pi 4B에서 Linux 시스템 호출, 커널 자원 관리, Character Driver, Device Tree 기반 8채널 GPIO 제어를 구현한 실습 저장소입니다.**

사용자 공간의 파일 I/O와 동시성에서 출발해 커널·사용자 공간 인터페이스를 구성하고, 하나의 Platform Driver로 여러 GPIO 장치를 독립 제어하도록 확장했습니다.

**C · Linux Kernel · ARM64 · Raspberry Pi 4B · Device Tree · Yocto**

## 핵심 구현

| 영역 | 구현 내용 | 코드 |
|---|---|---|
| **8채널 GPIO 제어** | 하나의 모듈이 8개 Platform Device에 바인딩하고, 장치별 Character Interface로 LED·버튼 제어 | [Platform Driver](ch07-platform-gpio/driver/lab_gpio.c) |
| **장치 제거와 수명 관리** | `kref`로 열린 FD의 수명 유지, `mutex`로 I/O와 remove 직렬화, 제거 후 기존 FD에 `ENODEV` 반환 | [장치 수명 관리](ch07-platform-gpio/driver/lab_gpio.c) |
| **이벤트 기반 Character Driver** | `read/write/ioctl`, 고정 크기 UAPI, Ring Buffer, Wait Queue, `poll` | [thermal_core.c](ch06-character/driver/thermal_core.c) |
| **사용자 공간 제어 앱** | 8채널 버튼 샘플링·디바운스·LED 토글, 백그라운드 실행, Unix Domain Socket을 통한 종료 제어 | [lab_gpio_test.c](ch07-platform-gpio/userspace/lab_gpio_test.c) |
| **실패 복구** | 메모리 할당 단계별 실패 주입, 역순 자원 해제, 초기화 실패 경로 구성 | [chain_lab.c](ch05-memory/chain_lab.c) |
| **검증 도구** | 잘못된 입력·ABI 검사, 이벤트 큐 overflow, 다중 producer/consumer, 지연 측정용 테스트 프로그램 | [Character Driver 테스트](ch06-character/stress/) · [GPIO 테스트](ch07-platform-gpio/tests/) |

## 대표 구현 — 8채널 Platform GPIO Driver

Device Tree에서 LED·버튼 GPIO를 정의하고, 동일한 `compatible`을 사용하는 8개 장치를 하나의 `lab_gpio.ko`로 처리합니다. 각 장치는 상태·잠금·참조 카운트를 별도로 보유합니다.

| 구성 요소 | 역할 |
|---|---|
| [Device Tree Overlay](ch07-platform-gpio/dt/lab-gpio-eight-overlay.dts) | 8개 장치의 LED·버튼 GPIO와 극성 정의 |
| [Platform Driver](ch07-platform-gpio/driver/lab_gpio.c) | `probe/remove`, GPIO descriptor 획득·반환, 장치별 misc device 등록 |
| [공유 UAPI](ch07-platform-gpio/include/lab_gpio_uapi.h) | LED 설정·버튼 상태 조회용 고정 크기 구조체와 ioctl 명령 |
| [C 애플리케이션](ch07-platform-gpio/userspace/lab_gpio_test.c) | 개별 제어, API selftest, 다중 채널 토글 시작·종료 |

### 설계 판단

- **장치별 상태 분리:** 전역 GPIO 상태 대신 각 Platform Device에 독립적인 구조체를 할당합니다.
- **열린 FD와 장치 수명 분리:** unbind 이후에도 FD가 남을 수 있어 `kref`로 메모리 수명을 관리합니다. 제거된 장치의 I/O는 `ENODEV`로 차단합니다.
- **자원 해제 순서 관리:** GPIO 획득에는 `devm_gpiod_get()`을 사용하고, 정리 action의 역순 실행을 고려해 출력 OFF와 참조 해제를 배치합니다.
- **입력 계약 검증:** 잘못된 값·ABI 버전·reserved 필드·사용자 포인터를 검사하고 오류 코드를 반환합니다.
- **버튼 처리 위치 명시:** 현재 GPIO 버튼 처리는 사용자 공간 샘플링 방식입니다. IRQ 기반 입력 처리는 후속 학습 범위입니다.

## 실장비 검증

아래 완료 결과는 **2026-10-30 기준 학습자가 전달한 실장비 검증 보고**에 근거합니다.

| 항목 | 확인 범위 |
|---|---|
| 8채널 장치 구성 | 하나의 모듈로 8개 Platform Device 바인딩 및 8개 Character Device를 통한 독립 제어 |
| LED·버튼 | 실물 LED·버튼 동작 및 `all toggle start/stop` 확인 |
| 장치 재바인딩 | 기존 1채널에서 `unbind/rebind` 20회 확인 |

8채널 각각의 반복 재바인딩과 장시간 안정성은 위 완료 범위에 포함하지 않습니다. [기존 Chapter 07 검증 문서](ch07-platform-gpio/CH07_VERIFICATION.md)는 실장비 검증 전 작성본으로, 해당 문서의 BLOCKED 표기는 이전 시점의 기록입니다.

## 학습 및 소스 구성

| 경로 | 주요 내용 |
|---|---|
| [02a](02a/) | 파일 I/O, 오류 처리, `EINTR`, partial write |
| [02b](02b/) | `fork/execve`, Copy-on-Write, `mmap`, 공유 메모리 |
| [02c](02c/) | pthread, race condition, mutex, AArch64 ABI |
| [ch05-memory](ch05-memory/) | 커널 메모리 할당, 실패 주입, 자원 수명과 해제 |
| [ch06-character](ch06-character/) | Character Driver, ioctl/UAPI, 이벤트 큐, poll, 스트레스 테스트 |
| [ch07-platform-gpio](ch07-platform-gpio/) | Device Tree, Platform Driver, 8채널 GPIO, 사용자 공간 앱 |

**후속 학습:** Chapter 08 IRQ·Timer, Chapter 09 Debugging·Profiling.

## 빌드 환경

- **Target:** Raspberry Pi 4 Model B / AArch64
- **Kernel Driver 기준:** Linux 6.18.33-v8
- **Host:** WSL / Linux x86-64
- **Toolchain:** 기존 Yocto 빌드의 ARM64 toolchain·sysroot·kernel artifacts

Chapter 07 빌드는 저장소 루트에서 실행합니다.

```sh
bash ch07-platform-gpio/scripts/build-existing.sh
```

기존 Yocto 환경이 필요하며, 경로와 전제조건은 [Chapter 07 빌드 안내](ch07-platform-gpio/README.md#빌드-wsl)를 따릅니다. 저장소의 Overlay는 기본 `disabled` 템플릿입니다. 실제 배선·GPIO 점유·극성을 확인한 장비 구성에 맞춰 적용해야 합니다.

## 관련 프로젝트

- [yocto-rpi4-lab](https://github.com/triton0305/yocto-rpi4-lab) — 드라이버·Device Tree Overlay·C 앱·자동 실행 서비스의 Yocto 이미지 통합
- [arm-under-the-hood](https://github.com/triton0305/arm-under-the-hood) — AArch64 Assembly와 저수준 실행 구조 학습
- [freertos-stm32-lab](https://github.com/triton0305/freertos-stm32-lab) — STM32 FreeRTOS Task·Queue·Mutex·Notification 및 UART CLI
