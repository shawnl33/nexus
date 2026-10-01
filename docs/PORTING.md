# 예스랭귀지 → C 포팅 규약

## 목표

예스랭귀지 원본과 C 코드가 서로 이해 가능한 형태로 유지되는 플랫폼을 만든다. 이를 위해 원본과 포팅이 같은 3분할 구조를 거울처럼 따른다.

| 종류 | 원본 (reference/yeslanguage/) | C 포팅 (src/core/) |
|---|---|---|
| 함수 | `functions/` | `functions/` |
| 지표 | `indicators/` | `indicators/` |
| 전략 | `strategies/` | `strategies/` |

새 원본은 처음부터 종류에 맞는 폴드에 직접 추가한다.

## 타입 판별 규칙

원본이 함수·지표·전략 중 무엇인지는 내용으로 판별한다.

- 매매 명령(Buy/Sell 계열)이 있으면 **전략**
- `PlotN()` 출력이 있으면 **지표**
- 둘 다 없고 타입 인자(`Input Numeric`/`NumericRef` 등)만 있으면 **함수**

애매하면 추측하지 않고 사용자에게 묻는다.

## 포팅 규약

- 파일은 1:1로 대응한다: `WSF_X.txt` ↔ `x.c` / `x.h`
- 원본 변수명을 그대로 유지한다
- 주석에 원본의 행 번호·섹션을 인용한다
- `Input` 인자 → config 구조체
- `OUT`/`ref` 인자 → 출력 구조체 또는 페이로드
- 불명은 추측 금지: 모르는 동작은 임의로 만들지 않고 불명으로 기록한다
- 단위 테스트에는 실측 기대값(손계산 또는 HTS 실측)을 둔다

## 암묵 시계열 원칙

예스랭귀지의 변수는 모두 시계열이다(봉마다 값이 쌓이고 `[n]` 참조가 가능). C에서는 이 '변수'를 표현하는 통일 타입 `yl_var`(src/core/market/var.h — 검증된 `tr_ring`의 double 전용 얇은 래퍼)로 재현한다. 인덱스는 최신 기준 상대값([0]=현재)이고, 가득 차면 가장 오래된 값을 덮어쓴다. 저장소는 호출자 소유이며 상태 구조체 안에 버퍼를 둔다.

| 예스랭귀지 | C (yl_var) |
|---|---|
| 변수 선언 `var x;` | `YL_VAR_STORAGE(x, cap);` + `ylv_init(&x, x_buf, cap);` |
| 대입 `x = expr;` (매 봉 1회) | `ylv_push(&x, expr);` |
| 같은 봉 내 재대입 | `ylv_set_current(&x, expr);` (과거를 밀어내지 않고 [0] 제자리 교체) |
| 참조 `x[N]` ([0]=현재) | `ylv_at(&x, N, &out)` — 범위 초과 시 false |

- 살아있는 규약 예시: `tests/test_var.c`의 `test_yeslanguage_1to1_example` (예스랭귀지 스니펫과 같은 값을 냄)
- 파일럿 전환: `src/core/functions/atr.c`의 SMA 이력(`sma_hist`)이 yl_var로 구현되어 있다

**값 복사 불안전 제약**: 저장소 버퍼를 상태 구조체 안에 두는 이 규약은 값 복사 불안전 타입을 만든다 — yl_var를 포함한 구조체를 통째로 값 복사(이식 등)하면 사본의 `ring.storage`가 원본의 버퍼를 가리키는 채로 남아, 원본 슬롯이 재사용될 때 두 상태가 버퍼를 공유하며 조용히 깨진다. 통째 복사 후에는 반드시 재연결 헬퍼로 사본 자신의 버퍼로 저장소 포인터를 복구한다: 범용 `ylv_relink(&s, new_buf)` 또는 모듈별 래퍼(예: `tr_atr_relink(&a)`). 적용 사례: `tr_engine_pipe_remove`의 파이프라인 이식(src/runtime/engine.c)과 회귀 테스트 `tests/test_engine.c`의 `test_pipe_transplant_atr_relink`. 후속 yl_var 전환에서도 같은 검토가 필요하다.

## HTS 고유 동작

HTS(예스랭귀지 실행 환경) 고유의 동작은 하나씩 동일하게 구현하고, 구현한 내용과 불명 항목을 [docs/yeslanguage_mapping.md](yeslanguage_mapping.md)에 기록한다.

## 변경 동기화 절차

원본은 git 관리가 원칙이다(투입·변경 시 커밋 먼저).

1. 변경 감지: `git diff` + SOURCE_INDEX의 SHA-256 해시 대조
2. diff 범위만 C 모듈과 테스트를 갱신한다
3. `ctest`로 검증한다
4. 해시를 SOURCE_INDEX에 재등록한다
5. 원본과 C를 같은 커밋으로 묶는다

큰 구조 변경은 작업 전에 영향 범위를 먼저 보고한다.
