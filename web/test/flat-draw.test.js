// 위치카운트는 점, 기울기·진폭은 짧은 가로, 직전구간은 긴 가로만.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/os-layers.js");
const Os = globalThis.OsLayers;

function fakeSeries() {
  return {
    stored: null,
    primitives: [],
    opts: {},
    setData(d) { this.stored = d; },
    data() { return this.stored || []; },
    priceToCoordinate(v) { return v; },
    update() {},
    applyOptions(opt) { Object.assign(this.opts, opt); },
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
    addHistogramSeries() { throw new Error("histogram"); },
    timeScale() { return { timeToCoordinate(t) { return (t - 1000) / 60 * 20 + 10; } }; },
    removeSeries() {},
    priceScale() { return { applyOptions() {} }; },
  };
  return chart;
}
function segsOf(chart, color) {
  const calls = [];
  const ctx = {
    beginPath() { calls.push(["begin"]); },
    moveTo(x, y) { calls.push(["m", x, y]); },
    lineTo(x, y) { calls.push(["l", x, y]); },
    stroke() { calls.push(["stroke", ctx.strokeStyle]); },
    arc(x, y) { calls.push(["arc", y]); },
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
  const segs = [];
  let seg = [];
  for (const c of calls) {
    if (c[0] === "begin") seg = [];
    else if (c[0] === "m" || c[0] === "l") seg.push(c);
    else if (c[0] === "stroke" && c[1] === color) segs.push(seg);
    else if (c[0] === "arc" && color === "dot") segs.push(c);
  }
  return { segs, calls };
}

test("기울기는 짧은 가로이고 직전구간은 긴 가로, 위치는 점이다", () => {
  const chart = fakeChart();
  const h = Os.flatHandle(chart);
  h.applySeed({
    barSeq: [1000, 1060],
    barInd: new Map([
      [1000, { os: { flatOn: true, flatPos: 4, flatPosRgb: 0xff9696, flatSlope: 5, flatSlopeRgb: 0xc80000, flatUpSlope: 12, flatMark: 1 } }],
      [1060, { os: { flatOn: true, flatPos: 5, flatPosRgb: 0xff9696, flatSlope: 6, flatSlopeRgb: 0xc80000, flatUpSlope: 12 } }],
    ]),
  });
  const slope = segsOf(chart, "#c80000").segs;
  assert.equal(slope.length, 2);
  for (const s of slope) {
    const xs = s.map((p) => p[1]);
    const ys = s.map((p) => p[2]);
    assert.equal(Math.min(...ys), Math.max(...ys));
    assert.ok(Math.max(...xs) - Math.min(...xs) >= 32);
  }
  const held = segsOf(chart, "#e67878").segs;
  assert.ok(held.length >= 1);
  assert.ok(held.every((s) => s.every((p) => p[2] === s[0][2])));
  assert.ok(held.some((s) => Math.max(...s.map((p) => p[1])) - Math.min(...s.map((p) => p[1])) >= 32));
  const dots = segsOf(chart, "dot").calls.filter((c) => c[0] === "arc");
  assert.ok(dots.some((c) => c[1] === 8));
  assert.ok(dots.some((c) => c[1] === 6));
});
