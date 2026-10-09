// 수익관리: 값은 봉마다 가로로만 잇고, 대각선으로 잇지 않는다.
// 수익관리 레이어는 지정 색이다. 값은 봉마다 가로로만 잇는다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/os-layers.js");
const Os = globalThis.OsLayers;

function fakeSeries() {
  return {
    stored: null,
    primitives: [],
    setData(d) { this.stored = d; },
    data() { return this.stored || []; },
    priceToCoordinate(v) { return v; },
    update() {},
    applyOptions() {},
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
    addHistogramSeries() { return fakeSeries(); },
    timeScale() {
      return { timeToCoordinate(t) { return (t - 1000) / 60 * 20 + 10; } };
    },
    removeSeries() {},
    priceScale() { return { applyOptions() {} }; },
  };
  return chart;
}

function drawAll(chart) {
  const calls = [];
  const ctx = {
    beginPath() { calls.push(["begin"]); },
    moveTo(x, y) { calls.push(["m", x, y]); },
    lineTo(x, y) { calls.push(["l", x, y]); },
    stroke() { calls.push(["stroke", ctx.strokeStyle, ctx.lineWidth]); },
    arc(x, y, r) { calls.push(["arc", x, y, r, ctx.strokeStyle]); },
    fillRect(x, y, w, h) { calls.push(["rect", x, y, w, h, ctx.fillStyle]); },
    fill() {},
  };
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

function hsegs(calls) {
  const out = [];
  let seg = [];
  for (const row of calls) {
    if (row[0] === "begin") { seg = []; continue; }
    if (row[0] === "stroke") {
      if (seg.length) out.push(seg);
      seg = [];
      continue;
    }
    if (row[0] === "m" || row[0] === "l") seg.push(row);
  }
  return out;
}

test("진입손익은 값이 달라도 가로 조각이고 대각선으로 잇지 않는다", () => {
  const chart = fakeChart();
  const h = Os.pnlHandle(chart);
  h.applySeed({
    barSeq: [1000, 1060],
    barInd: new Map([
      [1000, { os: { pnlSide: 1, pnlOpen: 10 } }],
      [1060, { os: { pnlSide: 1, pnlOpen: 20 } }],
    ]),
  });
  const segs = hsegs(drawAll(chart));
  assert.ok(segs.length >= 2);
  for (const seg of segs) {
    const ys = seg.map((p) => p[2]);
    assert.equal(Math.min(...ys), Math.max(...ys));
  }
  const ys = segs.flat().map((p) => p[2]);
  assert.ok(ys.includes(20));
  assert.ok(ys.includes(40));
});

test("지정한 레이어 색으로 그린다", () => {
  const chart = fakeChart();
  const h = Os.pnlHandle(chart);
  h.applySeed({
    barSeq: [1000],
    barInd: new Map([[1000, { os: {
      pnlSide: 1, pnlOpen: 4, pnlOpenRgb: 0xff00ff, pnlMfe: 8, pnlMfeRgb: 0x00ffff,
      pnlMae: -2, pnlMaeRgb: 0x0000ff,
      pnlLongOn: true, pnlLong: 46, pnlLongRgb: 0xff0000,
      pnlP27On: true, pnlP30On: true, pnlP30: 3, pnlExitOn: true, pnlExit: 1,
    } }]]),
  });
  const calls = drawAll(chart);
  const strokes = calls.filter((c) => c[0] === "stroke").map((c) => c[1]);
  assert.ok(strokes.includes("#ff00ff"));
  assert.ok(strokes.includes("#00ffff"));
  assert.ok(strokes.includes("#0000ff"));
  assert.ok(strokes.includes("#ff0000"));
  assert.ok(strokes.includes("#008000"));
  const rects = calls.filter((c) => c[0] === "rect");
  assert.equal(rects.length, 1);
  assert.equal(rects[0][5], "#00ffff");
});
