# 지표 선택형 다중 차트 대시보드 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 대시보드를 미래곡선 전용 하드코딩에서 지표 선택형 다중 차트 구조로 바꾼다 (기본 = 맨 차트 "없음" 상태).

**Architecture:** 엔진은 지표 값(SMA 5/20/60 추가)과 지표 매니페스트(선택 가능 지표·레이어 목록)를 발행한다. 프론트는 패널 매니저(칸 추가/삭제/크기 조절) + 지표 렌더러 레지스트리로 그리고, 화면틀 v2가 칸 수·칸별 지표 집합·크기를 저장한다.

**Tech Stack:** C99 (엔진), 순수 JS + lightweight-charts 4.2.3 (프론트), node:test

**Spec:** `docs/superpowers/specs/2026-09-29-indicator-selectable-dashboard-design.md`

## Global Constraints

- 계획서 §18: 프론트는 지표 값을 재계산하지 않는다. 레이아웃·표시 설정만 저장한다.
- 엔진은 한 번에 한 종목 — 모든 칸은 같은 종목을 본다.
- 스냅샷 `ind` 배열 기존 인덱스 0~27 절대 변경 금지. 새 필드는 [28]부터 append.
- status 페이로드의 기존 키 이름·순서 유지. 새 키는 뒤에 추가.
- 가격은 raw 정수 규칙 (선물·주식 모두 실제×100). 표시는 `fmtPrice`(÷100).
- 원본 V16 표시 규칙(색·굵기·숨김)은 바꾸지 않는다.
- C: `cmake --build build` + `ctest --test-dir build` 통과. 웹: `cd web && npm test` 통과.
- 커밋 메시지는 한국어(기존 스타일).

---

### Task 1: 엔진 SMA 모듈 + 페이로드/매니페스트

**Files:**
- Create: `src/core/indicators/sma.h`, `src/core/indicators/sma.c`
- Create: `tests/test_sma.c`
- Modify: `src/runtime/engine.h` (config 없음, 멤버만), `src/runtime/engine.c` (배선 + publish_status)
- Modify: `src/app/main.c` (스냅샷 ind append + 매니페스트)
- Modify: `CMakeLists.txt` (소스/테스트 등록)
- Modify: `docs/display_payload.md`

**Interfaces:**
- Produces:
  - `typedef struct { int period; int count; int head; double buf[64]; double value; bool valid; } tr_sma_t;`
  - `void tr_sma_init(tr_sma_t *s, int period);`
  - `void tr_sma_on_bar(tr_sma_t *s, double close, bool is_new_bar);` — 새 봉이면 슬롯 push, 진행 봉 재호출이면 현재 슬롯 덮어쓰기. `count >= period`이면 valid=true, value = 최근 period개 종가 평균.
  - status 페이로드 새 키: `"sma":[valid,s5,s20,s60]` (reg_flat 뒤, tick/day 뒤 — 맨 끝)
  - 스냅샷 ind: `[28]=sma_valid, [29]=sma5, [30]=sma20, [31]=sma60`
  - 스냅샷 페이로드 루트에 `"indicators"` 배열(매니페스트, 아래 Step 4의 JSON을 그대로)

**엔진 배선 (engine.c):**
- `tr_engine_t`에 `tr_sma_t sma5, sma20, sma60;` 추가. `tr_engine_init`에서 init(5/20/60).
- `engine_on_bar`: `tr_sma_on_bar(&e->smaN, (double)bar->close, is_new_bar)` 3개 호출 (게이트 없음 — 모든 timeframe).
- `tr_bar_status_t`에 `int sma_valid; double sma[3];` 추가하고 링 기록부에서 채운다.
- `publish_status`: 포맷 맨 끝에 `,"sma":[%d,%.10g,%.10g,%.10g]` 추가 (버퍼 1664 → 1792).

**Steps:**
- [ ] Step 1: `tests/test_sma.c` — 워밍업 전 valid=false, period개 후 평균값 단언, 진행 봉 덮어쓰기(is_new_bar=false 재호출이 창을 오염시키지 않음), push가 period를 넘으면 가장 오래된 값이 빠지는 것. 기존 테스트 파일 패턴(tests/test_atr? 없으면 test_linreg.c 참고)을 따른다. CMakeLists에 등록.
- [ ] Step 2: `cmake --build build && ctest --test-dir build -R sma` 로 실패 확인 후 sma.c 구현, 통과.
- [ ] Step 3: 엔진 배선 + 페이로드/스냅샷 확장. `tests/test_engine.c`에 sma 키 존재·ind 길이 29→33 확인 갱신.
- [ ] Step 4: `main.c`의 chart.snapshot 응답 루트에 매니페스트 추가:

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
  {"id":"sma60","name":"SMA 60","defaultOn":true}]}]
```

- [ ] Step 5: `docs/display_payload.md`에 sma 키·ind[28..31]·매니페스트 기록. `cmake --build build && ctest --test-dir build` 전부 통과 후 커밋.

---

### Task 2: 프론트 패널 매니저 + 미래곡선 렌더러 분리

현재 `app.js`는 단일 차트 전용이다. 칸 N개 구조로 재구성하되 **화면 동작은 현재와 동등**(미래곡선이 기본으로 켜진 1칸)을 유지한다 — 선택 UI는 Task 3에서 추가한다.

**Files:**
- Modify: `web/public/app.js` (대폭 재구성 — 데이터 피드 공유부와 패널부를 분리)
- Modify: `web/public/mirae-layers.js` (렌더러 계약 추가)
- Modify: `web/public/index.html` (`#panes` 컨테이너)
- Test: `web/test/` 기존 테스트 유지 (mirae-layers.test.js가 깨지면 안 됨)

