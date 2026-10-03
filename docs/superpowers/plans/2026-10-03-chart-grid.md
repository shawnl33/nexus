# 화면틀 격자 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 헤더의 행추가·열추가로 화면틀 격자를 만들고, 각 화면틀 안의 `+ 차트`만 그 틀의 시간축을 공유하게 한다.

**Architecture:** 화면틀 문서의 열 수·열 너비·화면틀 목록은 `workspace.js`의 순수 함수가 직렬화하고 검증한다. `app.js`는 `#panes` 안에 행·화면틀·차트 칸을 만들고, 화면틀마다 `PaneSync` 인스턴스를 하나씩 둔다. `pane-sync.js`의 전파 식은 바꾸지 않는다.

**Tech Stack:** 브라우저 대시보드(`web/public`), `workspace.js` 순수 함수, `node --test`.

**Spec:** `docs/superpowers/specs/2026-10-03-chart-grid-design.md`

## Global Constraints

- 스키마 버전은 2다. `schema_version`이 2가 아니면 `parse`는 `null`이다.
- 행추가·열추가의 새 화면틀은 빈 차트 하나다. 종목·지표·Data2는 없고, 캔들 모양은 캔들바, 지표 패널은 열림이다. 맞닿은 화면틀을 복사하지 않는다.
- `+ 차트`는 화면틀 위에만 둔다. 헤더와 왼쪽 지표 패널에는 두지 않는다. 복사 원본은 그 화면틀에서 마지막으로 다룬 칸이고, 없으면 그 틀의 맨 아래 칸이다.
- 복사하는 값은 종목 코드, 종목명, 지표 id와 레이어, `barStyle`, Data2다. 지표 패널 접힘, 트리 접힘, 검색 입력, 보이는 시간 범위는 복사하지 않는다. 새 칸의 지표 패널은 열린다.
- 원본에 종목이 없으면 새 칸은 빈 차트다.
- 종목이 있으면 `selectPaneSymbol`, `activateIndicator`, `setPaneData2`, `setBarStyle` 순서로만 복사한다.
- 행·열·칸을 더하기 전에 해당 비율의 합을 1로 맞춘다. 새 몫은 `1 / (기존 수 + 1)`이고, 기존 값은 `(기존 수) / (기존 수 + 1)`를 곱한다. 결과가 0.1보다 작아도 추가는 된다.
- 드래그 최소 비율은 0.1이다. 행 경계, 열 경계, 틀 안 차트 경계 모두 맞닿은 둘만 바꾸고 둘 다 0.1 아래로 줄지 않는다.
- 차트 `×`는 그 틀에 차트가 둘 이상이면 그 칸만 지운다. 하나이면 종목·지표·Data2를 지우고 캔들바·패널 열림으로 두며 칸은 남긴다.
- 화면틀 `×`는 행이 둘 이상이면 그 행 전체를, 행이 하나이고 열이 둘 이상이면 그 열 전체를 지운다. 1×1에는 버튼을 두지 않는다.
- 시간축과 크로스헤어는 같은 화면틀만 공유한다. 오른쪽 끝 시각과 px 봉 간격 계산식은 유지한다.
- `+ 차트` 시딩은 그 틀의 현재 보이는 범위를 옮기지 않는다. 시딩이 끝나면 새 칸이 그 범위를 따른다.
- 저장은 격자 전체 문서 하나다. `frames`가 없는 스키마 2는 화면틀 하나, 열 하나로 읽는다.
- 가격 칸 아래 지표 전용 하위 차트, 화면틀마다 다른 저장 문서, 200px 지표 패널 폭 변경은 하지 않는다.
- 엔진, `web/server.js`, `pane-sync.js`의 전파 식은 고치지 않는다.

---

### Task 1: 화면틀 문서와 비율 계산

**Files:**
- Modify: `web/public/workspace.js`
- Test: `web/test/workspace.test.js`

**Interfaces:**
- Consumes: 없음
- Produces:
  - `Workspace.serialize(name: string, frames: {height: number, panels: object[]}[], grid?: {cols?: number, colWeights?: number[]}) -> {schema_version, name, current_symbol, cols, colWeights, frames}`
  - `Workspace.parse(data, isKnownIndicator) -> {symbol, cols, colWeights, frames: {height, panels}[]} | null`
  - `Workspace.normalizeWeights(weights: number[]) -> number[]` (합이 1. 빈 배열은 빈 배열)
  - `Workspace.scaleAdd(weights: number[]) -> number[]` (합을 1로 맞춘 뒤 새 몫 `1/(n+1)`을 붙인다. 빈 배열은 `[1]`에서 시작한다)
  - `Workspace.frameClose(rowCount: number, colCount: number) -> "row" | "col" | "none"`

- [ ] **Step 1: Write the failing test**

`web/test/workspace.test.js`의 기존 `serialize`/`parse` 호출을 화면틀 인자로 바꾸고, 아래 테스트를 파일 끝에 추가한다. 기존 테스트가 `ws.panels` 또는 `parsed.panels`를 보면 `frames[0].panels`로 고친다. `parsed.symbol` 단언은 그대로 둔다.

기존 첫 테스트를 이렇게 바꾼다.

