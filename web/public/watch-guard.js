// stale(늦은 완료) 종목 선택의 watch 누수 판정 (다중 종목 Task C).
// 칸의 종목 선택은 비동기라(selSeq로 늦은 완료를 폐기) 폐기 시점에 엔진이 이미
// 그 종목을 watch했을 수 있다 — 아무도 안 쓰는 watch가 남지 않게 해지 여부를 판정한다.
// DOM 없는 순수 로직 (node:test 단위 테스트 대상).
// 브라우저에서는 전역 WatchGuard, node:test에서는 globalThis.WatchGuard로 쓴다.
"use strict";

const WatchGuard = (() => {
  // 폐기되는 선택의 watch를 해지해야 하는가:
  // 어느 칸도 그 종목을 보지 않고(symbol), 진행 중인 다른 선택도 그 종목을
  // 노리지 않을 때(selTarget)만 해지한다. 그 외는 다른 칸/선택이 이어받은
  // watch이므로 건드리지 않는다. panes는 { symbol, selTarget } 목록.
  function staleWatchLeaks(shcode, panes) {
    return !panes.some((p) => p.symbol === shcode || p.selTarget === shcode);
  }

  return { staleWatchLeaks };
})();

if (typeof globalThis !== "undefined") {
  globalThis.WatchGuard = WatchGuard;
}
