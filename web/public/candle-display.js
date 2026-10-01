// HTS식 몸통 표시 — 캔들 시리즈에 넘기기 직전의 표시 전용 변환 (데이터 무가공).
//
// 배경: 우리 캔들(LS t8412/라이브 틱)은 시가=그 분의 첫 체결가라, 분 안 체결이
// 한두 값이면 시가=종가인 봉이 많아 몸통이 실선처럼 끊겨 보인다. HTS는 몸통이
// 연속되어 보이는데, 그 규칙은 시가=직전 봉 종가다. 이 모듈은 그 표시 규칙만
// 구현한다 — 캐시(feed의 bars/barSeq)·지표·크로스헤어 getPrice는 원래 값을
// 유지하고, candleSeries에 넘길 값만 변환한다 (app.js의 htsBodyOn 옵션이 켜진
// 경로에서만 호출된다).
//
// 변환 규칙 (실제 봉마다):
// - open  = 직전 실제 봉의 close (첫 실제 봉은 자기 시가 유지)
// - high  = max(원래 high, 직전 종가), low = min(원래 low, 직전 종가)
//   — 직전 종가가 봉 범위 밖이면 꼬리를 늘려 몸통이 꼬리 밖으로 나가지 않게 한다
// - close·volume·time·부가 키(시딩 봉의 ind 등)는 원래대로
// - whitespace({time}만 있는 항목, gaps.js의 균일 분 그리드)는 그대로 통과시키고
//   체인은 whitespace를 건드리지 않고 실제 봉끼리 잇는다
//
// 표시 종가는 언제나 실제 종가와 같다 (close는 변환하지 않는다) — 그래서 라이브
// 꼬리의 직전 종가는 별도 추적 없이 캐시의 직전 실제 봉에서 읽어도 정확하다
// (app.js applyStatus 주석 참조). 입력 배열과 봉 객체는 변이하지 않는다.
// DOM 없는 순수 로직 (node:test 단위 테스트 대상).
// 브라우저에서는 전역 CandleDisplay, node:test에서는 globalThis.CandleDisplay로 쓴다.
"use strict";

const CandleDisplay = (() => {
  // whitespace 항목 판정: 가격 필드가 없는 {time}만의 항목 (gaps.js withWhitespace가 만든다)
  const isWhitespace = (b) => b.open === undefined;

  // 라이브 1봉 변환: prevClose(직전 실제 봉의 종가)를 시가로 쓴 표시용 새 객체를 돌려준다.
  // prevClose가 유한하지 않으면(첫 봉) 원래 봉을 그대로 돌려준다 — 자기 시가 유지.
  // 변환 시에만 새 객체를 만들므로 캐시 원본은 절대 변이되지 않는다.
  function htsChainBar(prevClose, bar) {
    if (!Number.isFinite(prevClose)) return bar;
    return {
      ...bar,
      open: prevClose,
      high: Math.max(bar.high, prevClose),
      low: Math.min(bar.low, prevClose),
    };
  }

  // 체인 변환: 봉 배열(whitespace 섞일 수 있음)을 받아 표시용 새 배열을 돌려준다.
  // 길이·시각 목록은 입력과 같아 wsCount/seriesTimes 계약(app.js)에 영향이 없다.
  function htsChain(bars) {
    if (!Array.isArray(bars)) return [];
    let prevClose; // 직전 실제 봉의 종가 — 첫 실제 봉 전까지는 undefined
    return bars.map((b) => {
      if (isWhitespace(b)) return b; // whitespace는 그대로 — 체인은 실제 봉끼리 잇는다
      const out = htsChainBar(prevClose, b);
      prevClose = b.close; // 표시 종가 = 실제 종가 (close는 변환하지 않는다)
      return out;
    });
  }

  return { htsChain, htsChainBar };
})();

if (typeof globalThis !== "undefined") {
  globalThis.CandleDisplay = CandleDisplay;
}
