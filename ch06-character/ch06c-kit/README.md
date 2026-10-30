# Chapter 06C — ioctl / Versioned UAPI

`/dev/thermal_core`에 RESET·GET·SET ioctl과 고정 크기 UAPI를 추가한 단계의 템플릿·전환 도구다. 현재 구현은 [driver/thermal_core.c](../driver/thermal_core.c), 공용 헤더는 [include/thermal_uapi.h](../include/thermal_uapi.h)에 있다.

## 구성

| 파일 | 역할 |
|---|---|
| `templates/thermal_uapi.h` | 06C 단계의 16바이트 상태 ABI |
| `templates/thermal_ioctl.inc` | ioctl 처리 코드 |
| `templates/thermal_ioctl_test.c` | ioctl·errno·EFAULT·기존 I/O 검사 |
| `prepare.py` | 06C 이전 소스 백업, ioctl 추가, Yocto recipe 준비 |
| `build_yocto.sh` | prepare 실행 후 모듈·앱 빌드 및 Windows 공유 폴더 배치 |

`prepare.py`는 초기 드라이버와 템플릿을 대조하는 전환 도구다. 06D 이후 공용 헤더에는 이벤트 구조체가 추가되어 있으므로, 현재 단계의 빌드는 프로젝트 소스와 기존 Yocto layer를 사용한다.

## ABI

```c
struct thermal_core_state {
    __u32 abi_version;
    __u32 state;
    __u32 reserved[2];
};
```

| 명령 | 동작 |
|---|---|
| `THERMAL_IOC_RESET` | 상태를 0으로 초기화 |
| `THERMAL_IOC_GET_STATE` | version=1, state=0/1, reserved=0 반환 |
| `THERMAL_IOC_SET_STATE` | version·state·reserved 검사 후 설정 |
| 잘못된 값·버전·reserved | `EINVAL` 반환 |
| 알 수 없는 명령·다른 구조체 크기 | `ENOTTY` 반환 |
| 접근 불가 사용자 포인터 | `EFAULT` 반환 |

구조체는 16바이트의 고정 너비 정수로 구성한다. ABI 레이아웃 변경 시 새 ioctl 명령 번호를 사용한다.

## Pi 실행

실행 중인 커널과 모듈의 vermagic을 대조하고, 기존 FD를 닫은 뒤 모듈을 교체한다.

```sh
uname -r
modinfo -F vermagic /tmp/thermal_core_06c.ko
insmod /tmp/thermal_core_06c.ko
/tmp/thermal_ioctl_test
echo $?
/tmp/thermal_test
echo $?
```

모듈이 이미 적재되어 있으면 기존 앱 종료 후 `rmmod thermal_core`로 해제한다.
ioctl 시험은 [검사 항목](tests/06c_test_plan.md)을 따르며, 기존 read/write 동작은 `thermal_test`로 회귀 검사한다.
