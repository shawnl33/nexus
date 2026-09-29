# 다중 종목 파이프라인 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 차트 칸마다 다른 종목을 볼 수 있게 하고, 칸별 종목이 화면틀에 저장되게 한다.

**Architecture:** 엔진의 지표 상태를 종목별 파이프라인으로 묶어 엔진이 최대 8종목을 동시 계산한다. 발행 메시지에 shcode를 싣고 명령 market.watch/unwatch로 관측을 관리한다. 프론트는 데이터 캐시를 종목별로 격리하고 칸별 종목 선택·라우팅한다.

**Tech Stack:** C99(엔진), 순수 JS + lightweight-charts 4.2.3(프론트), node:test

**Spec:** `docs/superpowers/specs/2026-09-29-multi-symbol-pipelines-design.md`

## Global Constraints

- 기존 단일 종목 API(tr_engine_init/on_tick/select_symbol)는 파이프라인 1개짜리 얇은 래퍼로 유지 — 기존 테스트(test_engine, test_replay 등)와 리플레이 경로가 그대로 동작해야 함
- status 페이로드 기존 키 순서 불변, `"shcode"`는 맨 끝에 추가. ind 배열 인덱스 0~31 불변
- 원본 V16 표시 규칙 불변. 프론트는 지표 값을 재계산하지 않는다
- 가격 raw 규칙(×100). 관측 상한 8종목
- C: `cmake --build build` + `ctest --test-dir build` 통과. 웹: `cd web && npm test` 통과
- 한국어 커밋 메시지

---

### Task 1: 엔진 코어 — 파이프라인 분리 (동작 동등)

**Files:**
- Modify: `src/runtime/engine.h`, `src/runtime/engine.c`
- Modify: `src/app/main.c` (저장소 풀 배정, 기존 전역 배열과의 호환)

**Interfaces:**
- Produces (Task 2가 소비):
  - `tr_pipeline_t` — 종목별 상태 전부를 갖는 구조체 (engine.h에 공개)
  - `tr_engine_t` = 공유부(cfg, ipc, stream_id) + `tr_pipeline_t *pipes[8]` + `int pipe_count`
  - `tr_pipeline_t *tr_engine_pipe_find(tr_engine_t *e, uint64_t instrument_id)` / `tr_engine_pipe_add(...)` / `tr_engine_pipe_remove(...)`
  - 기존 API 래퍼: tr_engine_init은 파이프라인 1개 생성, on_tick은 instrument_id로 라우팅

**규칙:**
- tr_engine_t에 있던 종목 종속 상태 전부(bb, lr3, htf, obd2, score, sma 3개, mkt, regmem, persist, dtl1/gap1/dalign, 링 3개, 세션 마커, generation, bar_index, has_prev_*)를 tr_pipeline_t로 이동
- 저장소(봉 링·상태 링·마켓 링 버퍼)는 파이프라인별 소유 — 정적 전역은 최대 8개분으로 `static tr_candle_t g_bb_pool[8][BB_CAP]` 형태로 main.c가 배정하거나, 파이프라인이 내부 버퍼를 소유. main.c의 기존 전역 배열은 pool[0]과 호환되게
- publish_status는 파이프라인을 받아 동작. 이 태스크에서는 아직 shcode를 싣지 않는다(Task 2)
- select_symbol = 기존 동작(파이프라인 0 재초기화)

**Steps:**
- [ ] Step 1: engine.h에 tr_pipeline_t 정의 + 파이프라인 API 선언. engine.c에서 상태 이동(순수 이관, 로직 변경 없음)
- [ ] Step 2: `cmake --build build && ctest --test-dir build` — 기존 37개 테스트가 무수정 통과해야 한다(동작 동등의 증거)
- [ ] Step 3: 커밋

---

### Task 2: 엔진 다중 관측 — watch/unwatch + shcode 발행

**Files:**
- Modify: `src/runtime/engine.c` (publish_status에 shcode), `src/app/main.c` (명령 핸들러)
- Modify: `tests/test_engine.c` (2파이프라인 독립성)
- Modify: `docs/display_payload.md`

**Interfaces:**
- Consumes: Task 1의 tr_pipeline_t/파이프라인 API
- Produces:
  - status 페이로드 맨 끝 키: `"shcode":"A016C000"` (파이프라인의 shcode; 리플레이는 "" 가능)
  - 명령 `market.watch {shcode}` → 파이프라인 생성(없으면)+실시간 구독+백필 → `{shcode,name,generation,backfilled}`; 이미 있으면 현 상태 응답
  - 명령 `market.unwatch {shcode}` → 구독 해지+파이프라인 제거. 마지막 1개는 제거 거부(rejected)
  - `chart.snapshot {shcode?, back_index}` — shcode 없으면 첫 파이프라인(구 호환)
  - 파이프라인별 generation (market.select는 전체 대체로 유지)

