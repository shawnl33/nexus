// 삼선 구간 이탈과 양매수 가설 1~4. 범위는 가격 축, 신호는 그 봉의 표시 가격.
"use strict";

const YmaeLayers = (() => {
  function parseYmae(v) {
    if (!Array.isArray(v) || v.length < 14) return null;
    const n = (i) => {
      const x = Number(v[i]);
      return Number.isFinite(x) ? x : 0;
    };
    return {
      pos: n(0), prevValid: n(1) === 1, prevHi: n(2), prevLo: n(3), twoHi: n(4), twoLo: n(5),
      h1: n(6), h2: n(7), h3: n(8), h4: n(9),
      mark1: n(10), mark2: n(11), mark3: n(12), mark4: n(13),
      first: v.length > 14 ? n(14) : 0,
    };
  }

  const YmaeRenderer = {
    id: "fx_ymae",
    createHandle(chart) {
      const layers = { range: true, hypo: true };
      const specs = [
        { layer: "range", key: "prevHi" },
        { layer: "range", key: "prevLo" },
        { layer: "range", key: "twoHi" },
        { layer: "range", key: "twoLo" },
        { layer: "hypo", key: "mark1", color: "#ff8c00" },
        { layer: "hypo", key: "mark2", color: "#b40000" },
        { layer: "hypo", key: "mark3", color: "#ff8c00" },
        { layer: "hypo", key: "mark4", color: "#b40000" },
      ];
      const lines = specs.map((d) => ({
        ...d,
        series: chart.addLineSeries({
          color: d.color || "#9aa4b2",
          lineWidth: d.layer === "hypo" ? 3 : 1,
          priceScaleId: "right",
          priceLineVisible: false,
          lastValueVisible: false,
        }),
      }));
      let ctx = null;
      function val(row, ln) {
        if (!row) return null;
        if (ln.layer === "range") {
          if (ln.key.startsWith("prev") && !row.prevValid) return null;
          const x = row[ln.key];
          return x ? x : null;
        }
        const x = row[ln.key];
        return x ? x : null;
      }
      function rebuild() {
        for (const ln of lines) {
          const data = [];
          if (layers[ln.layer] && ctx) {
            for (const t of ctx.barSeq) {
              const x = val(ctx.barInd.get(t)?.ymae, ln);
              data.push(x != null ? { time: t, value: x } : { time: t });
            }
          }
          ln.series.setData(data);
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
          const row = c.barInd.get(t)?.ymae;
          for (const ln of lines) {
            if (!layers[ln.layer]) continue;
            const x = val(row, ln);
            ln.series.update(x != null ? { time: t, value: x } : { time: t });
          }
        },
        applySeed(c) { ctx = c; rebuild(); },
        clear() { for (const ln of lines) ln.series.setData([]); },
        destroy() { for (const ln of lines) chart.removeSeries(ln.series); },
      };
    },
  };

  return { parseYmae, YmaeRenderer };
})();

if (typeof globalThis !== "undefined") globalThis.YmaeLayers = YmaeLayers;
