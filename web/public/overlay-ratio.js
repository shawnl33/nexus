// 겹침 종목의 가격을 불러온 구간 첫 유효 종가 = 100 인 비율로 바꾼다.
// DOM 없는 순수 함수. 차트 가격 포매터(값을 100으로 나눔)는 여기서 다루지 않는다.
// 브라우저에서는 전역 OverlayRatio, node:test에서는 globalThis.OverlayRatio.
"use strict";

const OverlayRatio = (() => {
  function roundRatio(value) {
    return Math.round(value * 1e6) / 1e6;
  }

  // bars: { time, close }[]. close가 유한하고 0이 아닌 첫 봉이 100이다.
  // 그 앞의 빈 값은 건너뛴다. 결과는 시간 순서를 유지한다.
  function points(bars) {
    return ohlc(bars).map((p) => ({ time: p.time, value: p.close }));
  }

  // 시가·고가·저가·종가. 기준은 points와 같은 첫 유효 종가다.
  // 유한하지 않거나 0인 시가·고가·저가는 그 봉의 종가로 채운다.
  function ohlc(bars) {
    const out = [];
    let base = null;
    for (const bar of Array.isArray(bars) ? bars : []) {
      const time = Number(bar?.time);
      const close = Number(bar?.close);
      if (!Number.isFinite(time) || !Number.isFinite(close) || close === 0) continue;
      if (base == null) base = close;
      const ratio = (v) => {
        const n = Number(v);
        return roundRatio((Number.isFinite(n) && n !== 0 ? n : close) / base * 100);
      };
      out.push({ time, open: ratio(bar?.open), high: ratio(bar?.high), low: ratio(bar?.low), close: ratio(close) });
    }
    return out;
  }

  return { points, ohlc };
})();

if (typeof globalThis !== "undefined") {
  globalThis.OverlayRatio = OverlayRatio;
}
