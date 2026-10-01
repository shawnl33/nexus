// 이평선 렌더러(sma-layers.js) 단위 테스트.
// 렌더러 계약(createHandle/setLayers/applyLive/applySeed/clear/destroy)과
// barInd 캐시(smaValid/sma[3]) 소비를 가짜 차트로 검증한다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/sma-layers.js");
const S = globalThis.SmaLayers;

function fakeSeries() {
  return {
    data: null, updates: [], options: null,
    setData(d) { this.data = d; },
    update(d) { this.updates.push(d); },
    applyOptions(o) { this.options = { ...this.options, ...o }; },
  };
}
function fakeChart() {
  const made = [];
  const removed = [];
  return {
    made, removed,
    addLineSeries(opts) { const s = fakeSeries(); s.options = { ...opts }; made.push(s); return s; },
    removeSeries(s) { removed.push(s); const i = made.indexOf(s); if (i >= 0) made.splice(i, 1); },
  };
}
function makeCtx(rows) {
  // rows: [t, smaValid, sma[3]]
  const barInd = new Map(rows.map(([t, valid, sma]) => [t, { smaValid: valid, sma }]));
  return {
    bars: new Map(), barInd,
    barSeq: rows.map(([t]) => t),
    barPos: new Map(rows.map(([t], i) => [t, i])),
    tickRaw: () => 5,
  };
}

test("SmaRenderer: createHandle이 계약 메서드와 색상 명세의 LineSeries 3개를 만든다", () => {
  const chart = fakeChart();
  const h = S.SmaRenderer.createHandle(chart, {});
  for (const m of ["setLayers", "applyLive", "applySeed", "clear", "destroy"]) {
    assert.equal(typeof h[m], "function", m);
  }
  assert.equal(chart.made.length, 3);
  // 브리프 명세: sma5 #ff9800, sma20 #4db6ac, sma60 #ba68c8, 굵기 2, 가격선/마지막값 숨김
  assert.deepEqual(chart.made.map((s) => s.options.color), ["#ff9800", "#4db6ac", "#ba68c8"]);
  for (const s of chart.made) {
    assert.equal(s.options.lineWidth, 2);
    assert.equal(s.options.priceLineVisible, false);
    assert.equal(s.options.lastValueVisible, false);
  }
});

test("SmaRenderer: applySeed가 캐시에서 복원하고 setLayers가 즉시 토글한다", () => {
  const chart = fakeChart();
  const h = S.SmaRenderer.createHandle(chart, {});
  const [s5, s20, s60] = chart.made;

  const rows = [];
  for (let i = 0; i < 70; i++) {
    // 60봉 전까지는 워밍업(무효) — 복원 데이터에서 빠져야 한다
    const valid = i >= 59;
    rows.push([1000 + i * 60, valid, [100 + i, 200 + i, 300 + i]]);
  }
  const ctx = makeCtx(rows);
  h.applySeed(ctx);
  assert.equal(s5.data.length, 11);
  assert.equal(s20.data.length, 11);
  assert.equal(s60.data.length, 11);
  assert.deepEqual(s5.data[0], { time: 1000 + 59 * 60, value: 159 });

  // 레이어 토글: sma60만 끄면 그 선만 비고, 다시 켜면 캐시에서 복원
  h.setLayers({ sma60: false });
  assert.deepEqual(s60.data, []);
  assert.equal(s5.data.length, 11);
  h.setLayers({ sma60: true });
  assert.equal(s60.data.length, 11);

  h.clear();
  assert.deepEqual(s5.data, []);
  assert.deepEqual(s60.data, []);
});

test("SmaRenderer: applyLive가 봉별 갱신(유효)과 갭(워밍업)을 반영한다", () => {
  const chart = fakeChart();
  const h = S.SmaRenderer.createHandle(chart, {});
  const [s5] = chart.made;
  const ctx = makeCtx([]);

  // 워밍업 봉: 무효 → whitespace 갭
  ctx.barSeq.push(5000);
  ctx.barPos.set(5000, 0);
  ctx.barInd.set(5000, { smaValid: false, sma: [NaN, NaN, NaN] });
  h.applyLive({ bar_open_time: 5000 * 1e6 }, ctx);
  assert.equal(s5.updates.length, 1);
  assert.equal(s5.updates[0].value, undefined);

  // 유효 봉: 각 선이 자기 값으로 갱신
  ctx.barSeq.push(5060);
  ctx.barPos.set(5060, 1);
  ctx.barInd.set(5060, { smaValid: true, sma: [11, 22, 33] });
  h.applyLive({ bar_open_time: 5060 * 1e6 }, ctx);
  assert.equal(s5.updates.length, 2);
  assert.deepEqual(s5.updates[1], { time: 5060, value: 11 });
  assert.deepEqual(chart.made[1].updates[1], { time: 5060, value: 22 });
  assert.deepEqual(chart.made[2].updates[1], { time: 5060, value: 33 });

  // 꺼진 레이어는 라이브 갱신도 건너뛴다
  h.setLayers({ sma20: false });
  ctx.barSeq.push(5120);
  ctx.barPos.set(5120, 2);
  ctx.barInd.set(5120, { smaValid: true, sma: [12, 23, 34] });
  h.applyLive({ bar_open_time: 5120 * 1e6 }, ctx);
  assert.equal(s5.updates.length, 3);
  assert.equal(chart.made[1].updates.length, 2); // sma20은 증가 없음
});

test("SmaRenderer: destroy가 시리즈를 차트에서 분리한다", () => {
  const chart = fakeChart();
  const h = S.SmaRenderer.createHandle(chart, {});
  h.destroy();
  assert.equal(chart.made.length, 0);
  assert.equal(chart.removed.length, 3);
});