```javascript
test("serialize: 화면틀 v2 형태로 수집한다 (칸별 종목 포함)", () => {
  const ws = W.serialize("내 세트", [
    { height: 1, panels: [
      { height: 0.6, symbol: "A016C000", indicators: [{ id: "mirae_v16", layers: { score: true, mktband: false } }] },
      { height: 0.4, symbol: "005930", indicators: [{ id: "sma", layers: { sma5: true, sma20: true, sma60: false } }] },
    ] },
  ]);
  assert.equal(ws.schema_version, 2);
  assert.equal(ws.name, "내 세트");
  assert.equal(ws.current_symbol, "A016C000");
  assert.equal(ws.cols, 1);
  assert.deepEqual(ws.colWeights, [1]);
  assert.equal(ws.frames[0].panels.length, 2);
  assert.equal(ws.frames[0].panels[0].height, 0.6);
  assert.equal(ws.frames[0].panels[0].symbol, "A016C000");
  assert.equal(ws.frames[0].panels[1].symbol, "005930");
  assert.deepEqual(ws.frames[0].panels[0].indicators[0], { id: "mirae_v16", layers: { score: true, mktband: false } });
  assert.deepEqual(ws.frames[0].panels[1].indicators[0].layers, { sma5: true, sma20: true, sma60: false });

  const src = { height: 1, symbol: "s", indicators: [{ id: "sma", layers: { sma5: true } }] };
  const out = W.serialize("t", [{ height: 1, panels: [src] }]);
  src.indicators[0].layers.sma5 = false;
  assert.equal(out.frames[0].panels[0].indicators[0].layers.sma5, true);

  assert.equal(W.serialize("t", [{ height: 1, panels: [{ height: 1, indicators: [] }] }]).frames[0].panels[0].symbol, "");
});
```

같은 방식으로 나머지 기존 테스트를 고친다. `serialize`의 두 번째 인자는 `[{ height: 1, panels: [기존 칸 객체들] }]`이다. `parse` 결과의 칸은 `parsed.frames[0].panels`다. `panelOpen`, `current_symbol` 폴백, 구 스키마 `null`, 알 수 없는 지표, `barStyle`, 높이 클램프, 빈 `panels`의 기본 칸은 그 칸 목록에서 그대로 검증한다.

파일 끝에 추가한다.

