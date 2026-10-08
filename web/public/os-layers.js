// 1분통합판정과 매물대압축. 값은 가격이 아니라 하단 눈금이다.
"use strict";

const OsLayers = (() => {
  function num(v) {
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  }

  function css(rgb) {
    return "#" + (Number(rgb) >>> 0).toString(16).padStart(6, "0").slice(-6);
  }

  function sideRgb(dir) {
    if (dir > 0) return "#ff5a5a";
    if (dir < 0) return "#5a82ff";
    return "#969696";
  }

  // 스냅샷·라이브 공통. 없으면 null.
  function parse(row) {
    if (!Array.isArray(row) || row.length < 23) return null;
    return {
      judge: num(row[0]), judgeRgb: Number(row[1]) || 0x969696,
      fut: num(row[2]), prof: num(row[3]), di: num(row[4]), adx: num(row[5]),
      sqOn: Number(row[6]) === 1, sqLen: num(row[7]), sqRgb: Number(row[8]) || 0x787878, sqW: Number(row[9]) || 6,
      hold: num(row[10]), holdRgb: Number(row[11]) || 0xdcdcdc,
      ratioOn: Number(row[12]) === 1, ratio: num(row[13]), ratioRgb: Number(row[14]) || 0x969696,
      relOn: Number(row[15]) === 1, relLen: num(row[16]), relRgb: Number(row[17]) || 0x787878, relW: Number(row[18]) || 3,
      cfOn: Number(row[19]) === 1, cfLen: num(row[20]), cfRgb: Number(row[21]) || 0x787878, cfW: Number(row[22]) || 3,
      entOn: row.length >= 27 && Number(row[23]) === 1,
      entY: row.length >= 27 ? num(row[24]) : null,
      entRgb: row.length >= 27 ? (Number(row[25]) || 0xffd7d7) : 0xffd7d7,
      entW: row.length >= 27 ? (Number(row[26]) || 2) : 2,
    };
  }

  function series(chart, scale, color, width) {
    return chart.addLineSeries({
      color, lineWidth: width || 1, priceScaleId: scale,
      priceLineVisible: false, lastValueVisible: false,
    });
  }

  function histogram(chart, scale, color) {
    return chart.addHistogramSeries({
      color, base: 0, priceScaleId: scale,
      priceLineVisible: false, lastValueVisible: false,
    });
  }

  function dots(chart, scale, color, radius) {
    return chart.addLineSeries({
      color, lineWidth: 1, priceScaleId: scale,
      priceLineVisible: false, lastValueVisible: false,
      lineVisible: false, pointMarkersVisible: true, pointMarkersRadius: radius,
    });
  }

  function paint(lines, ctx, pick) {
    for (const ln of lines) {
      if (ln.stage) {
        paintStage(ln, ctx, pick);
        continue;
      }
      const data = [];
      const marks = [];
      if (ln.on() && ctx) {
        for (const t of ctx.barSeq) {
          const row = pick(ctx.barInd.get(t));
          const value = row ? ln.value(row) : null;
          const point = value != null ? { time: t, value } : { time: t };
          if (value != null && ln.color) point.color = ln.color(row);
          data.push(point);
          if (ln.pen && value != null) {
            marks.push({ time: t, value, color: point.color, width: ln.width ? ln.width(row) : 4 });
          }
        }
      }
      ln.series.setData(data);
      if (ln.pen) ln.pen.setPoints(marks);
    }
  }

  function paintStage(ln, ctx, pick) {
    const buckets = Object.fromEntries(ln.colors.map((c) => [c.id, []]));
    if (ln.on() && ctx) {
      let open = null;
      for (const t of ctx.barSeq) {
        const row = pick(ctx.barInd.get(t));
        const value = row ? ln.value(row) : null;
        if (value == null) {
          if (open) buckets[open].push({ time: t });
          open = null;
          continue;
        }
        const sid = ln.colorId(row);
        if (open && open !== sid) buckets[open].push({ time: t });
        buckets[sid].push({ time: t, value });
        open = sid;
      }
    }
    for (const c of ln.colors) ln.series[c.id].setData(buckets[c.id]);
  }

  function live(lines, p, ctx, pick) {
    const t = Number(p.bar_open_time) / 1e6;
    const row = pick(ctx && ctx.barInd.get(t));
    for (const ln of lines) {
      if (ln.stage) {
        paintStage(ln, ctx, pick);
        continue;
      }
      if (!ln.on()) continue;
      const value = row ? ln.value(row) : null;
      if (value == null) ln.series.update({ time: t });
      else ln.series.update({ time: t, value, color: ln.color ? ln.color(row) : undefined });
      if (ln.pen && ctx) {
        const marks = [];
        for (const bt of ctx.barSeq) {
          const brow = pick(ctx.barInd.get(bt));
          const bv = brow && ln.on() ? ln.value(brow) : null;
          if (bv != null) marks.push({ time: bt, value: bv, color: ln.color(brow), width: ln.width ? ln.width(brow) : 4 });
        }
        ln.pen.setPoints(marks);
      }
    }
  }

  function judgeHandle(chart) {
    const scale = "fxjudge";
    const layers = { state: true, fut: true, prof: true, di: true, adx: true, entry: true, release: true };
    // 통합상태는 0선에서 세운 막대. 미래·매물은 각 높이의 얇은 가로선.
    // DI방향은 ±20의 속 빈 테두리 원. ADX는 ±10 가로선.
    function levelLine(color) {
      const s = series(chart, scale, color, 1);
      s.applyOptions({ lineVisible: false });
      s.attachPrimitive(globalThis.HorizLines.primitive(color, 1));
      return s;
    }
    const lines = [
      { layer: "state", series: histogram(chart, scale, "#969696"),
        on: () => layers.state, value: (r) => r.judge, color: (r) => css(r.judgeRgb) },
      { layer: "fut", series: levelLine("#ff5a5a"),
        on: () => layers.fut, value: (r) => (r.fut ? r.fut * 80 : null), color: (r) => sideRgb(r.fut) },
      { layer: "prof", series: levelLine("#5a82ff"),
        on: () => layers.prof, value: (r) => (r.prof ? r.prof * 40 : null), color: (r) => sideRgb(r.prof) },
      { layer: "di", series: (() => {
          const s = series(chart, scale, "#5a82ff", 1);
          s.applyOptions({ lineVisible: false });
          return s;
        })(),
        on: () => layers.di, value: (r) => (r.di ? r.di * 20 : null), color: (r) => sideRgb(r.di), width: () => 8 },
      { layer: "adx", series: levelLine("#787878"),
        on: () => layers.adx, value: (r) => (r.adx ? r.adx * 10 : null), color: () => "#787878" },
      { layer: "entry", series: (() => {
          const s = series(chart, scale, "#ffd7d7", 1);
          s.applyOptions({ lineVisible: false });
          return s;
        })(),
        on: () => layers.entry, value: (r) => (r.entOn ? r.entY : null), color: (r) => css(r.entRgb), width: (r) => r.entW },
      { layer: "release", series: dots(chart, scale, "#dc0000", 4),
        on: () => layers.release, value: (r) => (r.sqOn ? 0 : null), color: (r) => css(r.sqRgb) },
    ];
    const entryLn = lines.find((ln) => ln.layer === "entry");
    entryLn.pen = globalThis.HorizLines.dots(1);
    entryLn.series.attachPrimitive(entryLn.pen);
    const diLn = lines.find((ln) => ln.layer === "di");
    diLn.pen = globalThis.HorizLines.rings(1);
    diLn.series.attachPrimitive(diLn.pen);
    const all = lines.map((ln) => ln.series);
    function setAxis(id) {
      const own = id === "right";
      const next = own ? "right" : scale;
      for (const s of all) {
        if (!s.applyOptions) continue;
        s.applyOptions({
          priceScaleId: next,
          autoscaleInfoProvider: () => ({ priceRange: { minValue: -120, maxValue: 120 } }),
        });
      }
      if (typeof chart.priceScale === "function") {
        chart.priceScale(next).applyOptions(own
          ? { scaleMargins: { top: 0.08, bottom: 0.08 }, autoScale: true, visible: true }
          : { scaleMargins: { top: 0.72, bottom: 0 }, autoScale: true });
      }
    }
    setAxis(null);
    let ctx = null;
    const pick = (ind) => ind && ind.os;
    return {
      setAxis,
      setLayers(map) {
        if (map && "detail" in map) {
          const on = !!map.detail;
          layers.fut = layers.prof = layers.di = layers.adx = on;
        }
        for (const k of Object.keys(layers)) if (map && k in map) layers[k] = !!map[k];
        paint(lines, ctx, pick);
      },
      applySeed(c) { ctx = c; paint(lines, ctx, pick); },
      applyLive(p, c) { ctx = c || ctx; live(lines, p, ctx, pick); },
      clear() { for (const ln of lines) ln.series.setData([]); },
      destroy() { for (const ln of lines) chart.removeSeries(ln.series); },
    };
  }

  const RATIO_COLORS = [
    { id: "p100", color: "#ff0000" },
    { id: "p50", color: "#ff8c8c" },
    { id: "n100", color: "#0000ff" },
    { id: "n50", color: "#8ca5ff" },
    { id: "miss", color: "#969696" },
    { id: "z", color: "#6c6c6c" },
  ];
  function ratioColorId(rgb) {
    const n = Number(rgb) >>> 0;
    const hit = RATIO_COLORS.find((c) => Number.parseInt(c.color.slice(1), 16) === n);
    return hit ? hit.id : "z";
  }

  function packHandle(chart) {
    const scale = "fxpack";
    const layers = { hold: true, ratio: true, mark: true };
    function markSeries() {
      const s = series(chart, scale, "#dc0000", 1);
      s.applyOptions({ lineVisible: false });
      return s;
    }
    const lines = [
      { layer: "hold", series: histogram(chart, scale, "#dcdcdc"),
        on: () => layers.hold, value: (r) => (r.hold > 0 ? r.hold : null), color: (r) => css(r.holdRgb) },
      { layer: "ratio", stage: true, colors: RATIO_COLORS,
        series: Object.fromEntries(RATIO_COLORS.map((c) => {
          const s = series(chart, scale, c.color, 1);
          s.applyOptions({ lineVisible: false });
          s.attachPrimitive(globalThis.HorizLines.primitive(c.color, 1));
          return [c.id, s];
        })),
        on: () => layers.ratio, value: (r) => (r.ratioOn ? r.ratio : null), colorId: (r) => ratioColorId(r.ratioRgb) },
      { layer: "mark", series: markSeries(), pen: globalThis.HorizLines.dots(2),
        on: () => layers.mark, value: (r) => (r.relOn ? r.relLen : null), color: (r) => css(r.relRgb), width: (r) => r.relW },
      { layer: "mark", series: markSeries(), pen: globalThis.HorizLines.diamonds(2),
        on: () => layers.mark, value: (r) => (r.cfOn ? r.cfLen : null), color: (r) => css(r.cfRgb), width: (r) => r.cfW },
    ];
    for (const ln of lines) {
      if (ln.pen) ln.series.attachPrimitive(ln.pen);
    }
    const all = [];
    for (const ln of lines) {
      if (ln.stage) for (const c of ln.colors) all.push(ln.series[c.id]);
      else all.push(ln.series);
    }
    function setAxis(id) {
      const own = id === "right";
      const next = own ? "right" : scale;
      for (const s of all) {
        if (!s.applyOptions) continue;
        s.applyOptions({
          priceScaleId: next,
          autoscaleInfoProvider: (base) => {
            const res = base();
            const maxValue = Math.max(res?.priceRange?.maxValue ?? 100, 100);
            return { priceRange: { minValue: 0, maxValue } };
          },
        });
      }
      if (typeof chart.priceScale === "function") {
        chart.priceScale(next).applyOptions(own
          ? { scaleMargins: { top: 0.06, bottom: 0.04 }, autoScale: true, visible: true }
          : { scaleMargins: { top: 0.72, bottom: 0 }, autoScale: true });
      }
    }
    setAxis(null);
    let ctx = null;
    const pick = (ind) => ind && ind.os;
    return {
      setAxis,
      setLayers(map) {
        for (const k of Object.keys(layers)) if (map && k in map) layers[k] = !!map[k];
        paint(lines, ctx, pick);
      },
      applySeed(c) { ctx = c; paint(lines, ctx, pick); },
      applyLive(p, c) { ctx = c || ctx; live(lines, p, ctx, pick); },
      clear() {
        for (const s of all) s.setData([]);
        for (const ln of lines) if (ln.pen) ln.pen.setPoints([]);
      },
      destroy() { for (const s of all) chart.removeSeries(s); },
    };
  }

  return { parse, judgeHandle, packHandle };
})();

if (typeof globalThis !== "undefined") globalThis.OsLayers = OsLayers;
