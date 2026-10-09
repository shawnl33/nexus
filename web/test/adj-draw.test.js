// 조정분석 비율선은 Def 다. 봉마다 바뀌면 사선, 머물다 크게 바뀌면 세로. 기준선은 일정하다.

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
    opts: {},
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
    addHistogramSeries() { return fakeSeries(); },
    timeScale() { return { timeToCoordinate(t) { return (t - 1000) / 60 * 20 + 10; } }; },
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
    stroke() { calls.push(["stroke", ctx.strokeStyle]); },
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

test("가격조정비는 봉마다 바뀌면 사선이고 머물다 크게 바뀌면 세로다", () => {
  const chart = fakeChart();
  const h = Os.adjHandle(chart);
  const row = (price) => ({ os: { waveTime: 10, wavePrice: price, waveOpp: 4, waveState: 1, waveStateRgb: 0xdc0000 } });
  h.applySeed({
    barSeq: [1000, 1060, 1120, 1180],
    barInd: new Map([[1000, row(20)], [1060, row(35)], [1120, row(35)], [1180, row(80)]]),
  });
  const calls = drawAll(chart);
  const segs = [];
  let seg = [];
  for (const c of calls) {
    if (c[0] === "begin") seg = [];
    else if (c[0] === "m" || c[0] === "l") seg.push(c);
    else if (c[0] === "stroke" && c[1] === "#9600c8") segs.push(seg);
  }
  const flat = segs.find((s) => s.length >= 2 && s.every((p) => p[2] === s[0][2]));
  const rise = segs.find((s) => s.length >= 2 && s.every((p) => p[1] === s[0][1]) && s[0][2] !== s[1][2]);
  assert.ok(flat);
  assert.equal(flat[0][2], 70);
  assert.ok(rise);
  assert.ok(rise.some((p) => p[2] === 160));
  const levels = chart.made.map((s) => s.stored?.filter((p) => p.value != null).map((p) => p.value));
  const slope = segs.find((s) => {
    const xs = s.map((p) => p[1]);
    const ys = s.map((p) => p[2]);
    return Math.max(...xs) > Math.min(...xs) && Math.max(...ys) > Math.min(...ys);
  });
  assert.ok(slope);
  assert.ok(levels.some((vals) => vals?.length === 4 && vals.every((v) => v === 23.6)));
  assert.ok(levels.some((vals) => vals?.every((v) => v === 100)));
});

test("값이 없는 구간은 비율선을 잇지 않는다", () => {
  const chart = fakeChart();
  const h = Os.adjHandle(chart);
  const row = (price) => ({ os: { waveTime: 10, wavePrice: price, waveOpp: 4, waveState: 1, waveStateRgb: 0xdc0000 } });
  h.applySeed({
    barSeq: [1000, 1060, 1000 + 60 * 30],
    barInd: new Map([
      [1000, row(20)],
      [1060, row(20)],
      [1000 + 60 * 30, row(90)],
    ]),
  });
  const calls = drawAll(chart);
  const segs = [];
  let seg = [];
  for (const c of calls) {
    if (c[0] === "begin") seg = [];
    else if (c[0] === "m" || c[0] === "l") seg.push(c);
    else if (c[0] === "stroke" && c[1] === "#9600c8") segs.push(seg);
  }
  const spansGap = segs.some((s) => {
    const xs = s.map((p) => p[1]);
    return Math.max(...xs) - Math.min(...xs) > 40;
  });
  assert.equal(spansGap, false);
  const fit = chart.made.find((s) => s.opts.autoscaleInfoProvider)?.opts.autoscaleInfoProvider;
  const range = fit().priceRange;
  assert.ok(range.minValue < -15);
  assert.ok(range.maxValue > 100);
  assert.ok(range.maxValue < 140);
});
