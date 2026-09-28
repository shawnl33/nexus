# PROJECT STATE

기준 명세: [C_TRADING_MASTER_PLAN.md](C_TRADING_MASTER_PLAN.md)
갱신일: 2026-09-28

## 현재 단계

단계 2 (Tick/Candle 처리·세션·상위 봉 집계·과거 입력 재생) — 완료

## 완료 항목

- CMake 3.24+ 프로젝트 골격, C17 / extensions 비활성 / GCC·MSVC 경고 분리, Ninja 프리셋(Linux `default`, Windows `windows`, 크로스 `windows-cross`)
- `trading-engine` 실행 파일: `--help`, `--version`, `--data-dir`(예약)
- core 정적 라이브러리 (외부 의존성 없음, 표준 라이브러리만):
  - `model/units` — 스케일 정수 가격·수량, `tr_validity_t`, 스케일 검증
  - `model/time_us` — UTC epoch µs int64, overflow 검사 덧셈
  - `model/civil_time` — UTC ↔ 현지 역법 변환(오프셋, 윤년, pre-1970)
  - `model/instrument` — 종목 모델·유효성 검사
  - `model/envelope.h` — 이벤트 봉투·품질 플래그 (LATE/DUPLICATE/GAP/CORRECTED/FILLED_EMPTY)
  - `market/candle` — 봉 모델(OPEN/CLOSED, revision), TR_TF_DAY
  - `market/tick.h` — 체결 모델(원본 체결 ID, 거래량 의미)
  - `market/session` — 세션 정책(야간장·요일 마스크·트레이딩 데이)
  - `market/ring` — 고정 용량 Ring Buffer (최신 기준 상대 인덱스, 제자리 갱신, 가변 접근)
  - `market/bar_builder` — 틱→봉: 중복 제거, 경계/타이머/세션 폐장 확정, 늦은 입력 정정(revision+CORRECTED), 무거래 채움(FILLED_EMPTY)과 누락 구분
  - `market/bar_aggregator` — 확정 하위 봉→상위 봉: 세션 개장 정렬(UTC 나머지 아님), 일봉 트레이딩 데이(야간 자정 무시), 미확정 표시
- adapters/history 라이브러리: `replay` — 기록된 틱의 논리 시간 재생(순서 검증 포함)
- CTest 8개: ring, units, model, civil_time, session, bar_builder, aggregator, replay(동일 입력 재생)

## 검증 결과

- 환경: Linux, CMake 3.28.1, GCC 15.3.0, Ninja 1.11.1
- `ctest --preset default` — 8/8 통과
- Windows 크로스 컴파일(`windows-cross`) — 경고 없이 exe 생성 확인. Windows에서 exe 실행 확인(사용자, 2026-09-28). Windows 네이티브 ctest는 미검증

## 바로 다음 작업

- 단계 3: 기본 지표와 미래곡선의 독립 계산부 — [docs/yeslanguage_mapping.md](docs/yeslanguage_mapping.md) §5 포팅 단위 순서로 구현 (회귀 유틸 → Htf → V4 → 호가 V2 → 신형 V3)
- 병행 가능: 단계 6의 LS 읽기 전용 사전 확인 (토큰 발급, TR 매핑 문서화) — `.env`의 키 사용

## 완료된 분석

- YesLanguage 원본 6개 전수 분석 완료: [docs/yeslanguage_mapping.md](docs/yeslanguage_mapping.md)
  - 호출 그래프, 함수별 계약(인자/상태/출력/워밍업), P01~P06 갱신, 미제공 함수 영향, 포팅 단위 제안
  - 핵심 확인: V3 7인자 호출 vs 19인자 정의 불일치(메인은 구형 V3+외부 V4 체계), Htf `%1` 항상 참, 방향 출력은 수치, 점수에서 방향 0은 −2 분기
  - 원본 8개 해시 전부 SOURCE_INDEX와 일치

## 차단·미결 사항

- 미제공 원본 함수 6개 (`WSF_1m_DailyTrendLinkV1` 등, 계획서 §10.2) — 원본 추가 전까지 `MISSING_DEPENDENCY`
- HTS 기준 출력 묶음 없음 — 전체 원본 일치 검증은 별도 상태로 기록 예정
- Git 원격 저장소 미연결
- live 실거래 실행 여부: 사용자 결정 사항 (구현 에이전트가 임의 실행하지 않음)
