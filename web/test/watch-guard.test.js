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
