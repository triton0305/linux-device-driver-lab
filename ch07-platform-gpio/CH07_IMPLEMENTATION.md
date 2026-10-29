# Chapter 07 implementation

## 작업 0: 실제 기존 상태 (2026-10-29)

작업 시작 `git status --short`:

```text
?? ch06-character-pre06d-20261029/
?? ch06-character/
```

`ch07-platform-gpio/`는 빈 디렉터리였다. 저장소 remote는
`git@github.com:triton0305/linux-system-programming.git`. 새 저장소, commit, push 없음.
작업 시작 시 untracked였던 Chapter 06과 백업은 사용자의 작업으로 보존한다.
작업 중 외부 커밋/ignore 변경이 관측됐으며 최종 상태는 검증 문서 끝에 별도로 기록했다.

실제 Chapter 06 구조:

- `driver/thermal_core.c`, `driver/Makefile`
- `include/thermal_uapi.h`
- `userspace/thermal_test.c`, `thermal_ioctl_test.c`, `thermal_event_test.c`, Yocto 빌드 스크립트
- `yocto-recipe/`, `ch06c-kit/`, `ch06d-kit/`, `tests/test_plan.md`
- 작업 중인 meta-yocto-lab에는 별도로 `recipes-kernel/thermal-core/files/` 복사본이 있다.
  저장소 소스와 layer의 복사본을 자동으로 같다고 가정하면 안 된다.

| 단계 | 실제 코드·기록 | 판정 |
|---|---|---|
| 06B | atomic 가상 상태, ASCII read/write. test_plan의 두 잘못된 write 사례만 observed 표시 | 그 기록 외 전체 통과 근거 없음 |
| 06C | 16-byte 고정 ABI, RESET/GET/SET, version/reserved 검사, ioctl 앱 | 구현 존재; 이번 작업에서 target 회귀 실행 안 함 |
| 06D | 8개 이벤트 ring, drop-oldest, 변화 시 sequence/timestamp, waitqueue, poll, shared destructive read; D01–D16 앱 존재 | 소스 구현 존재. 실장비 전체 PASS 로그 없음 → 완료 인증 불가 |
| 06E | 명시된 요구·전용 구현·검증 기록 없음 | 완료 판정 불가. 06D 코드만으로 완료 추정 금지 |

thermal UAPI는 `'T'` magic, version 1, 16-byte state와 24-byte event.
`read`는 `'0\n'`/`'1\n'`와 FD별 offset/EOF, `write`는 정확히 한 바이트 `'0'`/`'1'`.
이벤트는 **thermal 출력 상태 변경**을 나타내며 여러 reader가 하나의 큐를 소모한다.
버튼 입력의 의미와 다르므로 Chapter 07에서 이 이벤트 구조나 thermal ioctl을 재해석하지 않는다.

재사용한 구현 패턴은 miscdevice, file_operations, nonseekable_open,
simple_read_from_buffer, 정확한 write 길이/값 검사, version/reserved가 있는 ioctl,
copy_to/from_user 오류 처리, mutex 직렬화와 C 앱 테스트 방식이다.
기존 모듈을 링크하거나 수정하지 않고 Chapter 07에서 이 구조를 확장했다.
가상 단일 global state를 그대로 GPIO에 연결하는 방식은 platform device 제거 수명에 맞지 않는다.

## DT와 GPIO controller 근거

기존 Yocto `build/tmp/work-shared/raspberrypi4-64/kernel-source`에서 확인:

- `arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b.dts`는 ARM 쪽 공용 DTS를 include한다.
- `arch/arm/boot/dts/broadcom/bcm283x.dtsi`의 `gpio: gpio@7e200000`은
  `gpio-controller`, `#gpio-cells = <2>`를 선언한다.
- `bcm2711.dtsi`의 `&gpio`가 `compatible = "brcm,bcm2711-gpio"`,
  `gpio-ranges = <&gpio 0 0 58>`로 확장한다.
- Pi 4B DTS에 GPIO17/27 line name이 있지만 이는 점유가 없다는 증거가 아니다.
- 기존 배포 DTB와 파일상 병합 후 두 phandle 모두 `/soc/gpio@7e200000`으로 해결됨을 검사했다.
  bus 주소를 CPU 물리 주소나 물리 header pin 번호로 혼동하지 않는다.

`yocto-lab,gpio-demo`는 이 저장소 전용 교육 compatible이며 upstream vendor prefix 등록을
주장하지 않는다. YAML maintainer 이메일도 교육용 placeholder다. upstream 제출 시 실제
maintainer와 vendor-prefix 검토가 필요하다. `led-gpios`, `button-gpios`는 각 1개 필수.
`<&gpio line-offset flags>`의 마지막 cell이 극성을 결정한다.
Driver는 `devm_gpiod_get(dev, "led", ...)` / `"button"`으로만 요청하며 번호를 모른다.
같은 line을 둘에 지정하면 exclusive GPIO 요청이 충돌하므로 probe 실패가 기대된다.

기본 예시는 active-high LED, active-low 버튼이며 **실회로 확정값이 아니다**.
외부 button bias를 전제로 내부 pull/pinctrl 강제 설정을 하지 않았다. 입력 부동 상태는
소프트웨어 polling으로 해결되지 않는다. 다른 회로라면 DT 극성과 bias 설계를 함께 바꿔야 한다.

## Platform driver와 수명

- Linux 6.18.33 로컬 header의 `void (*remove)(struct platform_device *)` 사용.
- 같은 소스의 `gpiod_set_value_cansleep()`는 **int 반환**이다. SET 실패를 앱으로 전파하며
  요청 상태 캐시는 성공했을 때만 갱신한다. 오래된 void API 문서만으로 판단하지 않았다.