```javascript
test("serialize→parse: 2행 2열의 열 수·너비·행 높이·칸이 유지된다", () => {
  const ws = W.serialize("격자", [
    { height: 0.6, panels: [{ height: 0.7, symbol: "005930", indicators: [{ id: "sma", layers: { sma5: true } }] }] },
    { height: 0.6, panels: [{ height: 1, symbol: "000660", indicators: [] }] },
    { height: 0.4, panels: [{ height: 0.4, symbol: "A016C000", indicators: [] }, { height: 0.6, symbol: "", indicators: [] }] },
    { height: 0.4, panels: [{ height: 1, symbol: "ESZ26", barStyle: "line", indicators: [] }] },
  ], { cols: 2, colWeights: [0.25, 0.75] });
  const parsed = W.parse(JSON.parse(JSON.stringify(ws)), known);
  assert.equal(parsed.cols, 2);
  assert.deepEqual(parsed.colWeights, [0.25, 0.75]);
  assert.equal(parsed.frames.length, 4);
  assert.equal(parsed.frames[0].height, 0.6);
  assert.equal(parsed.frames[1].height, 0.6);
  assert.equal(parsed.frames[2].height, 0.4);
  assert.equal(parsed.frames[2].panels.length, 2);
  assert.equal(parsed.frames[2].panels[0].height, 0.4);
  assert.equal(parsed.frames[2].panels[1].symbol, "");
  assert.equal(parsed.frames[3].panels[0].barStyle, "line");
});

test("parse: frames가 없는 스키마 2는 화면틀 하나, 열 하나다", () => {
  const parsed = W.parse({
    schema_version: 2,
    current_symbol: "005930",
    panels: [{ height: 0.4, symbol: "005930", indicators: [] }, { height: 0.6, symbol: "000660", indicators: [] }],
  }, known);
  assert.equal(parsed.cols, 1);
  assert.deepEqual(parsed.colWeights, [1]);
  assert.equal(parsed.frames.length, 1);
  assert.equal(parsed.frames[0].panels[0].symbol, "005930");
  assert.equal(parsed.frames[0].panels[1].symbol, "000660");
});

test("parse: cols가 화면틀 수를 나누지 못하면 열 하나로 읽고 화면틀은 남긴다", () => {
  const parsed = W.parse({
    schema_version: 2,
    cols: 2,
    colWeights: [0.2, 0.8],
    frames: [
      { height: 0.5, panels: [{ height: 1, symbol: "A", indicators: [] }] },
      { height: 0.5, panels: [{ height: 1, symbol: "B", indicators: [] }] },
      { height: 0.5, panels: [{ height: 1, symbol: "C", indicators: [] }] },
    ],
  }, known);
  assert.equal(parsed.cols, 1);
  assert.deepEqual(parsed.colWeights, [1]);
  assert.deepEqual(parsed.frames.map((f) => f.panels[0].symbol), ["A", "B", "C"]);
});

test("parse: 잘못된 colWeights는 균등 너비다", () => {
  const base = {
    schema_version: 2,
    cols: 2,
    frames: [
      { height: 0.5, panels: [{ height: 1, symbol: "A", indicators: [] }] },
      { height: 0.5, panels: [{ height: 1, symbol: "B", indicators: [] }] },
    ],
  };
  assert.deepEqual(W.parse({ ...base, colWeights: [1] }, known).colWeights, [0.5, 0.5]);
  assert.deepEqual(W.parse({ ...base, colWeights: [0, 1] }, known).colWeights, [0.5, 0.5]);
  assert.deepEqual(W.parse({ ...base, colWeights: [2, 2] }, known).colWeights, [0.5, 0.5]);
  assert.deepEqual(W.parse({ ...base }, known).colWeights, [0.5, 0.5]);
});

test("parse: panels가 없는 화면틀은 빈 차트 하나다", () => {
  const parsed = W.parse({
    schema_version: 2,
    cols: 1,
    frames: [{ height: 1 }],
  }, known);
  assert.deepEqual(parsed.frames[0].panels, [{
    height: 1, symbol: "", panelOpen: true, data2: "", candles: true, barStyle: "candle", indicators: [],
  }]);
});

test("parse: 한 행의 행 높이는 그 행 첫 화면틀의 height다", () => {
  const parsed = W.parse({
    schema_version: 2,
    cols: 2,
    colWeights: [0.5, 0.5],
    frames: [
      { height: 0.7, panels: [{ height: 1, indicators: [] }] },
      { height: 0.1, panels: [{ height: 1, indicators: [] }] },
      { height: 5, panels: [{ height: 1, indicators: [] }] },
      { height: 0.2, panels: [{ height: 1, indicators: [] }] },
    ],
  }, known);
  assert.equal(parsed.frames[0].height, 0.7);
  assert.equal(parsed.frames[1].height, 0.7);
  assert.equal(parsed.frames[2].height, 1);
  assert.equal(parsed.frames[3].height, 1);
});

test("비율: 추가 몫과 화면틀 닫기", () => {
  assert.deepEqual(W.normalizeWeights([2, 2]), [0.5, 0.5]);
  assert.deepEqual(W.normalizeWeights([]), []);
  const added = W.scaleAdd([0.75, 0.25]);
  assert.equal(added.length, 3);
  assert.ok(Math.abs(added[0] - 0.5) < 1e-12);
  assert.ok(Math.abs(added[1] - (0.25 * 2 / 3)) < 1e-12);
  assert.ok(Math.abs(added[2] - (1 / 3)) < 1e-12);
  assert.deepEqual(W.scaleAdd([]), [0.5, 0.5]);
  assert.equal(W.frameClose(2, 2), "row");
  assert.equal(W.frameClose(3, 1), "row");
  assert.equal(W.frameClose(1, 3), "col");
  assert.equal(W.frameClose(1, 1), "none");
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node --test web/test/workspace.test.js`

Expected: FAIL. `serialize`가 `frames`를 만들지 않거나, 기존 인자 형태와 어긋난다.

- [ ] **Step 3: Write minimal implementation**

`web/public/workspace.js`의 칸 객체 변환은 지금 로직을 `panelRecord(p)`로 옮긴다. `serialize`와 `parse`를 아래로 바꾼다. `MIN_HEIGHT`와 칸 필드 정규화(종목, panelOpen, data2, barStyle, candles, indicators, 알 수 없는 지표 제거)는 유지한다.

```javascript
function normalizeWeights(weights) {
  if (!weights.length) return [];
  const nums = weights.map((w) => Number(w));
  const total = nums.reduce((s, w) => s + (Number.isFinite(w) ? w : 0), 0);
  if (!(total > 0)) return nums.map(() => 1 / nums.length);
  return nums.map((w) => (Number.isFinite(w) ? w : 0) / total);
}

function scaleAdd(weights) {
  const base = normalizeWeights(weights.length ? weights : [1]);
  const n = base.length;
  const factor = n / (n + 1);
  return [...base.map((w) => w * factor), 1 / (n + 1)];
}

function frameClose(rowCount, colCount) {
  if (rowCount >= 2) return "row";
  if (rowCount === 1 && colCount >= 2) return "col";
  return "none";
}

function equalWeights(cols) {
  return Array.from({ length: cols }, () => 1 / cols);
}

function weightsOrEqual(given, cols) {
  if (!Array.isArray(given) || given.length !== cols) return equalWeights(cols);
  const nums = given.map((w) => Number(w));
  if (nums.some((w) => !(w > 0))) return equalWeights(cols);
  const total = nums.reduce((s, w) => s + w, 0);
  if (!(total > 0)) return equalWeights(cols);
  return nums.map((w) => w / total);
}

function clampHeight(h) {
  const n = Number(h);
  return Number.isFinite(n) ? Math.min(1, Math.max(MIN_HEIGHT, n)) : 1;
}

function serialize(name, frames, grid) {
  const cols = Number.isInteger(grid?.cols) && grid.cols >= 1 ? grid.cols : 1;
  const colWeights = weightsOrEqual(grid?.colWeights, cols);
  const outFrames = (frames ?? []).map((f) => ({
    height: f?.height,
    panels: (f?.panels ?? []).map((p) => panelRecord(p)),
  }));
  return {
    schema_version: SCHEMA_VERSION,
    name,
    current_symbol: outFrames[0]?.panels?.[0]?.symbol ?? "",
    cols,
    colWeights,
    frames: outFrames,
  };
}

function parse(data, isKnownIndicator) {
  if (!data || data.schema_version !== SCHEMA_VERSION) return null;
  const fallbackSymbol = typeof data.current_symbol === "string" ? data.current_symbol : "";
  const useFrames = Array.isArray(data.frames) && data.frames.length > 0;
  const raw = useFrames
    ? data.frames.map((f) => ({ height: f?.height, panels: readPanels(f?.panels, fallbackSymbol, isKnownIndicator) }))
    : [{ height: 1, panels: readPanels(data.panels, fallbackSymbol, isKnownIndicator) }];
  let cols = Number.isInteger(data.cols) && data.cols >= 1 ? data.cols : 1;
  let keepWeights = true;
  if (raw.length % cols !== 0) {
    cols = 1;
    keepWeights = false;
  }
  const frames = [];
  const rows = raw.length / cols;
  for (let r = 0; r < rows; r++) {
    const height = clampHeight(raw[r * cols]?.height);
    for (let c = 0; c < cols; c++) frames.push({ height, panels: raw[r * cols + c].panels });
  }
  return {
    symbol: fallbackSymbol,
    cols,
    colWeights: keepWeights ? weightsOrEqual(data.colWeights, cols) : equalWeights(cols),
    frames,
  };
}
```

