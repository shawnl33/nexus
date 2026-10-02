# 디스플레이 페이로드 명세

엔진(`trading-engine`)이 IPC PUB 채널로 발행하는 봉별 상태 페이로드와
`chart.snapshot` 명령 응답의 `ind` 배열 레이아웃을 기록한다.

- 발행부: `src/runtime/engine.c` `publish_status()`
- 스냅샷 생성부: `src/app/main.c` `"chart.snapshot"` 핸들러
- 가격 값은 raw 정수 규칙을 따른다 (선물 = 실제 × 100, 주식 = 실제 × 100).

## 1. 상태 스트림 페이로드 (스트림 id: `display`)

봉 이벤트(진행/확정)마다 JSON 1건을 발행한다.

| 키 | 타입 | 의미 |
|---|---|---|
| `bar_open_time` | 문자열(µs) | 봉 시작 시각 (UTC epoch µs) |
| `closed` | 0/1 | 1이면 확정 봉, 0이면 진행 봉 |
| `ohlc` | [o,h,l,c] | 봉 OHLC (raw) |
| `reg_valid` | 0/1 | 곡선회귀유효 |
| `reg_line` | float | 곡선회귀선 |
| `reg_slope` | float | 곡선회귀기울기 |
| `reg_r2` | float | 곡선회귀신뢰도 |
| `pred` | [p1,p2,p3] | MTF예측가격1~3 |
| `pred_dir` | [d1,d2,d3] | MTF예측방향1~3 (1/0/−1) |
| `resid` | float | 회귀잔차 (오차 띠 기준) |
| `pvol` | float | 예측변동성 ATR(14) |
| `score` | int | 1분봉 종합 점수 |
| `future_dir` | float | 점수 구성: 미래방향 |
| `market_dir` | int | 점수 구성: 마켓 방향 |
| `reg_dir` | int | 점수 구성: 회귀 방향 |
| `ob_dir` | int | 점수 구성: 호가 방향 |
| `ob_valid` | 0/1 | 호가 지표 유효 |
| `ob_score` | float | 호가 방향 점수 |
| `generation` | uint | 종목 전환 세대 (전환 시 +1) |
| `mkt` | [valid,center,u1,l1,u2,l2] | ⑧ 마켓 밴드 |
| `mem` | [valid,updated,dir,price,t1,t2,t3,u1,u2,u3,l1,l2,l3] | ⑥ 방향 기억 |
| `pst` | [saved,valid,dir,t1,t2,t3,u1,u2,u3,l1,l2,l3] | ⑦ 지속 사진 |
| `final` | [valid,dir,state,strength] | ⑤ 운영최종유효/방향/상태/강도 (일봉 체인) |
| `reg_flat` | float | 곡선회귀선_평탄 (회귀선을 틱 단위로 반올림) |
| `tick` | int | raw 단위 틱 크기 (선물 5 = 0.05pt, 주식 100 = 1원). ④ 결과 띠 오프셋(tick×4)·⑧ 거리 기준 하한에 사용 |
| `day` | int | 이 봉의 거래일 (세션 규칙 기준 일련번호). ④ 결과 띠 세션 가드에 사용 |
| `sma` | [valid,s5,s20,s60] | 이평선 5/20/60 (종가 기준 단순이동평균). 모든 timeframe에서 평가. valid=1은 세 기간 모두 창이 완성된 때 |
| `fx` | [mask, p1..p24] | 해외선물 1분봉만. mask의 bit k는 Plot(k+1) 표시. p1..p24는 Plot 값. 국내 봉과 정정으로 fx를 다시 계산하지 않은 봉에는 키가 없다 |
| `fx3` | [[plot, value, rgb, width], ...] | 해외선물 미래곡선 V3 표시. 켜진 Plot만, 번호 오름차순. 국내 봉에는 키가 없다 |
| `shcode` | 문자열 | 발행 파이프라인의 종목 코드. **항상 맨 끝 키** (기존 키 순서 불변). 리플레이는 `""` 가능 |

