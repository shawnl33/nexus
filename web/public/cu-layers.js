// #우드스탁_미래곡선_해외선물.
// 회귀선은 미래곡선 V1처럼 이전 점과 이번 점을 잇고, 색·굵기는 이번 봉 것이다.
// 마켓중심은 V1처럼 단계 색이 같은 동안만 가로선이고, 색이 바뀌면 끊긴다.
// 목표가·합성선은 같은 값인 동안만 가로다.
// 지난구간은 V1처럼 최고·최저 십자만 그린다. 0.382/0.5/0.618 실선은 그리지 않는다.
// 위쪽 초록 십자 위에 자홍선이 있으면 그 사이에 빨간 세로선,
// 아래쪽 오렌지 십자 아래에 청록선이 있으면 그 사이에 파란 세로선을 봉마다 긋는다.
"use strict";

const CuLayers = (() => {
  function parse(arr) {
    if (!Array.isArray(arr)) return [];
    const out = [];
    for (const row of arr) {
      if (!Array.isArray(row) || row.length < 2) continue;
      const id = Number(row[0]);
      const value = Number(row[1]);
      if (!Number.isFinite(id) || !Number.isFinite(value)) continue;
      out.push({
        id, value,
        rgb: Number.isFinite(Number(row[2])) ? Number(row[2]) : 0x969696,
        width: Number.isFinite(Number(row[3])) ? Number(row[3]) : 1,
      });
    }
    return out;
  }

  function css(rgb) {
    return "#" + (Number(rgb) >>> 0).toString(16).padStart(6, "0").slice(-6);
  }

  const GROUPS = [
    { layer: "score", ids: [1], scale: "right" },
    { layer: "reg", ids: [2, 3], scale: "right" },
    { layer: "target", ids: [4, 5, 6, 7, 8], scale: "right" },
    { layer: "swing", ids: [9, 10, 14, 15, 27, 28], scale: "right" },
    { layer: "synth", ids: [19, 20, 21, 22, 23, 24], scale: "right" },
    { layer: "entry", ids: [25, 26], scale: "right" },
  ];
  const STAGE = [
    { id: "p4", rgb: 0xdc0000, color: "#dc0000" },
    { id: "p2", rgb: 0xff6446, color: "#ff6446" },
    { id: "p1", rgb: 0xffb9b9, color: "#ffb9b9" },
    { id: "n4", rgb: 0x0000b4, color: "#0000b4" },
    { id: "n2", rgb: 0x3c82ff, color: "#3c82ff" },
    { id: "n1", rgb: 0xb4d2ff, color: "#b4d2ff" },
    { id: "z", rgb: 0x969696, color: "#969696" },
  ];
  function stageId(rgb) {
    const n = Number(rgb) >>> 0;
    const hit = STAGE.find((s) => s.rgb === n);
    return hit ? hit.id : "z";
  }
  const PLUS_IDS = new Set([9, 10, 14, 15]);
  const DOT_IDS = new Set([25, 26]);
  const RAIL_HI = 14;
  const RAIL_MAG = 27;
  const RAIL_LO = 10;
  const RAIL_CYAN = 28;

  // 위 초록 십자(지난하락 최고)와 그 위 자홍선, 아래 오렌지 십자(지난상승 최저)와 그 아래 청록선.
  // 조건이 맞은 봉마다 두 가격 사이에 세로선 하나.
  function rails() {
    let series = null;
    let chart = null;
    let points = [];
    return {
      setPoints(next) { points = next || []; },
      attached(param) {
        series = param.series;
        chart = param.chart;
      },
      detached() {
        series = null;
        chart = null;
      },
      updateAllViews() {},
      paneViews() {
        return [{
          renderer() {
            return {
              draw(target) {
                if (!series || !chart || !points.length) return;
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const xOf = (t) => chart.timeScale().timeToCoordinate(t);
                  const yOf = (v) => series.priceToCoordinate(v);
                  ctx.lineCap = "butt";
                  ctx.lineWidth = Math.max(1, hr);
                  function column(key0, key1, color) {
                    ctx.beginPath();
                    ctx.strokeStyle = color;
                    let any = false;
                    for (const p of points) {
                      if (!Number.isFinite(p[key0]) || !Number.isFinite(p[key1])) continue;
                      const x = xOf(p.time);
                      const y0 = yOf(p[key0]);
                      const y1 = yOf(p[key1]);
                      if (x == null || y0 == null || y1 == null) continue;
                      ctx.moveTo(x * hr, y0 * vr);
                      ctx.lineTo(x * hr, y1 * vr);
                      any = true;
                    }
                    if (any) ctx.stroke();
                  }
                  column("hi", "mag", "#ff0000");
                  column("lo", "cya", "#0000ff");
                });
              },
            };
          },
        }];
      },
    };
  }

  function createHandle(chart, groups) {
    const use = groups || GROUPS;
    const layers = {};
    for (const g of use) layers[g.layer] = true;
    const lines = [];
    for (const g of use) {
      for (const id of g.ids) {
        if (id === 3) {
          const series = {};
          for (const s of STAGE) {
            series[s.id] = chart.addLineSeries({
              color: s.color,
              lineWidth: 3,
              priceScaleId: g.scale,
              priceLineVisible: false,
              lastValueVisible: false,
              lineVisible: false,
            });
            series[s.id].attachPrimitive(globalThis.HorizLines.primitive(s.color, 3));
          }
          lines.push({ layer: g.layer, id, stage: true, series });
          continue;
        }
        const series = chart.addLineSeries({
          color: "#d7dde8",
          lineWidth: 2,
          priceScaleId: g.scale,
          priceLineVisible: false,
          lastValueVisible: false,
          lineVisible: false,
          autoscaleInfoProvider: id === 1 ? () => null : undefined,
        });
        const pens = [];
        if (DOT_IDS.has(id)) pens.push(globalThis.HorizLines.dots(4));
        else if (id === 29 || id === 30) pens.push(globalThis.HorizLines.dots(1));
        else if (id === 2) pens.push(globalThis.HorizLines.segments());
        else if (PLUS_IDS.has(id)) pens.push(globalThis.HorizLines.plus(id <= 10 ? "#ff7f00" : "#008000", true));
        else pens.push(globalThis.HorizLines.levels());
        for (const pen of pens) series.attachPrimitive(pen);
        lines.push({ layer: g.layer, id, series, pens });
      }
    }
    const railHost = lines.find((ln) => ln.id === RAIL_CYAN && !ln.stage);
    const railsPen = railHost
      && lines.some((ln) => ln.id === RAIL_HI)
      && lines.some((ln) => ln.id === RAIL_LO)
      && lines.some((ln) => ln.id === RAIL_MAG)
      ? rails() : null;
    if (railsPen) railHost.series.attachPrimitive(railsPen);
    let ctx = null;
    let synced = false;

    function point(ind, id) {
      const row = ind && Array.isArray(ind.cu) ? ind.cu.find((r) => r.id === id) : undefined;
      return row && row.width > 0 && Number.isFinite(row.value) ? row : null;
    }

    function rebuild() {
      for (const ln of lines) {
        if (ln.stage) {
          const buckets = Object.fromEntries(STAGE.map((s) => [s.id, []]));
          if (layers[ln.layer] && ctx) {
            let open = null;
            for (const t of ctx.barSeq) {
              const row = point(ctx.barInd.get(t), ln.id);
              if (!row) {
                if (open) buckets[open].push({ time: t });
                open = null;
                continue;
              }
              const sid = stageId(row.rgb);
              if (open && open !== sid) buckets[open].push({ time: t });
              buckets[sid].push({ time: t, value: row.value });
              open = sid;
            }
          }
          for (const s of STAGE) ln.series[s.id].setData(buckets[s.id]);
          continue;
        }
        const pts = [];
        const data = [];
        if (layers[ln.layer] && ctx) {
          for (const t of ctx.barSeq) {
            const row = point(ctx.barInd.get(t), ln.id);
            if (!row) {
              data.push({ time: t });
              continue;
            }
            const item = { time: t, value: row.value, color: css(row.rgb), width: row.width };
            pts.push(item);
            data.push({ time: t, value: row.value });
          }
        }
        ln.pts = pts;
        for (const pen of ln.pens) pen.setPoints(pts);
        ln.series.setData(data);
      }
      if (railsPen) railsPen.setPoints(railPoints());
      synced = true;
    }

    function railMap(id) {
      const ln = lines.find((row) => row.id === id && !row.stage);
      const map = new Map();
      if (!ln || !ln.pts) return map;
      for (const p of ln.pts) if (Number.isFinite(p.value)) map.set(p.time, p.value);
      return map;
    }

    function railPoints() {
      if (!railsPen || !ctx || layers.swing === false) return [];
      const hiM = railMap(RAIL_HI);
      const magM = railMap(RAIL_MAG);
      const loM = railMap(RAIL_LO);
      const cyaM = railMap(RAIL_CYAN);
      const out = [];
      for (const t of ctx.barSeq) {
        const hi = hiM.has(t) ? hiM.get(t) : null;
        const mag = magM.has(t) ? magM.get(t) : null;
        const lo = loM.has(t) ? loM.get(t) : null;
        const cya = cyaM.has(t) ? cyaM.get(t) : null;
        const item = { time: t };
        let on = false;
        if (hi != null && mag != null && mag > hi) {
          item.hi = hi;
          item.mag = mag;
          on = true;
        }
        if (lo != null && cya != null && cya < lo) {
          item.lo = lo;
          item.cya = cya;
          on = true;
        }
        if (on) out.push(item);
      }
      return out;
    }

    function liveTail(t) {
      const ind = ctx.barInd.get(t);
      for (const ln of lines) {
        if (!layers[ln.layer]) continue;
        if (ln.stage) {
          const row = point(ind, ln.id);
          const sid = row ? stageId(row.rgb) : null;
          for (const s of STAGE) {
            const series = ln.series[s.id];
            const raw = typeof series.data === "function" ? series.data() : [];
            const prev = Array.isArray(raw) && raw.length ? raw[raw.length - 1] : null;
            if (s.id === sid) series.update({ time: t, value: row.value });
            else if (prev && prev.time === t && Number.isFinite(prev.value)) series.update({ time: t });
          }
          continue;
        }
        const row = point(ind, ln.id);
        const pts = ln.pts || (ln.pts = []);
        const last = pts[pts.length - 1];
        if (last && last.time > t) { synced = false; rebuild(); return; }
        if (row) {
          const item = { time: t, value: row.value, color: css(row.rgb), width: row.width };
          if (last && last.time === t) pts[pts.length - 1] = item;
          else pts.push(item);
          ln.series.update({ time: t, value: row.value });
        } else {
          if (last && last.time === t) pts.pop();
          ln.series.update({ time: t });
        }
        for (const pen of ln.pens) pen.setPoints(pts);
      }
      if (railsPen) railsPen.setPoints(railPoints());
    }

    return {
      setLayers(map) {
        for (const k of Object.keys(layers)) if (k in map) layers[k] = !!map[k];
        rebuild();
      },
      applySeed(c) { ctx = c; rebuild(); },
      applyLive(p, c) {
        ctx = c || ctx;
        const t = Number(p.bar_open_time) / 1e6;
        if (!Number.isFinite(t) || t <= 0) return;
        if (!synced) { rebuild(); return; }
        liveTail(t);
      },
      clear() {
        synced = false;
        for (const ln of lines) {
          ln.pts = [];
          if (ln.stage) {
            for (const s of STAGE) ln.series[s.id].setData([]);
            continue;
          }
          for (const pen of ln.pens) pen.setPoints([]);
          ln.series.setData([]);
        }
        if (railsPen) railsPen.setPoints([]);
      },
      destroy() {
        for (const ln of lines) {
          if (ln.stage) {
            for (const s of STAGE) chart.removeSeries(ln.series[s.id]);
          } else chart.removeSeries(ln.series);
        }
      },
    };
  }

  function brkHandle(chart) {
    return createHandle(chart, [
      { layer: "mark", ids: [29, 30], scale: "right" },
      { layer: "line", ids: [31, 32], scale: "right" },
    ]);
  }

  return { parse, createHandle, brkHandle };
})();

if (typeof globalThis !== "undefined") globalThis.CuLayers = CuLayers;
