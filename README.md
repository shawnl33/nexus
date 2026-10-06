# Nexus Trading Engine

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

Windows (MinGW GCC + Ninja). 설치부터 따라가는 절차는 [docs/windows-build.md](docs/windows-build.md)다.

```bat
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

정적 라이브러리는 `third_party/mingw-prefix`에 있다. `MINGW_PREFIX`가 있으면 그 경로를 쓴다.

빌드 디렉터리는 OS별로 분리된다: Linux는 `build/`, Windows는 `build-windows/`.
WSL 공유 폴터처럼 양쪽 OS가 같은 소스를 보는 환경에서도 충돌하지 않는다.

Linux에서 Windows 바이너리 크로스 컴파일 (MinGW-w64 필요):

```sh
cmake --preset windows-cross
cmake --build --preset windows-cross
```

결과물은 `build-windows-cross/trading-engine.exe`. 테스트 실행은 Windows에서 해야 하므로
크로스 빌드는 'Windows 검증'으로 기록하지 않고 빌드 호환성 확인 용도로만 사용한다(계획서 §25 취지).

core 테스트가 있으므로 0개 테스트 실행을 통과로 간주하지 않는다(`ctest --no-tests=error`).

## 실행

저장소 루트에서 `traderctl up`으로 엔진과 대시보드를 함께 띄운다.
리눅스는 `./build/traderctl`, 윈도우는 `build-windows\traderctl.exe`다.
대시보드는 http://127.0.0.1:18080 이다. 18080이 이미 열려 있으면 18081을 쓴다.
`.env`는 엔진이 읽는다. `web`의 Node 패키지가 없거나 이 OS에서 불러오지 못하면 `npm install`을 먼저 실행한다.
Ctrl+C 한 번에 이 명령이 띄운 엔진과 대시보드가 종료된다. 이미 엔진이 떠 있으면 대시보드만 띄운다.

리플레이 (인증정보 불필요):

```sh
./build/traderctl up --replay examples/ticks_sample.csv --replay-delay 100
```

라이브 (`.env`의 LS 키 필요, 읽기 전용 시세):

```sh
./build/traderctl up --live 005930        # 주식 (S3_)
./build/traderctl up --live-fut A016C000  # 국내선물 (FC9)
./build/traderctl up --live ESZ26         # 해외선물
```

라이브 모드는 최근 1분봉으로 지표 워밍업(백필 근사 재생) 후 실시간 틱을 처리한다.
중지는 Ctrl+C 또는 `traderctl --json engine stop` (정상 종료). 현황은 `traderctl --json status`.
종목 전환은 `traderctl --json market select 000660` 또는 대시보드의 종목 입력 —
화면 상태만 바뀌며(세대/generation 증가, 지표 재워밍업) 전략 거래 대상과는 무관하다 (계획서 §18).

틱 CSV(`epoch_us,price,qty`)를 재생해 봉·지표를 계산하고 상태 스트림을 발행한다.
명령은 `tcp://127.0.0.1:5555` (`traderctl --endpoint`)이고, 구독은 그 다음 포트 `tcp://127.0.0.1:5556`이다.

대시보드 서버 테스트:

```sh
cd web
npm test      # 스텁 엔진 사용
```

브라우저에서 열린 뒤 상태 스트림을 차트로 표시한다. 화면틀 저장·불러오기에는 인증 토큰이 필요하며
첫 실행 시 `web/.runtime/token`에 생성된다 (Git 제외).

도움말·버전:

```sh
./build/traderctl --help
./build/traderctl --version
```

## 비밀정보

LS증권 OPEN API 키는 `.env`(Git 제외) 등 실행 환경의 비밀 설정으로 로딩한다.
키를 코드·문서·화면틀·로그에 기록하지 않는다. replay/backtest와 단위 테스트는 인증정보 없이 동작해야 한다.

## 원본 자료

`reference/` 아래 YesLanguage 원본과 자료 목록([reference/SOURCE_INDEX.md](reference/SOURCE_INDEX.md))은 변경하지 않는다.
