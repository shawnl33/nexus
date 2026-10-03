// 화면틀 v2(workspace.js) 직렬화/파싱 단위 테스트.
// DOM 없이 순수 함수만 검증한다: 열·화면틀 왕복, 구 panels 문서,
// 구 스키마 거부, 알 수 없는 지표 필터, 높이 클램프, current_symbol 폴백.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/workspace.js");
const W = globalThis.Workspace;

const KNOWN = new Set(["mirae_v16", "sma"]);
const known = (id) => KNOWN.has(id);

test("serialize: 화면틀 v2 형태로 수집한다 (칸별 종목 포함)", () => {
  const ws = W.serialize("내 세트", [
    { height: 1, panels: [
      { height: 0.6, symbol: "A016C000", indicators: [{ id: "mirae_v16", layers: { score: true, mktband: false } }] },
      { height: 0.4, symbol: "005930", indicators: [{ id: "sma", layers: { sma5: true, sma20: true, sma60: false } }] },
    ] },
  ]);
  assert.equal(ws.schema_version, 2);
  assert.equal(ws.name, "내 세트");
  assert.equal(ws.current_symbol, "A016C000"); // 구 독자 호환: 첫 칸 종목
  assert.equal(ws.cols, 1);
  assert.deepEqual(ws.colWeights, [1]);
  assert.equal(ws.frames[0].panels.length, 2);
  assert.equal(ws.frames[0].panels[0].height, 0.6);
  assert.equal(ws.frames[0].panels[0].symbol, "A016C000");
  assert.equal(ws.frames[0].panels[1].symbol, "005930");
  assert.deepEqual(ws.frames[0].panels[0].indicators[0], { id: "mirae_v16", layers: { score: true, mktband: false } });
  assert.deepEqual(ws.frames[0].panels[1].indicators[0].layers, { sma5: true, sma20: true, sma60: false });

  // layers 객체는 복사본이어야 한다 (원본 변경이 저장본에 영향 없음)
  const src = { height: 1, symbol: "s", indicators: [{ id: "sma", layers: { sma5: true } }] };
  const out = W.serialize("t", [{ height: 1, panels: [src] }]);
  src.indicators[0].layers.sma5 = false;
  assert.equal(out.frames[0].panels[0].indicators[0].layers.sma5, true);

  // 종목 미선택 칸은 빈 문자열로 저장한다
  assert.equal(W.serialize("t", [{ height: 1, panels: [{ height: 1, indicators: [] }] }]).frames[0].panels[0].symbol, "");
});

test("serialize→parse 왕복: 칸 수·종목·지표·레이어·높이가 보존된다", () => {
  const ws = W.serialize("왕복", [
    { height: 1, panels: [
      { height: 0.7, symbol: "005930", indicators: [{ id: "mirae_v16", layers: { band: true, mktband: false } }] },
      { height: 0.3, symbol: "", indicators: [] },
    ] },
  ]);
  const parsed = W.parse(JSON.parse(JSON.stringify(ws)), known);
  assert.equal(parsed.frames[0].panels.length, 2);
  assert.equal(parsed.frames[0].panels[0].height, 0.7);
  assert.equal(parsed.frames[0].panels[0].symbol, "005930");
  assert.equal(parsed.frames[0].panels[1].symbol, ""); // 미선택 칸은 미선택으로 돌아온다
  assert.deepEqual(parsed.frames[0].panels[0].indicators, [{ id: "mirae_v16", layers: { band: true, mktband: false } }]);
  assert.deepEqual(parsed.frames[0].panels[1].indicators, []);
});

test("panelOpen: serialize는 접힘(false)만 남기고 기본은 열림(true)으로 저장한다", () => {
  const ws = W.serialize("패널", [
    { height: 1, panels: [
      { height: 0.5, symbol: "005930", panelOpen: false, indicators: [] },
      { height: 0.5, symbol: "", indicators: [] }, // panelOpen 생략 → 열림
    ] },
  ]);
  assert.equal(ws.frames[0].panels[0].panelOpen, false);
  assert.equal(ws.frames[0].panels[1].panelOpen, true);

  // 왕복: 접힌 칸은 접힌 채로 돌아온다
  const parsed = W.parse(JSON.parse(JSON.stringify(ws)), known);
  assert.equal(parsed.frames[0].panels[0].panelOpen, false);
  assert.equal(parsed.frames[0].panels[1].panelOpen, true);
});

