import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/pvc-layers.js");
const P = globalThis.PvcLayers;

test("parsePvc: 직전 봉 가격·거래량 비율과 동시압축", () => {
  assert.deepEqual(P.parsePvc([1, 12.5, 1, 80, 1]), {
    priceOn: true, price: 12.5, volOn: true, vol: 80, both: true,
  });
  assert.equal(P.parsePvc([0, 0, 0, 0, 0]).priceOn, false);
  assert.equal(P.parsePvc([0, 0, 0, 0, 0]).both, false);
  assert.equal(P.parsePvc(null), null);
  assert.equal(P.parsePvc([1, 2]), null);
});

function fakeSeries() {
  return {
    data: null, updates: [], options: null, primitives: [],
    setData(d) { this.data = d; },
    update(d) { this.updates.push(d); },
    attachPrimitive(p) { this.primitives.push(p); },
  };
}
function fakeChart() {
  const made = [];
  return {
    made,
    addLineSeries(opts) {
      const s = fakeSeries();
      s.options = { ...opts };
      made.push(s);
      return s;
    },
    removeSeries() {},
    priceScale() { return { applyOptions() {} }; },
  };
}

test("PvcRenderer: 꺼진 비율은 끊고, 동시압축은 0에 찍는다", () => {
  const chart = fakeChart();
  const h = P.PvcRenderer.createHandle(chart);
  assert.deepEqual(chart.made.map((s) => s.options.color), ["#ff00ff", "#0000ff", "#ff8c00"]);
  assert.equal(chart.made[2].options.lineWidth, 3);
  assert.ok(chart.made.every((s) => s.options.lineVisible === false && s.primitives.length === 1));
  const barInd = new Map([
    [60, { pvc: { priceOn: true, price: 100, volOn: false, vol: 0, both: false } }],
    [120, { pvc: { priceOn: true, price: 1, volOn: true, vol: 25, both: true } }],
  ]);
  h.applySeed({ barSeq: [60, 120], barInd });
  const [price, vol, both] = chart.made;
  assert.deepEqual(price.data, [{ time: 60, value: 100 }, { time: 120, value: 1 }]);
  assert.deepEqual(vol.data, [{ time: 60 }, { time: 120, value: 25 }]);
  assert.deepEqual(both.data, [{ time: 60 }, { time: 120, value: 0 }]);
  h.setLayers({ price: false });
  assert.deepEqual(price.data, []);
});
