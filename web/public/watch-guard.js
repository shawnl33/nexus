// stale(늦은 완료) 종목 선택의 watch 누수 판정 (다중 종목 Task C).
// 칸의 종목 선택은 비동기라(selSeq로 늦은 완료를 폐기) 폐기 시점에 엔진이 이미
// 그 종목을 watch했을 수 있다 — 아무도 안 쓰는 watch가 남지 않게 해지 여부를 판정한다.
// 같은 판정은 채택 실패 경로에서도 쓴다: 다른 선택이 이어받기로 한 watch가
// 그 선택의 실패로 주인을 잃었을 때(selTarget 해제 후)의 해지 여부.
// DOM 없는 순수 로직 (node:test 단위 테스트 대상).
// 브라우저에서는 전역 WatchGuard, node:test에서는 globalThis.WatchGuard로 쓴다.
"use strict";

const WatchGuard = (() => {
  // 이 종목의 watch를 해지해야 하는가:
  // 어느 칸도 그 종목을 보지 않고(symbol), 진행 중인 다른 선택도 그 종목을
  // 노리지 않을 때(selTarget)만 해지한다. Data2 참조(data2)로 쓰는 종목도 유지한다.
  // panes는 { symbol, selTarget, data2 } 목록.
  function staleWatchLeaks(shcode, panes) {
    return !panes.some((p) => p.symbol === shcode || p.selTarget === shcode || p.data2 === shcode);
  }

  // 같은 종목의 watch/unwatch 요청을 직렬화하는 큐를 만든다.
  // 독립 fetch의 엔진 도착 순서는 보장되지 않는다(서버는 요청마다 새 DEALER를
  // 연다 — server.js engineCommand) — 먼저 낸 unwatch가 뒤에 낸 watch보다 늦게
  // 도착하면 막 채택한 종목을 끊는다. enqueue(shcode, op)는 그 종목의 앞 요청이
  // 끝난 뒤(성패 무관) op()를 실행하는 Promise를 돌려준다.
  function createOpQueue() {
    const tails = new Map(); // shcode → 마지막으로 enqueue된 요청 Promise
    return {
      enqueue(shcode, op) {
        const prev = tails.get(shcode) ?? Promise.resolve();
        const next = prev.then(op, op);
        tails.set(shcode, next);
        const cleanup = () => {
          if (tails.get(shcode) === next) tails.delete(shcode);
        };
        next.then(cleanup, cleanup); // 파생 Promise는 항상 resolve — 미처리 rejection 방지
        return next;
      },
    };
  }

  return { staleWatchLeaks, createOpQueue };
})();

if (typeof globalThis !== "undefined") {
  globalThis.WatchGuard = WatchGuard;
}
