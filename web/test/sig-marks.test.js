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
    applyOptions() {},
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
