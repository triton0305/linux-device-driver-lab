# Chapter 07 — 구현 및 설계

## 구성

Raspberry Pi 4B / Linux 6.18.33-v8 / Yocto ARM64 환경에서 하나의 Platform Driver로 8개 LED·버튼 채널을 관리한다.

| 구성 요소 | 역할 |
|---|---|
| Device Tree Overlay | 채널별 GPIO line offset, 극성, compatible 정의 |
| `lab_gpio.ko` | 장치별 GPIO 자원 획득과 misc character device 등록 |
| 공유 UAPI | 16바이트 구조체를 통한 LED 설정과 버튼 상태 조회 |
| `lab_gpio_test` | 개별 입출력, 8채널 토글, 디바운스, 실행·종료 제어 |

Chapter 06의 miscdevice, read/write, ioctl 입력 검사 패턴을 장치별 상태와 수명 관리로 확장했다.

## Device Tree와 8채널 매핑

`compatible = "yocto-lab,gpio-demo"`를 공유하는 노드마다 `probe()`가 호출된다.
드라이버는 `devm_gpiod_get(dev, "led", ...)`와 `"button"`으로 descriptor를 얻는다.

아래 숫자는 BCM GPIO controller의 line offset이다.

| 채널 | DT 노드 | LED GPIO | 버튼 GPIO | Character Device |
|---|---|---|---|---|
| D0 | `lab-gpio` | 5 | 4 | `/dev/lab_gpio-lab-gpio` |
| D1 | `lab-gpio-d1` | 6 | 17 | `/dev/lab_gpio-lab-gpio-d1` |
| D2 | `lab-gpio-d2` | 12 | 18 | `/dev/lab_gpio-lab-gpio-d2` |
| D3 | `lab-gpio-d3` | 13 | 27 | `/dev/lab_gpio-lab-gpio-d3` |
| D4 | `lab-gpio-d4` | 19 | 22 | `/dev/lab_gpio-lab-gpio-d4` |
| D5 | `lab-gpio-d5` | 16 | 23 | `/dev/lab_gpio-lab-gpio-d5` |
| D6 | `lab-gpio-d6` | 26 | 24 | `/dev/lab_gpio-lab-gpio-d6` |
| D7 | `lab-gpio-d7` | 20 | 25 | `/dev/lab_gpio-lab-gpio-d7` |

LED는 active-high, 버튼은 active-low로 정의한다. 입력 bias와 출력 전류 제한은 회로에 구성한다.
`<&gpio offset flags>`의 flags가 논리값과 전기적 극성의 대응을 결정한다.

소스 템플릿의 노드는 `disabled`이며, 장비 적용 사본에서 활성화한다. 8채널 Overlay는 단일 채널 Overlay를 교체하는 구성이다.

BCM2711 GPIO controller는 `/soc/gpio@7e200000`, `#gpio-cells = <2>`를 사용한다. 오프라인 검사 스크립트는 병합된 DT의 phandle, 노드 8개, GPIO 16개의 매핑과 중복을 검사한다.
`yocto-lab,gpio-demo`와 YAML binding은 프로젝트의 교육용 정의다.

## Platform driver와 수명

- Linux 6.18.33 로컬 header의 `void (*remove)(struct platform_device *)` 사용.
- 같은 소스의 `gpiod_set_value_cansleep()`는 **int 반환**이다. SET 실패를 앱으로 전파하며
  요청 상태 캐시는 성공했을 때만 갱신한다.
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
  platform unbind는 가능하다. 모듈 해제는 열린 FD를 닫은 뒤 수행한다.
- shutdown에서도 dead 표시와 OFF를 시도한다. close 자체는 공유 LED를 OFF하지 않는다.

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


두 UAPI 구조는 `__u32` 4개로 구성한다. `CONFIG_COMPAT`에서는 `compat_ptr_ioctl`을 연결한다.
동일 장치의 여러 FD는 LED 상태를 공유하며, `read` offset은 FD별로 유지한다. 장치가 다른 채널은 각자의 상태와 잠금을 사용한다.

## 사용자 공간 토글 컨트롤러

### 샘플링과 디바운스

- 한 프로세스가 최대 8개 FD를 열고 각 채널을 순서대로 읽는다.
- 스캔 완료 후 다음 스캔을 20 ms 뒤로 예약한다. 지연이 생기면 현재 시각을 기준으로 다음 간격을 잡는다.
- 같은 입력을 3회 연속 읽으면 안정 상태로 받아들인다.
- 안정된 해제 상태에서 재무장하고, 이후 안정된 눌림 상태에서 LED를 한 번 토글한다.
- 시작할 때 눌린 버튼은 해제 후 다시 눌렀을 때 토글된다.
- `watch`는 별도 관찰 명령으로, 100 ms 간격 300회 조회하고 10회마다 출력한다.

### 실행 조정과 IPC

장치 경로를 정규화하고 정렬한 뒤 gate lock과 owner lock을 얻는다. 전체 채널의 소유권을 확보한 뒤 GPIO 초기화를 시작한다.
경로별 Unix Domain Socket은 상태 조회와 종료 요청을 받는다.

| 명령 | 동작 |
|---|---|
| `all toggle start` | 8채널 OFF 초기화 후 백그라운드 컨트롤러 시작 |
| `all toggle status` | 각 채널 컨트롤러 상태 조회 |
| `all toggle stop` / `all off` | 전체 그룹 종료 및 채널별 OFF 요청 |
| 그룹 구성원의 `toggle stop` / `off` | 해당 구성원이 속한 그룹 전체 종료 |
| `on` / `selftest` | owner lock을 얻어 수동 출력과 컨트롤러의 충돌 방지 |

채널 지정은 단일 장치, `all`, 서로 다른 8개 장치의 쉼표 구분 목록을 지원한다.
lock은 이 앱의 명령 간 실행 조정에 사용하며, 커널 I/O는 드라이버의 mutex로 직렬화한다.

### 오류 및 종료 처리

초기 open·GET·OFF 오류나 실행 중 GET·SET 오류가 발생하면 그룹을 정리한다.
정리 과정은 각 채널의 OFF와 close를 순서대로 수행하고, 한 채널의 오류가 있어도 나머지 채널을 계속 처리한다.
정리가 끝난 뒤 owner lock을 놓고 종료 결과를 응답한다.

`SIGINT`·`SIGTERM`은 정리 경로로 연결하고 `SIGHUP`은 무시한다. `SIGKILL` 이후에는 `all off` 또는 재시작 초기화로 출력을 정리한다.
`on` 명령으로 설정한 LED는 앱 종료 후에도 유지되며, `off`로 끈다.

## 문서 및 검증

실장비 결과와 빌드 기록은 [CH07_VERIFICATION.md](CH07_VERIFICATION.md), 사용 명령과 빌드 절차는 [README.md](README.md)에 정리한다.
API 구현은 프로젝트의 Linux 6.18.33 header를 기준으로 한다.
