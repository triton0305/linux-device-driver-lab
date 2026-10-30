# Chapter 06C — ioctl 검사 항목

대상: Raspberry Pi 4B / Yocto ARM64 / Linux 6.18.33-v8.

## 실행

```sh
/tmp/thermal_ioctl_test
echo $?
/tmp/thermal_test
echo $?
```

## 확인 항목

| 항목 | 기대 동작 |
|---|---|
| 구조체 크기 | 상태 ABI 16바이트 |
| RESET·GET | RESET 후 version=1, state=0, 기존 read도 0 |
| SET·GET | SET 1 후 ioctl과 기존 read 모두 1 |
| state=2, version=2, reserved 값 설정 | `EINVAL`, 기존 상태 유지 |
| 알 수 없는 명령·잘못된 크기 | `ENOTTY` |
| 보호된 사용자 페이지의 SET·GET | `EFAULT` |
| 기존 `write("0", 1)` | 반환값 1, GET 상태 0 |
| 종료 전 상태 | SET 0, GET 0 |
| 결과 출력 | `SUMMARY: all PASS, 0 FAIL`, 종료 코드 0 |
| read/write 회귀 | `thermal_test`의 19개 검사 및 종료 코드 확인 |

결과 기록에는 `uname -r`, 모듈 vermagic, 앱 출력과 종료 코드를 함께 보관한다.
