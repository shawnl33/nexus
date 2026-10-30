// 스윙전체폭 라이브 갱신은 상태의 bar_open_time을 쓴다.
// 시각이 없는 점을 timeToCoordinate에 넘기면 예외가 나고, 같은 차트의 아래 칸 그리기가 멈춘다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/os-layers.js");
const Os = globalThis.OsLayers;

function fakeSeries() {
  const s = {
    stored: [],
    updates: [],
    primitives: [],
    opts: {},
    setData(d) { this.stored = d; },
    data() { return this.stored; },
    update(p) { this.updates.push(p); },
    priceToCoordinate(v) { return v; },
    applyOptions(opt) { Object.assign(this.opts, opt); },
    attachPrimitive(p) {
      this.primitives.push(p);
      p.attached?.({ chart: this._chart, series: this, requestUpdate() {} });
    },
  };
  return s;
}
function fakeChart() {
  const asked = [];
  const chart = {
    asked,
    addLineSeries() {
      const s = fakeSeries();
      s._chart = chart;
      return s;
    },
    timeScale() {
      return {
        timeToCoordinate(t) {
          asked.push(t);
          if (!Number.isFinite(t)) throw new TypeError("Cannot read properties of undefined (reading 'year')");
          return t;
        },
      };
    },
    removeSeries() {},
    priceScale() { return { applyOptions() {} }; },
  };
  return chart;
}
function draw(series) {
  for (const prim of series.primitives) {
    for (const view of prim.paneViews?.() || []) {
      view.renderer()?.draw({
        useBitmapCoordinateSpace(fn) {
          fn({ context: { beginPath() {}, moveTo() {}, lineTo() {}, stroke() {} }, horizontalPixelRatio: 1, verticalPixelRatio: 1 });
        },
      });
    }
  }
}

test("스윙 라이브는 bar_open_time으로 갱신하고, 시각 없는 점은 그리다 던지지 않는다", () => {
  const made = [];
  const chart = fakeChart();
  const orig = chart.addLineSeries.bind(chart);
  chart.addLineSeries = () => { const s = orig(); made.push(s); return s; };
  const handle = Os.swingHandle(chart);
  const ctx = {
    barSeq: [1000],
    barInd: new Map([[1000, { os: { swingOn: 1, swingPct: 40 } }]]),
  };
  handle.applySeed(ctx);
  ctx.barSeq.push(1060);
  ctx.barInd.set(1060, { os: { swingOn: 1, swingPct: 55 } });
  handle.applyLive({ bar_open_time: 1060 * 1e6 }, ctx);
  const series = made[0];
  assert.deepEqual(series.updates.at(-1), { time: 1060, value: 55 });
  series.primitives[0].points.push({ time: undefined, value: 10 });
  assert.doesNotThrow(() => draw(series));
  assert.ok(chart.asked.every((t) => Number.isFinite(t)));
});