`readPanels`는 지금의 `parse` 루프다. 결과가 0칸이면 지금과 같은 빈 차트 하나를 넣는다. `panelRecord`는 지금의 `serialize` 맵이다. 반환 객체에 `normalizeWeights`, `scaleAdd`, `frameClose`를 포함한다.

- [ ] **Step 4: Run test to verify it passes**

Run: `node --test web/test/workspace.test.js`

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add web/public/workspace.js web/test/workspace.test.js
git commit -m "화면틀 문서에 열과 화면틀 목록을 담는다"
```

---

### Task 2: 화면틀 격자와 화면틀별 시간축

**Files:**
- Modify: `web/public/index.html`
- Modify: `web/public/app.js` (`createPane`, `syncPaneSize`, `removePane`, `rebuildResizeBars`, `attachResize`, `mutePaneRange`, `mutePaneRangeForSeeding`, `captureAlignView`, `applyAlignView`, `alignAllPanes`, `bootstrap`, 헤더 클릭)

**Interfaces:**
- Consumes: `Workspace.scaleAdd`, `Workspace.normalizeWeights`, `PaneSync.create`
- Produces:
  - `gridRows: {el, heightFrac, frames: Frame[]}[]`
  - `colWeights: number[]`
  - `Frame = {el, bodyEl, closeEl, row, sync, panes, currentPane}`
  - `makeRow(heightFrac) -> {el, heightFrac, frames: Frame[]}`
  - `createFrame(row) -> Frame`
  - `createPane(frame, heightFrac) -> pane` (`pane.frame`이 그 화면틀)
  - `addFrameRow()`, `addFrameCol()`
  - `layoutGrid()`
  - `captureAlignView(frame) -> {spacingPx, rightTime} | null`
  - `applyAlignView(frame, view, settle)`
  - `alignFramePanes(frame, settle)`

- [ ] **Step 1: 헤더 버튼과 격자 CSS**

`web/public/index.html`에서 `#pane-add` 버튼을 다음으로 바꾼다.

```html
<button id="row-add" title="맨 아래에 화면틀 행을 추가한다">행추가</button>
<button id="col-add" title="맨 오른쪽에 화면틀 열을 추가한다">열추가</button>
```

`#panes` 규칙 다음에 넣는다.

```css
#panes { position: relative; }
.pane-row { display: flex; flex-direction: row; width: 100%; min-height: 0; flex: none; position: relative; }
.frame { display: flex; flex-direction: column; min-width: 0; min-height: 0; flex: none; box-sizing: border-box; border-right: 1px solid #2a2e39; border-bottom: 1px solid #2a2e39; }
.frame-tools { flex: none; display: flex; gap: 5px; align-items: center; padding: 3px 6px; background: #18202c; border-bottom: 1px solid #2a2e39; }
.frame-tools .frame-close { margin-left: auto; }
.frame-body { flex: 1 1 0; min-height: 0; display: flex; flex-direction: column; position: relative; }
#panes .pane { width: 100%; }
.resizebar.col { position: absolute; top: 0; bottom: 0; width: 6px; margin-left: -3px; z-index: 5; cursor: ew-resize; }
.resizebar.row { position: absolute; left: 0; right: 0; bottom: -3px; height: 6px; z-index: 5; cursor: ns-resize; }
```

기존 `.resizebar`의 `bottom/left/right/height`는 틀 안 차트 경계용으로 남긴다.

- [ ] **Step 2: 격자 상태와 빈 화면틀 추가**

`const paneSync = PaneSync.create()`를 지운다. 대신 다음을 둔다.

