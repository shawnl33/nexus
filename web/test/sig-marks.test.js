// 진입신호는 봉 아래·위에 화살표와 주문 이름을 그린다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/os-layers.js");
const Os = globalThis.OsLayers;

function fakeSeries() {
  return {
    primitives: [],
    setData() {},
    update() {},
    applyOptions(opt) {
      if (opt && typeof opt.autoscaleInfoProvider === "function" && this._chart) {
        this._chart.autos = this._chart.autos || [];
        this._chart.autos.push(opt.autoscaleInfoProvider());
      }
    },
    priceToCoordinate(v) { return v; },
    attachPrimitive(p) {
      this.primitives.push(p);
      p.attached?.({ chart: this._chart, series: this, requestUpdate() {} });
    },
  };
}
function fakeChart() {
  const made = [];
  const chart = {
    made,
    addLineSeries() {
      const s = fakeSeries();
      s._chart = chart;
      made.push(s);
      return s;
    },
    addHistogramSeries() {
      return chart.addLineSeries();
    },
    timeScale() { return { timeToCoordinate(t) { return t === 1000 ? 40 : 80; } }; },
    removeSeries() {},
    scales: [],
    priceScale(id) {
      return {
        applyOptions(opt) { chart.scales.push([id, opt]); },
      };
    },
  };
  return chart;
}
function drawAll(chart) {
  const calls = [];
  const ctx = {
    beginPath() { calls.push(["begin"]); },
    moveTo(x, y) { calls.push(["m", x, y]); },
    lineTo(x, y) { calls.push(["l", x, y]); },
    closePath() {},
    fill() { calls.push(["fill"]); },
    stroke() {},
    fillText(text, x, y) { calls.push(["text", text, x, y]); },
  };
  ctx.font = "";
  ctx.textAlign = "";
  ctx.textBaseline = "";
  ctx.fillStyle = "";
  for (const s of chart.made) {
    for (const prim of s.primitives) {
      for (const view of prim.paneViews?.() || []) {
        view.renderer()?.draw({
          useBitmapCoordinateSpace(fn) {
            fn({ context: ctx, horizontalPixelRatio: 2, verticalPixelRatio: 2 });
          },
        });
      }
    }
  }
  return calls;
}

test("주문 이름은 등급과 조정 종류를 붙인다", () => {
  assert.equal(Os.orderName(1, 1, 8, 0, 0), "후보돌파매수");
  assert.equal(Os.orderName(1, 1, 7, 0, 0), "후보T매수");
  assert.equal(Os.orderName(1, 1, 6, 0, 0), "후보R매수");
  assert.equal(Os.orderName(1, 1, 5, 0, 0), "후보S매수");
  assert.equal(Os.orderName(1, 1, 4, 0, 0), "후보구조매수");
  assert.equal(Os.orderName(1, 1, 3, 0, 0), "후보A매수");
  assert.equal(Os.orderName(1, 1, 2, 0, 0), "후보B매수");
  assert.equal(Os.orderName(1, 1, 1, 0, 0), "후보C매수");
  assert.equal(Os.orderName(1, 1, 0, 0, 0), "후보매수");
  assert.equal(Os.orderName(-1, 1, 8, 0, 0), "후보돌파매도");
  assert.equal(Os.orderName(1, 2, 0, 0, 0), "돌파매수");
  assert.equal(Os.orderName(-1, 3, 0, 0, 0), "선행매도");
  assert.equal(Os.orderName(1, 5, 0, 0, 0), "전환매수");
  assert.equal(Os.orderName(-1, 5, 0, 0, 0), "전환매도");
  assert.equal(Os.orderName(1, 4, 0, 1, 3), "조정A매수_강");
  assert.equal(Os.orderName(-1, 4, 0, 2, 2), "조정B매도_중");
  assert.equal(Os.orderName(1, 4, 0, 1, 1), "조정A매수_약");
  assert.equal(Os.exitName(1), "파랑청산");
  assert.equal(Os.exitName(-1), "빨강청산");
  assert.equal(Os.exitName(2), "시간청산매수");
  assert.equal(Os.exitName(-2), "시간청산매도");
});

