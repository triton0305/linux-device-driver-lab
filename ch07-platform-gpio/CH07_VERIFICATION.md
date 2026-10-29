# Chapter 07 verification

기록일: 2026-10-29 (Asia/Seoul). 실행 위치: WSL x86-64.
**빌드·정적 검사 완료, HW VERIFIED 항목 없음.**

PASS는 아래 명시한 범위에만 적용한다. NOT TESTED는 실행하지 않았음을,
BLOCKED는 현재 장비 접근 또는 선행 회로·부팅 확인이 없어 진행할 수 없음을 뜻한다.

## 실행 결과

| 항목 | 상태 | 실제 근거 |
|---|---|---|
| 기존 구조·git 상태 조사 | PASS | 06 및 pre06d 디렉터리는 기존 untracked; remote 유지 |
| 06D 완료 확인 | NOT TESTED | 코드와 D01–D16 test 존재, target PASS 기록 없음 |
| 06E 완료 확인 | NOT TESTED | 요구·구현·검증 완료 근거 없음 |
| 모듈 ARM64 빌드 | PASS | 기존 6.18.33 artifacts, W=1, CC/MODPOST/LD 성공 |
| 사용자 앱 ARM64 빌드 | PASS | 기존 Yocto GCC/sysroot, -Wall -Wextra -Werror, ELF AArch64 |
| module metadata | PASS | modinfo: of:N*T*Cyocto-lab,gpio-demo 및 C* alias |
| vermagic | PASS | 6.18.33-v8 SMP preempt mod_unload modversions aarch64 |
| Overlay CPP + dtc -@ | PASS | disabled dtbo 생성, 컴파일 진단 없음 |
| 기존 Pi DTB 파일상 병합 | PASS | fdtoverlay exit 0; target 적용 아님 |
| phandle/controller/극성 정적 확인 | PASS | 두 GPIO가 /soc/gpio@7e200000, #gpio-cells=2로 해결; 17/0, 27/1; disabled 유지 |
| binding YAML parse 및 required 선언 확인 | PASS (제한적) | PyYAML, offline 검사 스크립트 |
| 정식 dt-doc-validate / dt-validate | NOT TESTED | 기존 host에 dtschema 없음; YAML parse는 대체 검증 아님 |
| kernel checkpatch | PASS | driver/UAPI: 0 errors, 0 warnings |
| host 앱 컴파일/인수 처리 | PASS | GCC -Werror, 인수 없는 실행 usage 및 exit 2 |
| shell syntax | PASS | bash -n build-existing.sh, sh -n inspect-target.sh |
| BitBake recipe 패키징 | NOT TESTED | recipe 작성만 수행, layer 변경 없음 |
| target module registration | BLOCKED | Pi 접속·실행 경로 없음 |
| platform device matching / probe | BLOCKED | 실제 부팅 설정·GPIO 점유 미확인; Overlay 미적용 |
| GPIO resource acquisition | BLOCKED | 장비에서 gpioinfo/dmesg 검증 필요 |
| LED ON/OFF (전기·발광) | BLOCKED | 배선 및 실제 관측 필요 |
| button pressed/released | BLOCKED | 배선 및 눌림/해제 관측 필요 |
| target User App / selftest | BLOCKED | ARM64 실행 미수행 |
| 다중 open / concurrent I/O | BLOCKED | 수명 설계 검토만 수행 |
| unbind 열린 FD / rebind / remove cleanup | BLOCKED | target 실행 미수행 |
| 잘못된 DT의 probe 실패 | BLOCKED | 장비에서 의도된 실패 주입 미수행 |
| power-on / probe 실패 안전 출력 | BLOCKED | 회로·측정 미검증 |
| 설정 복구 및 반복 동작 | BLOCKED | target 적용 자체 미수행 |
| 32-bit compat 앱 | NOT TESTED | callback만 제공, 실행 미수행 |

초기 빌드에서 Linux 6.18에 없는 `no_llseek` 참조가 검출됐다. 해당 지정자를 제거하고
`nonseekable_open`을 유지한 뒤 다시 빌드하여 성공했다. 이 실패를 최종 성공으로 숨기지 않는다.
최종 빌드 원문은 로컬 `build/build.log`에 있으며 재생성 가능한 산출물이라 gitignore 대상이다.

실제 수행 명령 (저장소 루트):