```javascript
let gridRows = [];
let colWeights = [1];
let currentFrame = null;

function allFrames() {
  return gridRows.flatMap((row) => row.frames);
}

function syncPaneList() {
  const next = allFrames().flatMap((frame) => frame.panes);
  panes.splice(0, panes.length, ...next);
}

function layoutGrid() {
  const rowH = Workspace.normalizeWeights(gridRows.map((row) => row.heightFrac));
  gridRows.forEach((row, i) => { row.heightFrac = rowH[i]; });
  colWeights = Workspace.normalizeWeights(colWeights);
  for (const row of gridRows) {
    row.el.style.height = `${row.heightFrac * 100}%`;
    row.frames.forEach((frame, c) => {
      frame.el.style.width = `${colWeights[c] * 100}%`;
      const paneH = Workspace.normalizeWeights(frame.panes.map((p) => p.heightFrac));
      frame.panes.forEach((pane, i) => {
        pane.heightFrac = paneH[i] ?? 1;
        pane.el.style.height = `${pane.heightFrac * 100}%`;
        pane.chart.resize(pane.chartEl.clientWidth, pane.chartEl.clientHeight);
      });
    });
  }
  rebuildResizeBars();
  updateFrameCloseButtons();
}
```

`createFrame(row)`는 `PaneSync.create()`를 `frame.sync`에 둔다. 도구줄에 `+ 차트`(title `이 화면틀에 차트를 추가한다`, 클릭은 Task 3의 `addChart(frame)`)와 `×`(class `frame-close`, 클릭은 Task 3의 `closeFrame(frame)`)를 넣는다. 본문 요소 class는 `frame-body`다. `row.el`에 프레임 요소를 붙이고 `row.frames`에 넣는다.

`createPane(frame, heightFrac)`는 칸 요소를 `frame.bodyEl`에 붙인다. `pane.frame = frame`, `frame.panes.push(pane)`, `syncPaneList()`를 한다. `pane.syncHandle = frame.sync.add(...)`로 지금 `paneSync.add` 인자 그대로 등록한다. `#panes`에 직접 붙이지 않는다. 칸 `pointerdown`은 `currentPane = pane`과 `frame.currentPane = pane`, `currentFrame = frame`을 같이 적는다.

`addFrameRow`는 `Workspace.scaleAdd(gridRows.map(r => r.heightFrac))`로 행 높이를 바꾸고, 새 행을 아래에 만든 뒤 `colWeights.length`개의 빈 화면틀을 왼쪽부터 만든다. 각 틀에 `createPane(frame, 1)`을 한다. `layoutGrid()` 뒤 `currentFrame`은 새 행의 마지막 틀이다.

`addFrameCol`은 `Workspace.scaleAdd(colWeights)`로 열 너비를 바꾸고, 각 행의 맨 오른쪽에 빈 화면틀과 빈 차트 하나를 위부터 만든다. `currentFrame`은 마지막 새 틀이다.

`bootstrap`의 `createPane(1)`은 행 하나, 화면틀 하나, 그 안에 `createPane(frame, 1)`로 바꾼다. 엔진 관측 종목이 하나일 때의 `selectPaneSymbol(panes[0], ...)`는 그대로 둔다.

헤더 연결:

```javascript
document.getElementById("row-add").onclick = addFrameRow;
document.getElementById("col-add").onclick = addFrameCol;
```

`document.getElementById("pane-add")` 줄은 지운다.

- [ ] **Step 3: 크기 조절과 시간축을 화면틀로 제한**

`mutePaneRange`와 `mutePaneRangeForSeeding`는 `paneSync.mute` 대신 `pane.frame.sync.mute` / `unmute`를 쓴다.

`captureAlignView(frame)`는 `frame.panes`만 본다. `applyAlignView(frame, view, settle)`도 `frame.panes`만 맞춘다. `alignFramePanes(frame, settle)`는 그 틀만 캡처해서 적용한다.

`ResizeObserver` 콜백은 크기가 바뀐 `.chart-host`의 칸을 찾아 `alignFramePanes(pane.frame, false)`만 부른다. 다른 화면틀은 부르지 않는다.

`attachResize(bar, above, below, measureEl)`의 기준 높이는 `measureEl.clientHeight`다. 틀 안 차트 경계의 `measureEl`은 `frame.bodyEl`이다. 행 경계의 `measureEl`은 `#panes`이고, 바꾸는 값은 두 행의 `heightFrac`이다. 열 경계는 `#panes` 폭 기준이고 `colWeights[c]`, `colWeights[c+1]`만 바꾼다. 하한은 모두 `MIN_PANE_FRAC`(0.1)이다. 드래그가 끝나면 `layoutGrid()`를 부른다.

`rebuildResizeBars`는 `#panes` 안의 `.resizebar`를 지운 뒤 다음만 만든다.

- 각 화면틀 안에서 위아래 칸 사이 `.resizebar`
- 마지막 행을 제외한 각 행 아래 `.resizebar.row`
- 마지막 열을 제외한 각 열 경계 `.resizebar.col` 하나. `left`는 그 열까지의 `colWeights` 합의 퍼센트다. 행마다 따로 만들지 않는다.

