# 다중 종목 파이프라인 설계 (칸별 종목)

날짜: 2026-09-29
상태: 사용자가 접근 방식 A(엔진 다중 종목) 선택

## 목적

차트 칸마다 다른 종목을 볼 수 있게 하고, 칸별 종목이 화면틀에 저장되게 한다.

## 현재 제약

엔진(`tr_engine_t`)은 단일 종목 구조다: 지표 상태(lr3·score·mkt·regmem·persist·dtl1/gap1·sma·봉 링)가 엔진에 하나뿐이고, `market.select`는 전체를 새 종목으로 재초기화한다. 정적 저장소(g_bb_storage 2560봉, g_status_storage, g_mkt_storage)도 하나뿐이다.

## 설계

### 1. 엔진 코어: 파이프라인 분리 (`src/runtime/`)

- **`tr_pipeline_t` 신규** (종목별 상태 전부): instrument_id, shcode, is_futures, bar builder, lr3, htf, obd2, score, sma 3개, mkt, regmem, persist, dtl1/gap1/dalign, 봉 링·상태 링·마켓 링(각 파이프라인이 소유), 세션 마커, generation, bar_index
- **`tr_engine_t`**는 공유부만: cfg, ipc, stream_id, 파이프라인 목록(최대 8종목 — 관측 상한. 더 필요하면 상수만 올림)
- 틱 라우팅: 이벤트의 instrument_id로 파이프라인을 찾아 전달. 현재 realtime 구독이 이미 instrument_id 태깅을 지원
- 상태 발행: publish_status에 파이프라인의 shcode를 싣는다 — status 페이로드에 `"shcode"` 키 추가 (기존 키 불변)
- 기존 단일 종목 API(tr_engine_init/on_tick/select_symbol)는 파이프라인 1개짜리 엔진의 얇은 래퍼로 유지 — 기존 테스트·리플레이 경로 보존

### 2. 명령/스냅샷 (`src/app/main.c`)

- `market.select`(기존): 호환 유지 — 모든 워치를 대체하는 "이 종목만" 동작
- `market.watch {shcode}` 신규: 파이프라인 생성(없으면) + 실시간 구독 + 백필(분봉 + 일봉 프라임, 기존 backfill_minute_bars 경로 재사용) → `{shcode, name, generation, backfilled}`
- `market.unwatch {shcode}` 신규: 구독 해지 + 파이프라인 제거
- `chart.snapshot {shcode, back_index}`: shcode 지정 필수화(없으면 첫 파이프라인 — 구 호환)
- 종목 마스터(t8436+t8467)는 엔진 공용 (기존 그대로)

### 3. 대시보드 서버 (`web/server.js`)

- `POST /api/symbols/watch` → market.watch 프록시 (기존 토큰·Origin 검사)
- `POST /api/symbols/unwatch` → market.unwatch
- `/api/chart?shcode=` 전달
- WS 브리지는 무수정 (전부 전달, 프론트가 shcode로 라우팅)

### 4. 프론트 (`web/public/`)

- 데이터 캐시(bars/barInd/barSeq/tickRaw 등)를 **종목 키(shcode)로 격리** — 같은 종목을 보는 칸은 캐시 공유
- 칸별 종목 선택: 칸 도구줄에 종목 입력(기존 검색 드롭다운 재사용). 종목을 고르면 watch → `/api/chart?shcode=`로 시딩 → 이후 status 중 그 shcode만 반영
- 헤더의 전역 종목 선택은 제거하고 칸별 선택으로 대체. 헤더 배지(점수 등)는 미래곡선이 켜진 첫 칸의 값
- 화면틀 v2 확장: panels[i]에 `"symbol": "A016C000"` 추가 (schema_version 2 유지, symbol 없으면 현재 종목 폴백)
- 신규 파일: `feed.js`(종목별 캐시·구독 관리). app.js는 패널/피드 연결만

### 5. 세대(generation) 정합

파이프라인별 generation. status의 generation + shcode 조합으로 늦은 응답 폐기(기존 §18 규칙의 종목별 확장).

### 6. 검증

- C: ctest 전부 통과 + 다중 파이프라인 테스트(2종목 동시: 각자 회귀/점수 독립, shcode 라우팅)
- 웹: npm test 통과 + 피드 라우팅 단위 테스트
- 라이브: A016C000 + 005930 두 칸 동시 수신, 화면틀 왕복, 시간축·크로스헤어 동기화는 종목과 무관하게 전 칸에 적용(다른 종목 칸에 그 시각 데이터가 없으면 크로스헤어 점만 표시되지 않는다 — 기존 캐시 미스 처리와 동일)

## 범위 밖

- 칸별 다른 주기(1분봉 외)
- 8종목 초과 관측
- 주문/전략의 다중 종목화 (화면 표시만)
