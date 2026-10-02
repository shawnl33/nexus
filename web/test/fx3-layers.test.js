import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/fx3-layers.js");
const F = globalThis.Fx3Layers;

function fakeChart() {
  const made = [];
  const chart = {
    made,
    addLineSeries(opts) {
      const s = {
        data: null, updates: [], options: { ...opts }, primitives: [],
        setData(d) { this.data = d; this.sets = (this.sets || 0) + 1; },
        update(d) {
          this.updates.push(d);
          if (!Array.isArray(this.data)) this.data = [];
          const last = this.data[this.data.length - 1];
          if (last && last.time === d.time) this.data[this.data.length - 1] = d;
          else this.data.push(d);
        },
        applyOptions(o) { this.options = { ...this.options, ...o }; },
        priceToCoordinate(v) { return v; },
        attachPrimitive(p) {
          this.primitives.push(p);
          p.attached?.({ chart, series: this, requestUpdate() {} });
        },
      };
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

test("미래목표선은 직전 봉에서 우측으로, 범위색은 신뢰도 기준이다", () => {
  const raw = [1,
    [100, 110, 90, 1],
    [200, 220, 180, -1],
    [300, 330, 270, 0],
    [400, 440, 360, 1],
    [500, 550, 450, -1],
  ];
  const strong = F.rayLines(raw, 0.5, true);
  assert.equal(strong[0].c, "#ff9191");
  assert.equal(strong[0].w, 1);
  assert.equal(strong[1].c, "#bed2be");
  assert.equal(strong[2].c, "#bed2be");
  assert.equal(strong[3].c, "#0000ff");
  assert.equal(strong[3].w, 3);
  assert.equal(strong[4].c, "#a5a5a5");
  const weak = F.rayLines(raw, 0.2, true);
  assert.equal(weak[1].c, "#b4b4b4");
  assert.equal(weak[4].c, "#b4b4b4");
  assert.equal(F.rayLines(raw, 0.5, false).length, 5);

  const prim = F.createRaysPrimitive();
  const calls = [];
  prim.attached({
    chart: { timeScale: () => ({ timeToCoordinate: (t) => (t === 940 ? 12 : null) }) },
    series: { priceToCoordinate: (v) => v },
    requestUpdate() {},
  });
  prim.set({ prevTime: 940, time: 1000, lines: [{ v: 100, c: "#ff0000", w: 3 }] });
  prim.paneViews()[0].renderer().draw({
    useMediaCoordinateSpace(fn) {
      fn({
        context: {
          beginPath() {},
          moveTo(x, y) { calls.push(["m", x, y]); },
          lineTo(x, y) { calls.push(["l", x, y]); },
          stroke() { calls.push(["s", this.strokeStyle, this.lineWidth]); },
        },
        mediaSize: { width: 400 },
      });
    },
  });
  assert.deepEqual(calls, [
    ["m", 12, 100], ["l", 400, 100], ["s", "#ff0000", 3],
  ]);
});

test("parseFx3는 [plot,값,rgb,두께]만 남긴다", () => {
  assert.deepEqual(F.parseFx3([[1, 4, 0xdc0000, 6], "x", [7]]), [
    { id: 1, value: 4, rgb: 0xdc0000, width: 6 },
  ]);
  assert.deepEqual(F.parseFx3(undefined), []);
});

test("Fx3Renderer는 단계화를 별도 스케일에 그리고 꺼진 Plot은 잇지 않는다", () => {
  const chart = fakeChart();
  const h = F.Fx3Renderer.createHandle(chart, {});
  assert.equal(chart.made[0].options.priceScaleId, "fx3score");
  const ctx = {
    barSeq: [1000],
    barInd: new Map([[1000, { fx3: [{ id: 1, value: 4, rgb: 0xdc0000, width: 6 }] }]]),
  };
  h.applySeed(ctx);
  assert.deepEqual(chart.made[0].data, [{ time: 1000, value: 4 }]);
  assert.equal(chart.made[0].options.color, "#dc0000");
  assert.deepEqual(chart.made[1].data, [{ time: 1000 }]); // 회귀선 Plot7 없음
  h.setLayers({ stage: false });
  assert.deepEqual(chart.made[0].data, []);
});

test("라이브 틱은 V3 선을 통째로 다시 깔지 않는다", () => {
  const chart = fakeChart();
  const h = F.Fx3Renderer.createHandle(chart, {});
  h.applySeed({
    barSeq: [1000],
    barInd: new Map([[1000, { fx3: [{ id: 7, value: 4500, rgb: 0xdc0000, width: 2 }] }]]),
  });
  for (const s of chart.made) s.sets = 0;
  h.applyLive({ bar_open_time: 1060 * 1e6, reg_r2: 0.8 }, {
    barSeq: [1000, 1060],
    barInd: new Map([
      [1000, { fx3: [{ id: 7, value: 4500, rgb: 0xdc0000, width: 2 }] }],
      [1060, { fx3: [{ id: 7, value: 4510, rgb: 0x0000b4, width: 6 }] }],
    ]),
  });
  const sets = chart.made.reduce((n, s) => n + s.sets, 0);
  const reg = chart.made[1];
  assert.equal(sets, 0);
  assert.equal(reg.updates.at(-1).value, 4510);
});

test("V3 회귀선은 직선 선분이고 마켓중심은 같은 값만 수평이다", () => {
  const chart = fakeChart();
  const h = F.Fx3Renderer.createHandle(chart, {});
  const reg = chart.made.find((s) => s.primitives[0] && s.primitives[0].setPoints && s === chart.made[1]);
  const mkt = chart.made.find((s) => s.primitives[0] && chart.made.indexOf(s) > 1 && s.primitives[0] !== reg.primitives[0]);
  h.applySeed({
    barSeq: [1000, 1060, 1120],
    barInd: new Map([
      [1000, { fx3: [
        { id: 7, value: 4500, rgb: 0xdc0000, width: 2 },
        { id: 51, value: 100, rgb: 0xdc0000, width: 3 },
      ] }],
      [1060, { fx3: [
        { id: 7, value: 4510, rgb: 0x0000b4, width: 6 },
        { id: 51, value: 100, rgb: 0xdc0000, width: 3 },
      ] }],
      [1120, { fx3: [
        { id: 7, value: 4520, rgb: 0x0000b4, width: 6 },
        { id: 51, value: 130, rgb: 0xdc0000, width: 3 },
      ] }],
    ]),
  });
  function strokes(series) {
    const out = [];
    const ctx = {
      beginPath() { out.push("b"); },
      moveTo(x, y) { out.push(["m", x, y]); },
      lineTo(x, y) { out.push(["l", x, y]); },
      stroke() { out.push(["s", ctx.strokeStyle, ctx.lineWidth]); },
    };
    series.primitives[0].paneViews()[0].renderer().draw({
      useBitmapCoordinateSpace(fn) {
        fn({ context: ctx, horizontalPixelRatio: 2, verticalPixelRatio: 2 });
      },
    });
    return out;
  }
  const regSeries = chart.made[1];
  assert.deepEqual(strokes(regSeries).filter((row) => row[0] === "m" || row[0] === "l" || row[0] === "s"), [
    ["m", 20, 9000], ["l", 60, 9020], ["s", "#0000b4", 12],
    ["m", 60, 9020], ["l", 100, 9040], ["s", "#0000b4", 12],
  ]);
  const mktSeries = chart.made.find((s) => Array.isArray(s.data) && s.data.some((p) => p.value === 100) && s !== regSeries);
  assert.deepEqual(strokes(mktSeries).filter((row) => row[0] === "m"), [
    ["m", 20, 200],
  ]);
});
