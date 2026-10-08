// 지난구간: 위 초록 십자와 그 위 자홍선 사이는 빨간 세로선,
// 아래 오렌지 십자와 그 아래 청록선 사이는 파란 세로선. 봉마다 한 줄.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/cu-layers.js");
const Cu = globalThis.CuLayers;

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
    timeScale() {
      return { timeToCoordinate(t) { return (t - 1000) / 60 * 20 + 10; } };
    },
    removeSeries() {},
    priceScale() { return { applyOptions() {} }; },
  };
  return chart;
}

function cu(id, value, rgb, width) {
  return { id, value, rgb, width };
}

function drawAll(chart) {
  const calls = [];
  const ctx = {
    beginPath() { calls.push(["begin"]); },
    moveTo(x, y) { calls.push(["m", x, y]); },
    lineTo(x, y) { calls.push(["l", x, y]); },
    stroke() { calls.push(["stroke", ctx.strokeStyle, ctx.lineWidth]); },
    arc() {},
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

function strokesOf(calls, color) {
  const out = [];
  let seg = [];
  for (const row of calls) {
    if (row[0] === "begin") {
      seg = [];
      continue;
    }
    if (row[0] === "stroke") {
      if (row[1] === color) out.push({ width: row[2], seg });
      seg = [];
      continue;
    }
    if (row[0] === "m" || row[0] === "l") seg.push(row);
  }
  return out;
}

test("자홍선이 위 초록 십자보다 높으면 그 사이에 빨간 세로선을, 청록선이 아래 오렌지 십자보다 낮으면 파란 세로선을 봉마다 긋는다", () => {
  const chart = fakeChart();
  const h = Cu.createHandle(chart);
  h.applySeed({
    barSeq: [1000, 1060],
    barInd: new Map([
      [1000, { cu: [
        cu(14, 100, 0x008000, 2),
        cu(27, 130, 0xdc00dc, 3),
        cu(10, 90, 0xff7f00, 2),
        cu(28, 70, 0x00aaaa, 3),
      ] }],
      [1060, { cu: [
        cu(14, 102, 0x008000, 2),
        cu(27, 128, 0xdc00dc, 3),
        cu(10, 88, 0xff7f00, 2),
        cu(28, 74, 0x00aaaa, 3),
      ] }],
    ]),
  });
  const calls = drawAll(chart);
  const red = strokesOf(calls, "#ff0000");
  const blue = strokesOf(calls, "#0000ff");
  assert.equal(red.length, 1);
  assert.equal(blue.length, 1);
  assert.equal(red[0].width, 2);
  assert.equal(blue[0].width, 2);
  assert.deepEqual(red[0].seg, [
    ["m", 20, 200], ["l", 20, 260],
    ["m", 60, 204], ["l", 60, 256],
  ]);
  assert.deepEqual(blue[0].seg, [
    ["m", 20, 180], ["l", 20, 140],
    ["m", 60, 176], ["l", 60, 148],
  ]);
});

test("자홍선이 초록 십자 아래이거나 청록선이 오렌지 십자 위이면 세로선을 긋지 않고, 지난 구간을 끄면 지운다", () => {
  const chart = fakeChart();
  const h = Cu.createHandle(chart);
  h.applySeed({
    barSeq: [1000, 1060],
    barInd: new Map([
      [1000, { cu: [
        cu(14, 100, 0x008000, 2),
        cu(27, 90, 0xdc00dc, 3),
        cu(10, 80, 0xff7f00, 2),
        cu(28, 95, 0x00aaaa, 3),
      ] }],
      [1060, { cu: [
        cu(14, 100, 0x008000, 2),
        cu(10, 80, 0xff7f00, 2),
        cu(28, 95, 0x00aaaa, 3),
      ] }],
    ]),
  });
  let calls = drawAll(chart);
  assert.equal(strokesOf(calls, "#ff0000").length, 0);
  assert.equal(strokesOf(calls, "#0000ff").length, 0);

  h.applySeed({
    barSeq: [1000],
    barInd: new Map([[1000, { cu: [
      cu(14, 100, 0x008000, 2),
      cu(27, 130, 0xdc00dc, 3),
      cu(10, 90, 0xff7f00, 2),
      cu(28, 70, 0x00aaaa, 3),
    ] }]]),
  });
  h.setLayers({ swing: false });
  calls = drawAll(chart);
  assert.equal(strokesOf(calls, "#ff0000").length, 0);
  assert.equal(strokesOf(calls, "#0000ff").length, 0);
});
