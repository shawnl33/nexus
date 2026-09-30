// watch-guard.js 단위 테스트: stale(늦은 완료) 종목 선택을 폐기할 때
// 엔진에 남은 watch의 해지 여부 판정. 칸 상태는 { symbol, selTarget } 목으로 충분하다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/watch-guard.js");
const WatchGuard = globalThis.WatchGuard;

test("staleWatchLeaks: 어느 칸도 안 보고 진행 중 선택도 없으면 해지한다 (누수)", () => {
  const panes = [{ symbol: "005930", selTarget: "005930" }];
  assert.equal(WatchGuard.staleWatchLeaks("000660", panes), true);
  assert.equal(WatchGuard.staleWatchLeaks("000660", []), true);
});

test("staleWatchLeaks: 다른 칸이 그 종목을 보면 해지하지 않는다 (인계된 watch)", () => {
  const panes = [
    { symbol: "005930", selTarget: "005930" },
    { symbol: "000660", selTarget: "000660" },
  ];
  assert.equal(WatchGuard.staleWatchLeaks("000660", panes), false);
});

test("staleWatchLeaks: 진행 중인 다른 선택이 노리면 해지하지 않는다", () => {
  // 같은 칸에 같은 종목을 연속 선택(Enter 두 번): 앞 선택은 stale로 폐기되지만
  // 뒤 선택(selTarget)이 그 종목을 이어받는다
  const panes = [{ symbol: "005930", selTarget: "000660" }];
  assert.equal(WatchGuard.staleWatchLeaks("000660", panes), false);

  // 다른 칸의 진행 중 선택이 노리는 경우도 같다
  const two = [
    { symbol: "005930", selTarget: "005930" },
    { symbol: "", selTarget: "000660" },
  ];
  assert.equal(WatchGuard.staleWatchLeaks("000660", two), false);
});

test("staleWatchLeaks: 선택이 다른 종목으로 넘어갔으면 이전 종목은 해지한다", () => {
  // A 선택 중 B로 재선택: A의 늦은 완료는 폐기 + 해지 대상
  const panes = [{ symbol: "005930", selTarget: "068270" }];
  assert.equal(WatchGuard.staleWatchLeaks("A016C000", panes), true);
});

test("staleWatchLeaks: 채택 실패로 selTarget이 지워지면 보류됐던 watch는 해지한다", () => {
  // 같은 종목을 두 칸이 동시에 선택 → 한 칸의 stale 완료는 다른 칸의 selTarget 때문에
  // 해지를 보류했다. 그 선택이 실패해 selTarget이 지워진 직후 상태에서는 해지 대상이다
  const panes = [
    { symbol: "005930", selTarget: "" },
    { symbol: "", selTarget: "" }, // 000660 선택 실패 직후
  ];
  assert.equal(WatchGuard.staleWatchLeaks("000660", panes), true);
});

test("staleWatchLeaks: 실패한 선택 외에 아직 진행 중인 이어받기가 있으면 해지하지 않는다", () => {
  const panes = [
    { symbol: "005930", selTarget: "" },
    { symbol: "", selTarget: "000660" }, // 이 칸의 선택은 아직 진행 중
  ];
  assert.equal(WatchGuard.staleWatchLeaks("000660", panes), false);
});

test("staleWatchLeaks: 실패 후에도 이미 그 종목을 보는 칸이 있으면 해지하지 않는다", () => {
  const panes = [
    { symbol: "000660", selTarget: "" },
    { symbol: "", selTarget: "" },
  ];
  assert.equal(WatchGuard.staleWatchLeaks("000660", panes), false);
});

test("createOpQueue: 같은 종목의 요청은 enqueue 순서대로 실행된다", async () => {
  const q = WatchGuard.createOpQueue();
  const order = [];
  const run = (ms, tag) => () =>
    new Promise((r) => setTimeout(() => { order.push(tag); r(tag); }, ms));
  // 앞 요청이 더 오래 걸려도 뒤 요청은 앞 요청이 끝난 뒤에 시작한다
  const p1 = q.enqueue("005930", run(30, "unwatch"));
  const p2 = q.enqueue("005930", run(0, "watch"));
  const [r1, r2] = await Promise.all([p1, p2]);
  assert.deepEqual(order, ["unwatch", "watch"]);
  assert.equal(r1, "unwatch"); // enqueue는 op의 반환값을 그대로 돌려준다
  assert.equal(r2, "watch");
});

test("createOpQueue: 다른 종목의 요청은 서로 기다리지 않는다", async () => {
  const q = WatchGuard.createOpQueue();
  const order = [];
  const run = (ms, tag) => () =>
    new Promise((r) => setTimeout(() => { order.push(tag); r(); }, ms));
  const p1 = q.enqueue("005930", run(30, "A"));
  const p2 = q.enqueue("000660", run(0, "B"));
  await Promise.all([p1, p2]);
  assert.deepEqual(order, ["B", "A"]); // 늦게 enqueue됐어도 빨리 끝나면 먼저 완료
});

test("createOpQueue: 앞 요청이 실패해도 뒤 요청은 실행된다", async () => {
  const q = WatchGuard.createOpQueue();
  const p1 = q.enqueue("005930", () => Promise.reject(new Error("boom")));
  const ran = [];
  const p2 = q.enqueue("005930", () => { ran.push("next"); });
  await assert.rejects(p1, /boom/);
  await p2;
  assert.deepEqual(ran, ["next"]);
});