**규칙:**
- main.c의 live 컨텍스트: 단일 shcode 필드를 관측 목록으로 확장. market.select(기존)은 모든 워치 제거 후 watch 1개로 동작하도록 재구성
- 백필은 watch 시점에 backfill_minute_bars(일봉 프라임 포함)를 그대로 재사용
- 8종목 초과 watch는 rejected("watch_limit")
- 라이브 틱 라우팅: instrument_id로 파이프라인 조회, 없으면 드롭(구독 해지된 채널의 지연 메시지)

**Steps:**
- [ ] Step 1: publish_status에 shcode 추가 + docs 갱신
- [ ] Step 2: test_engine.c에 2파이프라인 테스트 — 서로 다른 틱을 instrument_id로 라우팅해 각자 독립 회귀/점수, shcode가 페이로드에 실리는지
- [ ] Step 3: market.watch/unwatch/snapshot shcode 구현
- [ ] Step 4: `ctest` 전부 통과 후 커밋

---

### Task 3: 서버 프록시

**Files:**
- Modify: `web/server.js` — `POST /api/symbols/watch`, `POST /api/symbols/unwatch` (mutationAllowed 검사), `/api/chart`에 shcode 쿼리 전달
- Modify: `web/test/server.test.js` — 새 엔드포인트의 인증/검증 거부 테스트

**Interfaces:**
- Consumes: Task 2의 market.watch/unwatch 명령
- Produces: `/api/symbols/watch` {shcode} → 엔진 응답 그대로 전달(502 매핑은 기존 select 패턴)

**Steps:**
- [ ] Step 1: 엔드포인트 2개 + chart shcode 전달 (기존 /api/symbols/select 패턴 복제)
- [ ] Step 2: 테스트 갱신 + npm test 통과 + 커밋

---

### Task 4: 프론트 — 종목별 피드 + 칸별 종목 선택 + 화면틀 symbol

**Files:**
- Create: `web/public/feed.js` — 종목별 캐시(bars/barInd/barSeq/tickRaw 등)와 구독 관리
- Modify: `web/public/app.js`, `web/public/index.html`, `web/public/workspace.js`, `web/public/pane-sync.js`(필요시)
- Modify: `web/test/workspace.test.js` + 신규 feed 테스트

**Interfaces:**
- Consumes: Task 3의 서버 API, Task 1의 매니페스트, status의 shcode
- Produces:
  - `Feed.forSymbol(shcode)` → 캐시 객체 {bars, barInd, barSeq, barPos, tickRaw, lastMktCenter…} (없으면 생성)
  - 칸 도구줄에 종목 입력+검색 드롭다운(기존 헤더 검색 로직 이동). 종목 선택 시: `/api/symbols/watch` → `/api/chart?shcode=` 시딩 → 그 칸은 이후 그 shcode의 status만 반영
  - status 라우팅: payload.shcode → 해당 캐시 갱신 + 그 종목을 보는 모든 칸의 렌더러 applyLive
  - 화면틀: panels[i].symbol 저장/복원 (symbol 없는 구 화면틀은 현재 종목 폴백)
  - 헤더의 전역 종목 선택 제거. 헤더 배지는 mirae_v16이 켜진 첫 칸의 캐시 값
  - pane-sync(시간축/크로스헤어)는 종목 무관 전 칸 유지

**규칙:**
- 종목 미선택 칸 = 빈 차트(캔들도 없음) + 종목 입력만
- 기본 상태: 칸 1개 + 종목 미선택(엔진의 첫 파이프라인이 있으면 그 종목을 제안하되 자동 선택은 하지 않는다 — 맨 차트 기본 원칙 유지). 단, 기존 사용자 흐름 보호를 위해 시딩 시 엔진이 이미 관측 중인 종목이 하나뿐이면 그 종목을 칸 1에 자동 설정한다
- applyStatus/seedChart는 캐시 단위로 동작하도록 분리

**Steps:**
- [ ] Step 1: feed.js + 단위 테스트(캐시 격리, 라우팅)
- [ ] Step 2: app.js — 칸별 종목 선택/시딩/라이브 라우팅, 헤더 정리
- [ ] Step 3: workspace.js — panels[i].symbol 왕복 + 테스트
- [ ] Step 4: npm test 전부 통과 + node --check. 커밋

---

### Task 5: 라이브 검증 (컨트롤러)

- [ ] 엔진 라이브 기동 → 칸1 A016C000(미래곡선) + 칸2 005930(이평선) 동시 수신 스크린샷
- [ ] 화면틀 저장/새로고침/불러오기 왕복 (칸별 종목 포함)
- [ ] 시간축/크로스헤어가 종목이 다른 칸에도 동기화되는지 확인
- [ ] 최종 리뷰 디스패치
