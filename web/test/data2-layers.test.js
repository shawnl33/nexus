import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/data2-layers.js");
await import("../public/ymae-layers.js");
const D = globalThis.Data2Layers;

const base = {
  pgap: [{ ready: true, ratio: 20 }],
  rgap: { ready: true, ratio: 10 },
  mgap: { ready: true, ratio: 40 },
  ymae: { pos: -1, prevValid: true, twoHi: 10, twoLo: 8, h1: 1 },
  sniper: { priceRatio: 30, compound: 1, rgb: 0xff0000, ratio: 2, pxExit: -1, below: 1, above: 0, reset: 0 },
};

test("scopeFrom: 참조봉 비율과 이탈 표시", () => {
  const s = D.scopeFrom(base);
  assert.equal(s.sam, 20);
  assert.equal(s.reg, 10);
  assert.equal(s.market, null); // 30 이상은 숨긴다
  assert.equal(s.price, 30);
  assert.equal(s.posHi, 103);
  assert.equal(s.posLo, null);
  assert.equal(s.up, null);
  assert.equal(s.compound, -9);
  assert.equal(s.samWidth, 10);
  assert.equal(s.squeezeDn, 109);
  assert.equal(s.squeezeUp, null);
  assert.equal(s.holdDn, null);
  assert.equal(s.holdUp, null);
  assert.equal(s.signal, "both");
  assert.equal(s.emphasis, "yellow");
});

test("scopeFrom: 세션 첫 봉은 직전 이탈을 숨기고, 비율 30 이상은 상단유지를 그린다", () => {
  const s = D.scopeFrom({
    pgap: [{ ready: true, ratio: 40 }],
    ymae: { pos: 0, prevValid: false, twoHi: 0, twoLo: 0, h1: 1 },
    sniper: { ratio: 0, rgb: 0xdcdcdc, pxExit: 1, below: 0, above: 1, reset: 1, compound: 1 },
  });
  assert.equal(s.squeezeUp, null);
  assert.equal(s.up, null);
  assert.equal(s.compound, null);
  assert.equal(s.holdUp, -15);
  assert.equal(s.signal, null);
  assert.equal(s.emphasis, null);
});

test("defaultCode: ES와 NQ는 같은 월물로 짝을 맞춘다", () => {
  assert.equal(D.defaultCode("ESZ26"), "NQZ26");
  assert.equal(D.defaultCode("nqh26"), "ESH26");
  assert.equal(D.defaultCode("YMZ26"), "");
});

test("scopeFrom: 삼선이 준비되지 않으면 비율을 그리지 않는다", () => {
  const s = D.scopeFrom({ pgap: [{ ready: false, ratio: 10 }] });
  assert.equal(s.sam, null);
  assert.equal(s.price, null);
  assert.equal(s.posHi, null);
  assert.equal(s.posLo, null);
});

