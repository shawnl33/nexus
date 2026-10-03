import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/symbol-pick.js");
const P = globalThis.SymbolPick;

const items = [
  { shcode: "00066A", name: "SK하이닉스2우B" },
  { shcode: "000660", name: "SK하이닉스" },
  { shcode: "ESZ26", name: "E-mini S&P 500" },
];

test("이름과 같은 후보를 코드보다 앞의 비슷한 이름보다 고른다", () => {
  const hit = P.pick("sk하이닉스", items);
  assert.equal(hit.shcode, "000660");
  assert.equal(hit.name, "SK하이닉스");
});

test("코드가 같으면 그 후보를 고른다", () => {
  assert.equal(P.pick("esz26", items).shcode, "ESZ26");
});

test("이름도 코드도 같지 않으면 목록의 첫 후보를 고른다", () => {
  assert.equal(P.pick("하이닉스", items).shcode, "00066A");
});

test("후보가 없으면 null이다", () => {
  assert.equal(P.pick("sk하이닉스", []), null);
  assert.equal(P.pick("  ", items), null);
});

test("처음 커서는 엔터가 고를 후보에 있다", () => {
  assert.equal(P.activeIndex("sk하이닉스", items), 1);
  assert.equal(P.activeIndex("하이닉스", items), 0);
  assert.equal(P.activeIndex("sk하이닉스", []), -1);
});

test("위아래 키는 후보 목록 안에서만 커서를 옮긴다", () => {
  assert.equal(P.move(1, 1, 3), 2);
  assert.equal(P.move(1, -1, 3), 0);
  assert.equal(P.move(0, -1, 3), 0);
  assert.equal(P.move(2, 1, 3), 2);
  assert.equal(P.move(0, 1, 0), -1);
});
