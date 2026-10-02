// 스나이퍼 점수·비율 점수·압축 단계. 가격과 단위가 달라 아래 스케일에 둔다.
"use strict";

const SniperLayers = (() => {
  function parseSniper(v) {
    if (!Array.isArray(v) || v.length < 8) return null;
    const n = (i) => Number(v[i]);
    return {
      score: n(0), scoreEx: n(1), ratio: n(2), stage: n(3), compound: n(4),
      targetRatio: n(5), priceRatio: n(6), rgb: n(7),
      pxExit: v.length > 8 ? n(8) : 0,
      below: v.length > 9 ? n(9) : 0,
      above: v.length > 10 ? n(10) : 0,
      reset: v.length > 11 ? n(11) : 0,
    };
  }
  function css(rgb) {
    return "#" + (Number(rgb) >>> 0).toString(16).padStart(6, "0").slice(-6);
  }
  const SniperRenderer = {
    id: "fx_sniper",
    createHandle(chart) {
      const layers = { score: true, ratio: true, squeeze: true };
      const specs = [
        { layer: "score", key: "score" },
        { layer: "ratio", key: "ratio" },
        { layer: "squeeze", key: "stage" },
      ];
      const lines = specs.map((d) => ({
        ...d,
        series: chart.addLineSeries({
          color: "#d7dde8",
          lineWidth: 2,
          priceScaleId: "sniper",
          priceLineVisible: false,
          lastValueVisible: false,
        }),
      }));
      if (typeof chart.priceScale === "function") {
        chart.priceScale("sniper").applyOptions({ scaleMargins: { top: 0.78, bottom: 0 } });
      }
      let ctx = null;
      function rebuild() {
        for (const ln of lines) {
          const data = [];
          if (layers[ln.layer] && ctx) {
            for (const t of ctx.barSeq) {
              const row = ctx.barInd.get(t)?.sniper;
              const x = row && Number.isFinite(row[ln.key]) ? row[ln.key] : null;
              data.push(x != null ? { time: t, value: x } : { time: t });
            }
          }
          ln.series.setData(data);
          const lastT = ctx && ctx.barSeq.length ? ctx.barSeq[ctx.barSeq.length - 1] : 0;
          const last = ctx ? ctx.barInd.get(lastT)?.sniper : null;
          if (last && ln.layer === "score") ln.series.applyOptions({ color: css(last.rgb) });
        }
      }
      return {
        setLayers(map) {
          let changed = false;
          for (const k of Object.keys(layers)) {
            if (k in map) {
              layers[k] = !!map[k];
              changed = true;
            }
          }
          if (changed) rebuild();
        },
        applyLive(p, c) {
          ctx = c;
          const t = Number(p.bar_open_time) / 1e6;
          if (!Number.isFinite(t) || t <= 0) return;
          const row = c.barInd.get(t)?.sniper;
          for (const ln of lines) {
            if (!layers[ln.layer]) continue;
            const x = row && Number.isFinite(row[ln.key]) ? row[ln.key] : null;
            if (row && ln.layer === "score") ln.series.applyOptions({ color: css(row.rgb) });
            ln.series.update(x != null ? { time: t, value: x } : { time: t });
          }
        },
        applySeed(c) { ctx = c; rebuild(); },
        clear() { for (const ln of lines) ln.series.setData([]); },
        destroy() { for (const ln of lines) chart.removeSeries(ln.series); },
      };
    },
  };
  return { parseSniper, SniperRenderer };
})();

if (typeof globalThis !== "undefined") globalThis.SniperLayers = SniperLayers;
