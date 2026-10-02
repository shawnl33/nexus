// 해외선물 미래곡선 V3 표시. 엔진 fx3 배열 [plot, value, rgb, width]를 선으로 그린다.
// 브라우저에서는 전역 Fx3Layers, node:test에서는 globalThis.Fx3Layers.
"use strict";

const Fx3Layers = (() => {
  function parseFx3(arr) {
    if (!Array.isArray(arr)) return [];
    const out = [];
    for (const row of arr) {
      if (!Array.isArray(row) || row.length < 2) continue;
      const id = Number(row[0]);
      const value = Number(row[1]);
      if (!Number.isFinite(id)) continue;
      out.push({
        id,
        value,
        rgb: Number.isFinite(Number(row[2])) ? Number(row[2]) : 0x969696,
        width: Number.isFinite(Number(row[3])) ? Number(row[3]) : 1,
      });
    }
    return out;
  }

  function css(rgb) {
    const n = Number(rgb) >>> 0;
    return "#" + n.toString(16).padStart(6, "0").slice(-6);
  }

  // 단계화는 가격과 단위가 달라 아래 스케일. 나머지는 가격.
  const GROUPS = [
    { layer: "stage", ids: [1], scale: "fx3score" },
    { layer: "reg", ids: [7], scale: "right" },
    { layer: "past", ids: [21], scale: "right" },
    { layer: "state", ids: [30, 31], scale: "right" },
    { layer: "memory", ids: [32, 33, 34, 35, 56, 57], scale: "right" },
    { layer: "persist", ids: [42, 43, 44, 85, 86], scale: "right" },
    { layer: "market", ids: [51, 52, 53, 54, 55], scale: "right" },
    { layer: "swing", ids: [60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82], scale: "right" },
    { layer: "swing", ids: [70], scale: "fx3time" },
  ];

  // TL 목표선 5개. 색은 예측방향, 굵기는 1/3/1/2/3. 범위선은 신뢰도 0.4 미만이면 전부 회색.
  const RAY_STYLE = [
    { w: 1, up: "#ff9191", dn: "#91aaff", z: "#a0a0a0" },
    { w: 3, up: "#ff0000", dn: "#0000ff", z: "#828282" },
    { w: 1, up: "#cd3737", dn: "#3755cd", z: "#a0a0a0" },
    { w: 2, up: "#cd3737", dn: "#3755cd", z: "#a0a0a0" },
    { w: 3, up: "#cd3737", dn: "#3755cd", z: "#a0a0a0" },
  ];
  const RANGE_OK = ["#bed2be", "#a5a5a5", "#cdc3cd", "#cdc3cd", "#cdc3cd"];
  const RANGE_WEAK = "#b4b4b4";

  function rayLines(raw, r2, showRange) {
    if (!Array.isArray(raw) || raw.length < 6) return [];
    const weak = !(Number(r2) >= 0.4);
    const out = [];
    for (let k = 0; k < 5; k++) {
      const row = raw[k + 1];
      if (!Array.isArray(row)) continue;
      const px = Number(row[0]);
      const up = Number(row[1]);
      const dn = Number(row[2]);
      const dir = Number(row[3]);
      const st = RAY_STYLE[k];
      if (Number.isFinite(px)) {
        out.push({ v: px, c: dir > 0 ? st.up : dir < 0 ? st.dn : st.z, w: st.w });
      }
      if (showRange) {
        const c = weak ? RANGE_WEAK : RANGE_OK[k];
        if (Number.isFinite(up)) out.push({ v: up, c, w: 1 });
        if (Number.isFinite(dn)) out.push({ v: dn, c, w: 1 });
      }
    }
    return out;
  }

  function createRaysPrimitive() {
    let state = null;
    let chart = null;
    let series = null;
    let requestUpdate = () => {};
    const renderer = {
      draw(target) {
        const s = state;
        if (!s || !chart || !series) return;
        target.useMediaCoordinateSpace(({ context: ctx, mediaSize }) => {
          let x0 = chart.timeScale().timeToCoordinate(s.prevTime);
          if (x0 == null) x0 = chart.timeScale().timeToCoordinate(s.time);
          if (x0 == null) x0 = 0;
          const x1 = mediaSize.width;
          for (const ln of s.lines) {
            if (!Number.isFinite(ln.v)) continue;
            const y = series.priceToCoordinate(ln.v);
            if (y == null) continue;
            ctx.beginPath();
            ctx.strokeStyle = ln.c;
            ctx.lineWidth = ln.w;
            ctx.moveTo(x0, y);
            ctx.lineTo(x1, y);
            ctx.stroke();
          }
        });
      },
    };
    const paneView = { renderer: () => (state ? renderer : null) };
    return {
      attached(param) {
        chart = param.chart;
        series = param.series;
        requestUpdate = param.requestUpdate || (() => {});
      },
      detached() {
        chart = null;
        series = null;
        requestUpdate = () => {};
      },
      paneViews: () => [paneView],
      set(next) {
        state = next;
        requestUpdate();
      },
      clear() {
        if (state !== null) {
          state = null;
          requestUpdate();
        }
      },
    };
  }

  const PLUS_IDS = new Set([60, 61, 65, 66]);
  const HIDDEN_IDS = new Set([62, 63, 64, 67, 68, 69]);

  function kindOf(id) {
    if (id === 7) return "reg";
    if (PLUS_IDS.has(id)) return "plus";
    return "flat";
  }

  const Fx3Renderer = {
    id: "fx_mirae_v3",
    createHandle(chart, candleSeries) {
      const layers = {
        stage: true, reg: true, past: true, state: true, memory: true, persist: true,
        market: true, swing: true, rays: true, rayBand: true,
      };
      const rays = createRaysPrimitive();
      if (candleSeries && typeof candleSeries.attachPrimitive === "function") {
        candleSeries.attachPrimitive(rays);
      }
      let raySnap = null;
      function prevTime(seq, t) {
        if (!Array.isArray(seq)) return t - 60;
        const i = seq.indexOf(t);
        if (i > 0) return seq[i - 1];
        return Number.isFinite(t) ? t - 60 : t;
      }
      function syncRays() {
        const snap = raySnap;
        if (!layers.rays || !snap || !Array.isArray(snap.raw) || snap.raw.length < 6) {
          rays.clear();
          return;
        }
        rays.set({
          time: snap.time,
          prevTime: prevTime(snap.seq, snap.time),
          lines: rayLines(snap.raw, snap.r2, layers.rayBand),
        });
      }
      const lines = [];
      for (const g of GROUPS) {
        for (const id of g.ids) {
          if (HIDDEN_IDS.has(id)) continue;
          const kind = kindOf(id);
          const series = chart.addLineSeries({
            color: "#d7dde8",
            lineWidth: 2,
            priceScaleId: g.scale,
            priceLineVisible: false,
            lastValueVisible: false,
            lineVisible: false,
          });
          const pen = kind === "reg" ? globalThis.HorizLines.segments()
            : kind === "plus" ? globalThis.HorizLines.plus("#ff7f00")
            : globalThis.HorizLines.flat();
          series.attachPrimitive(pen);
          lines.push({ layer: g.layer, id, kind, series, pen });
        }
      }
      if (typeof chart.priceScale === "function") {
        chart.priceScale("fx3score").applyOptions({ scaleMargins: { top: 0.84, bottom: 0.02 } });
        chart.priceScale("fx3time").applyOptions({ scaleMargins: { top: 0.92, bottom: 0 } });
      }
      let ctx = null;
      let synced = false;

      function point(ind, id) {
        const row = ind && Array.isArray(ind.fx3) ? ind.fx3.find((r) => r.id === id) : undefined;
        return row && Number.isFinite(row.value) ? row : null;
      }

      function rebuild() {
        for (const ln of lines) {
          const pts = [];
          const data = [];
          if (layers[ln.layer] && ctx) {
            for (const t of ctx.barSeq) {
              const row = point(ctx.barInd.get(t), ln.id);
              if (!row || !(row.width > 0)) {
                data.push({ time: t });
                continue;
              }
              pts.push({
                time: t,
                value: row.value,
                color: css(row.rgb),
                width: row.width,
              });
              data.push({ time: t, value: row.value });
            }
          }
          ln.pts = pts;
          ln.pen.setPoints(pts);
          ln.series.setData(data);
          const last = pts.at(-1);
          if (last) ln.series.applyOptions({ color: last.color });
        }
        synced = true;
      }

      function liveTail(t) {
        const ind = ctx.barInd.get(t);
        for (const ln of lines) {
          if (!layers[ln.layer]) continue;
          const row = point(ind, ln.id);
          const on = !!(row && row.width > 0 && Number.isFinite(row.value));
          const pts = ln.pts || (ln.pts = []);
          const last = pts[pts.length - 1];
          if (last && last.time > t) { synced = false; rebuild(); return; }
          if (on) {
            const item = { time: t, value: row.value, color: css(row.rgb), width: row.width };
            if (last && last.time === t) pts[pts.length - 1] = item;
            else pts.push(item);
            ln.series.update({ time: t, value: row.value });
            ln.series.applyOptions({ color: item.color });
          } else {
            if (last && last.time === t) pts.pop();
            ln.series.update({ time: t });
          }
          ln.pen.setPoints(pts);
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
          if (changed) {
            rebuild();
            syncRays();
          }
        },
        applyLive(p, c) {
          ctx = c;
          const t = Number(p.bar_open_time) / 1e6;
          raySnap = { raw: p.rays, r2: p.reg_r2, time: t, seq: c && c.barSeq };
          syncRays();
          if (!Number.isFinite(t) || t <= 0) return;
          if (!synced) { rebuild(); return; }
          liveTail(t);
        },
        applySeed(c) {
          ctx = c;
          rebuild();
        },
        clear() {
          synced = false;
          for (const ln of lines) {
            ln.pts = [];
            ln.pen.setPoints([]);
            ln.series.setData([]);
          }
          rays.clear();
        },
        destroy() {
          rays.clear();
          if (candleSeries && typeof candleSeries.detachPrimitive === "function") {
            candleSeries.detachPrimitive(rays);
          }
          for (const ln of lines) chart.removeSeries(ln.series);
        },
      };
    },
  };

  return { parseFx3, rayLines, createRaysPrimitive, Fx3Renderer };
})();

if (typeof globalThis !== "undefined") globalThis.Fx3Layers = Fx3Layers;
