# PROJECT STATE

기준 명세: [C_TRADING_MASTER_PLAN.md](C_TRADING_MASTER_PLAN.md)
갱신일: 2026-09-28

## 현재 단계

단계 1 (공통 모델·종목·시간·이벤트·Ring Buffer) — 완료

## 완료 항목

- CMake 3.24+ 프로젝트 골격, C17 / extensions 비활성 / GCC·MSVC 경고 분리
- `trading-engine` 실행 파일: `--help`, `--version`, `--data-dir`(예약) — `src/app/main.c`
- `.gitignore` (`build/`, `.env`, `*:Zone.Identifier` 제외)
- core 정적 라이브러리 (외부 의존성 없음, 표준 라이브러리만):
  - `src/core/model/units` — 스케일 정수 가격·수량, `tr_validity_t`(값 부재와 0 구분), 스케일 검증
  - `src/core/model/time_us` — UTC epoch µs int64, overflow 검사 덧셈
  - `src/core/model/instrument` — 종목 모델·유효성 검사
  - `src/core/model/envelope.h` — 이벤트 봉투·품질 플래그
  - `src/core/market/candle` — 봉 모델(OPEN/CLOSED, revision)·불변식 검사
  - `src/core/market/ring` — 고정 용량 Ring Buffer (최신 기준 상대 인덱스, 제자리 갱신)
- CTest 테스트 3개: `ring`(빈 상태/용량 1/순환/덮어쓰기/범위 초과/반복 갱신), `units`(스케일·변환·overflow), `model`(종목·봉 불변식·품질 플래그)

## 검증 결과

- 환경: Linux, CMake 3.28.1, GCC 15.3.0
- `cmake -S . -B build && cmake --build build --config Debug` 경고 없이 통과
- `ctest --test-dir build -C Debug --output-on-failure --no-tests=error` — 3/3 통과
- `./build/trading-engine --help` / `--version` 정상 동작
- Windows(MSVC) 빌드: 미검증

## 바로 다음 작업

- 단계 2: Tick/Candle 처리·세션·상위 봉 집계·과거 입력 재생 (`src/core/market/` 확장, `src/runtime/` 시작)
- 병행 가능: 단계 6의 LS 읽기 전용 사전 확인 (토큰 발급, TR 매핑 문서화) — `.env`의 키 사용

## 차단·미결 사항

- 미제공 원본 함수 6개 (`WSF_1m_DailyTrendLinkV1` 등, 계획서 §10.2) — 원본 추가 전까지 `MISSING_DEPENDENCY`
- HTS 기준 출력 묶음 없음 — 전체 원본 일치 검증은 별도 상태로 기록 예정
- Git 원격 저장소 미연결
- live 실거래 실행 여부: 사용자 결정 사항 (구현 에이전트가 임의 실행하지 않음)
