// 화면틀 v2(workspace.js) 직렬화/파싱 단위 테스트.
// DOM 없이 순수 함수만 검증한다: 왕복, 구 스키마 거부, 알 수 없는 지표 필터, 높이 클램프.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/workspace.js");
const W = globalThis.Workspace;

const KNOWN = new Set(["mirae_v16", "sma"]);
const known = (id) => KNOWN.has(id);

test("serialize: 화면틀 v2 형태로 수집한다", () => {
  const ws = W.serialize("내 세트", "A016C000", [
    { height: 0.6, indicators: [{ id: "mirae_v16", layers: { score: true, mktband: false } }] },
    { height: 0.4, indicators: [{ id: "sma", layers: { sma5: true, sma20: true, sma60: false } }] },
  ]);
  assert.equal(ws.schema_version, 2);
  assert.equal(ws.name, "내 세트");
  assert.equal(ws.current_symbol, "A016C000");
  assert.equal(ws.panels.length, 2);
  assert.equal(ws.panels[0].height, 0.6);
  assert.deepEqual(ws.panels[0].indicators[0], { id: "mirae_v16", layers: { score: true, mktband: false } });
  assert.deepEqual(ws.panels[1].indicators[0].layers, { sma5: true, sma20: true, sma60: false });

  // layers 객체는 복사본이어야 한다 (원본 변경이 저장본에 영향 없음)
  const src = { height: 1, indicators: [{ id: "sma", layers: { sma5: true } }] };
  const out = W.serialize("t", "1", [src]);
  src.indicators[0].layers.sma5 = false;
  assert.equal(out.panels[0].indicators[0].layers.sma5, true);
});

test("serialize→parse 왕복: 칸 수·지표·레이어·높이·종목이 보존된다", () => {
  const ws = W.serialize("왕복", "005930", [
    { height: 0.7, indicators: [{ id: "mirae_v16", layers: { band: true, mktband: false } }] },
    { height: 0.3, indicators: [] },
  ]);
  const parsed = W.parse(JSON.parse(JSON.stringify(ws)), known);
  assert.equal(parsed.symbol, "005930");
  assert.equal(parsed.panels.length, 2);
  assert.equal(parsed.panels[0].height, 0.7);
  assert.deepEqual(parsed.panels[0].indicators, [{ id: "mirae_v16", layers: { band: true, mktband: false } }]);
  assert.deepEqual(parsed.panels[1].indicators, []);
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
      indicators: [
        { id: "mirae_v16", layers: { band: 1, mktband: 0 } },
        { id: "future_indicator", layers: {} }, // 이 프론트가 모르는 지표
        { id: 42 },                             // 잘못된 형태
      ],
    }],
  }, known);
  assert.equal(parsed.panels[0].indicators.length, 1);
  assert.deepEqual(parsed.panels[0].indicators[0], { id: "mirae_v16", layers: { band: true, mktband: false } });
});

test("parse: 높이는 0.1~1로 클램프하고, 칸이 없으면 맨 차트 1칸이 기본이다", () => {
  assert.equal(W.MIN_HEIGHT, 0.1); // app.js 드래그 최소 높이(MIN_PANE_FRAC)와 같은 값이어야 한다
  const parsed = W.parse({
    schema_version: 2,
    panels: [{ height: 5, indicators: [] }, { height: -1, indicators: [] }, { indicators: [] }],
  }, known);
  assert.equal(parsed.panels[0].height, 1);
  assert.equal(parsed.panels[1].height, W.MIN_HEIGHT);
  assert.equal(parsed.panels[2].height, 1); // height 누락

  const empty = W.parse({ schema_version: 2, panels: [] }, known);
  assert.deepEqual(empty.panels, [{ height: 1, indicators: [] }]);

  const sym = W.parse({ schema_version: 2, panels: [] }, known);
  assert.equal(sym.symbol, ""); // current_symbol 누락/비문자 허용
});

// 화면틀 적용 시 저장 종목 복원 — switchSymbol(app.js)은 입력창 값을 읽으므로
// 입력창 대입이 전환 호출보다 먼저여야 한다 (Critical 회귀 방지).
test("restoreSymbol: 저장 종목이 다르면 입력창을 저장 종목으로 맞춘 뒤 전환한다", () => {
  const input = { value: "005930" }; // DOM 스텁
  const calls = [];
  const switchSymbol = () => calls.push(input.value); // 호출 시점의 입력창 값을 기록
  const parsed = { symbol: "A016C000", panels: [] };

  assert.equal(W.restoreSymbol(parsed, input, switchSymbol), true);
  assert.equal(input.value, "A016C000");          // 입력창이 저장 종목으로 바뀐다
  assert.deepEqual(calls, ["A016C000"]);          // 전환은 저장 종목으로 1회 호출된다
});

test("restoreSymbol: 같은 종목이거나 저장값이 없으면 전환하지 않는다", () => {
  const input = { value: "005930" };
  let called = 0;
  const switchSymbol = () => called++;

  assert.equal(W.restoreSymbol({ symbol: "005930", panels: [] }, input, switchSymbol), false);
  assert.equal(W.restoreSymbol({ symbol: "", panels: [] }, input, switchSymbol), false);
  assert.equal(W.restoreSymbol({ panels: [] }, input, switchSymbol), false);
  assert.equal(W.restoreSymbol({ symbol: "  ", panels: [] }, input, switchSymbol), false);
  assert.equal(called, 0);
  assert.equal(input.value, "005930"); // 입력창 불변
});
