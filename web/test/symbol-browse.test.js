import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/symbol-browse.js");
const B = globalThis.SymbolBrowse;

test("parseOptName: 월물과 위클리 이름을 콜풋·만기·행사가로 나눈다", () => {
  assert.deepEqual(B.parseOptName("C 2610   745.0"), { cp: "C", expiry: "2610", strike: 745 });
  assert.deepEqual(B.parseOptName("P 2610   745.0"), { cp: "P", expiry: "2610", strike: 745 });
  assert.deepEqual(B.parseOptName("C 월 W1 1,140.0"), { cp: "C", expiry: "월 W1", strike: 1140 });
  assert.equal(B.parseOptName("삼성전자"), null);
  assert.equal(B.parseOptName("F 2612"), null);
});

test("futMonthLabel: 코스피200 선물 이름을 만기로 보여 준다", () => {
  assert.equal(B.futMonthLabel("F 2612"), "2026-12");
  assert.equal(B.futMonthLabel("F 2703"), "2027-03");
  assert.equal(B.expiryLabel("2610"), "2026-10");
  assert.equal(B.expiryLabel("월 W1"), "월 W1");
  assert.deepEqual(B.sortExpiries(["2611", "월 W2", "2610", "월 W1"]), ["월 W1", "월 W2", "2610", "2611"]);
});

test("ovsRank: 해외선물 카드는 ES NQ YM RTY CL GC 순이다", () => {
  assert.equal(B.ovsPrefix("ESZ26"), "ES");
  assert.equal(B.ovsPrefix("RTYZ26"), "RTY");
  assert.equal(B.ovsPrefix("CLX26"), "CL");
  const codes = ["GCZ26", "ESZ26", "CLX26", "NQZ26"];
  codes.sort((a, b) => B.ovsRank(a) - B.ovsRank(b));
  assert.deepEqual(codes, ["ESZ26", "NQZ26", "CLX26", "GCZ26"]);
});

test("nearestStrike: 기초자산 가격에 가장 가까운 행사가가 ATM이다", () => {
  assert.equal(B.nearestStrike([1125, 1127.5, 1130], 1127.15), 1127.5);
  assert.equal(B.nearestStrike([100, 102.5], 101.25), 100);
  assert.equal(B.nearestStrike([], 100), null);
});

test("chainRows: 같은 행사가의 콜과 풋을 한 줄로 모은다", () => {
  const rows = B.chainRows([
    { shcode: "P750", cp: "P", strike: 750, name: "P 750" },
    { shcode: "C745", cp: "C", strike: 745, name: "C 745" },
    { shcode: "P745", cp: "P", strike: 745, name: "P 745" },
  ]);
  assert.deepEqual(rows.map((r) => r.strike), [750, 745]);
  assert.equal(rows[0].call, null);
  assert.equal(rows[0].put.shcode, "P750");
  assert.equal(rows[1].call.shcode, "C745");
  assert.equal(rows[1].put.shcode, "P745");
  const named = B.chainRows([{ shcode: "C", name: "C 2610   1,127.5", cp: "C", strike: 1128 }]);
  assert.equal(named[0].strike, 1127.5);
});
