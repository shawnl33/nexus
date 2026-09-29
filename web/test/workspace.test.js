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

test("parse: 높이는 0.05~1로 클램프하고, 칸이 없으면 맨 차트 1칸이 기본이다", () => {
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