- `of_device_id`, `MODULE_DEVICE_TABLE(of, ...)`, `module_platform_driver` 제공.
- `platform_set_drvdata`로 per-device 상태 연결. 노드별 `/dev/lab_gpio-<dev_name>` 생성.
  템플릿의 일반적인 이름은 `/dev/lab_gpio-lab-gpio`이며 실제 sysfs/devnode를 기준으로 한다.
- 입력 descriptor를 먼저 획득하고 출력은 `GPIOD_OUT_LOW`로 요청한다. 여기서 LOW는
  **logical OFF**로 active-low 출력에서는 물리 high일 수 있다.
- optional GPIO API를 쓰지 않는다. 누락, 충돌, provider 미준비 등의 errno를
  `dev_err_probe`로 보존한다. `-EPROBE_DEFER`는 provider 준비 뒤 재시도할 수 있다.
- devm 등록 순서: 상태 소유 참조 해제 → button → LED → safe-off action.
  정리 시 역순으로 OFF를 시도하고 descriptor를 해제한 뒤 상태 소유 참조를 놓는다.
- state는 `kzalloc + kref`로 할당한다. `devm_kzalloc`만 쓰면 열린 FD가 unbind 뒤
  해제된 메모리에 접근할 수 있으므로 이 객체만은 FD 수명까지 유지한다.
- `open`은 misc framework의 등록 mutex 안에서 호출되고 별도 state mutex로 dead 검사와
  참조 획득을 한다. 이 근거는 로컬 `drivers/char/misc.c`에서 확인했다.
- `remove`: state mutex로 진행 중 I/O 종료 대기 → dead 표시와 OFF 시도 → mutex 해제 →
  misc_deregister → devm 정리. misc_deregister 동안 state mutex를 잡지 않아 lock 역전을 막는다.
- 이후 옛 FD의 read/write/ioctl은 `ENODEV`; descriptor를 만지지 않는다. 마지막 close가 객체 해제.
  재bind는 별도 객체를 만들므로 옛 FD를 새 GPIO로 연결하지 않는다.
- `.owner = THIS_MODULE`로 정상 module unload는 열린 FD가 있으면 거부된다.
  platform unbind는 가능하다. 강제 unload는 지원하지 않는다.
- shutdown에서도 dead 표시와 OFF를 시도한다. close 자체는 공유 LED를 OFF하지 않는다.

OFF API가 실패하면 경고를 남긴다. 전원 인가 직후, GPIO 획득 자체 실패, provider 고장,
전원 손실, descriptor 해제 이후 전압을 이 드라이버만으로 보장할 수 없다.
GPIO default level과 외부 회로가 비활성 상태를 보장해야 한다.

예상 로그 (실행 관측 로그가 아님):

```text
lab-gpio lab-gpio: bound; GPIOs acquired; logical LED OFF; /dev/lab_gpio-lab-gpio
lab-gpio lab-gpio: unbound; old FDs return ENODEV; GPIO cleanup follows
```

## 사용자 API 계약

| 작업 | 계약 |
|---|---|
| `read` | LED 요청 캐시를 ASCII 2바이트로 반환, 부분 read와 EOF 유지. 재조회는 reopen 또는 GET |
| `write` | 0 길이는 0, 정확한 `'0'`/`'1'` 1바이트만 허용, 그 외 EINVAL |
| `LAB_GPIO_IOC_OFF` | logical LED OFF; button과 무관 |
| `LAB_GPIO_IOC_SET` | 16-byte `lab_gpio_led`; version=1, led=0/1, reserved 모두 0 |
| `LAB_GPIO_IOC_GET` | 16-byte `lab_gpio_state`; version, cached led, sampled button, reserved=0 |
| invalid user pointer | EFAULT; SET에서 출력 변경 없음 |
| unknown command/size | ENOTTY |
| removed device FD | read/write/ioctl ENODEV; close 가능 |

두 구조는 `__u32` 4개이며 포인터가 없다. CONFIG_COMPAT에서는 compat_ptr_ioctl을 사용하나
32-bit 사용자 앱 실행은 검증하지 않았다. 여러 open은 LED 상태를 공유하고 I/O는 mutex로
직렬화된다. 마지막으로 성공한 SET이 최종 출력이며 FD별 독립 출력은 아니다.
`read` offset만 FD별이다. button GET은 요청마다 현재 logical sample을 읽으며 queue를 소모하지 않는다.

`watch`는 userspace nanosleep 100 ms × 300회(약 30초)이다. IRQ, gpiod_to_irq,
request_irq, interrupt DT property, kernel timer/workqueue, poll readiness 구현은 없다.
`poll(2)`로 버튼 변화를 기다리는 ABI가 아니다. debounce도 하지 않으므로 접점 bounce나
샘플 간 짧은 누름을 놓칠 수 있다. Chapter 08에서 별도 설계할 범위다.

## 근거와 한계

빌드 API의 최종 근거는 위 로컬 6.18.33 header와 실제 빌드다.
[Linux GPIO consumer documentation](https://cdn.kernel.org/doc/html/latest/driver-api/gpio/consumer.html)의
logical polarity 및 sleep 가능한 consumer API 설명을 참고했다. 온라인 latest의 함수 반환형은
이 로컬 버전과 다를 수 있으므로 그대로 복사하지 않는다.
하드웨어 검증 전 완료 주장은 하지 않는다. 검증 표와 복구 절차는 별도 문서에 기록한다.