엔진은 종목별 파이프라인(최대 8개, `tr_engine_pipe_add`)을 동시에 돌리며, 각 봉 이벤트는
해당 파이프라인의 `shcode`를 실어 발행된다. 구독자는 `shcode`+`generation` 조합으로
자기 종목·세대의 메시지만 골라 반영한다 (늦은 응답 폐기).

### `final` 배열 (⑤ 매매 상태)

일봉 추세 연결(dtl1) → 갭 레짐(gap1) → 일봉 정렬(dalign2) 체인의 출력이다.
1분봉(timeframe 60s)에서만 평가하며, 갭 판별이 유효해지기 전(완성 세션 TR 10개 미만)에는
`[0,0,0,0]`을 유지한다. 다른 주기에서는 항상 `[0,0,0,0]`.

| 인덱스 | 원본 대응 | 의미 |
|---|---|---|
| `final[0]` | 운영최종유효 | 1이면 매매 상태 유효 |
| `final[1]` | 운영최종방향 | 1/0/−1 |
| `final[2]` | 운영최종상태 | −2..+2 (±2 = 강한 상태) |
| `final[3]` | 운영최종강도 | 0~100 정수 |

## 2. `chart.snapshot` 응답

과거 봉 시딩용. `bars`와 같은 순서(오름차순)로 `ind` 배열을 돌려준다.

요청 `data`에 `shcode`를 지정하면 그 종목의 파이프라인에서 읽는다 (미관측이면
`rejected`/`not_watched`). 생략하면 첫 파이프라인(구 호환). 응답의 `shcode`·
`generation`은 읽은 파이프라인 기준이다.

### `bars[i]` = `[open_time, o, h, l, c, volume]`

### `gaps[i]` = `[start_us, end_us]` (시간축 공백)

같은 세션 안에서 시간상 이웃한 두 봉의 open 시각 차가 timeframe(60s)을 넘으면 구멍으로
기록한다. 각 구간은 `[이전 봉 open + 60s, 다음 봉 open − 60s]` — 빈 분의 양 끝 시각이다
(bars와 같은 µs 단위). 오름차순이며 페이지 경계에 걸친 구멍도 누락되지 않는다(각 페이지가
창의 가장 오래된 봉과 그 직전 봉의 쌍까지 검사한다).

- 세션 판별은 파이프라인의 세션 정책(`tr_session_span` 개장 시각 일치)을 따른다.
  개장일이 다른 전이(익일 개장, 야간 세션 마감→주간 개장 등)는 구멍이 아니다.
- **거래 없는 분도 데이터 유실과 구분할 수 없어 구멍으로 표시된다.** 세션 정책상 같은
  세션인 휴장(선물 주간↔야간 사이 15:45~18:00 등)도 구멍으로 나온다.
- 대시보드는 시딩 시 각 구간을 분 단위 whitespace(`{time}`만 있는 시리즈 항목)로 펼쳐
  캔들 사이에 섞는다 (`web/public/gaps.js` `withWhitespace`). 라이브 꼬리에는 넣지 않는다
  (RT 캐치업·재시딩이 맡는다).

### `ind[i]` 레이아웃 (인덱스 0~31)

