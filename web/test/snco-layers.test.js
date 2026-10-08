import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/horiz-lines.js");
await import("../public/snco-layers.js");
const S = globalThis.SncoLayers;

function fakeChart() {
  const made = [];
  const chart = {
    made,
    addLineSeries(opts) {
      const s = {
        data: null, options: { ...opts }, primitives: [],
        setData(d) { this.data = d; },
        update(d) {
          if (!Array.isArray(this.data)) this.data = [];
          const last = this.data[this.data.length - 1];
          if (last && last.time === d.time) this.data[this.data.length - 1] = d;
          else this.data.push(d);
        },
        applyOptions(o) { this.options = { ...this.options, ...o }; },
        attachPrimitive(p) {
          this.primitives.push(p);
          p.attached?.({ chart, series: this, requestUpdate() {} });
        },
      };
      made.push(s);
      return s;
    },
    priceScale() { return { applyOptions() {} }; },
    removeSeries() {},
  };
  return chart;
}

const sample = [
  [10, 22, 0xff0000, 10],
  [11, 12, 0x808080, 2],
  [12, 40, 0x808080, 0],
  [13, 30, 0x808080, 1],
  [21, 103, 0x0000ff, 1],
  [23, 106, 0x000096, 1],
  [29, 22, 0x008000, 3],
  [30, 22, 0xffff00, 3],
  [31, 80, 0x0000ff, 1],
  [33, 1, 0x646464, 1],
];

test("viewFrom: Data2와 같은 칸으로 읽고 굵기 0과 Plot33은 뺀다", () => {
  const v = S.viewFrom(S.parse(sample));
  assert.equal(v.sam, 22);
  assert.equal(v.samWidth, 10);
  assert.equal(v.reg, 12);
  assert.equal(v.market, null);
  assert.equal(v.price, 30);
  assert.equal(v.vol, 80);
  assert.equal(v.posHi, 103);
  assert.equal(v.posLo, null);
  assert.equal(v.down, 106);
  assert.equal(v.up, null);
  assert.equal(v.signal, "both");
  assert.equal(v.signalY, 22);
  assert.equal(v.emphasis, "yellow");
  assert.equal(v.emphasisY, 22);
});

test("CO_V3 표시는 Data2와 같이 선 없이 점·가로선·화살표다", () => {
  const chart = fakeChart();
  const h = S.createHandle(chart);
  const circles = chart.made.filter((s) => s.options.pointMarkersVisible === true);
  assert.equal(circles.length, 6);
  assert.equal(circles.filter((s) => s.options.pointMarkersRadius === 5).length, 3);
  assert.equal(circles.filter((s) => s.options.pointMarkersRadius === 3).length, 3);
  for (const s of chart.made) assert.equal(s.options.lineVisible, false);
  const sam = chart.made.find((s) => s.options.color === "#d7dde8");
  assert.equal(sam.primitives.length, 1);
  assert.equal(typeof sam.primitives[0].setPoints, "function");
  const firstDn = chart.made.find((s) => s.options.color === "#000096");
  assert.equal(firstDn.primitives.length, 1);
  h.applySeed({
    barSeq: [1000, 1060],
    barInd: new Map([[1000, { snco: S.parse(sample) }]]),
  });
  const both = circles.find((s) => s.options.color === "#008000");
  const yellow = circles.find((s) => s.options.color === "#ffff00");
  assert.deepEqual(both.data, [{ time: 1000, value: 22 }]);
  assert.deepEqual(yellow.data, [{ time: 1000, value: 22 }]);
  assert.deepEqual(firstDn.data, [{ time: 1000, value: 106 }, { time: 1060 }]);
  const vol = chart.made.find((s) => s.options.color === "#0000ff" && s.options.lineWidth === 1 && s.options.pointMarkersVisible !== true && s.primitives.length === 1);
  assert.ok(vol.data.some((p) => p.value === 80));

  h.setLayers({ signal: false, vol: false, range: false });
  assert.deepEqual(both.data, []);
  assert.deepEqual(yellow.data, [{ time: 1000, value: 22 }]);
  assert.deepEqual(vol.data, []);
  assert.deepEqual(firstDn.data, []);
});