**Interfaces:**
- Produces (Task 3이 소비):
  - `const RENDERERS = { mirae_v16: MiraeRenderer, sma: SmaRenderer }` (app.js 전역)
  - Pane 객체: `{ id, el, chart, candleSeries, active: Map<indId, {renderer, handle}> }`
  - `createPane(heightFrac)`, `removePane(pane)`, `panes[]` (app.js 전역)
  - 렌더러 계약 (mirae-layers.js가 구현):
    - `createHandle(chart, candleSeries)` → handle (시리즈/프리미티브 생성)
    - `handle.setLayers(map)` — layer id → bool
    - `handle.applyLive(p, ctx)` — status 페이로드 1건 반영
    - `handle.applySeed(ctx)` — 시딩 완료 후 전체 복원
    - `handle.clear()`
  - 공유 ctx (app.js가 제공): `{ bars, barInd, barSeq, barPos, tickRaw(), sameSession, recentBars }`

**구조:**
- 공유 데이터(bars/barInd/barSeq/barPos/tickRaw/lastMktCenter 대신 렌더러 내부)는 app.js에 남긴다.
- 현재 applyStatus의 지표 표시부(②③④⑤⑥⑦⑧ + 배지)를 MiraeRenderer.handle.applyLive로 옮긴다. 배지(헤더) 갱신은 app.js에 남기되 `anyMiraeActive()`일 때만.
- seedChart는 시딩 후 각 pane의 활성 렌더러에 applySeed를 부른다.
- index.html: `#chart` 단일 div → `<div id="panes"></div>`. 칸 높이는 % (드래그 핸들은 Task 3에서).

**Steps:**
- [ ] Step 1: mirae-layers.js에 `MiraeRenderer` 모듈 객체 추가 — 위 렌더러 계약(createHandle/setLayers/applyLive/applySeed/clear)을 구현하고, 기존 전역 싱글톤 로직을 handle 기반으로 감싼다 (기존보내기 함수들은 유지, mirae-layers.test.js 깨짐 금지). — 기존 전역 싱글톤 로직을 handle 기반으로 감싼다 (기존보내기 함수들은 유지, 테스트 깨짐 금지).
- [ ] Step 2: app.js 재구성 — createPane이 chart+candleSeries+렌더러 handle을 만든다. 기존 지표 시리즈 생성부를 렌더러로 이동.
- [ ] Step 3: `cd web && npm test` 통과 + `node --check public/app.js`.
- [ ] Step 4: 커밋.

---

### Task 3: 지표 선택 UI + SMA 렌더러 + 화면틀 v2

**Files:**
- Create: `web/public/sma-layers.js`
- Modify: `web/public/app.js`, `web/public/index.html`
- Test: `web/test/workspace.test.js` (신규 또는 기존 테스트 파일에 추가)

**Interfaces:**
- Consumes: Task 2의 RENDERERS/createPane/렌더러 계약, Task 1의 매니페스트(snapshot의 `indicators`)와 `sma` 페이로드/ind[28..31].
- Produces:
  - 칸 도구줄(각 칸 좌상단): `없음 | 지표 칩…`(매니페스트에서 생성, 복수 선택) + 지표가 켜지면 레이어 칩(매니페스트의 layers, defaultOn 반영) + `×` 삭제
  - 화면틀 v2: `{schema_version:2, name, current_symbol, panels:[{height, indicators:[{id, layers:{...}}]}]}`
  - SMA 렌더러: LineSeries 3개 (sma5 `#ff9800`, sma20 `#4db6ac`, sma60 `#ba68c8`, lineWidth 2, priceLineVisible/lastValueVisible false)

**규칙:**
- 기본 상태 = 칸 1개, 지표 없음(맨 차트). "없음" 칩은 선택이 비었을 때 on.
- 헤더 지표 배지(점수/회귀선/예측/상태/호가)는 mirae_v16이 하나라도 켜진 칸이 있을 때만 표시.
- 밴드/결과띠 버튼 제거 → 레이어 칩으로 대체 (mktband=⑧ 기본 off, band=④ 기본 on).
- 구 화면틀(schema_version 없음)은 불러오면 무시하고 기본 상태 유지 + alert.
- `web/public/design-mockup.html` 삭제 (목업 역할 종료).
- 화면틀 저장/불러오기 후에는 모든 칸을 스냅샷에서 다시 시딩하지 않아도 되게 — 공유 데이터는 유지하고 칸만 재구성(applySeed 재호출).

**Steps:**
- [ ] Step 1: `sma-layers.js` — SmaRenderer (계약 구현). 단위 테스트: parseInd가 ind[28..31]을 읽는지(mirae-layers.test.js에 추가).
- [ ] Step 2: app.js — 지표 칩 UI + 레이어 칩 + 패널 드래그 핸들(높이 % 조절) + 화면틀 v2 수집/적용 함수.
- [ ] Step 3: 화면틀 v2 직렬화/적용 단위 테스트 (node:test, DOM 없이 함수 단위로).
- [ ] Step 4: `npm test` 전부 통과 + `node --check`. 커밋.

---

### Task 4: 라이브 검증 (컨트롤러 직접 수행)

- [ ] `./build/trading-engine --live-fut A016C000` + `node web/server.js` 기동
- [ ] Playwright로 확인: ① 기본 = 맨 차트, ② 칸 추가 후 위=미래곡선/아래=이평선, ③ ⑧ 기본 off·④ 기본 on, ④ 화면틀 저장 → 새로고침 → 불러오기 복원, ⑤ 스냅샷에 indicators 매니페스트·ind 33개 확인
- [ ] 스크린샷 캡처로 기록
