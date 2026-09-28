# C Trading Engine

C 기반 개인용 자동매매 시스템. 기준 명세는 [C_TRADING_MASTER_PLAN.md](C_TRADING_MASTER_PLAN.md)다.

## 현재 상태

단계 0 (저장소 골격) 완료. 실제 매매 기능은 없다. 진행 상태는 [PROJECT_STATE.md](PROJECT_STATE.md)를 본다.

## 요구 환경

- CMake 3.24 이상
- Linux: GCC / Windows: MSVC (Clang·MinGW는 별도 매트릭스)
- C17, compiler extensions 비활성

## 빌드

프리셋 사용 (짧은 명령):

```sh
cmake --preset default        # 구성 (최초 1회 또는 CMake 변경 시)
cmake --build --preset default
ctest --preset default
```

동일한 긴 명령:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure --no-tests=error
```

core 테스트가 있으므로 0개 테스트 실행을 통과로 간주하지 않는다(긴 명령의 `--no-tests=error`).

## 실행

```sh
./build/trading-engine --help
./build/trading-engine --version
```

## 비밀정보

LS증권 OPEN API 키는 `.env`(Git 제외) 등 실행 환경의 비밀 설정으로 로딩한다.
키를 코드·문서·화면틀·로그에 기록하지 않는다. replay/backtest와 단위 테스트는 인증정보 없이 동작해야 한다.

## 원본 자료

`reference/` 아래 YesLanguage 원본과 자료 목록([reference/SOURCE_INDEX.md](reference/SOURCE_INDEX.md))은 변경하지 않는다.
