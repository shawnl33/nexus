// 해외선물 미래곡선 V1 렌더러. barInd.fxMask/fx[24]만 읽고, 꺼진 Plot은 잇지 않는다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/fx-layers.js");
const F = globalThis.FxLayers;

function fakeSeries() {
  return {
    stored: null, updates: [], options: null, primitives: [],
    setData(d) { this.stored = d; },
    data() { return this.stored || []; },
    priceToCoordinate(v) { return v; },
    update(d) {
      this.updates.push(d);
      const cur = this.stored || (this.stored = []);
      const last = cur[cur.length - 1];
      if (last && last.time === d.time) cur[cur.length - 1] = d;
      else cur.push(d);
    },
    applyOptions(o) { this.options = { ...this.options, ...o }; },
    attachPrimitive(p) {
      this.primitives.push(p);
      p.attached?.({ chart: this._chart, series: this, requestUpdate() {} });
    },
  };
}
function fakeChart() {
  const made = [];
  const scales = {};
  const chart = {
    made,
    addLineSeries(opts) {
      const s = fakeSeries();
      s._chart = chart;
      s.options = { ...opts };
      made.push(s);
      return s;
    },
    timeScale() {
      return { timeToCoordinate(t) { return (t - 1000) / 60 * 20 + 10; } };
    },
    removeSeries(s) {
      const i = made.indexOf(s);
      if (i >= 0) made.splice(i, 1);
    },
    priceScale(id) {
      if (!scales[id]) scales[id] = { applyOptions() {} };
      return scales[id];
    },
  };
  return chart;
}

test("FxRenderer: 단계색 선과 나머지 Plot을 만든다", () => {
  const chart = fakeChart();
  const h = F.FxRenderer.createHandle(chart, {});
  for (const m of ["setLayers", "applyLive", "applySeed", "clear", "destroy"]) {
    assert.equal(typeof h[m], "function", m);
  }
  assert.equal(chart.made.length, 30); // 단계·마켓 7색씩, 회귀선 1선, 나머지 15. 0.382/0.5/0.618은 그리지 않는다
  assert.equal(chart.made[0].options.priceScaleId, "fxscore");
  assert.equal(chart.made.find((s) => s.options.color === "#ff7f00").options.color, "#ff7f00");
  assert.ok(chart.made.some((s) => s.options.color === "#008000"));
});

test("회귀선은 이전 점과 이번 점을 새 색의 직선으로 잇는다", () => {
  const chart = fakeChart();
  const h = F.FxRenderer.createHandle(chart, {});
  const reg = chart.made.find((s) => s.options.color === "#d7dde8" && s.options.lineWidth === 3);
  const fx1 = Array.from({ length: 24 }, () => NaN);
  fx1[0] = 4;
  fx1[1] = 4500;
  const fx2 = fx1.slice();
  fx2[0] = -4;
  fx2[1] = 4510;
  h.applySeed({
    barInd: new Map([
      [1000, { fxMask: 0b11, fx: fx1, r2: 0.2 }],
      [1060, { fxMask: 0b11, fx: fx2, r2: 0.8 }],
    ]),
    barSeq: [1000, 1060],
  });
  const strokes = [];
  const ctx = {
    beginPath() { strokes.push(["begin"]); },
    moveTo(x, y) { strokes.push(["m", x, y]); },
    lineTo(x, y) { strokes.push(["l", x, y]); },
    stroke() { strokes.push(["stroke", ctx.strokeStyle, ctx.lineWidth]); },
  };
  reg.primitives[0].paneViews()[0].renderer().draw({
    useBitmapCoordinateSpace(fn) {
      fn({ context: ctx, horizontalPixelRatio: 2, verticalPixelRatio: 2 });
    },
  });
  assert.deepEqual(strokes, [
    ["begin"], ["m", 20, 9000], ["l", 60, 9020], ["stroke", "#0000b4", 12],
  ]);

  h.setLayers({ reg: false });
  assert.deepEqual(reg.data(), []);
});

function drawPluses(series) {
  const calls = [];
  const ctx = {
    beginPath() {},
    moveTo(x, y) { calls.push(["m", x, y]); },
    lineTo(x, y) { calls.push(["l", x, y]); },
    stroke() { calls.push(["stroke", ctx.strokeStyle, ctx.lineWidth]); },
  };
  const prim = series.primitives[0];
  prim.paneViews()[0].renderer().draw({
    useBitmapCoordinateSpace(fn) {
      fn({ context: ctx, horizontalPixelRatio: 2, verticalPixelRatio: 2 });
    },
  });
  return calls;
}

