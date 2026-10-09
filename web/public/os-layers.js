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
      adjOn: row.length >= 59 && Number(row[27]) === 1,
      adjY: row.length >= 59 ? num(row[28]) : null,
      adjRgb: row.length >= 59 ? (Number(row[29]) || 0) : 0,
      adjW: row.length >= 59 ? (Number(row[30]) || 3) : 3,
      brkOn: row.length >= 59 && Number(row[31]) === 1,
      brkY: row.length >= 59 ? num(row[32]) : null,
      brkRgb: row.length >= 59 ? (Number(row[33]) || 0) : 0,
      leadOn: row.length >= 59 && Number(row[34]) === 1,
      leadY: row.length >= 59 ? num(row[35]) : null,
      leadRgb: row.length >= 59 ? (Number(row[36]) || 0) : 0,
      flatPos: row.length >= 59 ? num(row[37]) : null,
      flatSlope: row.length >= 59 ? num(row[38]) : null,
      flatPosRgb: row.length >= 59 ? (Number(row[39]) || 0) : 0,
      flatSlopeRgb: row.length >= 59 ? (Number(row[40]) || 0) : 0,
      flatMark: row.length >= 59 ? Number(row[41]) || 0 : 0,
      waveTime: row.length >= 59 ? num(row[42]) : null,
      wavePrice: row.length >= 59 ? num(row[43]) : null,
      waveOpp: row.length >= 59 ? num(row[44]) : null,
      waveState: row.length >= 59 ? Number(row[45]) || 0 : 0,
      waveStateRgb: row.length >= 59 ? (Number(row[46]) || 0) : 0,
      waveSig: row.length >= 59 ? Number(row[47]) || 0 : 0,
      waveSigRgb: row.length >= 59 ? (Number(row[48]) || 0) : 0,
      waveSigW: row.length >= 59 ? (Number(row[49]) || 3) : 3,
      paintRgb: row.length >= 59 ? (Number(row[50]) || 0) : 0,
      sigDir: row.length >= 59 ? Number(row[51]) || 0 : 0,
      sigExit: row.length >= 59 ? Number(row[52]) || 0 : 0,
      sigKind: row.length >= 135 ? Number(row[134]) || 0 : 0,
      sigQty: row.length >= 137 ? Number(row[135]) || 0 : 0,
      sigExitQty: row.length >= 137 ? Number(row[136]) || 0 : 0,
      pnlOpen: row.length >= 59 ? num(row[53]) : null,
      pnlMfe: row.length >= 59 ? num(row[54]) : null,
      pnlMae: row.length >= 59 ? num(row[55]) : null,
      pnlClosed: row.length >= 59 ? num(row[56]) : null,
      pnlEntries: row.length >= 59 ? num(row[57]) : null,
      pnlWins: row.length >= 59 ? num(row[58]) : null,
      pnlKeep: row.length >= 60 ? num(row[59]) : null,
      pnlOpenRgb: row.length >= 84 ? (Number(row[60]) || 0) : 0,
      pnlOpenW: row.length >= 84 ? (Number(row[61]) || 0) : 1,
      pnlMfeRgb: row.length >= 84 ? (Number(row[62]) || 0) : 0,
      pnlMfeW: row.length >= 84 ? (Number(row[63]) || 0) : 1,
      pnlMaeRgb: row.length >= 84 ? (Number(row[64]) || 0) : 0,
      pnlMaeW: row.length >= 84 ? (Number(row[65]) || 0) : 1,
      pnlKeepRgb: row.length >= 84 ? (Number(row[66]) || 0) : 0,
      pnlKeepW: row.length >= 84 ? (Number(row[67]) || 1) : 1,
      pnlOpenOn: row.length >= 84 ? Number(row[68]) === 1 : true,
      pnlMfeOn: row.length >= 84 ? Number(row[69]) === 1 : true,
      pnlMaeOn: row.length >= 84 ? Number(row[70]) === 1 : true,
      pnlKeepOn: row.length >= 84 ? Number(row[71]) === 1 : true,
      pnlP4On: row.length >= 84 && Number(row[72]) === 1,
      pnlP4: row.length >= 84 ? num(row[73]) : null,
      pnlP5On: row.length >= 84 && Number(row[74]) === 1,
      pnlP5: row.length >= 84 ? num(row[75]) : null,
      pnlFlips: row.length >= 84 ? (Number(row[76]) || 0) : 0,
      pnlDanger: row.length >= 84 ? (Number(row[77]) || 0) : 0,
      pnlP26On: row.length >= 84 && Number(row[78]) === 1,
      pnlP27On: row.length >= 84 && Number(row[79]) === 1,
      pnlP30On: row.length >= 84 && Number(row[80]) === 1,
      pnlP30: row.length >= 84 ? num(row[81]) : null,
      pnlKeep2On: row.length >= 84 && Number(row[82]) === 1,
      pnlKeep2: row.length >= 84 ? num(row[83]) : null,
      pnlLongOn: row.length >= 134 && Number(row[131]) === 1,
      pnlLong: row.length >= 134 ? num(row[132]) : null,
      pnlLongRgb: row.length >= 134 ? (Number(row[133]) || 0xff7f00) : 0xff7f00,
      pnlSide: row.length >= 142 ? Number(row[137]) || 0 : 0,
      pnlShortOn: row.length >= 142 && Number(row[138]) === 1,
      pnlShort: row.length >= 142 ? num(row[139]) : null,
      pnlExitOn: row.length >= 142 && Number(row[140]) === 1,
      pnlExit: row.length >= 142 ? num(row[141]) : null,
      w1: waveAt(row, 84),
      w2: waveAt(row, 92),
      w4: waveAt(row, 100),
      w5: waveAt(row, 108),
      ecOn: row.length >= 131 && Number(row[116]) === 1,
      ecPx: row.length >= 131 ? num(row[117]) : null,
      ecRgb: row.length >= 131 ? (Number(row[118]) || 0) : 0,
      ecW: row.length >= 131 ? (Number(row[119]) || 2) : 2,
      ecLineOn: row.length >= 131 && Number(row[120]) === 1,
      ecLine: row.length >= 131 ? num(row[121]) : null,
      ecLineRgb: row.length >= 131 ? (Number(row[122]) || 0) : 0,
      rsOn: row.length >= 131 && Number(row[123]) === 1,
      rsPx: row.length >= 131 ? num(row[124]) : null,
      rsRgb: row.length >= 131 ? (Number(row[125]) || 0) : 0,
      rsW: row.length >= 131 ? (Number(row[126]) || 3) : 3,
      bkOn: row.length >= 131 && Number(row[127]) === 1,
      bkPx: row.length >= 131 ? num(row[128]) : null,
      bkRgb: row.length >= 131 ? (Number(row[129]) || 0) : 0,
      osPaint: row.length >= 131 ? (Number(row[130]) || 0) : 0,
    };
  }

  function waveAt(row, base) {
    if (!Array.isArray(row) || row.length < base + 8) return null;
    return {
      time: num(row[base]), price: num(row[base + 1]), opp: num(row[base + 2]),
      state: Number(row[base + 3]) || 0, stateRgb: Number(row[base + 4]) || 0,
      sig: Number(row[base + 5]) || 0, sigRgb: Number(row[base + 6]) || 0,
      sigW: Number(row[base + 7]) || 3,
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

  function barAt(ctx, t) {
    if (!ctx?.bars) return null;
    if (typeof ctx.bars.get === "function") return ctx.bars.get(t) || null;
    if (!ctx.barPos) return null;
    const i = ctx.barPos.get(t);
    return Number.isInteger(i) ? ctx.bars[i] : null;
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
          const bar = barAt(ctx, t);
          const value = row ? ln.value(row, bar) : null;
          const point = value != null ? { time: t, value } : { time: t };
          if (value != null && ln.color) point.color = ln.color(row);
          data.push(point);
          if (ln.pen && value != null) {
            marks.push({
              time: t, value, color: point.color, width: ln.width ? ln.width(row) : 4,
              text: ln.text ? ln.text(row, bar) : "",
              side: ln.side ? ln.side(row) : 1,
            });
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
      const value = row ? ln.value(row, barAt(ctx, t)) : null;
      if (value == null) ln.series.update({ time: t });
      else ln.series.update({ time: t, value, color: ln.color ? ln.color(row) : undefined });
      if (ln.pen && ctx) {
        const marks = [];
        for (const bt of ctx.barSeq) {
          const brow = pick(ctx.barInd.get(bt));
          const bbar = barAt(ctx, bt);
          const bv = brow && ln.on() ? ln.value(brow, bbar) : null;
          if (bv != null) {
            marks.push({
              time: bt, value: bv, color: ln.color(brow), width: ln.width ? ln.width(brow) : 4,
              text: ln.text ? ln.text(brow, bbar) : "",
              side: ln.side ? ln.side(brow) : 1,
            });
          }
        }
        ln.pen.setPoints(marks);
      }
    }
  }

  function judgeHandle(chart) {
    const scale = "fxjudge";
    const layers = { state: true, fut: true, prof: true, di: true, adx: true, entry: true, release: true, resume: true, break: true, lead: true };
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
      { layer: "resume", series: dots(chart, scale, "#ff8c00", 4),
        on: () => layers.resume, value: (r) => (r.adjOn ? r.adjY : null), color: (r) => css(r.adjRgb) },
      { layer: "break", series: dots(chart, scale, "#ffbe00", 4),
        on: () => layers.break, value: (r) => (r.brkOn ? r.brkY : null), color: (r) => css(r.brkRgb) },
      { layer: "lead", series: dots(chart, scale, "#00b400", 4),
        on: () => layers.lead, value: (r) => (r.leadOn ? r.leadY : null), color: (r) => css(r.leadRgb) },
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
          autoscaleInfoProvider: () => ({ priceRange: { minValue: -180, maxValue: 180 } }),
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

  function dotLine(chart, scale) {
    const s = series(chart, scale, "#969696", 1);
    s.applyOptions({ lineVisible: false });
    const pen = globalThis.HorizLines.dots(2);
    s.attachPrimitive(pen);
    return { s, pen };
  }

  function paneHandle(chart, scale, lines, margins) {
    const all = lines.map((ln) => ln.series);
    function setAxis(id) {
      const own = id === "right";
      const next = own ? "right" : scale;
      for (const s of all) {
        if (s.applyOptions) s.applyOptions({ priceScaleId: next });
      }
      // 가격 눈금에 그대로 두는 신호는 오른쪽 축을 숨기거나 여백을 바꾸지 않는다.
      if (typeof chart.priceScale === "function" && (next !== "right" || own)) {
        chart.priceScale(next).applyOptions({
          scaleMargins: own ? { top: 0.06, bottom: 0.04 } : margins,
          autoScale: true,
          visible: own,
        });
      }
    }
    setAxis(null);
    let ctx = null;
    const pick = (ind) => ind && ind.os;
    return {
      setAxis,
      setLayers(map) {
        for (const ln of lines) if (map && ln.layer in map) ln.on = () => !!map[ln.layer];
        paint(lines, ctx, pick);
      },
      applySeed(c) { ctx = c; paint(lines, ctx, pick); },
      applyLive(p, c) { ctx = c || ctx; live(lines, p, ctx, pick); },
      clear() { for (const ln of lines) ln.series.setData([]); },
      destroy() { for (const s of all) chart.removeSeries(s); },
    };
  }

  function flatHandle(chart) {
    const scale = "fxflat";
    const pos = histogram(chart, scale, "#ff0000");
    const slope = series(chart, scale, "#b40000", 2);
    const mark = dotLine(chart, scale);
    const lines = [
      { layer: "pos", series: pos, on: () => true, value: (r) => r.flatPos, color: (r) => css(r.flatPosRgb) },
      { layer: "slope", series: slope, on: () => true, value: (r) => r.flatSlope, color: (r) => css(r.flatSlopeRgb) },
      { layer: "mark", series: mark.s, pen: mark.pen, on: () => true,
        value: (r) => (r.flatMark ? r.flatMark * 3 : null), color: (r) => (r.flatMark > 0 ? "#ff0000" : "#0000ff") },
    ];
    return paneHandle(chart, scale, lines, { top: 0.72, bottom: 0 });
  }

  function fitHeight(list) {
    return () => {
      let min = Infinity;
      let max = -Infinity;
      for (const s of list) {
        const rows = typeof s.data === "function" ? s.data() : [];
        for (const p of rows) {
          const v = p && Number(p.value);
          if (!Number.isFinite(v)) continue;
          if (v < min) min = v;
          if (v > max) max = v;
        }
      }
      if (!Number.isFinite(min) || !Number.isFinite(max)) return null;
      if (min === max) {
        min -= 1;
        max += 1;
      }
      const pad = (max - min) * 0.08;
      return { priceRange: { minValue: min - pad, maxValue: max + pad } };
    };
  }

  function stepLine(chart, scale, color, width) {
    const s = series(chart, scale, color, width);
    s.applyOptions({ lineVisible: false });
    const pen = globalThis.HorizLines.defLine(false, true);
    s.attachPrimitive(pen);
    return { s, pen };
  }

  function adjLines(chart, scale, pick) {
    const state = histogram(chart, scale, "#dc0000");
    const time = stepLine(chart, scale, "#ff8c00", 2);
    const price = stepLine(chart, scale, "#9600c8", 2);
    const opp = stepLine(chart, scale, "#969696", 1);
    const sig = dotLine(chart, scale);
    const view = (r) => (r ? pick(r) : null);
    const guide = (value, color) => ({
      layer: "ratio", series: series(chart, scale, color, 1), on: () => true,
      value: (r) => (view(r) ? value : null), color: () => color,
    });
    const lines = [
      { layer: "state", series: state, on: () => true, value: (r) => (view(r)?.state ? -15 : null), color: (r) => css(view(r).stateRgb) },
      { layer: "ratio", series: time.s, pen: time.pen, on: () => true,
        value: (r) => view(r)?.time || null, color: () => "#ff8c00", width: () => 2 },
      { layer: "ratio", series: price.s, pen: price.pen, on: () => true,
        value: (r) => view(r)?.price || null, color: () => "#9600c8", width: () => 2 },
      { layer: "ratio", series: opp.s, pen: opp.pen, on: () => true,
        value: (r) => view(r)?.opp || null, color: () => "#969696", width: () => 1 },
      guide(23.6, "#dcdcdc"),
      guide(38.2, "#c8c8c8"),
      guide(61.8, "#c8c8c8"),
      guide(100, "#aaaaaa"),
      { layer: "sig", series: sig.s, pen: sig.pen, on: () => true,
        value: (r) => (view(r)?.sig ? 130 : null), color: (r) => css(view(r).sigRgb), width: (r) => view(r).sigW },
    ];
    const fitted = fitHeight(lines.map((ln) => ln.series));
    for (const ln of lines) ln.series.applyOptions({ autoscaleInfoProvider: fitted });
    return lines;
  }

  function adjHandle(chart) {
    return paneHandle(chart, "fxadj", adjLines(chart, "fxadj", (r) => ({
      time: r.waveTime, price: r.wavePrice, opp: r.waveOpp,
      state: r.waveState, stateRgb: r.waveStateRgb, sig: r.waveSig, sigRgb: r.waveSigRgb, sigW: r.waveSigW,
    })), { top: 0.72, bottom: 0 });
  }

  function adjHandleOf(chart, scale, key) {
    return paneHandle(chart, scale, adjLines(chart, scale, (r) => r[key]), { top: 0.72, bottom: 0 });
  }

  function flatHandleV2(chart) {
    const scale = "fxflat2";
    const pos = histogram(chart, scale, "#ff0000");
    const slope = series(chart, scale, "#b40000", 2);
    const lines = [
      { layer: "pos", series: pos, on: () => true, value: (r) => r.flatPos, color: (r) => css(r.flatPosRgb) },
      { layer: "slope", series: slope, on: () => true, value: (r) => r.flatSlope, color: (r) => css(r.flatSlopeRgb) },
    ];
    return paneHandle(chart, scale, lines, { top: 0.72, bottom: 0 });
  }

  function ecHandle(chart, withExtra) {
    const dot = dotLine(chart, "right");
    const line = series(chart, "right", "#ff7878", 1);
    const lines = [
      { layer: "dot", series: dot.s, pen: dot.pen, on: () => true,
        value: (r) => (r.ecOn ? r.ecPx : null), color: (r) => css(r.ecRgb), width: (r) => r.ecW },
      { layer: "line", series: line, on: () => true,
        value: (r) => (r.ecLineOn ? r.ecLine : null), color: (r) => css(r.ecLineRgb) },
    ];
    if (withExtra) {
      const resume = dotLine(chart, "right");
      const brk = dotLine(chart, "right");
      lines.push(
        { layer: "resume", series: resume.s, pen: resume.pen, on: () => true,
          value: (r) => (r.rsOn ? r.rsPx : null), color: (r) => css(r.rsRgb), width: (r) => r.rsW },
        { layer: "break", series: brk.s, pen: brk.pen, on: () => true,
          value: (r) => (r.bkOn ? r.bkPx : null), color: (r) => css(r.bkRgb), width: () => 6 },
      );
    }
    return paneHandle(chart, "right", lines, { top: 0.08, bottom: 0.08 });
  }

  const BUY_NAME = ["매수", "후보매수", "돌파매수", "선행매수", "조정매수"];
  const SELL_NAME = ["매도", "후보매도", "돌파매도", "선행매도", "조정매도"];

  function sigLabel(name) {
    return name;
  }

  // 시스템 주문 화살. side > 0 은 가격 아래에서 위를 가리키고, side < 0 은 위에서 아래를 가리킨다.
  function orderMarks() {
    let series = null;
    let chart = null;
    let points = [];
    return {
      setPoints(next) { points = next || []; },
      attached(param) { series = param.series; chart = param.chart; },
      detached() { series = null; chart = null; },
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
                  ctx.textAlign = "center";
                  ctx.font = `${Math.round(11 * hr)}px sans-serif`;
                  for (const p of points) {
                    if (!p || !Number.isFinite(p.value)) continue;
                    const mx = xOf(p.time);
                    const my = series.priceToCoordinate(p.value);
                    if (mx == null || my == null) continue;
                    const x = mx * hr;
                    const y = my * vr;
                    const up = (p.side || 1) > 0;
                    const gap = 10 * vr;
                    const ah = 8 * vr;
                    const aw = 5 * hr;
                    const tip = up ? y + gap : y - gap;
                    const base = up ? tip + ah : tip - ah;
                    ctx.beginPath();
                    ctx.fillStyle = p.color || "#00ff00";
                    ctx.moveTo(x, tip);
                    ctx.lineTo(x - aw, base);
                    ctx.lineTo(x + aw, base);
                    ctx.closePath();
                    ctx.fill();
                    if (p.text) {
                      const lines = String(p.text).split("\n");
                      const step = 12 * vr;
                      ctx.textBaseline = up ? "top" : "bottom";
                      lines.forEach((line, i) => {
                        const ty = up ? base + 2 * vr + i * step : base - 2 * vr - (lines.length - 1 - i) * step;
                        ctx.fillText(line, x, ty);
                      });
                    }
                  }
                });
              },
            };
          },
        }];
      },
    };
  }

  function sigHandle(chart) {
    function markLine() {
      const s = series(chart, "right", "#969696", 1);
      s.applyOptions({ lineVisible: false, lastValueVisible: false, priceLineVisible: false });
      const pen = orderMarks();
      s.attachPrimitive(pen);
      return { s, pen };
    }
    const buy = markLine();
    const sell = markLine();
    const exit = markLine();
    const lines = [
      { layer: "entry", series: buy.s, pen: buy.pen, on: () => true,
        value: (r, bar) => (r.sigDir > 0 && bar ? bar.low : null),
        color: () => "#00ff00", side: () => 1,
        text: (r) => sigLabel(BUY_NAME[r.sigKind] || BUY_NAME[0]) },
      { layer: "entry", series: sell.s, pen: sell.pen, on: () => true,
        value: (r, bar) => (r.sigDir < 0 && bar ? bar.high : null),
        color: () => "#00ffff", side: () => -1,
        text: (r) => sigLabel(SELL_NAME[r.sigKind] || SELL_NAME[0]) },
      { layer: "exit", series: exit.s, pen: exit.pen, on: () => true,
        value: (r, bar) => (r.sigExit && bar ? (r.sigExit > 0 ? bar.high : bar.low) : null),
        color: () => "#ffd000",
        side: (r) => (r.sigExit > 0 ? -1 : 1),
        text: (r) => sigLabel(
          Math.abs(r.sigExit) === 2
            ? (r.sigExit > 0 ? "시간청산매수" : "시간청산매도")
            : (r.sigExit > 0 ? "파랑청산" : "빨강청산")) },
    ];
    const handle = paneHandle(chart, "right", lines, { top: 0.08, bottom: 0.08 });
    const paint0 = handle.applySeed;
    handle.applySeed = (c) => {
      handle._bars = c;
      paint0(c);
    };
    return handle;
  }

  // 봉마다 가로 조각. 같은 값이 이어지면 잇고, 값이 바뀌거나 봉이 떨어지면 끊는다.
  function ticks() {
    let series = null;
    let chart = null;
    let points = [];
    return {
      setPoints(next) { points = next || []; },
      attached(param) { series = param.series; chart = param.chart; },
      detached() { series = null; chart = null; },
      updateAllViews() {},
      paneViews() {
        return [{
          renderer() {
            return {
              draw(target) {
                if (!series || !chart || !points.length) return;
                let step = 60;
                for (let i = 1; i < points.length; i++) {
                  const d = points[i].time - points[i - 1].time;
                  if (d > 0 && d < step) step = d;
                }
                const segs = [];
                for (let i = 0; i < points.length; i++) {
                  const a = points[i];
                  const b = points[i + 1];
                  if (!a || !Number.isFinite(a.value)) continue;
                  const joined = b && Number.isFinite(b.value) && a.value === b.value &&
                    (b.time - a.time) <= step * 1.5;
                  segs.push({
                    t0: a.time,
                    t1: joined ? b.time : a.time + step,
                    v: a.value,
                    color: a.color,
                    width: a.width,
                  });
                }
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const xOf = (t) => chart.timeScale().timeToCoordinate(t);
                  for (const seg of segs) {
                    const x0 = xOf(seg.t0);
                    const x1 = xOf(seg.t1);
                    const y = series.priceToCoordinate(seg.v);
                    if (x0 == null || x1 == null || y == null) continue;
                    ctx.beginPath();
                    ctx.strokeStyle = seg.color || "#d7dde8";
                    ctx.lineCap = "butt";
                    ctx.lineWidth = Math.max(1, seg.width || 1) * vr;
                    ctx.moveTo(x0 * hr, y * vr);
                    ctx.lineTo(x1 * hr, y * vr);
                    ctx.stroke();
                  }
                });
              },
            };
          },
        }];
      },
    };
  }

  function squares(side) {
    let series = null;
    let chart = null;
    let points = [];
    return {
      setPoints(next) { points = next || []; },
      attached(param) { series = param.series; chart = param.chart; },
      detached() { series = null; chart = null; },
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
                  const s = (side > 0 ? side : 8) * hr;
                  for (const p of points) {
                    if (!p || !Number.isFinite(p.value)) continue;
                    const x = xOf(p.time);
                    const y = series.priceToCoordinate(p.value);
                    if (x == null || y == null) continue;
                    ctx.fillStyle = p.color || "#00ffff";
                    ctx.fillRect(x * hr - s / 2, y * vr - s / 2, s, s);
                  }
                });
              },
            };
          },
        }];
      },
    };
  }

  function mark(chart, scale, pen) {
    const s = series(chart, scale, "#888888", 1);
    s.applyOptions({ lineVisible: false });
    s.attachPrimitive(pen);
    s._pnlPen = pen;
    return s;
  }

  function paintMarks(ln, ctx, pick) {
    const pts = [];
    const data = [];
    if (ln.on() && ctx) {
      for (const t of ctx.barSeq) {
        const row = pick(ctx.barInd.get(t));
        const value = row ? ln.value(row) : null;
        if (value == null) {
          data.push({ time: t });
          continue;
        }
        pts.push({ time: t, value, color: ln.color(row), width: ln.width ? ln.width(row) : 1 });
        data.push({ time: t, value });
      }
    }
    ln.series.setData(data);
    if (ln.series._pnlPen) ln.series._pnlPen.setPoints(pts);
  }

  function pnlHandle(chart) {
    const scale = "fxpnl";
    const layers = {
      mfeLong: true, maeLong: true, openLong: true,
      mfeShort: true, maeShort: true, openShort: true,
      exit: true, closedLong: true, closedShort: true,
      keepShort: true, keepLong: true, keep2: true,
      p26: true, p27: true, p30: true,
    };
    // 수식에 색이 있으면 그 색. 없으면 레이어에 지정한 색. 값은 가로로만 잇는다.
    const line = (layer, value, color) => ({
      layer, series: mark(chart, scale, ticks()), value, width: () => 1,
      color: typeof color === "function" ? color : () => color,
    });
    const lines = [
      line("mfeLong", (r) => (r.pnlSide > 0 ? r.pnlMfe : null), (r) => css(r.pnlMfeRgb || 0xff0000)),
      line("maeLong", (r) => (r.pnlSide > 0 ? r.pnlMae : null), (r) => css(r.pnlMaeRgb || 0x0000ff)),
      line("openLong", (r) => (r.pnlSide > 0 ? r.pnlOpen : null), (r) => css(r.pnlOpenRgb || 0x0000ff)),
      line("mfeShort", (r) => (r.pnlSide < 0 && r.pnlMfe > 0 ? r.pnlMfe : null), (r) => css(r.pnlMfeRgb || 0xff0000)),
      line("maeShort", (r) => (r.pnlSide < 0 ? r.pnlMae : null), (r) => css(r.pnlMaeRgb || 0x0000ff)),
      line("openShort", (r) => (r.pnlSide < 0 ? r.pnlOpen : null), (r) => css(r.pnlOpenRgb || 0x0000ff)),
      line("exit", (r) => (r.pnlExitOn ? r.pnlExit : null), "#ff0000"),
      line("closedLong", (r) => (r.pnlLongOn ? r.pnlLong : null), (r) => css(r.pnlLongRgb || 0xff7f00)),
      line("closedShort", (r) => (r.pnlShortOn ? r.pnlShort : null), "#6a8f23"),
      line("keepShort", (r) => (r.pnlSide < 0 && r.pnlKeepOn ? r.pnlKeep : null), "#5ec8f0"),
      line("keepLong", (r) => (r.pnlSide > 0 && r.pnlKeepOn ? r.pnlKeep : null), "#0000ff"),
      line("keep2", (r) => (r.pnlKeep2On ? r.pnlKeep2 : null), "#7a3ff0"),
      line("p26", (r) => (r.pnlP26On ? 0 : null), "#8b0000"),
      { layer: "p27", series: mark(chart, scale, squares(8)),
        value: (r) => (r.pnlP27On ? 0 : null), width: () => 8, color: () => "#00ffff" },
      line("p30", (r) => (r.pnlP30On ? r.pnlP30 : null), "#008000"),
    ];
    const all = lines.map((ln) => ln.series);
    function setAxis(id) {
      const own = id === "right";
      const next = own ? "right" : scale;
      for (const s of all) {
        if (s.applyOptions) s.applyOptions({ priceScaleId: next });
      }
      if (typeof chart.priceScale === "function") {
        chart.priceScale(next).applyOptions({
          scaleMargins: { top: 0.72, bottom: 0 }, autoScale: true, visible: own,
        });
      }
    }
    setAxis(null);
    let ctx = null;
    const pick = (ind) => ind && ind.os;
    function draw() {
      for (const ln of lines) {
        ln.on = () => layers[ln.layer];
        paintMarks(ln, ctx, pick);
      }
    }
    return {
      setAxis,
      setLayers(map) {
        for (const k of Object.keys(layers)) if (map && k in map) layers[k] = !!map[k];
        draw();
      },
      applySeed(c) { ctx = c; draw(); },
      applyLive(p, c) { ctx = c || ctx; draw(); },
      clear() { for (const s of all) s.setData([]); },
      destroy() { for (const s of all) chart.removeSeries(s); },
    };
  }

  function paintHandle() {
    const layers = { bar: true };
    return {
      setLayers(map) { if (map && "bar" in map) layers.bar = !!map.bar; },
      applySeed() {},
      applyLive() {},
      clear() {},
      destroy() {},
      wantsBar: () => layers.bar,
    };
  }

  return {
    parse, judgeHandle, packHandle, flatHandle, flatHandleV2, adjHandle, adjHandleOf,
    ecHandle, sigHandle, pnlHandle, paintHandle,
  };
})();

if (typeof globalThis !== "undefined") globalThis.OsLayers = OsLayers;