function fakeChart() {
  const made = [];
  const chart = {
    made,
    addLineSeries(opts) {
      const s = {
        data: null, options: { ...opts }, primitives: [],
        setData(d) { this.data = d; this.sets = (this.sets || 0) + 1; },
        update(d) {
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
      s._chart = chart;
      made.push(s);
      return s;
    },
    timeScale() {
      return { timeToCoordinate(t) { return (t - 1000) / 60 * 20 + 10; } };
    },
    priceScale() { return { applyOptions() {} }; },
    removeSeries() {},
  };
  return chart;
}

test("스나이퍼 신호점은 선 없이 원이다", () => {
  const chart = fakeChart();
  const h = D.Data2Renderer.createHandle(chart);
  const circles = chart.made.filter((s) => s.options.pointMarkersVisible === true);
  assert.equal(circles.length, 6);
  const signal = circles.filter((s) => s.options.pointMarkersRadius === 4);
  const emphasis = circles.filter((s) => s.options.pointMarkersRadius === 2);
  assert.equal(signal.length, 3);
  assert.equal(emphasis.length, 3);
  for (const s of circles) assert.equal(s.options.lineVisible, false);
  const ratio = chart.made.find((s) => s.options.lineWidth === 2 && s.options.color === "#d7dde8");
  assert.equal(ratio.options.lineVisible, false);
  assert.equal(ratio.options.pointMarkersVisible, false);
  assert.equal(ratio.primitives.length, 1);
  assert.ok(chart.made.filter((s) => s.options.lineWidth === 1 && s.options.pointMarkersVisible !== true).every((s) => s.primitives.length >= 1));

  h.setSource({
    barSeq: [1000, 1060],
    barInd: new Map([[1000, base]]),
  });
  const both = circles.find((s) => s.options.color === "#008000");
  const yellow = circles.find((s) => s.options.color === "#ffff00");
  const up = circles.find((s) => s.options.color === "#0000ff");
  assert.deepEqual(both.data, [{ time: 1000, value: 20 }]);
  assert.deepEqual(yellow.data, [{ time: 1000, value: 20 }]);
  assert.deepEqual(up.data, []);
});

test("parseYmae: 첫이탈 표시는 배열 14번이다", () => {
  const row = globalThis.YmaeLayers.parseYmae([0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, -1]);
  assert.equal(row.h1, 1);
  assert.equal(row.first, -1);
});

test("scopeFrom: 화살표는 구간당 첫이탈의 다음 봉만이다", () => {
  const stay = D.scopeFrom({
    pgap: [{ ready: true, ratio: 20 }],
    ymae: { pos: -1, prevValid: true, h1: 1, first: 0 },
    sniper: { reset: 0 },
  });
  assert.equal(stay.posHi, 103);
  assert.equal(stay.up, null);
  assert.equal(stay.down, null);
  const up = D.scopeFrom({
    pgap: [{ ready: true, ratio: 20 }],
    ymae: { pos: 1, prevValid: true, first: 1 },
    sniper: { reset: 0 },
  });
  assert.equal(up.posLo, -3);
  assert.equal(up.up, -6);
  assert.equal(up.down, null);
  const dn = D.scopeFrom({
    ymae: { first: -1 },
    sniper: { reset: 0 },
  });
  assert.equal(dn.down, 106);
  assert.equal(dn.posHi, null);
  const session = D.scopeFrom({
    ymae: { first: 1 },
    sniper: { reset: 1 },
  });
  assert.equal(session.up, null);
});

test("첫이탈 화살표는 그 봉에만 있고 직전범위 선은 그대로다", () => {
  const chart = fakeChart();
  const h = D.Data2Renderer.createHandle(chart);
  const posHi = chart.made.find((s) => s.options.color === "#0000ff" && s.options.pointMarkersVisible !== true);
  const posLo = chart.made.find((s) => s.options.color === "#ff0000" && s.options.pointMarkersVisible !== true);
  const firstUp = chart.made.find((s) => s.options.color === "#b40000");
  const firstDn = chart.made.find((s) => s.options.color === "#000096");
  assert.equal(posHi.primitives.length, 1);
  assert.equal(posLo.primitives.length, 1);
  assert.equal(firstUp.primitives.length, 2);
  assert.equal(firstDn.primitives.length, 2);
  const outside = {
    pgap: [{ ready: true, ratio: 20 }],
    ymae: { pos: -1, prevValid: true, first: 0 },
    sniper: { rgb: 0xdcdcdc, ratio: 0, reset: 0 },
  };
  const broke = {
    pgap: [{ ready: true, ratio: 20 }],
    ymae: { pos: -1, prevValid: true, first: -1 },
    sniper: { rgb: 0xdcdcdc, ratio: 0, reset: 0 },
  };
  h.setSource({
    barSeq: [1000, 1060, 1120],
    barInd: new Map([[1000, outside], [1060, broke], [1120, broke]]),
  });
  assert.equal(posHi.data.filter((p) => p.value === 103).length, 3);
  assert.deepEqual(firstDn.data.map((p) => p.value), [undefined, 106, 106]);
  const tips = [];
  const ctx = {
    beginPath() {},
    moveTo(x, y) { tips.push(["m", x, y]); },
    lineTo() {},
    closePath() {},
    fill() {},
  };
  firstDn.primitives[1].paneViews()[0].renderer().draw({
    useBitmapCoordinateSpace(fn) {
      fn({ context: ctx, horizontalPixelRatio: 2, verticalPixelRatio: 2 });
    },
  });
  assert.equal(tips.length, 2);
  assert.equal(tips[0][2] > 106 * 2, true);
  assert.equal(tips[1][2] > 106 * 2, true);
});

test("삼선비율은 봉마다 색과 굵기의 원이고 잇지 않는다", () => {
  const chart = fakeChart();
  const h = D.Data2Renderer.createHandle(chart);
  const ratio = chart.made.find((s) => s.options.color === "#d7dde8");
  const row = (n, rgb, score) => ({
    pgap: [{ ready: true, ratio: n }],
    sniper: { rgb, ratio: score, reset: 0 },
  });
  h.setSource({
    barSeq: [1000, 1060, 1120],
    barInd: new Map([
      [1000, row(20, 0xdcdcdc, 0)],
      [1060, row(20, 0xff0000, 2)],
      [1120, row(40, 0x0000ff, 1)],
    ]),
  });
  const calls = [];
  const ctx = {
    beginPath() {},
    arc(x, y, r) { calls.push(["c", x, y, r, ctx.fillStyle]); },
    fill() {},
  };
  ratio.primitives[0].paneViews()[0].renderer().draw({
    useBitmapCoordinateSpace(fn) {
      fn({ context: ctx, horizontalPixelRatio: 2, verticalPixelRatio: 2 });
    },
  });
  assert.deepEqual(calls, [
    ["c", 20, 40, 8, "#dcdcdc"],
    ["c", 60, 40, 10, "#ff0000"],
    ["c", 100, 80, 6, "#0000ff"],
  ]);
});

test("라이브 틱은 마커 전체를 다시 깔지 않고 마지막 봉만 고친다", () => {
  const chart = fakeChart();
  const h = D.Data2Renderer.createHandle(chart);
  const row = (pos, first) => ({
    pgap: [{ ready: true, ratio: 20 }],
    ymae: { pos, prevValid: true, first },
    sniper: { rgb: 0xdcdcdc, ratio: 0, reset: 0 },
  });
  h.setSource({
    barSeq: [1000, 1060],
    barInd: new Map([[1000, row(-1, 0)], [1060, row(-1, 0)]]),
  });
  for (const s of chart.made) s.sets = 0;
  h.applyLive(null, {
    barSeq: [1000, 1060, 1120],
    barInd: new Map([[1000, row(-1, 0)], [1060, row(-1, 0)], [1120, row(-1, -1)]]),
  });
  const sets = chart.made.reduce((n, s) => n + s.sets, 0);
  const firstDn = chart.made.find((s) => s.options.color === "#000096");
  assert.equal(sets, 0);
  assert.equal(firstDn.data.length, 3);
  assert.equal(firstDn.data[2].value, 106);
});
