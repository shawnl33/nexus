// 겹침 종목 비율선. 첫 유효 종가 = 100, 이후는 그 배수.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/overlay-ratio.js");
const R = globalThis.OverlayRatio;

test("points: 첫 유효 종가가 100이고 이후 종가는 그 비율이다", () => {
  assert.deepEqual(R.points([
    { time: 10, close: 200 },
    { time: 70, close: 220 },
    { time: 130, close: 180 },
  ]), [
    { time: 10, value: 100 },
    { time: 70, value: 110 },
    { time: 130, value: 90 },
  ]);
});

test("points: 종가가 없는 항목은 빼고, 첫 유효 종가를 기준으로 삼는다", () => {
  assert.deepEqual(R.points([
    { time: 10 },
    { time: 70, close: 0 },
    { time: 130, close: 50 },
    { time: 190, close: 75 },
  ]), [
    { time: 130, value: 100 },
    { time: 190, value: 150 },
  ]);
});

test("points: 빈 입력은 빈 배열이다", () => {
  assert.deepEqual(R.points(null), []);
  assert.deepEqual(R.points([]), []);
});

test("ohlc: 시가·고가·저가·종가를 첫 유효 종가 기준으로 맞춘다", () => {
  assert.deepEqual(R.ohlc([
    { time: 10, open: 190, high: 210, low: 180, close: 200 },
    { time: 70, open: 200, high: 230, low: 190, close: 220 },
  ]), [
    { time: 10, open: 95, high: 105, low: 90, close: 100 },
    { time: 70, open: 100, high: 115, low: 95, close: 110 },
  ]);
});

test("ohlc: 종가가 없는 봉은 빼고, 빠진 시가·고가·저가는 그 봉 종가로 채운다", () => {
  assert.deepEqual(R.ohlc([
    { time: 10, close: 0 },
    { time: 70, close: 50 },
    { time: 130, high: 80, close: 75 },
  ]), [
    { time: 70, open: 100, high: 100, low: 100, close: 100 },
    { time: 130, open: 150, high: 160, low: 150, close: 150 },
  ]);
});