```sh
bash ch07-platform-gpio/scripts/build-existing.sh
python3 ch07-platform-gpio/tests/check_offline.py
modinfo ch07-platform-gpio/driver/lab_gpio.ko
gcc -std=c11 -Wall -Wextra -Werror -I ch07-platform-gpio/include \
  ch07-platform-gpio/userspace/lab_gpio_test.c -o ch07-platform-gpio/build/lab_gpio_test-host
ch07-platform-gpio/build/lab_gpio_test-host # usage, exit 2가 기대값
bash -n ch07-platform-gpio/scripts/build-existing.sh
sh -n ch07-platform-gpio/scripts/inspect-target.sh
# 기존 kernel-source/scripts/checkpatch.pl --no-tree --file driver 및 UAPI
```

## 정식 binding 검사 대기

기존 환경에서 dtschema가 제공되는 시점에 다음을 실행한다. 검사용 Python 도구를
설치하는 것과 새 cross toolchain/build tree를 만드는 것은 별개지만 이번에는 둘 다 하지 않았다.
전체 커널/DT rebuild 없이 binding과 병합된 DTB를 검사할 수 있다.

```sh
dt-doc-validate ch07-platform-gpio/dt/yocto-lab,gpio-demo.yaml
dt-validate -s ch07-platform-gpio/dt/yocto-lab,gpio-demo.yaml \
  -l yocto-lab,gpio-demo ch07-platform-gpio/build/merged-offline.dtb
```

진단 출력도 확인한다. 추후 활성화 사본도 검사하며, 누락 속성/배열 길이/unknown property를
변형한 검사용 DTB가 schema 오류를 내는지 확인한다. disabled 템플릿의 검사만으로
활성화된 최종 배선 설정까지 인증하지 않는다.

## Target 검증 순서 (모두 아직 BLOCKED)

1. README의 읽기 전용 조사 수행. live DT와 실제 config include/부팅 체인, GPIO consumer와
   mux를 확인한다. `uname -r`, 모델, 설정 백업 해시, gpioinfo, pinctrl, boot 로그를 보관한다.
   학습자 배선 확인 전에는 아래 구동 단계로 넘어가지 않는다.
2. 승인한 후보/극성의 Overlay 사본을 활성화하고 지원 확인된 방식으로 부팅한다.
   live DT 노드와 properties가 의도대로 반영됐는지 확인한다.
3. `insmod ./lab_gpio.ko` 뒤 `lsmod`, `/sys/module/lab_gpio`,
   `/sys/bus/platform/drivers/lab-gpio`, platform device의 `driver` symlink와 `modalias`,
   `/dev/lab_gpio-*`, dmesg를 각각 기록한다. 모듈 등록과 probe 성공은 별도 판정한다.
4. GPIO consumer가 이 driver에 연결됐는지 확인한다. `get` 직후 LED OFF,
   `on`/`off`의 실제 LED 상태, active-low이면 물리 수준 반전까지 계측·관측한다.
5. `watch` 실행 중 버튼을 누르고 해제하여 문자열을 관측한다. 짧은 pulse나 bounce는
   보장 범위가 아니다. `selftest` exit 0과 로그는 API 검증으로만 기록한다.
6. 서로 다른 open FD의 상태 공유, 부분 read/EOF, 잘못된 값·ABI·reserved·ioctl 크기·
   NULL 포인터 오류를 확인한다. selftest가 이 중 대부분을 검사한다.
   병행 검증은 별도 두 앱에서 on/off를 반복하면서 GET 값이 0/1인지 확인하며,
   마지막 값을 단정하지 않는다. 공유 상태 selftest 두 개를 동시에 돌려서는 안 된다.
7. 아래의 FD/remove 시험 후 gpioinfo에서 consumer 해제 및 dmesg 오류 유무를 확인한다.
   해제 후 전기적 상태는 외부 bias와 실제 측정으로 확인한다.
8. 실패 주입과 설정 복구 후 정상 bind→동작→unbind→bind를 최소 20회 반복하고
   kernel warning/oops, descriptor 누수, 노드 중복, 복구 실패가 없는지 기록한다.

### 열린 FD, unbind, 재bind

승인된 장비에서 실제 platform 이름을 `DEV`로 확인하고 실행한다. 예시는 템플릿 이름이다.

