# C Trading Engine

C 기반 개인용 자동매매 시스템. 기준 명세는 [C_TRADING_MASTER_PLAN.md](C_TRADING_MASTER_PLAN.md)다.

## 현재 상태

단계 0 (저장소 골격) 완료. 실제 매매 기능은 없다. 진행 상태는 [PROJECT_STATE.md](PROJECT_STATE.md)를 본다.

## 요구 환경

- CMake 3.24 이상, Ninja
- Linux: GCC / Windows: MinGW-w64 또는 MSVC (Clang은 별도 매트릭스)
- C17, compiler extensions 비활성

## 빌드

프리셋 사용 (짧은 명령). **프로젝트 루트에서 실행한다.**

Linux:

```sh
cmake --preset default        # 구성 (최초 1회 또는 CMake 변경 시)
cmake --build --preset default
ctest --preset default
```

Windows (MinGW GCC + Ninja, MSYS2 환경 또는 gcc/ninja가 PATH에 있는 상태):

```bat
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

빌드 디렉터리는 OS별로 분리된다: Linux는 `build/`, Windows는 `build-windows/`.
WSL 공유 폴터처럼 양쪽 OS가 같은 소스를 보는 환경에서도 충돌하지 않는다.

core 테스트가 있으므로 0개 테스트 실행을 통과로 간주하지 않는다(`ctest --no-tests=error`).

## 실행

Linux: `./build/trading-engine --help`
Windows: `build-windows\trading-engine.exe --help`

## 비밀정보

LS증권 OPEN API 키는 `.env`(Git 제외) 등 실행 환경의 비밀 설정으로 로딩한다.
키를 코드·문서·화면틀·로그에 기록하지 않는다. replay/backtest와 단위 테스트는 인증정보 없이 동작해야 한다.

## 원본 자료

`reference/` 아래 YesLanguage 원본과 자료 목록([reference/SOURCE_INDEX.md](reference/SOURCE_INDEX.md))은 변경하지 않는다.