`removePane`의 높이 재분배는 그 칸의 `frame.panes`만 대상으로 한다. `pane.frame.sync.remove(pane.syncHandle)`를 쓴다. 이 태스크에서는 `×`가 아직 마지막 칸을 지울 수 있다. Task 3에서 막는다.

- [ ] **Step 4: 브라우저에서 빈 격자와 시간축 분리를 확인**

대시보드 `http://127.0.0.1:18080`에서:

- 시작은 화면틀 하나, 빈 차트 하나다. 헤더에 `행추가`, `열추가`가 있고 `+ 차트`는 없다.
- 열추가 뒤 오른쪽 화면틀은 빈 차트 하나다. 행추가 뒤 아래 행의 두 틀도 빈 차트 하나다.
- 열 경계를 드래그하면 위아래 행의 세로 경계가 함께 움직이고, 어느 열도 화면의 10% 미만으로 줄지 않는다.
- 콘솔에서 `allFrames().every((f, i, arr) => arr.every((g) => g === f || g.sync !== f.sync))`가 참이다. 같은 화면틀의 칸은 `pane.frame.sync`가 그 틀의 `sync`와 같다.

Run: `node --test web/test/workspace.test.js`

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add web/public/index.html web/public/app.js
git commit -m "화면틀 격자와 화면틀별 시간축을 둔다"
```

---

### Task 3: 틀 안 차트 추가와 삭제

**Files:**
- Modify: `web/public/app.js` (`addPane`을 `addChart(frame)`으로, `buildPaneToolButtons`의 `×`, `removePane`)

**Interfaces:**
- Consumes: `Frame.currentPane`, `Frame.panes`, `Frame.sync`, `createPane`, `layoutGrid`, `Workspace.scaleAdd`, `selectPaneSymbol`, `activateIndicator`, `setPaneData2`, `setBarStyle`, `withChartLoad`, `releaseSymbol`
- Produces: `addChart(frame)`, `clearPaneContent(pane)`, `closeFrame(frame)`

- [ ] **Step 1: 틀 안의 `+ 차트`**

`addPane`을 지우고 `addChart(frame)`을 둔다. `createFrame`의 `+ 차트` 클릭이 이것을 부른다.

```javascript
async function addChart(frame) {
  const src = frame.currentPane && frame.panes.includes(frame.currentPane)
    ? frame.currentPane
    : frame.panes[frame.panes.length - 1];
  const view = captureAlignView(frame);
  const next = Workspace.scaleAdd(frame.panes.map((p) => p.heightFrac));
  frame.panes.forEach((p, i) => { p.heightFrac = next[i]; });
  const pane = createPane(frame, next[next.length - 1]);
  frame.currentPane = pane;
  currentPane = pane;
  currentFrame = frame;
  layoutGrid();
  if (!src?.symbol) return;
  await withChartLoad(async () => {
    await selectPaneSymbol(pane, src.symbol, src.symName);
    if (pane.symbol !== src.symbol) return;
    for (const [id, entry] of src.active) activateIndicator(pane, id, entry.layers);
    if (src.data2) await setPaneData2(pane, src.data2);
    setBarStyle(pane, src.barStyle || "candle");
    buildPaneTools(pane);
    if (view) applyAlignView(frame, view, true);
  });
}
```

`selectPaneSymbol`이 이미 `withChartLoad`를 쓰므로 베일 카운터가 중첩되어도 바깥 호출이 끝날 때까지 유지된다. 시딩 중 `mutePaneRangeForSeeding`이 새 칸의 범위 이벤트가 틀의 다른 칸으로 퍼지는 것을 막는다. 끝난 뒤 `view`가 있으면 그 틀만 다시 맞춘다.

- [ ] **Step 2: 차트 `×`와 화면틀 `×`**

칸 `×`의 title은 `이 차트 삭제`다. 클릭은 `deleteChart(pane)`다.

```javascript
function clearPaneContent(pane) {
  const prev = pane.symbol;
  const prevData2 = pane.data2;
  pane.symbol = "";
  pane.symName = "";
  pane.selTarget = "";
  pane.data2 = "";
  for (const id of [...pane.active.keys()]) deactivateIndicator(pane, id);
  setBarStyle(pane, "candle");
  setPanelOpen(pane, true);
  syncPaneSymbolUi(pane);
  buildPaneTools(pane);
  if (prev) releaseSymbol(prev);
  if (prevData2 && prevData2 !== prev) releaseSymbol(prevData2);
}

function deleteChart(pane) {
  const frame = pane.frame;
  if (frame.panes.length <= 1) {
    clearPaneContent(pane);
    return;
  }
  removePane(pane);
  if (frame.currentPane === pane) frame.currentPane = frame.panes[frame.panes.length - 1] || null;
  layoutGrid();
}

function updateFrameCloseButtons() {
  const mode = Workspace.frameClose(gridRows.length, colWeights.length);
  for (const frame of allFrames()) {
    frame.closeEl.hidden = mode === "none";
    frame.closeEl.title = mode === "row" ? "이 행 삭제" : "이 열 삭제";
  }
}

function closeFrame(frame) {
  const mode = Workspace.frameClose(gridRows.length, colWeights.length);
  if (mode === "none") return;
  if (mode === "row") removeFrameRow(frame.row);
  else removeFrameCol(frame.row.frames.indexOf(frame));
}

