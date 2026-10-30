# Chapter 06D — Event Queue Test Kit

공유 이벤트 큐와 `poll` 인터페이스를 검사하는 사용자 공간 테스트 도구다.

| 파일 | 역할 |
|---|---|
| `thermal_event_test.c` | D01–D16 커널 인터페이스 검사 |
| `thermal-event-test_1.0.bb` | 이벤트 테스트 앱 BitBake recipe |
| `build_06d_yocto.sh` | recipe 준비, ARM64 빌드 및 공유 폴더 배치 |

현재 실행용 소스는 [userspace/thermal_event_test.c](../userspace/thermal_event_test.c), 공용 ABI는 [include/thermal_uapi.h](../include/thermal_uapi.h)에 있다.

## 빌드와 실행

기존 WSL Yocto 환경의 `ch06-character`에서 실행한다.

```sh
bash userspace/build_06d_yocto.sh
```

Pi에서 이벤트 인터페이스를 제공하는 모듈을 적재한 뒤 `thermal_event_test`를 실행한다.
모든 16개 시나리오가 통과하면 종료 코드 0을 반환한다. 기존 read/write와 ioctl 회귀 검사는 `thermal_test`와 `thermal_ioctl_test`로 수행한다.

## 이벤트 큐 정책

- 여러 reader가 하나의 큐에서 이벤트를 나누어 소비한다.
- 테스트는 상태를 초기화하고 기존 이벤트를 비운 뒤 시나리오를 시작한다.
- overflow 검사는 마지막 8개 이벤트의 상대적인 순서를 확인한다.
- 절대 sequence 3–10을 재현하려면 모듈을 새로 적재한 상태에서 시작한다.
- D16은 두 reader가 소비한 이벤트의 무결성과 중복 여부를 검사한다.

시험 중에는 별도의 reader·producer를 종료해 입력을 통제한다.
