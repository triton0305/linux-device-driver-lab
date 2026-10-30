# Chapter 07 — 검증 기록

## 실장비 완료 결과

2026-10-30 실습 완료 보고를 반영했다. 대상은 Raspberry Pi 4B의 ARM64 Linux 환경이다.

| 검증 항목 | 결과 |
|---|---|
| Platform Device 바인딩 | 하나의 `lab_gpio.ko`로 8개 장치 바인딩 확인 |
| Character Interface | 8개 Character Device를 통한 채널별 독립 제어 확인 |
| 실물 LED·버튼 | 8채널 입출력 동작 확인 |
| 통합 토글 | `all toggle start/stop` 동작 확인 |
| 재바인딩 | 기존 1채널에서 `unbind/rebind` 20회 확인 |

## 빌드·정적 검사 기록

2026-10-29 WSL x86-64 환경에서 수행한 초기 모듈·앱·단일 채널 Overlay 검사 결과다.

| 항목 | 상태 | 기록 |
|---|---|---|
| 모듈 ARM64 빌드 | PASS | 기존 6.18.33 artifacts, W=1, CC/MODPOST/LD 성공 |
| 사용자 앱 ARM64 빌드 | PASS | 기존 Yocto GCC/sysroot, -Wall -Wextra -Werror, ELF AArch64 |
| module metadata | PASS | modinfo: of:N*T*Cyocto-lab,gpio-demo 및 C* alias |
| vermagic | PASS | 6.18.33-v8 SMP preempt mod_unload modversions aarch64 |
| Overlay CPP + dtc -@ | PASS | disabled dtbo 생성, 컴파일 진단 없음 |
| 기존 Pi DTB 파일상 병합 | PASS | fdtoverlay exit 0, 오프라인 병합 성공 |
| phandle/controller/극성 정적 확인 | PASS | 두 GPIO가 /soc/gpio@7e200000, #gpio-cells=2로 해결; 17/0, 27/1; disabled 유지 |
| binding YAML parse 및 required 선언 확인 | PASS | PyYAML, offline 검사 스크립트 |
| kernel checkpatch | PASS | driver/UAPI: 0 errors, 0 warnings |
| host 앱 컴파일/인수 처리 | PASS | GCC -Werror, 인수 없는 실행 usage 및 exit 2 |
| shell syntax | PASS | bash -n build-existing.sh, sh -n inspect-target.sh |

초기 빌드의 `no_llseek` 참조 오류는 해당 지정자를 제거하고 `nonseekable_open`을 유지해 해결했다.

## 자동 검사 구성

| 파일 | 검사 내용 |
|---|---|
| [check_offline.py](tests/check_offline.py) | 단일 채널 Overlay 및 소스 정적 검사 |
| [check_eight_offline.py](tests/check_eight_offline.py) | disabled·active 사본의 DT 병합, 8개 노드, 16개 GPIO 매핑·극성·phandle |
| [test_toggle_eight.py](tests/test_toggle_eight.py) | mock GPIO로 8채널 앱 동작, 실행 충돌, 오류 정리, 시그널, 관찰 명령 검사 |
| [lab_gpio_test.c](userspace/lab_gpio_test.c)의 `selftest` | read/write/ioctl, 공유 상태, 부분 read·EOF, 잘못된 값·ABI·포인터 오류 |
| [lab_gpio_test.c](userspace/lab_gpio_test.c)의 `hold` | 열린 FD 유지, unbind 이후 GET/read/write/OFF의 `ENODEV` 검사 |

mock 테스트의 시나리오는 독립 토글·동시 입력·길게 누름·입력 bounce·초기 눌림·중복 실행·역순 동시 시작·누락 장치·I/O 실패·그룹 OFF·시그널 종료·stale socket 복구를 포함한다.

## 재현 명령

기존 Yocto 빌드 환경의 저장소 루트에서 실행한다.

```sh
bash ch07-platform-gpio/scripts/build-existing.sh
python3 ch07-platform-gpio/tests/check_offline.py
bash ch07-platform-gpio/scripts/build-eight-existing.sh
modinfo ch07-platform-gpio/driver/lab_gpio.ko
```

Pi에서 모듈과 Overlay 적용 후 실행한다.

```sh
uname -r
ls -l /dev/lab_gpio-*
ls -l /sys/bus/platform/drivers/lab-gpio/
dmesg | tail -60
./lab_gpio_test all toggle start
./lab_gpio_test all toggle status
./lab_gpio_test all toggle stop
```

## 재바인딩·FD 수명 검사 절차

실제 platform device 이름을 sysfs에서 확인한다. D0 예시는 다음과 같다.

```sh
# 터미널 A
./lab_gpio_test /dev/lab_gpio-lab-gpio hold

# 터미널 B
DEV=lab-gpio
printf '%s' "$DEV" > /sys/bus/platform/drivers/lab-gpio/unbind
printf '%s' "$DEV" > /sys/bus/platform/drivers/lab-gpio/bind
./lab_gpio_test /dev/lab_gpio-lab-gpio get
```

`hold`는 unbind 이후 오류 반환을 확인하고 FD를 닫는다. 재bind는 새 장치 객체를 생성하고 LED를 OFF로 초기화한다.
반복 검사에서는 회차, 장치 이름, 앱 종료 코드, devnode, GPIO consumer와 커널 로그를 함께 기록한다.

## 확장 검증 항목

- 8채널 각각의 반복 재바인딩과 장시간 연속 운전
- 열린 옛 FD를 유지한 상태의 재bind 및 open/remove 경합
- 잘못된 DT 속성, GPIO 점유 충돌, provider 지연에 대한 probe 오류 처리
- `dt-doc-validate`·`dt-validate`를 이용한 binding 검사
- 32-bit compat 앱 실행과 전원 전환 시 출력 계측

구현 설명은 [CH07_IMPLEMENTATION.md](CH07_IMPLEMENTATION.md), 빌드·장비 적용은 [README.md](README.md)를 참조한다.
