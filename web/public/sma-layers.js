// 이평선(SMA 5/20/60) 렌더러 — 패널 매니저(app.js)의 렌더러 계약 구현체.
// 계산은 엔진(src/core/functions/sma.c)이 하고, 이 파일은 상태 스트림의
// sma 키와 스냅샷 ind[28..31] 값의 표시만 담당한다 (app.js가 barInd 캐시에 심는다).
// 브라우저에서는 전역 SmaLayers, node:test에서는 globalThis.SmaLayers로 쓴다.
"use strict";

const SmaLayers = (() => {
  // 매니페스트 레이어 id ↔ LineSeries 색 (브리프 명세)
  const LINES = [
    { layer: "sma5", index: 0, color: "#ff9800" },
    { layer: "sma20", index: 1, color: "#4db6ac" },
    { layer: "sma60", index: 2, color: "#ba68c8" },
  ];

  // createHandle(chart, candleSeries) → handle
  //   handle.setLayers(map)   — "sma5"|"sma20"|"sma60" → bool. 즉시 다시 그린다
  //   handle.applyLive(p, ctx) — status 페이로드 1건 반영 (값은 ctx.barInd에서 읽는다)
  //   handle.applySeed(ctx)    — 시딩 완료 후 ctx 캐시에서 전체 복원
  //   handle.clear()           — 모든 선 제거
  //   handle.destroy()         — 시리즈를 차트에서 분리 (지표 해제 시)
  const SmaRenderer = {
    id: "sma",
    createHandle(chart, candleSeries) {
      void candleSeries; // 라인 시리즈만 쓴다
      const layers = { sma5: true, sma20: true, sma60: true }; // 매니페스트 defaultOn 전부 true
      const lines = LINES.map((d) => ({
        ...d,
        series: chart.addLineSeries({
          color: d.color, lineWidth: 2,
          priceLineVisible: false, lastValueVisible: false,
        }),
      }));
      let ctx = null; // 마지막 applyLive/applySeed의 ctx — setLayers 재구축에 사용

      // 레이어 칩 상태대로 barInd 캐시에서 전체 복원/제거
      function rebuild() {
        for (const ln of lines) {
          const data = [];
          if (layers[ln.layer] && ctx) {
            for (const t of ctx.barSeq) {
              const ind = ctx.barInd.get(t);
              if (ind && ind.smaValid && Number.isFinite(ind.sma?.[ln.index])) {
                data.push({ time: t, value: ind.sma[ln.index] });
              }
            }
          }
          ln.series.setData(data);
        }
      }

      return {
        setLayers(map) {
          let changed = false;
          for (const ln of lines) {
            if (ln.layer in map) {
              layers[ln.layer] = !!map[ln.layer];
              changed = true;
            }
          }
          if (changed) rebuild();
        },

        // 라이브 status 1건 — app.js가 barInd를 먼저 채운 뒤 부른다. 워밍업 봉은 갭.
        applyLive(p, c) {
          ctx = c;
          const t = Number(p.bar_open_time) / 1e6;
          if (!Number.isFinite(t) || t <= 0) return;
          const ind = c.barInd.get(t);
          for (const ln of lines) {
            if (!layers[ln.layer]) continue;
            const v = ind && ind.smaValid ? ind.sma?.[ln.index] : NaN;
            ln.series.update(Number.isFinite(v) ? { time: t, value: v } : { time: t });
          }
        },

        applySeed(c) {
          ctx = c;
          rebuild();
        },

        clear() {
          for (const ln of lines) ln.series.setData([]);
        },

        destroy() {
          for (const ln of lines) chart.removeSeries(ln.series);
        },
      };
    },
  };

  return { LINES, SmaRenderer };
})();

if (typeof globalThis !== "undefined") {
  globalThis.SmaLayers = SmaLayers;
}