test("통합판정 눈금은 전환 ±190을 포함하고 전환 점을 읽는다", () => {
  const chart = fakeChart();
  Os.judgeHandle(chart);
  const ranges = (chart.autos || []).map((r) => r.priceRange);
  assert.ok(ranges.some((r) => r.minValue === -190 && r.maxValue === 190));
  const row = Array(156).fill(0);
  row[149] = 1;
  row[150] = -190;
  row[151] = 0x006400;
  row[152] = 7;
  const parsed = Os.parse(row);
  assert.equal(parsed.turnOn, true);
  assert.equal(parsed.turnY, -190);
  assert.equal(parsed.turnRgb, 0x006400);
  assert.equal(parsed.turnW, 7);
});

test("포지션감시 줄은 수익전환과 총손익을 읽는다", () => {
  const row = Array(226).fill(0);
  row[206] = 5;
  row[208] = 1;
  row[209] = 7;
  row[212] = 2;
  row[213] = 1;
  row[214] = 1;
  row[215] = 20;
  const parsed = Os.parse(row);
  assert.equal(parsed.pos1.mfeW, 5);
  assert.equal(parsed.pos1.keepMfeOn, true);
  assert.equal(parsed.pos1.keepMfe, 7);
  assert.equal(parsed.pos1.flips, 2);
  assert.equal(parsed.pos1.danger, 1);
  assert.equal(parsed.pos1.totalOn, true);
  assert.equal(parsed.pos1.total, 20);
});

test("백테스트 체결 글자는 이름 아래에 계약수를 그린다", () => {
  const chart = fakeChart();
  const s = chart.addLineSeries();
  const pen = Os.labelMarks();
  s.attachPrimitive(pen);
  pen.setPoints([
    { time: 1000, value: 90, position: "belowBar", color: "#d4ff4a", text: "돌파매수\n2계약" },
    { time: 1060, value: 110, position: "aboveBar", color: "#ff8c00", text: "색청산매수" },
  ]);
  const texts = drawAll(chart).filter((c) => c[0] === "text").map((c) => c[1]);
  assert.deepEqual(texts, ["돌파매수", "2계약", "색청산매수"]);
});

test("짝 화살은 계약수와 1차 틱을 적는다", () => {
  assert.equal(Os.pairLabel({
    pairDir: 1, pairKind: 1, pairGrade: 8, pairAk: 0, pairAg: 0,
    pairSuf: 1, pairQty: 2, pairTicks: 15,
  }, false), "후보돌파매수_반대방향\n2계약\n1차 15틱");
  assert.equal(Os.pairLabel({
    zdDir: -1, zdKind: 1, zdGrade: 3, zdSuf: 2, zdQty: 1, zdTicks: 10,
  }, true), "압축매도_약세\n1계약\n1차 10틱");
});

test("승률 화살은 원본 주문 이름을 적는다", () => {
  const chart = fakeChart();
  const h = Os.sigHandle(chart, "wr");
  h.applySeed({
    barSeq: [1000],
    barPos: new Map([[1000, 0]]),
    bars: new Map([[1000, { high: 110, low: 90, close: 100 }]]),
    barInd: new Map([[1000, { os: { wrDir: 1, wrOrder: 55, sigDir: 1, sigKind: 2, wrName: "시가회복매수" } }]]),
  });
  const texts = drawAll(chart).filter((c) => c[0] === "text").map((c) => c[1]);
  assert.deepEqual(texts, ["시가회복매수"]);
});

test("승률 번호 55는 시가회복매수다", () => {
  const chart = fakeChart();
  const h = Os.sigHandle(chart, "wr");
  h.applySeed({
    barSeq: [1000],
    barPos: new Map([[1000, 0]]),
    bars: new Map([[1000, { high: 110, low: 90, close: 100 }]]),
    barInd: new Map([[1000, { os: { wrDir: 1, wrOrder: 55, sigDir: 1, sigKind: 1, sigGrade: 8 } }]]),
  });
  const texts = drawAll(chart).filter((c) => c[0] === "text").map((c) => c[1]);
  assert.deepEqual(texts, ["시가회복매수"]);
});

