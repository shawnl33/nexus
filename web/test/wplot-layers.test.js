// 위클리 지표 선 복원. 엔진이 압축한 구간을 차트 점으로 되돌리는지만 본다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/wplot-layers.js");
const W = globalThis.WplotLayers;

test("expandRuns: 60초로 이어진 값은 시작 시각부터 분마다 찍는다", () => {
  assert.deepEqual(W.expandRuns([[1000, 0, 4.5, -1]]), [
    { time: 1000, value: 0 },
    { time: 1060, value: 4.5 },
    { time: 1120, value: -1 },
  ]);
});

test("expandRuns: 끊긴 구간은 잇지 않고 다음 시작을 쓴다", () => {
  assert.deepEqual(W.expandRuns([[1000, 1, 2], [1180, 3]]), [
    { time: 1000, value: 1 },
    { time: 1060, value: 2 },
    { time: 1180, value: 3 },
  ]);
});

test("expandSpans: 같은 가격은 시작과 끝만 잇는다", () => {
  assert.deepEqual(W.expandSpans([[1000, 1120, 9.5]]), [
    { time: 1000, value: 9.5 },
    { time: 1120, value: 9.5 },
  ]);
});

test("pointsFor: 영점선은 자기 봉 전체 구간의 0이다", () => {
  const line = { id: "zero", kind: "zero" };
  assert.deepEqual(W.pointsFor(line, { zero: true }, { t0: 1000, t1: 1120 }), [
    { time: 1000, value: 0 },
    { time: 1120, value: 0 },
  ]);
  assert.deepEqual(W.pointsFor(line, { zero: false }, { t0: 1000, t1: 1120 }), []);
});

test("D3_첫만남가격은 가격 눈금 위의 점선이다", () => {
  const spec = W.specFor("w_link_short");
  assert.equal(spec.scale, "right");
  assert.equal(spec.lineStyle, 1);
  assert.equal(spec.lines[0].key, "sl");
});

test("chartPoints: 가격 눈금만 100을 곱하고 합산수익률은 수식 값이다", () => {
  assert.deepEqual(W.chartPoints([{ time: 1000, value: 4.5 }], true), [
    { time: 1000, value: 450 },
  ]);
  assert.deepEqual(W.chartPoints([{ time: 1000, value: 14.88 }], false), [
    { time: 1000, value: 14.88 },
  ]);
});

test("합산수익률은 0에서 선 막대이고 영점기준은 자홍 선이다", () => {
  const spec = W.specFor("w_ret_short");
  assert.equal(spec.lines[0].plot, "histogram");
  assert.equal(spec.lines[0].color, "#4040a0");
  assert.equal(spec.lines[1].plot, "line");
  assert.equal(spec.lines[1].color, "#fc40fc");
  const long = W.specFor("w_ret_long");
  assert.equal(long.lines[0].plot, "histogram");
  assert.equal(long.lines[1].color, "#fc40fc");
});

test("pointsFor: 상대와 선물이 없어도 영점선은 자기 봉 구간으로 그린다", () => {
  const line = { id: "zero", kind: "zero" };
  assert.deepEqual(W.pointsFor(line, { zero: true }, null, { t0: 1700, t1: 1820 }), [
    { time: 1700, value: 0 },
    { time: 1820, value: 0 },
  ]);
});