function removeFrameRow(row) {
  const doomed = row.frames.flatMap((frame) => [...frame.panes]);
  for (const pane of doomed) destroyPane(pane);
  row.el.remove();
  gridRows = gridRows.filter((r) => r !== row);
  const heights = Workspace.normalizeWeights(gridRows.map((r) => r.heightFrac));
  gridRows.forEach((r, i) => { r.heightFrac = heights[i]; });
  if (currentFrame && !allFrames().includes(currentFrame)) currentFrame = allFrames()[0] || null;
  layoutGrid();
}

function removeFrameCol(col) {
  for (const row of gridRows) {
    const frame = row.frames[col];
    for (const pane of [...frame.panes]) destroyPane(pane);
    frame.el.remove();
    row.frames.splice(col, 1);
  }
  colWeights.splice(col, 1);
  colWeights = Workspace.normalizeWeights(colWeights);
  if (currentFrame && !allFrames().includes(currentFrame)) currentFrame = allFrames()[0] || null;
  layoutGrid();
}
```

`destroyPane`은 지금 `removePane`에서 동기화 해지, 차트 제거, DOM 제거, `releaseSymbol`까지 하되, 전역 높이 재분배와 `rebuildResizeBars`는 하지 않는다. `frame.panes`에서 빼고 `syncPaneList()`만 한다. `removePane`은 `destroyPane` 뒤 그 틀의 남은 칸 높이를 `Workspace.normalizeWeights`로 맞춘다.

`clearPaneContent` 전에 `syncPaneSymbolUi`가 빈 종목을 표시하는지 확인한다. 종목명 배지를 숨기고 입력은 비운다. 함수가 없으면 그 두 DOM만 직접 맞춘다.

- [ ] **Step 3: 확인**

Run: `node --test web/test/workspace.test.js`

Expected: PASS

브라우저 `http://127.0.0.1:18080`:

- 화면틀의 `+ 차트`는 그 틀에만 칸을 더한다. 옆 틀의 칸 수는 그대로다.
- 종목이 있는 칸을 그 틀에서 마지막으로 누른 뒤 `+ 차트`를 누르면 새 칸이 그 종목·지표·캔들 모양·Data2를 가진다. 지표 패널은 열린다.
- 빈 칸만 있는 틀의 `+ 차트`는 빈 칸을 더한다.
- 차트가 둘이면 `×`가 그 칸만 지운다. 차트가 하나이면 `×`가 종목을 비우고 틀은 남는다.
- 2×2에서 화면틀 `×`의 title은 `이 행 삭제`이고, 누르면 그 행의 틀이 모두 사라진다. 1행 2열에서 title은 `이 열 삭제`다. 1×1에는 그 버튼이 없다.
- 한 틀을 스크롤하거나 줌해도 다른 틀의 보이는 범위는 그대로다. 같은 틀의 칸은 오른쪽 끝 시각과 봉 간격이 같다.

- [ ] **Step 4: Commit**

```bash
git add web/public/app.js
git commit -m "화면틀 안에서 차트를 더하고 행과 열을 지운다"
```

---

### Task 4: 격자 화면틀 저장과 불러오기

**Files:**
- Modify: `web/public/app.js` (`collectWorkspace`, `applyWorkspace`, `normalizeHeights`)

**Interfaces:**
- Consumes: `Workspace.serialize(name, frames, grid)`, `Workspace.parse`의 `{cols, colWeights, frames}`, `createFrame`, `createPane`, `destroyPane`, `layoutGrid`, `alignFramePanes`
- Produces: 저장 문서가 스펙의 `frames` 형식이다. 예전 `panels`만 있는 문서는 화면틀 하나로 열린다.

- [ ] **Step 1: 저장**

```javascript
function collectWorkspace() {
  const frames = allFrames().map((frame) => ({
    height: frame.row.heightFrac,
    panels: frame.panes.map((pane) => ({
      height: pane.heightFrac,
      symbol: pane.symbol,
      panelOpen: pane.panelOpen,
      data2: pane.data2 || "",
      candles: pane.barStyle !== "none",
      barStyle: pane.barStyle || "candle",
      indicators: [...pane.active.entries()].map(([id, entry]) => ({ id, layers: { ...entry.layers } })),
    })),
  }));
  return Workspace.serialize(el.wsName.value.trim(), frames, {
    cols: colWeights.length,
    colWeights,
  });
}
```

- [ ] **Step 2: 불러오기**

`applyWorkspace(parsed)`는 지금처럼 `withChartLoad` 안에서 기존 칸의 종목을 `release: false`로 걷어 낸 뒤 격자를 다시 만든다.