test("지난 상승과 하락은 선을 끄고 값이 있는 봉마다 십자를 그린다", () => {
  const chart = fakeChart();
  const h = F.FxRenderer.createHandle(chart, {});
  const orange = chart.made.filter((s) => s.options.color === "#ff7f00");
  const green = chart.made.filter((s) => s.options.color === "#008000");
  assert.equal(orange.length, 2);
  assert.equal(green.length, 2);
  for (const s of [...orange, ...green]) {
    assert.equal(s.options.lineVisible, false);
    assert.equal(s.options.pointMarkersVisible, false);
    assert.equal(s.primitives.length, 1);
  }
  const solid = chart.made.find((s) => s.options.color === "#6e6e6e");
  assert.equal(solid.options.lineVisible, false);
  assert.equal(solid.primitives.length, 1);

  const fx = Array.from({ length: 24 }, () => NaN);
  fx[8] = 7700;
  fx[13] = 7600;
  const fx2 = Array.from({ length: 24 }, () => NaN);
  h.applySeed({
    barInd: new Map([
      [1000, { fxMask: (1 << 8) | (1 << 13), fx }],
      [1060, { fxMask: 0, fx: fx2 }],
    ]),
    barSeq: [1000, 1060],
  });
  const calls = drawPluses(orange[0]);
  assert.deepEqual(calls.filter((c) => c[0] === "m"), [
    ["m", 14, 15400],
    ["m", 20, 15394],
  ]);
  assert.deepEqual(calls.filter((c) => c[0] === "l"), [
    ["l", 26, 15400],
    ["l", 20, 15406],
  ]);
  assert.equal(calls.at(-1)[1], "#ff7f00");
  assert.equal(drawPluses(green[0]).filter((c) => c[0] === "m").length, 2);
  assert.equal(drawPluses(orange[1]).filter((c) => c[0] === "m").length, 0);
});

test("값은 같은 구간만 수평선이고 값이 바뀌면 끊긴다", () => {
  const chart = fakeChart();
  const h = F.FxRenderer.createHandle(chart, {});
  const gray = chart.made.find((s) => s.options.color === "#6e6e6e");
  const fx = Array.from({ length: 24 }, () => NaN);
  const a = fx.slice();
  a[3] = 100;
  const b = fx.slice();
  b[3] = 100;
  const c = fx.slice();
  c[3] = 130;
  h.applySeed({
    barInd: new Map([
      [1000, { fxMask: 1 << 3, fx: a }],
      [1060, { fxMask: 1 << 3, fx: b }],
      [1120, { fxMask: 1 << 3, fx: c }],
    ]),
    barSeq: [1000, 1060, 1120],
  });
  const calls = drawPluses(gray);
  assert.deepEqual(calls.filter((row) => row[0] === "m"), [
    ["m", 20, 200],
    ["m", 100, 260],
  ]);
  assert.deepEqual(calls.filter((row) => row[0] === "l"), [
    ["l", 60, 200],
    ["l", 132, 260],
  ]);
});

test("라이브 틱은 지난 봉을 다시 깔지 않고 마지막 봉만 고친다", () => {
  const chart = fakeChart();
  const h = F.FxRenderer.createHandle(chart, {});
  const reg = chart.made.find((s) => s.options.color === "#d7dde8" && s.options.lineWidth === 3);
  const fx = (v, score, r2) => {
    const row = Array.from({ length: 24 }, () => NaN);
    row[0] = score;
    row[1] = v;
    return { fxMask: 0b11, fx: row, r2 };
  };
  h.applySeed({
    barSeq: [1000, 1060],
    barInd: new Map([[1000, fx(4500, 4, 0.2)], [1060, fx(4510, -4, 0.8)]]),
  });
  let sets = 0;
  const orig = reg.setData.bind(reg);
  reg.setData = (d) => { sets += 1; orig(d); };
  const fx3 = fx(4520, -4, 0.8);
  h.applyLive(null, {
    barSeq: [1000, 1060, 1120],
    barInd: new Map([[1000, fx(4500, 4, 0.2)], [1060, fx(4510, -4, 0.8)], [1120, fx3]]),
  });
  assert.equal(sets, 0);
  assert.equal(reg.updates.at(-1).time, 1120);
  assert.equal(reg.updates.at(-1).value, 4520);
});
