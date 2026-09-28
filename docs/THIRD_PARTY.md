# 서드파티 구성 요소

프로젝트에 포함되는 외부 코드와 의존 라이브러리의 버전·출처·라이선스 기록이다 (계획서 §2, §30).

## 의존성 관리 결정 (2026-09-28)

vcpkg는 도입하지 않는다. 이 프로젝트는 Windows에서도 MSVC가 아닌 MinGW-w64를 사용하므로,
각 환경의 네이티브 경로를 따른다:

| 환경 | 방식 |
|---|---|
| Linux (개발·테스트) | 시스템 패키지 (`sqlite3`, `libzmq3-dev`) |
| Windows 독립 MinGW | 소형 라이브러리는 벤더, 중형(libzmq 등)은 같은 MinGW로 소스 빌드 후 `CMAKE_PREFIX_PATH` 지정 |
| 크로스 컴파일 | 외부 라이브 의존 타겟은 별도 준비 전까지 제외 (미검증 기록) |

## 벤더된 코드 (third_party/)

| 구성 요소 | 버전 | 출처 | 라이선스 |
|---|---|---|---|
| yyjson | 0.10.0 | https://github.com/ibireme/yyjson (src/yyjson.c, yyjson.h) | MIT |

## 시스템 패키지 의존성 (Linux)

| 패키지 | 용도 | 비고 |
|---|---|---|
| SQLite3 (3.46.1로 검증) | 저장소 어댑터 | WAL, synchronous=FULL |
| libzmq3-dev | IPC 어댑터 | ROUTER/DEALER + PUB/SUB |
| libcurl (8.21.0 런타임) | LS 어댑터 | dev 패키지 권장. 미설치 시 `third_party/curl-linux/include`의 헤더 + 시스템 `libcurl.so.4`로 빌드 (deb 추출 경량 경로) |
| libwebsockets (4.3.5 정적) | LS 실시간 어댑터 | `third_party/lws-linux` (deb 추출 헤더+`libwebsockets.a`). 정식 경로는 `sudo apt install libwebsockets-dev` |

`third_party/lws-linux`은 `libwebsockets-dev_4.3.5-6` deb에서 추출한 헤더와 정적 라이브러리다.
정적 링크 시 `ssl crypto z cap`를 함께 링크한다.

### libwebsockets 검증 기록 (2026-09-28)

- 실제 LS 서버(wss://openapi.ls-sec.co.kr:9443) 대상 TLS 연결·구독·틱 수신 검증 완료 (tests/test_ls_rt_live.c).
- ~~이 정적 빌드는 낶부 상태 기계(netlink coldplug)와 poll 지연 특성이 있어 이중 컨텍스트에서 간헐 정지~~
  → **근본 원인 규명(2026-09-28)**: lws 4.3.5 upstream이 lws_service의 timeout 인자를 무시한다
  (양수이면 LWS_POLL_WAIT_LIMIT≈23일로 강제, lib/plat/unix/unix-service.c:99-104).
  netlink와 무관. poll 대기 상한은 sul 스케줄러로만 제한 가능하므로 tr_ls_rt_service가
  호출마다 wake sul(lws_sul_schedule)을 걸어 timeout을 보장한다. 이로써 명령 응답 지연이
  3~15초에서 17~27ms로 해소됐다.
- 컨텍스트 생성 시 `LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT` 사용, 클라이언트 TLS는 `LCCSCF_USE_SSL`.

## Windows 독립 MinGW 빌드 절차 (미검증 기록)

1. SQLite: 공식 amalgamation(`sqlite3.c`)을 사용하거나 소스 빌드.
2. libzmq: 공식 CMake 빌드를 같은 MinGW로 수행 (`-DBUILD_SHARED=OFF -DBUILD_TESTS=OFF -DWITH_LIBSODIUM=OFF`),
   `CMAKE_INSTALL_PREFIX`에 설치 후 이 프로젝트 구성 시 `-DCMAKE_PREFIX_PATH=<prefix>` 지정.
3. yyjson: `third_party/yyjson`을 그대로 사용.
