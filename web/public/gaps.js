// 시간축 공백(구멍) 표시 — 스냅샷의 gaps 구간을 분 단위 whitespace 포인트로 펼쳐
// 캔들 시리즈에 섞는다 (2026-09-30 RT 공백 사건 후속, G2).
// lightweight-charts whitespace 항목은 {time}만 가진다 — 가격 필드가 없어 그 자리는
// 캔들 없이 빈 칸으로 그려진다. 엔진이 구멍 구간(같은 세션 안의 누락 분)을 알려주고,
// 이 파일은 시딩 시 그 구간을 시리즈 데이터로 펼치기만 한다 (계산·판별은 엔진 소유).
// DOM 없는 순수 로직 (node:test 단위 테스트 대상).
// 브라우저에서는 전역 Gaps, node:test에서는 globalThis.Gaps로 쓴다.
"use strict";

const Gaps = (() => {
  const MIN_SEC = 60; // 1분봉 간격 (엔진 timeframe_sec=60 기준 구멍 구간)

  // withWhitespace(bars, gaps): 시각 오름차순 캔들 행({time: 초, open, ...})과 구멍
  // 구간 목록([[startSec, endSec], ...] — 빈 분의 양 끝, 역시 초)을 받아, 각 구간의
  // 매분을 {time}만 가진 whitespace 항목으로 펼쳐 봉 사이에 섞은 새 배열을 돌려준다.
  // - 구간 경계는 봉 시각과 겹치지 않는 게 엔진 계약이지만, 겹치는 포인트는 만들지
  //   않는다 (같은 time이 두 번이면 setData가 거부하므로 방어).
  // - 반환 길이 − bars.length = whitespace 수. pane-sync의 getLength가 시리즈 길이
  //   (봉 + whitespace)를 돌려주는 데 이 차이(wsCount)를 쓴다 (app.js 주석 참조).
  function withWhitespace(bars, gaps) {
    if (!Array.isArray(gaps) || gaps.length === 0) return bars.slice();
    const barTimes = new Set();
    for (const b of bars) barTimes.add(b.time);
    const ws = [];
    for (const g of gaps) {
      const a = Number(g?.[0]), b = Number(g?.[1]);
      if (!Number.isFinite(a) || !Number.isFinite(b)) continue;
      for (let t = a; t <= b; t += MIN_SEC) {
        if (!barTimes.has(t)) ws.push({ time: t });
      }
    }
    if (ws.length === 0) return bars.slice();
    return bars.concat(ws).sort((x, y) => x.time - y.time);
  }

  return { withWhitespace };
})();

if (typeof globalThis !== "undefined") {
  globalThis.Gaps = Gaps;
}