test("등급 8 매수 화살은 후보돌파매수라고 적는다", () => {
  const chart = fakeChart();
  const h = Os.sigHandle(chart);
  h.applySeed({
    barSeq: [1000],
    barPos: new Map([[1000, 0]]),
    bars: new Map([[1000, { high: 110, low: 90, close: 100 }]]),
    barInd: new Map([[1000, { os: { sigDir: 1, sigKind: 1, sigGrade: 8 } }]]),
  });
  const texts = drawAll(chart).filter((c) => c[0] === "text").map((c) => c[1]);
  assert.deepEqual(texts, ["후보돌파매수"]);
});

test("매수 신호는 저가 아래에 위쪽 화살과 주문 이름을 그린다", () => {
  const chart = fakeChart();
  const h = Os.sigHandle(chart);
  assert.equal(chart.scales.some((row) => row[0] === "right" && row[1].visible === false), false);
  h.applySeed({
    barSeq: [1000],
    barPos: new Map([[1000, 0]]),
    bars: new Map([[1000, { high: 110, low: 90, close: 100 }]]),
    barInd: new Map([[1000, { os: { sigDir: 1, sigKind: 2 } }]]),
  });
  const calls = drawAll(chart);
  const texts = calls.filter((c) => c[0] === "text").map((c) => c[1]);
  assert.deepEqual(texts, ["돌파매수"]);
  const ys = calls.filter((c) => c[0] === "m" || c[0] === "l").map((c) => c[2]);
  assert.ok(Math.min(...ys) > 90 * 2);
});

test("제로데이 1차 목표에 닿은 봉은 1차청산매도를 그린다", () => {
  const chart = fakeChart();
  const h = Os.sigHandle(chart, "zd");
  h.applySeed({
    barSeq: [1000],
    barPos: new Map([[1000, 0]]),
    bars: new Map([[1000, { high: 110, low: 90, close: 100 }]]),
    barInd: new Map([[1000, { os: { zdLimit: -1 } }]]),
  });
  const texts = drawAll(chart).filter((c) => c[0] === "text").map((c) => c[1]);
  assert.deepEqual(texts, ["1차청산매도"]);
});

test("청산 표시는 전달받은 수량을 쓰고 전량 청산 후 가상 색청산은 생략한다", () => {
  const cases = [
    { mode: "zd", fields: { 193: -1, 194: 1, 230: 2 }, want: ["색청산매도_손절", "2계약"] },
    { mode: "zd", fields: { 193: 2, 230: 1 }, want: ["시간청산매수", "1계약"] },
    { mode: "zd", fields: { 227: -1, 231: 1 }, want: ["1차청산매도", "1계약"] },
    { mode: "zd", fields: { 193: -1, 227: -1, 230: 1, 231: 1 },
      want: ["1차청산매도", "1계약", "색청산매도", "1계약"] },
    { mode: "zd", fields: { 193: -1, 230: 0 }, want: [] },
    { mode: "pair", fields: { 183: 1, 228: 2 }, want: ["파랑청산", "2계약"] },
    { mode: "pair", fields: { 226: 1, 229: 1 }, want: ["1차목표청산매수", "1계약"] },
    { mode: undefined, fields: { 52: -2, 136: 3 }, want: ["시간청산매도", "3계약"] },
  ];
  for (const { mode, fields, want } of cases) {
    const row = Array(232).fill(0);
    for (const [index, value] of Object.entries(fields)) row[index] = value;
    const chart = fakeChart();
    const h = Os.sigHandle(chart, mode);
    h.applySeed({
      barSeq: [1000], barPos: new Map([[1000, 0]]),
      bars: new Map([[1000, { high: 110, low: 90, close: 100 }]]),
      barInd: new Map([[1000, { os: Os.parse(row) }]]),
    });
    const calls = drawAll(chart);
    assert.deepEqual(calls.filter((c) => c[0] === "text").map((c) => c[1]), want);
    if (!want.length) assert.equal(calls.some((c) => c[0] === "fill"), false);
  }
});
