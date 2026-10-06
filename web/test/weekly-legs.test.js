// 위클리 프라이스링크에 고른 상대·선물이 합산수익률과 국내선물 Data2로 이어지는지.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/weekly-legs.js");
const L = globalThis.WeeklyLegs;

test("합산수익률 양매수는 프라이스링크 양매수의 상대와 선물을 쓴다", () => {
  const legs = L.setLeg(L.empty(), "w_link_long", "opp", "b016a745");
  const withFut = L.setLeg(legs, "w_link_long", "fut", "a016c000");
  const other = L.setLeg(withFut, "w_link_short", "opp", "cafc0a29");
  assert.deepEqual(L.legForIndicator(other, "w_ret_long"), { opp: "B016A745", fut: "A016C000" });
  assert.deepEqual(L.legForIndicator(other, "w_link_long"), { opp: "B016A745", fut: "A016C000" });
});

test("합산수익률 양매도는 프라이스링크 양매도의 상대와 선물을 쓴다", () => {
  const legs = L.setLeg(L.empty(), "w_link_short", "opp", "CAFC0A29");
  const withFut = L.setLeg(legs, "w_link_short", "fut", "A016C000");
  assert.deepEqual(L.legForIndicator(withFut, "w_ret_short"), { opp: "CAFC0A29", fut: "A016C000" });
});

test("같은 쪽에서 상대와 선물에 같은 종목을 고르면 이전 역할은 비운다", () => {
  const legs = L.setLeg(L.empty(), "w_link_long", "opp", "B016A745");
  const swapped = L.setLeg(legs, "w_link_long", "fut", "B016A745");
  assert.deepEqual(L.legForIndicator(swapped, "w_link_long"), { opp: "", fut: "B016A745" });
});

test("국내선물 Data2 선물은 양매수 프라이스링크를 먼저 쓰고, 없으면 양매도를 쓴다", () => {
  const onlyShort = L.setLeg(L.empty(), "w_link_short", "fut", "A016C000");
  assert.equal(L.futForData2(onlyShort), "A016C000");
  const both = L.setLeg(onlyShort, "w_link_long", "fut", "A016C300");
  assert.equal(L.futForData2(both), "A016C300");
});

test("기준이 콜이면 콜만, 풋이면 풋만 시스템 차트다", () => {
  const codes = ["A016C000", "BAFC0A57", "CAFC0A33"];
  const letters = ["", "C", "P"];
  assert.deepEqual(L.optionCharts(codes, letters, "C"), [
    { self: "BAFC0A57", opp: "CAFC0A33", fut: "A016C000" },
  ]);
  assert.deepEqual(L.optionCharts(codes, letters, "P"), [
    { self: "CAFC0A33", opp: "BAFC0A57", fut: "A016C000" },
  ]);
  assert.deepEqual(L.optionCharts(["BAFC0A57"], ["C"], "C"), []);
});

test("종목 순서는 지수, 콜, 풋이다", () => {
  const codes = ["a016c000", "BAFC0A49", "cafc0a41"];
  const legs = L.fromSymbolOrder(codes);
  assert.deepEqual(L.legForIndicator(legs, "w_link_long"), { opp: "CAFC0A41", fut: "A016C000" });
  assert.deepEqual(L.legForIndicator(legs, "w_link_short"), { opp: "BAFC0A49", fut: "A016C000" });
  assert.equal(L.selfFor(codes, 1), "BAFC0A49");
  assert.equal(L.selfFor(codes, -1), "CAFC0A41");
  assert.equal(L.selfFor(["A016C000"], -1), "");
});

test("차트에서 뺀 종목은 프라이스링크 역할에서 빠진다", () => {
  let legs = L.setLeg(L.empty(), "w_link_long", "opp", "B016A745");
  legs = L.setLeg(legs, "w_link_short", "fut", "B016A745");
  legs = L.forgetCode(legs, "b016a745");
  assert.deepEqual(L.legForIndicator(legs, "w_link_long"), { opp: "", fut: "" });
  assert.deepEqual(L.legForIndicator(legs, "w_link_short"), { opp: "", fut: "" });
});
