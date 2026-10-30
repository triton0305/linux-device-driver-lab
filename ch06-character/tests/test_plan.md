# Chapter 06B — Read/Write 검사 항목

대상: Raspberry Pi 4B / Yocto ARM64 / Linux 6.18.33-v8. 장치: `/dev/thermal_core`.

## 실장비 확인 기록

| 입력 | 결과 |
|---|---|
| `printf '12' \| dd ...` | `Invalid argument`, shell 종료 코드 1, 상태 0 유지 |
| `printf 'x' \| dd ...` | `Invalid argument`, shell 종료 코드 1, 상태 0 유지 |

## 자동 검사

[thermal_test.c](../userspace/thermal_test.c)는 다음 동작을 검사한다.

| 항목 | 기대 동작 |
|---|---|
| 정상 write | 한 바이트 `0`·`1` 쓰기 성공, 상태 반영 |
| 길이 2 write | `EINVAL`, 상태 유지 |
| 잘못된 값 write | `EINVAL`, 상태 유지 |
| 길이 0 write | 0 반환, 상태 유지 |
| 길이 0 read | 0 반환, offset 유지 |
| 부분 read | 1바이트씩 2회 읽은 후 EOF |
| 큰 버퍼 read | 2바이트 반환 |
| 보호 페이지 write | `EFAULT`, 상태 유지 |
| 보호 페이지 read | `EFAULT` |
| 결과 판정 | 모든 검사 통과 시 종료 코드 0 |

## 실행과 기록

```sh
uname -r
ls -l /dev/thermal_core
/tmp/thermal_test
echo $?
```

빌드 시 BitBake task summary와 ELF Machine을 확인한다.
Pi 실행 결과에는 커널 버전, 장치 노드, 앱 출력과 종료 코드를 함께 기록한다.