```javascript
function destroyGrid() {
  for (const pane of [...panes]) destroyPane(pane);
  for (const row of gridRows) row.el.remove();
  gridRows = [];
  colWeights = [1];
  currentFrame = null;
}

async function applyWorkspace(parsed) {
  return withChartLoad(async () => {
    const before = new Set(panes.map((p) => p.symbol).filter(Boolean));
    destroyGrid();
    colWeights = parsed.colWeights.slice();
    const cols = parsed.cols;
    const jobs = [];
    parsed.frames.forEach((spec, index) => {
      let row = gridRows[gridRows.length - 1];
      if (index % cols === 0) row = makeRow(spec.height);
      const frame = createFrame(row);
      for (const panel of spec.panels) {
        const pane = createPane(frame, panel.height);
        setPanelOpen(pane, panel.panelOpen);
        setBarStyle(pane, panel.barStyle || (panel.candles === false ? "none" : "candle"));
        for (const ind of panel.indicators) activateIndicator(pane, ind.id, ind.layers);
        if (panel.data2) pane.data2 = panel.data2;
        buildPaneTools(pane);
        jobs.push((async () => {
          try {
            if (panel.symbol) await selectPaneSymbol(pane, panel.symbol);
            if (panel.data2) await setPaneData2(pane, panel.data2);
          } catch (err) {
            console.error(`화면틀 칸 적용 실패 (${panel.symbol || "?"})`, err);
          }
        })());
      }
    });
    layoutGrid();
    updateBadgeVisibility();
    await Promise.all(jobs);
    for (const pane of panes) {
      const w = pane.chartEl.clientWidth;
      const h = pane.chartEl.clientHeight;
      if (w > 0 && h > 0) pane.chart.resize(w, h);
    }
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    for (const sh of new Set(panes.map((p) => p.symbol).filter(Boolean))) {
      try { renderSymbolPanes(sh); } catch (err) { console.error(`[${sh}] 다시 그리기 실패`, err); }
    }
    for (const sh of new Set(panes.map((p) => p.data2).filter(Boolean))) {
      try { refreshData2(sh); } catch (err) { console.error(`[${sh}] 참조 다시 그리기 실패`, err); }
    }
    for (const frame of allFrames()) {
      try { alignFramePanes(frame, true); } catch (err) { console.error("화면틀 시간축 맞추기 실패", err); }
    }
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    for (const frame of allFrames()) {
      try { alignFramePanes(frame, true); } catch (err) { console.error("화면틀 시간축 맞추기 실패", err); }
    }
    const after = new Set(panes.map((p) => p.symbol).filter(Boolean));
    for (const sh of before) if (!after.has(sh)) await releaseSymbol(sh);
  });
}
```

`makeRow`는 Task 2에서 만든 행 생성이다. `normalizeHeights`가 전 칸 높이를 한 합으로 나누면 지운다. 행 높이와 칸 높이는 `layoutGrid`가 각각 맞춘다.

- [ ] **Step 3: 확인**

Run: `node --test web/test/workspace.test.js`

Expected: PASS

브라우저:

- `frames`가 없는 기존 화면틀을 불러오면 화면틀 하나의 세로 차트 목록이다.
- 2×2를 저장한 뒤 불러오면 열 수, 열 너비, 행 높이, 각 틀의 종목·지표·캔들 모양·Data2가 돌아온다.
- 불러온 뒤에도 틀마다 스크롤이 따로이고, 같은 틀의 칸은 오른쪽 끝 시각과 봉 간격이 같다.

- [ ] **Step 4: Commit**

```bash
git add web/public/app.js
git commit -m "화면틀 격자 전체를 저장하고 예전 문서는 한 틀로 연다"
```

---

### Task 5: 브라우저에서 스펙 전체를 확인

**Files:**
- Modify: 없음. 실패한 동작만 고친다.

**Interfaces:**
- Consumes: Task 1부터 4의 화면

- [ ] **Step 1: 단위 테스트**

Run: `cd web && npm test`

Expected: PASS. `workspace.test.js`가 포함된다.

- [ ] **Step 2: 라이브 대시보드**

`http://127.0.0.1:18080`에서 스펙의 브라우저 검증을 순서대로 한다.

- 행추가는 맨 아래에 빈 차트 하나의 화면틀 행을 만든다.
- 열추가는 맨 오른쪽에 빈 차트 하나의 화면틀 열을 만든다.
- 화면틀의 `+ 차트`는 그 틀에서 보던 칸을 복사한다.
- 행·열·틀 안 경계를 드래그하면 맞닿은 둘만 변하고 10% 미만으로 줄지 않는다. 열 경계는 모든 행이 함께 움직인다.
- 차트가 둘 이상이면 `×`는 그 차트만 지운다. 하나이면 칸을 비우고 틀은 남는다.
- 2행 이상에서 화면틀 빼기는 그 행을 지운다. 1행 2열 이상에서는 그 열을 지운다. 1×1에는 그 버튼이 없다.
- 예전 문서는 화면틀 하나로 열린다. 2×2 저장 후 불러오기가 격자와 칸 내용을 유지한다.
- 같은 화면틀의 칸은 오른쪽 끝 시각과 봉 간격이 같다. 다른 화면틀의 스크롤, 줌, 크로스헤어는 따라가지 않는다.

하나라도 어긋나면 그 태스크의 함수를 고치고 이 단계를 다시 한다.

- [ ] **Step 3: Commit**

고친 파일이 있을 때만 커밋한다.

```bash
git add web/public/app.js web/public/index.html web/public/workspace.js web/test/workspace.test.js
git commit -m "화면틀 격자 확인에서 어긋난 동작을 고친다"
```
