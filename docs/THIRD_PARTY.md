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

## Windows 독립 MinGW 빌드 절차 (미검증 기록)

1. SQLite: 공식 amalgamation(`sqlite3.c`)을 사용하거나 소스 빌드.
2. libzmq: 공식 CMake 빌드를 같은 MinGW로 수행 (`-DBUILD_SHARED=OFF -DBUILD_TESTS=OFF -DWITH_LIBSODIUM=OFF`),
   `CMAKE_INSTALL_PREFIX`에 설치 후 이 프로젝트 구성 시 `-DCMAKE_PREFIX_PATH=<prefix>` 지정.
3. yyjson: `third_party/yyjson`을 그대로 사용.