```sh
# 터미널 A (30초 동안 FD 유지; 자동 sysfs 쓰기 없음)
./lab_gpio_test /dev/lab_gpio-lab-gpio hold
# 터미널 B
DEV=lab-gpio
printf '%s' "$DEV" > /sys/bus/platform/drivers/lab-gpio/unbind
# A: GET/read/write/OFF가 ENODEV인지 출력 및 exit status 확인
printf '%s' "$DEV" > /sys/bus/platform/drivers/lab-gpio/bind
./lab_gpio_test /dev/lab_gpio-lab-gpio get
```

재bind 상태는 OFF여야 한다. `hold`는 unbind 확인 직후 close하므로 **옛 FD와 재bind의 동시
존재까지 자동 시험한 것은 아니다**. 추가 수명 검증에서는 별도 FD를 계속 보관한 상태로
재bind하고 옛 FD는 ENODEV, 새 FD만 정상인지 확인한다. open/close와 unbind 경합도 별도
stress 대상으로 둔다. 모듈을 일반 `rmmod lab_gpio`로 내리는 시험은 열린 FD에서 busy,
모두 닫은 뒤 성공을 기대한다. 강제 unload를 쓰지 않는다.

### 잘못된 설정의 실패 기대값

모든 실패 주입은 본래 소비자를 파괴하지 않는 별도 승인된 시험 구성에서만 수행한다.
각 경우 수정한 DTB, dmesg errno, devnode 유무, GPIO consumer, 복구 결과를 보관한다.

| 시험 | 기대값 |
|---|---|
| disabled 노드 | 모듈 등록 가능, platform device/probe 없음 |
| compatible 불일치 | 이 driver와 match 없음 |
| button-gpios 누락 | probe 실패, devnode 없음, LED 요청 전 중단 |
| led-gpios 누락 | probe 실패, 먼저 얻은 button devm 반환 |
| led/button 동일 line | exclusive 요청 충돌, probe 실패, 잔여 consumer 없음 |
| 다른 GPIO consumer 점유 | EBUSY 등 원인 로그, 소유권 탈취 없음 |
| provider 미준비 | 가능한 경우 EPROBE_DEFER 및 provider 준비 후 재probe |
| invalid line 또는 잘못된 GPIO cells | DT 검사 또는 provider 요청 단계 실패 |
| misc 등록 실패 (fault injection 가능 환경) | OFF action 후 GPIO 반환, 열린 노드 없음 |
| GPIO write/read provider 오류 | errno 전파; write 실패는 요청 캐시를 갱신하지 않음 |
| polarity를 실제 회로와 반대로 지정 | binding으로 회로 진실을 알 수 없음; 사전 회로 검토에서 차단 |

출력 API 실패, GPIO 획득 실패 중 잠깐의 전압 변화, 전원 인가/해제 시 수준은 별도 계측
대상이다. software OFF 요청만으로 안전 상태를 검증했다고 기록하지 않는다.

## 최종 저장소 상태 주의

작업 도중 외부에서 `8a5ddad feat(ch06): implement and verify thermal character driver`
(2026-10-29 11:33:21 +0900) 커밋과 root `.gitignore`의 `/ch07-platform-gpio/` 제외 규칙이
추가된 것이 최종 점검에서 관측됐다. 이 작업에서는 git commit/push를 실행하지 않았고
외부 변경을 되돌리지 않았다. 커밋 제목 자체는 06D/06E 실장비 통과 증거로 사용하지 않는다.

그 결과 최종 `git status --short`는 비어 있지만 Chapter 07 파일은 디스크에 존재한다.
`git check-ignore -v ch07-platform-gpio/README.md`는 root `.gitignore:45` 규칙을 가리킨다.
추후 추적 여부를 사용자가 결정해야 하며 이번에는 ignore 규칙을 변경하거나 git add하지 않았다.
기존 `thermal_core.c`, `thermal_uapi.h`는 작업 시작 시 저장한 SHA-256과 일치한다.

이번에 작성한 파일: README.md, CH07_IMPLEMENTATION.md, CH07_VERIFICATION.md,
.gitignore, driver/lab_gpio.c, driver/Makefile, include/lab_gpio_uapi.h,
userspace/lab_gpio_test.c, dt/lab-gpio-overlay.dts, dt/yocto-lab,gpio-demo.yaml,
scripts/build-existing.sh, scripts/inspect-target.sh, tests/check_offline.py,
yocto/lab-gpio_1.0.bb, yocto/lab-gpio-test_1.0.bb.