| 인덱스 | 키 대응 | 의미 |
|---|---|---|
| [0] | `closed` | 확정 봉 여부 |
| [1] | `reg_valid` | 곡선회귀유효 |
| [2] | `reg_line` | 곡선회귀선 |
| [3] | `reg_r2` | 곡선회귀신뢰도 |
| [4..6] | `pred[0..2]` | MTF예측가격1~3 |
| [7] | `score` | 종합 점수 |
| [8] | `ob_valid` | 호가 유효 |
| [9] | `ob_score` | 호가 점수 |
| [10] | `resid` | 회귀잔차 |
| [11] | `pvol` | 예측변동성 |
| [12..14] | `pred_dir[0..2]` | 예측방향1~3 |
| [15] | `mkt[0]` | 마켓 밴드 유효 |
| [16] | `mkt[1]` | 마켓 중심 |
| [17] | `mkt[2]` | 마켓 상단1 |
| [18] | `mkt[3]` | 마켓 하단1 |
| [19] | `mkt[4]` | 마켓 상단2 |
| [20] | `mkt[5]` | 마켓 하단2 |
| [21] | `final[0]` | 운영최종유효 |
| [22] | `final[1]` | 운영최종방향 |
| [23] | `final[2]` | 운영최종상태 |
| [24] | `final[3]` | 운영최종강도 |
| [25] | `reg_flat` | 곡선회귀선_평탄 (스냅샷에서는 `reg_line`을 틱 반올림해 재계산) |
| [26] | `tick` | raw 단위 틱 크기 |
| [27] | `day` | 거래일 (④ 세션 가드) |
| [28] | `sma[0]` | 이평선 유효 (5/20/60 모두 창 완성 시 1) |
| [29] | `sma[1]` | SMA 5 |
| [30] | `sma[2]` | SMA 20 |
| [31] | `sma[3]` | SMA 60 |
| [32] | `fx[0]` | Plot 표시 비트. 해외선물 1분봉(`fx_on`)만 존재. bit 0 = Plot1 |
| [33..56] | `fx[1..24]` | Plot1..Plot24 값. 표시 비트와 짝이다 |

인덱스 0~20은 기존 레이아웃과 호환된다. 21~25는 ⑤ 매매 상태 도입 시, 26~27은 ④ 세션 가드·틱 적응 도입 시, 28~31은 이평선(SMA 5/20/60) 도입 시 뒤에 추가되었다. 32~56은 해외선물 미래곡선 V1만 뒤에 붙고, 국내 봉은 32개에서 끝난다.

`fx` Plot 순서 (`#WSF_해외선물미래곡선V1`): 1 단계화, 2 곡선회귀선_평탄, 3 마켓중심, 4~8 지속저장목표, 9~13 지난상승(최고/최저/382/500/618), 14~18 지난하락, 19~24 합성 5/15/30분 평탄회귀·마켓중심.

### `mem[i]` / `pst[i]`

`mem[i]`는 갱신(`mem_updated`) 또는 세션 리셋(`mem_reset`)이 일어난 봉만 포함한다.
`pst[i]`는 저장(`pst_saved`)이 일어난 봉만 포함한다. 각 원소는 `[open_time, ...]`로
시작해 어느 봉의 이벤트인지 식별할 수 있다.

`mem[i]` 전체 레이아웃 (17개 값):
`[open_time, valid, dir, price, t1, t2, t3, u1, u2, u3, l1, l2, l3, showT, showU, showL, reset]`

끝의 `reset`(0/1)은 그 봉에 ⑥ 회귀기억 세션 리셋이 일어났음을 표시한다 (저장 여부와 무관).
시딩 측은 `reset`=1 이벤트에서 진행 중 기억선 세트를 끊는다 — 리셋 봉에 새 세트가 함께
저장된 경우(`valid`=1)에는 이 이벤트가 새 세트의 시작이고, 아니면 다음 유효 이벤트까지
공백이다 (라이브 status의 `mem[0]=0` 처리와 동일). 구형 엔진은 16개 원소를 내며
`reset` 없음은 0으로 읽는다.

### `indicators` (지표 매니페스트)

스냅샷 응답 루트에 포함되는 지표 목록이다. 대시보드 지표 선택 패널이 이 배열로
표시할 지표·레이어와 초기 on/off(`defaultOn`)를 구성한다.

