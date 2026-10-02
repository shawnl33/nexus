// 지속목표차 삼선·오선. 값은 가격이 아니라 폭·비율이라 별도 스케일이다.
// 브라우저에서는 전역 PgapLayers, node:test에서는 globalThis.PgapLayers.
"use strict";

const PgapLayers = (() => {
  function num(v) {
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  }

  function parseSide(row) {
    if (!Array.isArray(row) || row.length < 7) return null;
    return {
      ready: Number(row[0]) === 1,
      gap: num(row[1]), peak: num(row[2]), prev: num(row[3]),
      ratio: num(row[4]), prevRatio: num(row[5]), cnt: num(row[6]),
      rgb4: Number(row[7]) || 0x808080,
      w4: Number(row[8]) || 1,
      rgb5: Number(row[9]) || 0x808080,
      plot3: Number(row[10]) === 1,
      plot5: Number(row[11]) === 1,
      unionRgb: Number.isFinite(Number(row[12])) ? Number(row[12]) : null,
      unionW: Number.isFinite(Number(row[13])) ? Number(row[13]) : 1,
    };
  }

  function parsePgap(v) {
    if (!Array.isArray(v)) return [null, null];
    return [parseSide(v[0]), parseSide(v[1])];
  }

  function css(rgb) {
    return "#" + (Number(rgb) >>> 0).toString(16).padStart(6, "0").slice(-6);
  }

  const LINES = [
    { layer: "width", key: "gap", scale: "pgap", color: "#d7dde8" },
    { layer: "width", key: "peak", scale: "pgap", color: "#f0c36a" },
    { layer: "width", key: "prev", scale: "pgap", color: "#8ecae6", gate: "plot3" },
    { layer: "ratio", key: "ratio", scale: "pgratio", color: "#808080" },
    { layer: "ratio", key: "prevRatio", scale: "pgratio", color: "#808080", gate: "plot5" },
    { layer: "count", key: "cnt", scale: "pgratio", color: "#c9a227" },
  ];

  function make(id, side) {
    return {
      id,
      createHandle(chart) {
        const layers = { width: true, ratio: true, count: true };
        const lines = LINES.map((d) => ({
          ...d,
          series: chart.addLineSeries({
            color: d.color,
            lineWidth: 1,
            priceScaleId: d.scale,
            priceLineVisible: false,
            lastValueVisible: false,
          }),
        }));
        if (typeof chart.priceScale === "function") {
          chart.priceScale("pgap").applyOptions({ scaleMargins: { top: 0.72, bottom: 0 } });
          chart.priceScale("pgratio").applyOptions({ scaleMargins: { top: 0.72, bottom: 0 } });
        }
        let ctx = null;
        function rowAt(ind) {
          const pair = ind && ind.pgap;
          return Array.isArray(pair) ? pair[side] : null;
        }
        function rebuild() {
          for (const ln of lines) {
            const data = [];
            if (layers[ln.layer] && ctx) {
              for (const t of ctx.barSeq) {
                const row = rowAt(ctx.barInd.get(t));
                const show = row && row.ready && (!ln.gate || row[ln.gate]);
                const value = show ? row[ln.key] : null;
                data.push(value != null ? { time: t, value } : { time: t });
              }
            }
            ln.series.setData(data);
          }
        }
        return {
          setLayers(map) {
            let changed = false;
            for (const key of Object.keys(layers)) {
              if (key in map) {
                layers[key] = !!map[key];
                changed = true;
              }
            }
            if (changed) rebuild();
          },
          applyLive(p, c) {
            ctx = c;
            const t = Number(p.bar_open_time) / 1e6;
            if (!Number.isFinite(t) || t <= 0) return;
            const row = rowAt(c.barInd.get(t));
            for (const ln of lines) {
              if (!layers[ln.layer]) continue;
              const show = row && row.ready && (!ln.gate || row[ln.gate]);
              const value = show ? row[ln.key] : null;
              if (ln.key === "ratio" && row) {
                ln.series.applyOptions({ color: css(row.rgb4), lineWidth: Math.max(1, row.w4 || 1) });
              }
              if (ln.key === "prevRatio" && row) ln.series.applyOptions({ color: css(row.rgb5) });
              ln.series.update(value != null ? { time: t, value } : { time: t });
            }
          },
          applySeed(c) { ctx = c; rebuild(); },
          clear() { for (const ln of lines) ln.series.setData([]); },
          destroy() { for (const ln of lines) chart.removeSeries(ln.series); },
        };
      },
    };
  }

  function makeSingle(id, field) {
    const inner = make(id, 0);
    return {
      id,
      createHandle(chart) {
        const handle = inner.createHandle(chart);
        const wrap = (c) => {
          if (!c) return c;
          const barInd = new Map();
          for (const [t, ind] of c.barInd) {
            barInd.set(t, ind ? { ...ind, pgap: [ind[field], null] } : ind);
          }
          return { ...c, barInd };
        };
        return {
          setLayers: handle.setLayers,
          applyLive(p, c) { handle.applyLive(p, wrap(c)); },
          applySeed(c) { handle.applySeed(wrap(c)); },
          clear: handle.clear,
          destroy: handle.destroy,
        };
      },
    };
  }

  function makeUnion(id) {
    const specs = [
      { layer: "three", pick: (ind) => ind?.pgap?.[0], colorKey: "union" },
      { layer: "five", pick: (ind) => ind?.pgap?.[1], colorKey: "union" },
      { layer: "reg", pick: (ind) => ind?.rgap, colorKey: "rgb4" },
      { layer: "market", pick: (ind) => ind?.mgap, colorKey: "rgb4" },
    ];
    return {
      id,
      createHandle(chart) {
        const layers = { three: true, five: true, reg: true, market: true };
        const lines = specs.map((d) => ({
          ...d,
          series: chart.addLineSeries({
            color: "#d7dde8",
            lineWidth: 1,
            priceScaleId: "pgap",
            priceLineVisible: false,
            lastValueVisible: false,
          }),
        }));
        if (typeof chart.priceScale === "function") {
          chart.priceScale("pgap").applyOptions({ scaleMargins: { top: 0.72, bottom: 0 } });
        }
        let ctx = null;
        function rebuild() {
          for (const ln of lines) {
            const data = [];
            if (layers[ln.layer] && ctx) {
              for (const t of ctx.barSeq) {
                const row = ln.pick(ctx.barInd.get(t));
                data.push(row && row.ready && row.gap != null ? { time: t, value: row.gap } : { time: t });
              }
            }
            ln.series.setData(data);
          }
        }
        return {
          setLayers(map) {
            let changed = false;
            for (const key of Object.keys(layers)) {
              if (key in map) {
                layers[key] = !!map[key];
                changed = true;
              }
            }
            if (changed) rebuild();
          },
          applyLive(p, c) {
            ctx = c;
            const t = Number(p.bar_open_time) / 1e6;
            if (!Number.isFinite(t) || t <= 0) return;
            const ind = c.barInd.get(t);
            for (const ln of lines) {
              if (!layers[ln.layer]) continue;
              const row = ln.pick(ind);
              const on = row && row.ready && row.gap != null;
              if (on && ln.colorKey === "union" && row.unionRgb != null) {
                ln.series.applyOptions({ color: css(row.unionRgb), lineWidth: Math.max(1, row.unionW || 1) });
              }
              ln.series.update(on ? { time: t, value: row.gap } : { time: t });
            }
          },
          applySeed(c) { ctx = c; rebuild(); },
          clear() { for (const ln of lines) ln.series.setData([]); },
          destroy() { for (const ln of lines) chart.removeSeries(ln.series); },
        };
      },
    };
  }

  return { parsePgap, parseSide, make, makeSingle, makeUnion };
})();

if (typeof globalThis !== "undefined") globalThis.PgapLayers = PgapLayers;
