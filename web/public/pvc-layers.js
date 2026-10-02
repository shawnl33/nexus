// 가격거래량압축. 직전 봉의 두 구간 가격 비율과 구간 평균 거래량 비율.
// 동시 압축은 0선에 굵은 점으로 찍는다. 가격 축과 단위가 달라 아래 스케일에 둔다.
"use strict";

const PvcLayers = (() => {
  function parsePvc(v) {
    if (!Array.isArray(v) || v.length < 5) return null;
    const n = (i) => Number(v[i]);
    return {
      priceOn: n(0) === 1,
      price: n(1),
      volOn: n(2) === 1,
      vol: n(3),
      both: n(4) === 1,
    };
  }

  const PvcRenderer = {
    id: "fx_pvc",
    createHandle(chart) {
      const layers = { price: true, volume: true, both: true };
      const specs = [
        { layer: "price", color: "#ff00ff", width: 1 },
        { layer: "volume", color: "#0000ff", width: 1 },
        { layer: "both", color: "#ff8c00", width: 3 },
      ];
      const lines = specs.map((d) => ({
        ...d,
        series: chart.addLineSeries({
          color: d.color,
          lineWidth: d.width,
          priceScaleId: "pvc",
          priceLineVisible: false,
          lastValueVisible: false,
          lineVisible: false,
        }),
      }));
      for (const ln of lines) ln.series.attachPrimitive(globalThis.HorizLines.primitive(ln.color, ln.width));
      function setAxis(id) {
        const scale = id || "pvc";
        for (const ln of lines) if (ln.series.applyOptions) ln.series.applyOptions({ priceScaleId: scale });
        if (typeof chart.priceScale === "function") {
          chart.priceScale(scale).applyOptions({ scaleMargins: { top: 0.08, bottom: 0.08 } });
        }
      }
      setAxis("pvc");
      let ctx = null;
      function point(row, layer) {
        if (!row) return null;
        if (layer === "price") return row.priceOn && Number.isFinite(row.price) ? row.price : null;
        if (layer === "volume") return row.volOn && Number.isFinite(row.vol) ? row.vol : null;
        return row.both ? 0 : null;
      }
      function rebuild() {
        for (const ln of lines) {
          const data = [];
          if (layers[ln.layer] && ctx) {
            for (const t of ctx.barSeq) {
              const x = point(ctx.barInd.get(t)?.pvc, ln.layer);
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
        setAxis,
        applyLive(p, c) {
          ctx = c;
          const t = Number(p.bar_open_time) / 1e6;
          if (!Number.isFinite(t) || t <= 0) return;
          const row = c.barInd.get(t)?.pvc;
          for (const ln of lines) {
            if (!layers[ln.layer]) continue;
            const x = point(row, ln.layer);
            ln.series.update(x != null ? { time: t, value: x } : { time: t });
          }
        },
        applySeed(c) { ctx = c; rebuild(); },
        clear() { for (const ln of lines) ln.series.setData([]); },
        destroy() { for (const ln of lines) chart.removeSeries(ln.series); },
      };
    },
  };

  return { parsePvc, PvcRenderer };
})();

if (typeof globalThis !== "undefined") globalThis.PvcLayers = PvcLayers;