test("panelOpen: 키가 없는 구 화면틀은 기본값(열림)으로 읽고, 명시 false만 접힘이다", () => {
  const parsed = W.parse({
    schema_version: 2,
    panels: [
      { height: 0.5, symbol: "005930", indicators: [] },              // panelOpen 키 없음 (구 저장본)
      { height: 0.5, symbol: "", panelOpen: false, indicators: [] },  // 접힘
      { height: 0.5, symbol: "", panelOpen: 0, indicators: [] },      // false 아닌 값은 열림 취급
    ],
  }, known);
  assert.equal(parsed.frames[0].panels[0].panelOpen, true);
  assert.equal(parsed.frames[0].panels[1].panelOpen, false);
  assert.equal(parsed.frames[0].panels[2].panelOpen, true);
});

test("parse: panels[i].symbol이 없는 구 v2 화면틀은 current_symbol로 폴백한다", () => {
  const parsed = W.parse({
    schema_version: 2,
    current_symbol: "A016C000",
    panels: [
      { height: 0.5, indicators: [{ id: "sma", layers: {} }] }, // symbol 키 없음 (구 저장본)
      { height: 0.5, symbol: "000660", indicators: [] },        // 칸별 종목이 있으면 그 값이 우선
    ],
  }, known);
  assert.equal(parsed.symbol, "A016C000");
  assert.equal(parsed.frames[0].panels[0].symbol, "A016C000");
  assert.equal(parsed.frames[0].panels[1].symbol, "000660");

  // current_symbol도 없으면 미선택("")이다
  const bare = W.parse({ schema_version: 2, panels: [{ height: 1, indicators: [] }] }, known);
  assert.equal(bare.frames[0].panels[0].symbol, "");
});

test("parse: 구 스키마(schema_version 없음/1)는 null로 거부한다", () => {
  // v1 이전 파일 (서버가 schema_version 1을 붙여 저장한 구 화면틀 포함)
  assert.equal(W.parse({ panels: [{ id: "chart" }], current_symbol: "1" }, known), null);
  assert.equal(W.parse({ schema_version: 1, panels: [] }, known), null);
  assert.equal(W.parse(null, known), null);
  assert.equal(W.parse("x", known), null);
});

test("parse: 알 수 없는 지표는 걸러내고 레이어를 bool로 정규화한다", () => {
  const parsed = W.parse({
    schema_version: 2,
    panels: [{
      height: 1,
      symbol: "005930",
      indicators: [
        { id: "mirae_v16", layers: { band: 1, mktband: 0 } },
        { id: "future_indicator", layers: {} }, // 이 프론트가 모르는 지표
        { id: 42 },                             // 잘못된 형태
      ],
    }],
  }, known);
  assert.equal(parsed.frames[0].panels[0].symbol, "005930");
  assert.equal(parsed.frames[0].panels[0].indicators.length, 1);
  assert.deepEqual(parsed.frames[0].panels[0].indicators[0], { id: "mirae_v16", layers: { band: true, mktband: false } });
});

test("parse: 분봉 테두리는 유지하고, 예전 몸통 테두리는 분봉 테두리로 읽는다", () => {
  const ws = W.serialize("테두리", [{ height: 1, panels: [{ height: 1, symbol: "ESZ26", barStyle: "outline", indicators: [] }] }]);
  assert.equal(ws.frames[0].panels[0].barStyle, "outline");
  assert.equal(W.parse(ws, known).frames[0].panels[0].barStyle, "outline");
  const old = W.parse({ schema_version: 2, panels: [{ height: 1, barStyle: "candle", candleBody: "outline", indicators: [] }] }, known);
  assert.equal(old.frames[0].panels[0].barStyle, "outline");
});

test("parse: 높이는 0.1~1로 클램프하고, 칸이 없으면 맨 차트 1칸이 기본이다", () => {
  assert.equal(W.MIN_HEIGHT, 0.1); // app.js 드래그 최소 높이(MIN_PANE_FRAC)와 같은 값이어야 한다
  const parsed = W.parse({
    schema_version: 2,
    current_symbol: "005930",
    panels: [{ height: 5, indicators: [] }, { height: -1, indicators: [] }, { indicators: [] }],
  }, known);
  assert.equal(parsed.frames[0].panels[0].height, 1);
  assert.equal(parsed.frames[0].panels[1].height, W.MIN_HEIGHT);
  assert.equal(parsed.frames[0].panels[2].height, 1); // height 누락

  // 빈 화면틀의 기본 1칸도 current_symbol 폴백을 받는다
  const empty = W.parse({ schema_version: 2, current_symbol: "005930", panels: [] }, known);
  assert.deepEqual(empty.frames[0].panels, [{ height: 1, symbol: "005930", panelOpen: true, data2: "", candles: true, barStyle: "candle", indicators: [] }]);

  const sym = W.parse({ schema_version: 2, panels: [] }, known);
  assert.equal(sym.symbol, ""); // current_symbol 누락/비문자 허용
  assert.equal(sym.frames[0].panels[0].symbol, "");
});

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
  assert.equal(parsed.frames[3].height, 0.4);
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
