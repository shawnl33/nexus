// 스나이퍼 Data2. 참조 종목 1분봉에서 이미 계산된 삼선·회귀·마켓·가격 비율을
// 이 차트 시각에 겹친다. 비율 30 미만만 그리는 선은 원본 굵기 0(숨김)과 같다.
"use strict";

const Data2Layers = (() => {
  // 차트가 ES면 NQ, NQ면 ES. 월물 코드는 그대로 둔다.
  const PAIR = { ES: "NQ", NQ: "ES" };
  function defaultCode(symbol) {
    const m = String(symbol || "").toUpperCase().match(/^([A-Z]{1,4})([FGHJKMNQUVXZ])(\d{1,2})$/);
    if (!m) return "";
    const root = PAIR[m[1]];
    return root ? root + m[2] + m[3] : "";
  }

  function css(rgb) {
    const n = Number(rgb);
    if (!Number.isFinite(n)) return "#808080";
    return "#" + (n >>> 0).toString(16).padStart(6, "0").slice(-6);
  }

  // 한 봉의 표시 값. 없으면 null (그 시각은 선을 끊는다).
  function scopeFrom(ind) {
    const pg = ind?.pgap?.[0];
    const sn = ind?.sniper || null;
    const rg = ind?.rgap || null;
    const mg = ind?.mgap || null;
    const ym = ind?.ymae || null;
    const samOn = !!(pg && pg.ready && Number.isFinite(pg.ratio));
    const sam = samOn ? pg.ratio : null;
    const reg = rg && rg.ready && Number.isFinite(rg.ratio) && rg.ratio < 30 ? rg.ratio : null;
    const market = mg && mg.ready && Number.isFinite(mg.ratio) && mg.ratio < 30 ? mg.ratio : null;
    const price = samOn && sn && Number.isFinite(sn.priceRatio) && ym &&
      (ym.twoHi !== 0 || ym.twoLo !== 0) && sn.priceRatio <= 50
      ? sn.priceRatio : null;
    const reset = !!(sn && sn.reset === 1);
    let posHi = null;
    let posLo = null;
    if (samOn && ym && ym.prevValid) {
      if (ym.pos === -1) posHi = 103;
      else if (ym.pos === 1) posLo = -3;
    }
    let upSig = 0;
    let dnSig = 0;
    if (samOn && ym && ym.prevValid) {
      if (ym.pos === -1) upSig = 1;
      else if (ym.pos === 1) dnSig = 1;
    }
    if (!reset && ym && ym.h1 === 1) dnSig = 1;
    if (!reset && ym && ym.h1 === -1) upSig = 1;
    if (!reset && sn && sn.compound === 1) dnSig = 1;
    if (!reset && sn && sn.pxExit === 1) dnSig = 1;
    if (!reset && sn && sn.pxExit === -1) upSig = 1;
    if (samOn && sam >= 30 && sn) {
      if (sn.below === 0) dnSig = 1;
      else if (sn.above === 0) upSig = 1;
    }
    let signal = null;
    if (samOn && sam < 30 && (upSig || dnSig)) {
      signal = upSig && dnSig ? "both" : upSig ? "up" : "down";
    }
    let emphasis = null;
    if (samOn && sn && sn.ratio > 0) {
      const rgb = Number(sn.rgb) >>> 0;
      if (rgb === 0xffa0a0 || rgb === 0xff0000 || rgb === 0xb40000) emphasis = "yellow";
      else if (rgb === 0x80a0ff || rgb === 0x0000ff || rgb === 0x000096) emphasis = "cyan";
      else if (rgb === 0x008000) emphasis = "white";
    }
    return {
      sam, reg, market, price, posHi, posLo,
      up: !reset && ym && ym.first === 1 ? -6 : null,
      down: !reset && ym && ym.first === -1 ? 106 : null,
      compound: !reset && sn && sn.compound === 1 ? -9 : null,
      squeezeUp: !reset && sn && sn.pxExit === 1 ? -12 : null,
      squeezeDn: !reset && sn && sn.pxExit === -1 ? 109 : null,
      holdUp: samOn && sam >= 30 && sn && sn.below === 0 ? -15 : null,
      holdDn: samOn && sam >= 30 && sn && sn.below !== 0 && sn.above === 0 ? 112 : null,
      signal, emphasis,
      color: sn ? sn.rgb : 0xdcdcdc,
      samWidth: sam == null ? 0 : (sam <= 30 ? 8 : 5) + (Number.isFinite(sn?.ratio) ? sn.ratio : 0),
    };
  }

  const MARKS = [
    { key: "posHi", color: "#0000ff" },
    { key: "posLo", color: "#ff0000" },
    { key: "up", color: "#b40000", arrow: "up" },
    { key: "down", color: "#87ceeb", arrow: "down" },
    { key: "compound", color: "#ff8c00" },
    { key: "squeezeUp", color: "#ff00ff" },
    { key: "squeezeDn", color: "#00a0a0" },
    { key: "holdUp", color: "#ff0000" },
    { key: "holdDn", color: "#0000ff" },
  ];
  const DOTS = [
    { key: "signal", match: "up", color: "#0000ff", radius: 4 },
    { key: "signal", match: "down", color: "#ff0000", radius: 4 },
    { key: "signal", match: "both", color: "#008000", radius: 4 },
    { key: "emphasis", match: "yellow", color: "#ffff00", radius: 2 },
    { key: "emphasis", match: "cyan", color: "#00ffff", radius: 2 },
    { key: "emphasis", match: "white", color: "#ffffff", radius: 2 },
  ];

  const Data2Renderer = {
    id: "fx_data2",
    createHandle(chart) {
      const layers = { three: true, reg: true, market: true, price: true, marks: true };
      const specs = [
        { layer: "three", key: "sam", color: "#d7dde8", width: 2 },
        { layer: "reg", key: "reg", color: "#c9a227", width: 2 },
        { layer: "market", key: "market", color: "#8ecae6", width: 2 },
        { layer: "price", key: "price", color: "#808080", width: 1 },
      ];
      const lines = specs.map((d) => ({
        ...d,
        series: chart.addLineSeries({
          color: d.color,
          lineWidth: d.width,
          priceScaleId: "data2",
          priceLineVisible: false,
          lastValueVisible: false,
          lineStyle: 0,
          lineType: 0,
          lineVisible: false,
          pointMarkersVisible: false,
        }),
      }));
      for (const ln of lines) {
        const pen = ln.key === "sam"
          ? globalThis.HorizLines.dots()
          : globalThis.HorizLines.primitive(ln.color, ln.width);
        ln.series.attachPrimitive(pen);
        ln.pen = pen;
      }
      const marks = MARKS.map((d) => ({
        ...d,
        series: chart.addLineSeries({
          color: d.color,
          lineWidth: 1,
          priceScaleId: "data2",
          priceLineVisible: false,
          lastValueVisible: false,
          lineVisible: false,
        }),
      }));
      for (const mk of marks) {
        mk.series.attachPrimitive(globalThis.HorizLines.primitive(mk.color, 1));
        if (mk.arrow) mk.series.attachPrimitive(globalThis.HorizLines.arrows(mk.arrow, mk.color, true));
      }
      const dots = DOTS.map((d) => ({
        ...d,
        series: chart.addLineSeries({
          color: d.color,
          lineWidth: 1,
          priceScaleId: "data2",
          priceLineVisible: false,
          lastValueVisible: false,
          lineVisible: false,
          pointMarkersVisible: true,
          pointMarkersRadius: d.radius,
        }),
      }));
      const all = [...lines, ...marks, ...dots];
      function setAxis(id) {
        const scale = id || "data2";
        for (const s of all) {
          if (!s.series.applyOptions) continue;
          s.series.applyOptions({
            priceScaleId: scale,
            // 값이 한곳에 몰려도 0~100 눈금을 유지한다. 52가 칸 한가운데로 확대되지 않게.
            autoscaleInfoProvider: (base) => {
              const res = base();
              const minValue = Math.min(res?.priceRange?.minValue ?? 0, -20);
              const maxValue = Math.max(res?.priceRange?.maxValue ?? 100, 120);
              return { priceRange: { minValue, maxValue } };
            },
          });
        }
        if (typeof chart.priceScale === "function") {
          chart.priceScale(scale).applyOptions({
            scaleMargins: { top: 0.08, bottom: 0.08 },
            autoScale: true,
          });
        }
      }
      setAxis("data2");
      let cache = null;
      let synced = false;
      function rowAt(t) {
        return scopeFrom(cache?.barInd?.get(t));
      }
      function rebuild() {
        const seq = cache?.barSeq ?? [];
        for (const ln of lines) {
          if (ln.key === "sam") {
            const pts = [];
            const data = [];
            if (layers.three) {
              for (const t of seq) {
                const row = rowAt(t);
                if (!row || row.sam == null) {
                  data.push({ time: t });
                  continue;
                }
                pts.push({
                  time: t,
                  value: row.sam,
                  color: css(row.color),
                  width: row.samWidth,
                });
                data.push({ time: t, value: row.sam });
              }
            }
            ln.pts = pts;
            ln.pen.setPoints(pts);
            ln.series.setData(data);
            continue;
          }
          const data = [];
          if (layers[ln.layer]) {
            for (const t of seq) {
              const row = rowAt(t);
              const x = row ? row[ln.key] : null;
              data.push(x != null ? { time: t, value: x } : { time: t });
            }
          }
          ln.series.setData(data);
        }
        for (const mk of marks) {
          const data = [];
          if (layers.marks) {
            for (const t of seq) {
              const row = rowAt(t);
              const x = row ? row[mk.key] : null;
              data.push(x != null ? { time: t, value: x } : { time: t });
            }
          }
          mk.series.setData(data);
        }
        for (const dot of dots) {
          const data = [];
          if (layers.three) {
            for (const t of seq) {
              const row = rowAt(t);
              if (row && row[dot.key] === dot.match && row.sam != null) {
                data.push({ time: t, value: row.sam });
              }
            }
          }
          dot.series.setData(data);
        }
        synced = true;
      }
      function lastOf(series) {
        const raw = typeof series.data === "function" ? series.data() : series.data;
        return Array.isArray(raw) && raw.length ? raw[raw.length - 1] : null;
      }
      function liveTail() {
        const seq = cache?.barSeq;
        const t = seq && seq.length ? seq[seq.length - 1] : NaN;
        if (!Number.isFinite(t)) return;
        const row = rowAt(t);
        for (const ln of lines) {
          if (ln.key === "sam") {
            const pts = ln.pts || (ln.pts = []);
            const last = pts[pts.length - 1];
            if (last && last.time > t) { synced = false; rebuild(); return; }
            if (layers.three && row && row.sam != null) {
              const p = { time: t, value: row.sam, color: css(row.color), width: row.samWidth };
              if (last && last.time === t) pts[pts.length - 1] = p;
              else pts.push(p);
              ln.series.update({ time: t, value: row.sam });
            } else {
              if (last && last.time === t) pts.pop();
              ln.series.update({ time: t });
            }
            ln.pen.setPoints(pts);
            continue;
          }
          if (!layers[ln.layer]) continue;
          const x = row ? row[ln.key] : null;
          ln.series.update(x != null ? { time: t, value: x } : { time: t });
        }
        if (layers.marks) {
          for (const mk of marks) {
            const x = row ? row[mk.key] : null;
            mk.series.update(x != null ? { time: t, value: x } : { time: t });
          }
        }
        if (layers.three) {
          for (const dot of dots) {
            const on = row && row[dot.key] === dot.match && row.sam != null;
            const last = lastOf(dot.series);
            if (on) dot.series.update({ time: t, value: row.sam });
            else if (last && last.time === t && Number.isFinite(last.value)) dot.series.update({ time: t });
          }
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
        setSource(c) { cache = c || null; rebuild(); },
        applyLive(_p, c) {
          if (c) cache = c;
          if (!synced) { rebuild(); return; }
          liveTail();
        },
        applySeed(c) { if (c) cache = c; rebuild(); },
        clear() {
          cache = null;
          synced = false;
          for (const ln of lines) {
            ln.pts = [];
            ln.series.setData([]);
          }
          for (const mk of marks) mk.series.setData([]);
          for (const dot of dots) dot.series.setData([]);
        },
        destroy() {
          for (const ln of lines) chart.removeSeries(ln.series);
          for (const mk of marks) chart.removeSeries(mk.series);
          for (const dot of dots) chart.removeSeries(dot.series);
        },
      };
    },
  };

  return { scopeFrom, css, defaultCode, Data2Renderer };
})();

if (typeof globalThis !== "undefined") globalThis.Data2Layers = Data2Layers;
