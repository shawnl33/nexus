# 지표 선택형 다중 차트 대시보드 설계

날짜: 2026-09-29
상태: 사용자 승인됨 (목업 `web/public/design-mockup.html`로 시각 확인 후 승인)

## 목적

대시보드를 미래곡선 전용 하드코딩에서 범용 구조로 바꾼다.

- 사용자가 보조지표를 선택해 켜고 끈다 (기본은 지표 없음 = 맨 캔들 차트)
- 차트 칸(패널) 수와 각 칸의 크기를 사용자가 정한다
- 화면틀은 "칸 수 + 칸별 지표 집합(레이어 설정 포함) + 크기"를 저장·불러오는 도구다
- 나중에 새 지표가 엔진에 추가되면 대시보드 골격은 그대로 따라간다

## 경계 (계획서 §18 유지)

- 엔진: 지표 값 계산 + 지표 매니페스트 발행. 화면 레이아웃을 모른다
- 대시보드: 표시 전용. 지표 값을 재계산하지 않는다. 레이아웃·표시 설정만 저장한다
- 엔진 계산 파라미터(지속봉수, 이평선 기간 등)는 이번 범위에서 설정 불가 (엔진 고정 기본값)
- 엔진은 한 번에 한 종목 — 모든 칸은 같은 종목을 본다 (칸별 다른 종목은 별도 과제)

## 구성

### 1. 엔진

- **SMA 모듈 신규**: `src/core/indicators/sma.c` — 종가 SMA 5/20/60. 봉 확정/진행 모두에서 현재 값 제공
- **페이로드 확장**: status에 `"sma":[s5,s20,s60]`, 스냅샷 ind 배열에 `[28..30]` append (기존 0~27 불변)
- **지표 매니페스트**: `chart.snapshot` 응답 페이로드 루트에 `"indicators"` 배열로 1회 제공 — `[{id, name, layers:[{id, name, defaultOn}]}]`
  - 지표 집합은 엔진 기동 설정으로 정해지고 세대(generation) 내에서 바뀌지 않으므로 시딩 시 1회면 충분하다 (매 봉 status에 싣지 않는다)
  - `mirae_v16`: 레이어 ①~⑧ (id: score, reg, rays, band, state, memory, snap, mktband)
  - `sma`: 레이어 sma5, sma20, sma60
- `docs/display_payload.md` 갱신

### 2. 대시보드 프론트엔드

- **렌더러 레지스트리**: 지표 id → 렌더러 객체 `{ mount(pane), applyBar(...), applySeed(...), setLayers(map), clear() }`
  - `mirae_v16` 렌더러: 현재 mirae-layers.js/app.js의 지표 표시부를 이전
  - `sma` 렌더러: LineSeries 3개 (신규 소형 파일)
- **패널 매니저**: 칸 추가/삭제/높이 비율 조절(드래그 핸들), 각 칸은 독립 lightweight-charts 인스턴스 + 자기 지표 집합. 시딩/라이브 데이터는 모든 칸이 공유(같은 종목)
- **지표 선택 UI**: 칸별로 `없음 | 매니페스트의 지표 칩들`(복수 선택). 지표를 켜면 그 지표의 레이어 칩이 매니페스트에서 자동 생성. "없음" = 칸에 지표 없음
- 헤더의 지표 배지(점수·회귀선·예측·상태·호가)는 미래곡선이 하나라도 켜진 칸이 있을 때만 표시

### 3. 화면틀 v2 (JSON)

```json
{
  "schema_version": 2,
  "name": "내 세트",
  "current_symbol": "A016C000",
  "panels": [
    { "height": 0.6, "indicators": [ {"id": "mirae_v16", "layers": {"score": true, "mktband": false, ...}} ] },
    { "height": 0.4, "indicators": [ {"id": "sma", "layers": {"sma5": true, "sma20": true, "sma60": false}} ] }
  ]
}
```

- 기본 상태(첫 방문): 칸 1개 + 지표 없음
- 저장/불러오기: 집합·칸 수·크기 통째로 적용. 구 스키마(schema_version 없음)는 무시하고 기본 상태로 시작
- 서버(web/server.js)는 v2도 그대로 통과시킨다 (검증 로직은 이름/크기 정도만)

### 4. 파일 변경 요약

- 엔진: `src/core/indicators/sma.{c,h}` 신규, `src/runtime/engine.{c,h}` 배선+페이로드, `src/app/main.c` 스냅샷·config, `tests/test_engine.c`·신규 `tests/test_sma.c`
- 프론트: `web/public/app.js` 대폭 재구성(패널 매니저), `web/public/mirae-layers.js`(렌더러 계약 맞춤), `web/public/sma-layers.js` 신규, `web/public/index.html`, `web/test/*.test.js` 갱신
- 문서: `docs/display_payload.md`

### 5. 검증

- C: `ctest` 전부 통과 (SMA 단위 테스트 추가)
- 웹: `npm test` 통과 (레지스트리·화면틀 v2 파서 테스트 추가)
- 라이브: F 2612로 기동해 ① 칸 2개로 나눠 위=미래곡선/아래=이평선, ② 화면틀 저장→새로고침→불러오기로 복원, ③ 없음 상태가 맨 차트인 것 확인

## 범위 밖 (명시)

- 칸별 다른 종목 (엔진 다중 종목 지원 필요)
- 엔진 계산 파라미터의 화면 편집 (지속봉수·최소신뢰도 등)
- 미래곡선 외 새 지표의 실제 계산 (SMA 제외)