```json
"indicators":[
 {"id":"mirae_v16","name":"미래곡선 V16","layers":[
  {"id":"score","name":"① 통합 점수","defaultOn":true},
  {"id":"reg","name":"② 회귀선","defaultOn":true},
  {"id":"rays","name":"③ 미래 목표선","defaultOn":true},
  {"id":"band","name":"④ 결과 띠","defaultOn":true},
  {"id":"state","name":"⑤ 매매 상태","defaultOn":true},
  {"id":"memory","name":"⑥ 방향 기억","defaultOn":true},
  {"id":"snap","name":"⑦ 지속 사진","defaultOn":true},
  {"id":"mktband","name":"⑧ 마켓 밴드","defaultOn":false}]},
 {"id":"sma","name":"이평선 5/20/60","layers":[
  {"id":"sma5","name":"SMA 5","defaultOn":true},
  {"id":"sma20","name":"SMA 20","defaultOn":true},
  {"id":"sma60","name":"SMA 60","defaultOn":true}]},
 {"id":"fx_mirae_v1","name":"해외선물 미래곡선 V1","layers":[
  {"id":"score","name":"단계화","defaultOn":true},
  {"id":"reg","name":"회귀선","defaultOn":true},
  {"id":"market","name":"마켓중심","defaultOn":true},
  {"id":"persist","name":"지속 목표","defaultOn":true},
  {"id":"swingUp","name":"지난상승","defaultOn":true},
  {"id":"swingDn","name":"지난하락","defaultOn":true},
  {"id":"synth","name":"합성 5/15/30","defaultOn":false}]}]
```

`sma` 레이어의 데이터는 상태 스트림의 `sma` 키(현재 봉)와 스냅샷 `ind[28..31]`(과거 봉)에서 가져온다.
`fx_mirae_v1`은 해외선물 1분봉의 `fx` 키와 `ind[32..56]`만 값이 있다. 국내 봉에서는 레이어가 비어 있다.

## 3. 관측(watch) 명령

다중 종목 동시 관측을 관리하는 명령이다. 관측 상한은 8종목(`TR_ENGINE_MAX_PIPES`).

### `market.watch` — 요청 `data: {"shcode":"005930"}`

파이프라인 생성(없으면) + 실시간 구독 + 백필(분봉 + 일봉 프라임)을 수행한다.

- 응답(applied): `{"shcode":"005930","name":"삼성전자","generation":1,"backfilled":1423}`
- 이미 관측 중이면 백필 없이 현 상태(`generation`, `backfilled:0`)를 돌려준다
- 8종목 초과: `rejected` / `watch_limit`

### `market.unwatch` — 요청 `data: {"shcode":"005930"}`

구독 해지 + 파이프라인 제거. 응답(applied): `{"shcode":"005930","watches":2}` (남은 관측 수).

- 마지막 1개는 제거할 수 없다: `rejected` / `last_watch`
- 관측 중이 아니면: `rejected` / `not_watched`
- 대상이 첫 파이프라인(선택 종목)이면 엔진이 마지막 파이프라인의 상태를 그대로
  이식받아 계속한다 — 다른 종목의 차트가 끊기지 않는다

### `market.select` (기존)

전체 대체로 유지한다: 모든 워치를 해지하고 요청 종목 하나만 watch한다.
응답 형식은 `market.watch`와 같다.

대시보드 참고: 다중 칸 프론트(app.js)는 이 명령을 쓰지 않고 칸별 `market.watch`/
`market.unwatch`로 관측을 관리한다 (`/api/symbols/watch|unwatch` 프록시). 서버의
`/api/symbols/select`는 구 프론트·CLI(`traderctl market select`) 호환용으로 남은
경로다. 이 경로로 select하면 칸 상태와 무관하게 엔진 관측 목록이 요청 종목 하나로
교체되어, 다른 종목을 보는 칸은 갱신이 멈추고 마지막 데이터에 고정된다 (프론트의
`engineWatches`도 다음 `/api/status` 갱신까지 실제 엔진 상태와 어긋난다).

해지된 채널의 지연 메시지는 `instrument_id` 라우팅에서 조용히 드롭된다
(파이프라인 없음 = 폐기).
