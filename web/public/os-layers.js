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
  function posExtra(row, at) {
    if (!Array.isArray(row) || row.length <= at) {
      return { mfeW: 1, maeW: 1, keepMfeOn: false, keepMfe: null, keepMaeOn: false, keepMae: null,
        flips: 0, danger: 0, totalOn: false, total: null };
    }
    return {
      mfeW: Number(row[at]) || 0,
      maeW: Number(row[at + 1]) || 0,
      keepMfeOn: Number(row[at + 2]) === 1,
      keepMfe: num(row[at + 3]),
      keepMaeOn: Number(row[at + 4]) === 1,
      keepMae: num(row[at + 5]),
      flips: Number(row[at + 6]) || 0,
      danger: Number(row[at + 7]) || 0,
      totalOn: Number(row[at + 8]) === 1,
      total: num(row[at + 9]),
    };
  }

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
      flatOn: row.length >= 148 && Number(row[142]) === 1,
      flatUpPos: row.length >= 148 ? num(row[143]) : null,
      flatDnPos: row.length >= 148 ? num(row[144]) : null,
      flatUpSlope: row.length >= 148 ? num(row[145]) : null,
      flatDnSlope: row.length >= 148 ? num(row[146]) : null,
      flatAmp: row.length >= 148 ? num(row[147]) : null,
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
      turnOn: row.length >= 156 && Number(row[149]) === 1,
      turnY: row.length >= 156 ? num(row[150]) : null,
      turnRgb: row.length >= 156 ? (Number(row[151]) || 0) : 0,
      turnW: row.length >= 156 ? (Number(row[152]) || 7) : 7,
      sigGrade: row.length >= 156 ? (Number(row[153]) || 0) : 0,
      pos1: row.length >= 176 ? {
        side: Number(row[156]) || 0,
        open: num(row[157]), mfe: num(row[158]), mae: num(row[159]),
        exitOn: Number(row[160]) === 1, exit: num(row[161]),
        long: num(row[162]), short: num(row[163]),
        wins: Number(row[164]) || 0, losses: Number(row[165]) || 0,
        openRgb: Number(row[196]) || 0, mfeRgb: Number(row[197]) || 0,
        maeRgb: Number(row[198]) || 0, longRgb: Number(row[199]) || 0, openW: Number(row[200]) || 1,
        ...posExtra(row, 206),
      } : null,
      pos2: row.length >= 176 ? {
        side: Number(row[166]) || 0,
        open: num(row[167]), mfe: num(row[168]), mae: num(row[169]),
        exitOn: Number(row[170]) === 1, exit: num(row[171]),
        long: num(row[172]), short: num(row[173]),
        wins: Number(row[174]) || 0, losses: Number(row[175]) || 0,
        openRgb: Number(row[201]) || 0, mfeRgb: Number(row[202]) || 0,
        maeRgb: Number(row[203]) || 0, longRgb: Number(row[204]) || 0, openW: Number(row[205]) || 1,
        ...posExtra(row, 216),
      } : null,
      pairDir: Number(row[176]) || 0, pairKind: Number(row[177]) || 0, pairGrade: Number(row[178]) || 0,
      pairAk: Number(row[179]) || 0, pairAg: Number(row[180]) || 0, pairQty: Number(row[181]) || 0,
      pairSuf: Number(row[182]) || 0, pairExit: Number(row[183]) || 0, pairLoss: Number(row[184]) || 0,
      pairTicks: Number(row[185]) || 0, pairLimit: Number(row[226]) || 0,
      pairExitQty: num(row[228]), pairLimitQty: num(row[229]),
      zdDir: Number(row[186]) || 0, zdKind: Number(row[187]) || 0, zdGrade: Number(row[188]) || 0,
      zdAk: Number(row[189]) || 0, zdAg: Number(row[190]) || 0, zdQty: Number(row[191]) || 0,
      zdSuf: Number(row[192]) || 0, zdExit: Number(row[193]) || 0, zdLoss: Number(row[194]) || 0,
      zdTicks: Number(row[195]) || 0, zdLimit: Number(row[227]) || 0,
      zdExitQty: num(row[230]), zdLimitQty: num(row[231]),
      swingOn: row.length >= 234 && Number(row[232]) === 1,
      swingPct: row.length >= 234 ? num(row[233]) : null,
      wrDir: row.length >= 236 ? Number(row[234]) || 0 : 0,
      wrOrder: row.length >= 236 ? Number(row[235]) || 0 : 0,
      wrExitDir: row.length >= 237 ? Number(row[236]) || 0 : 0,
      wrName: row.length >= 238 && typeof row[237] === "string" ? row[237] : "",
      wrExitName: row.length >= 239 && typeof row[238] === "string" ? row[238] : "",
      wrQty: row.length >= 240 ? Number(row[239]) || 0 : 0,
      wrExitQty: row.length >= 241 ? Number(row[240]) || 0 : 0,
      adjKind: row.length >= 156 ? (Number(row[154]) || 0) : 0,
      adjGrade: row.length >= 156 ? (Number(row[155]) || 0) : 0,
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
    const layers = { state: true, fut: true, prof: true, di: true, adx: true, entry: true, release: true, resume: true, break: true, lead: true, turn: true };
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
      { layer: "turn", series: dots(chart, scale, "#006400", 7),
        on: () => layers.turn, value: (r) => (r.turnOn ? r.turnY : null), color: (r) => css(r.turnRgb), width: (r) => r.turnW || 7 },
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
          autoscaleInfoProvider: () => ({ priceRange: { minValue: -190, maxValue: 190 } }),
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

  function flatSeries(chart, scale, color, pen) {
    const s = series(chart, scale, color, 1);
    s.applyOptions({ lineVisible: false });
    s.attachPrimitive(pen);
    return { s, pen };
  }

  function flatHandle(chart) {
    const scale = "fxflat";
    const pos = flatSeries(chart, scale, "#ff9696", globalThis.HorizLines.dots(1));
    const slope = flatSeries(chart, scale, "#c80000", dashes(1));
    const upPos = flatSeries(chart, scale, "#ffc8c8", holds());
    const dnPos = flatSeries(chart, scale, "#c8d2ff", holds());
    const upSlope = flatSeries(chart, scale, "#e67878", holds());
    const dnSlope = flatSeries(chart, scale, "#788ce6", holds());
    const amp = flatSeries(chart, scale, "#969696", dashes(1));
    const mark = flatSeries(chart, scale, "#ff0000", globalThis.HorizLines.dots(1));
    const on = (r) => r && (r.flatOn || r.flatPos || r.flatSlope);
    const prev = (r, v) => (on(r) && v ? v : null);
    const lines = [
      { layer: "pos", series: pos.s, pen: pos.pen, on: () => true,
        value: (r) => (on(r) ? r.flatPos : null), color: (r) => css(r.flatPosRgb || 0xb4b4b4), width: () => 6 },
      { layer: "slope", series: slope.s, pen: slope.pen, on: () => true,
        value: (r) => (on(r) ? r.flatSlope : null), color: (r) => css(r.flatSlopeRgb || 0x787878), width: () => 2 },
      { layer: "prev", series: upPos.s, pen: upPos.pen, on: () => true,
        value: (r) => prev(r, r.flatUpPos), color: () => "#ffc8c8", width: () => 1 },
      { layer: "prev", series: dnPos.s, pen: dnPos.pen, on: () => true,
        value: (r) => prev(r, r.flatDnPos), color: () => "#c8d2ff", width: () => 1 },
      { layer: "prev", series: upSlope.s, pen: upSlope.pen, on: () => true,
        value: (r) => prev(r, r.flatUpSlope), color: () => "#e67878", width: () => 1 },
      { layer: "prev", series: dnSlope.s, pen: dnSlope.pen, on: () => true,
        value: (r) => prev(r, r.flatDnSlope), color: () => "#788ce6", width: () => 1 },
      { layer: "amp", series: amp.s, pen: amp.pen, on: () => true,
        value: (r) => (on(r) ? r.flatAmp : null), color: () => "#969696", width: () => 1 },
      { layer: "mark", series: mark.s, pen: mark.pen, on: () => true,
        value: (r) => (r.flatMark ? r.flatMark * 3 : null), width: () => 10,
        color: (r) => (r.flatMark > 0 ? "#ff0000" : "#0000ff") },
    ];
    const fitted = fitHeight(lines.map((ln) => ln.series));
    for (const ln of lines) ln.series.applyOptions({ autoscaleInfoProvider: fitted });
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

  // 값이 같은 봉은 하나로 이어진 가로선, 값이 바뀌면 세로로 잇지 않는다. 없는 봉은 끊긴다.
  function swingSegments() {
    let series = null;
    let chart = null;
    let points = [];
    let requestUpdate = () => {};
    return {
      setPoints(next) {
        points = next || [];
        requestUpdate();
      },
      get points() { return points; },
      attached(param) {
        series = param.series;
        chart = param.chart;
        requestUpdate = typeof param.requestUpdate === "function" ? param.requestUpdate : () => {};
      },
      detached() {
        series = null;
        chart = null;
        requestUpdate = () => {};
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
                  const ts = chart.timeScale();
                  ctx.strokeStyle = "#783ca0";
                  ctx.lineWidth = Math.max(1, 2 * vr);
                  ctx.lineCap = "butt";
                  ctx.beginPath();
                  for (let i = 0; i < points.length; i++) {
                    const p = points[i];
                    // 시각이 없으면 timeToCoordinate가 예외를 던지고, 그 아래 칸 그리기까지 멈춘다.
                    if (!p || !Number.isFinite(p.time) || p.value == null || !Number.isFinite(p.value)) continue;
                    const x0 = ts.timeToCoordinate(p.time);
                    const y = series.priceToCoordinate(p.value);
                    if (x0 == null || y == null) continue;
                    let x1 = x0;
                    const nxt = points[i + 1];
                    const prv = points[i - 1];
                    if (nxt && Number.isFinite(nxt.time)) {
                      const xn = ts.timeToCoordinate(nxt.time);
                      if (xn != null) x1 = xn;
                    } else if (prv && Number.isFinite(prv.time)) {
                      const xp = ts.timeToCoordinate(prv.time);
                      if (xp != null) x1 = x0 + (x0 - xp);
                    }
                    ctx.moveTo(x0 * hr, y * vr);
                    ctx.lineTo(x1 * hr, y * vr);
                  }
                  ctx.stroke();
                });
              },
            };
          },
        }];
      },
    };
  }

  function swingHandle(chart) {
    const scale = "fxswing";
    const line = series(chart, scale, "#783ca0", 2);
    line.applyOptions({
      lineVisible: false,
      autoscaleInfoProvider: () => ({ priceRange: { minValue: 0, maxValue: 100 } }),
    });
    const pen = swingSegments();
    line.attachPrimitive(pen);
    let ctx = null;
    let show = true;
    function rows() {
      if (!show || !ctx?.barSeq) return [];
      const out = [];
      for (const t of ctx.barSeq) {
        const row = ctx.barInd.get(t)?.os;
        const value = row?.swingOn ? row.swingPct : null;
        out.push({ time: t, value: Number.isFinite(value) ? value : null });
      }
      return out;
    }
    function draw() {
      const pts = rows();
      line.setData(pts.map((p) => (p.value == null ? { time: p.time } : p)));
      pen.setPoints(pts);
    }
    function setAxis(id) {
      const own = id === "right";
      line.applyOptions({ priceScaleId: own ? "right" : scale });
      if (typeof chart.priceScale === "function") {
        chart.priceScale(own ? "right" : scale).applyOptions(own
          ? { scaleMargins: { top: 0.06, bottom: 0.04 }, autoScale: true, visible: true }
          : { scaleMargins: { top: 0.72, bottom: 0 }, autoScale: true });
      }
    }
    setAxis(null);
    return {
      setAxis,
      setLayers(map) {
        if (map && "width" in map) show = !!map.width;
        draw();
      },
      applySeed(c) { ctx = c; draw(); },
      applyLive(p, c) {
        ctx = c || ctx;
        if (!show || !p || !ctx) return;
        const t = Number(p.bar_open_time) / 1e6;
        if (!Number.isFinite(t)) return;
        const row = ctx.barInd.get(t)?.os;
        const value = row?.swingOn && Number.isFinite(row.swingPct) ? row.swingPct : null;
        const point = value == null ? { time: t } : { time: t, value };
        const pts = pen.points;
        const last = pts.length ? pts[pts.length - 1] : null;
        // 과거 봉 정정은 마지막 점 갱신으로 끼워 넣을 수 없다. 가진 봉을 다시 깐다.
        if (last && t < last.time) {
          draw();
          return;
        }
        if (last && last.time === t) pts[pts.length - 1] = point;
        else pts.push(point);
        pen.setPoints(pts);
        try { line.update(point); }
        catch { draw(); }
      },
      clear() { line.setData([]); pen.setPoints([]); },
      destroy() { chart.removeSeries(line); },
    };
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

  function orderName(dir, kind, grade, adjKind, adjGrade) {
    const sell = dir < 0;
    if (kind === 1) {
      if (grade === 8) return sell ? "후보돌파매도" : "후보돌파매수";
      if (grade === 7) return sell ? "후보T매도" : "후보T매수";
      if (grade === 6) return sell ? "후보R매도" : "후보R매수";
      if (grade === 5) return sell ? "후보S매도" : "후보S매수";
      if (grade === 4) return sell ? "후보구조매도" : "후보구조매수";
      if (grade === 3) return sell ? "후보A매도" : "후보A매수";
      if (grade === 2) return sell ? "후보B매도" : "후보B매수";
      if (grade === 1) return sell ? "후보C매도" : "후보C매수";
      return sell ? "후보매도" : "후보매수";
    }
    if (kind === 2) return sell ? "돌파매도" : "돌파매수";
    if (kind === 3) return sell ? "선행매도" : "선행매수";
    if (kind === 5) return sell ? "전환매도" : "전환매수";
    if (adjKind === 1 && adjGrade === 3) return sell ? "조정A매도_강" : "조정A매수_강";
    if (adjKind === 1 && adjGrade === 2) return sell ? "조정A매도_중" : "조정A매수_중";
    if (adjKind === 1 && adjGrade === 1) return sell ? "조정A매도_약" : "조정A매수_약";
    if (adjKind === 2 && adjGrade === 3) return sell ? "조정B매도_강" : "조정B매수_강";
    if (adjKind === 2 && adjGrade === 2) return sell ? "조정B매도_중" : "조정B매수_중";
    if (adjKind === 2 && adjGrade === 1) return sell ? "조정B매도_약" : "조정B매수_약";
    return sell ? "조정매도" : "조정매수";
  }

  function exitName(sigExit) {
    if (sigExit === 2) return "시간청산매수";
    if (sigExit === -2) return "시간청산매도";
    return sigExit < 0 ? "빨강청산" : "파랑청산";
  }

  // 봉 위·아래 글자. 백테스트 화살은 라이브러리가 그리고, 계약수는 여기서 다음 줄에 적는다.
  function labelMarks() {
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
                    if (!p || !p.text || !Number.isFinite(p.value)) continue;
                    const mx = xOf(p.time);
                    const my = series.priceToCoordinate(p.value);
                    if (mx == null || my == null) continue;
                    const x = mx * hr;
                    const y = my * vr;
                    const above = p.position === "aboveBar";
                    const lines = String(p.text).split("\n");
                    const step = 12 * vr;
                    const gap = 16 * vr;
                    ctx.fillStyle = p.color || "#d4ff4a";
                    ctx.textBaseline = above ? "bottom" : "top";
                    lines.forEach((line, i) => {
                      const ty = above ? y - gap - (lines.length - 1 - i) * step : y + gap + i * step;
                      ctx.fillText(line, x, ty);
                    });
                  }
                });
              },
            };
          },
        }];
      },
    };
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

  function groupName(dir, kind, grade) {
    const buy = dir > 0;
    const brk = kind === 2 || kind === 3 || (kind === 1 && (grade === 8 || grade === 7 || grade === 4));
    const rev = kind === 5 || (kind === 1 && grade === 6);
    if (brk) return buy ? "돌파매수" : "돌파매도";
    if (rev) return buy ? "반전매수" : "반전매도";
    if (kind === 4) return buy ? "조정매수" : "조정매도";
    return buy ? "압축매수" : "압축매도";
  }

  function pairLabel(r, zd) {
    const dir = zd ? r.zdDir : r.pairDir;
    const kind = zd ? r.zdKind : r.pairKind;
    const grade = zd ? r.zdGrade : r.pairGrade;
    const ak = zd ? r.zdAk : r.pairAk;
    const ag = zd ? r.zdAg : r.pairAg;
    const suf = zd ? r.zdSuf : r.pairSuf;
    const qty = zd ? r.zdQty : r.pairQty;
    const ticks = zd ? r.zdTicks : r.pairTicks;
    let name = zd ? groupName(dir, kind, grade) : orderName(dir, kind, grade, ak, ag);
    if (suf === 2) name += zd ? "_약세" : "_장약세";
    else if (suf === 1) name += zd ? "_반대" : "_반대방향";
    else if (!zd && suf === 3) name += "_직전실패";
    else if (!zd && suf === 4) name += "_전일실패";
    else if (!zd && suf === 5) name += "_직전매수실패";
    if (qty > 0) name += "\n" + qty + "계약";
    if (ticks > 0) name += "\n1차 " + ticks + "틱";
    return name;
  }

  function pairExitLabel(r, zd) {
    const code = zd ? r.zdExit : r.pairExit;
    const loss = zd ? r.zdLoss : r.pairLoss;
    if (!zd) return exitName(code);
    const side = code > 0 ? "매수" : "매도";
    const kind = code === 2 || code === -2 ? "시간청산" : "색청산";
    return kind + side + (loss ? "_손절" : "");
  }

  function quantityLabel(name, qty) {
    return qty > 0 ? name + "\n" + qty + "계약" : name;
  }

  function sigHandle(chart, mode) {
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
    const zd = mode === "zd";
    const wr = mode === "wr";
    const pair = mode === "pair" || zd;
    const dirOf = (r) => (wr ? r.wrDir : (pair ? (zd ? r.zdDir : r.pairDir) : r.sigDir));
    const wrOrderName = (n) => ({
      1: "압축매수", 2: "압축매도", 3: "돌파매수", 4: "돌파매도",
      5: "추세매수", 6: "추세매도", 7: "전환매수", 8: "전환매도",
      55: "시가회복매수", 58: "시가반락매도", 61: "흡수돌파매수", 64: "흡수돌파매도",
      67: "무거래_확장반등매수", 79: "전일저이탈매도", 82: "전일고돌파매수",
      85: "초기하락매도", 88: "초기상승매수", 94: "무거래30매수",
    }[n] || "");
    const wrNum = (name, qty) => (name ? name + "\n" + qty : "");
    const wrEntryText = (r) => wrNum(r.wrName || wrOrderName(r.wrOrder) || "", r.wrQty || 0);
    const exitQtyOf = (r) => (pair ? (zd ? r.zdExitQty : r.pairExitQty) : r.sigExitQty);
    const limitQtyOf = (r) => (zd ? r.zdLimitQty : r.pairLimitQty);
    // 구버전의 수량 누락(null)은 이름만 유지한다. 잔량 0의 가상 청산은 표시하지 않는다.
    const exitOf = (r) => (wr ? (r.wrExitDir || 0) : (pair && exitQtyOf(r) === 0 ? 0 : (pair ? (zd ? r.zdExit : r.pairExit) : r.sigExit)));
    const limitOf = (r) => (pair && limitQtyOf(r) !== 0 ? (zd ? r.zdLimit : r.pairLimit) : 0);
    const markOf = (r) => exitOf(r) || limitOf(r);
    const lines = [
      { layer: "entry", series: buy.s, pen: buy.pen, on: () => true,
        value: (r, bar) => (dirOf(r) > 0 && bar ? bar.low : null),
        color: () => "#00ff00", side: () => 1,
        text: (r) => (wr ? wrEntryText(r) : (pair ? pairLabel(r, zd) : orderName(1, r.sigKind, r.sigGrade, r.adjKind, r.adjGrade))) },
      { layer: "entry", series: sell.s, pen: sell.pen, on: () => true,
        value: (r, bar) => (dirOf(r) < 0 && bar ? bar.high : null),
        color: () => "#00ffff", side: () => -1,
        text: (r) => (wr ? wrEntryText(r) : (pair ? pairLabel(r, zd) : orderName(-1, r.sigKind, r.sigGrade, r.adjKind, r.adjGrade))) },
      { layer: "exit", series: exit.s, pen: exit.pen, on: () => true,
        value: (r, bar) => (markOf(r) && bar ? (markOf(r) > 0 ? bar.high : bar.low) : null),
        color: () => "#ffd000",
        side: (r) => (markOf(r) > 0 ? -1 : 1),
        text: (r) => {
          const lines = [];
          const hit = limitOf(r);
          if (hit) lines.push(quantityLabel((zd ? "1차청산" : "1차목표청산") + (hit > 0 ? "매수" : "매도"), limitQtyOf(r)));
          if (exitOf(r)) lines.push(wr && r.wrExitName ? wrNum(r.wrExitName, r.wrExitQty || 0) : quantityLabel(pair ? pairExitLabel(r, zd) : exitName(exitOf(r)), exitQtyOf(r)));
          return lines.join("\n");
        } },
    ];
    const handle = paneHandle(chart, "right", lines, { top: 0.08, bottom: 0.08 });
    const paint0 = handle.applySeed;
    handle.applySeed = (c) => {
      handle._bars = c;
      paint0(c);
    };
    return handle;
  }

  function barStep(points) {
    let step = 60;
    for (let i = 1; i < points.length; i++) {
      const d = points[i].time - points[i - 1].time;
      if (d > 0 && d < step) step = d;
    }
    return step;
  }

  function hlines(build) {
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
                const segs = build(points, barStep(points));
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const xOf = (t) => chart.timeScale().timeToCoordinate(t);
                  let px = 12;
                  for (const seg of segs) {
                    const x0 = xOf(seg.t0);
                    let x1 = xOf(seg.t1);
                    const y = series.priceToCoordinate(seg.v);
                    if (x0 == null || y == null) continue;
                    if (x1 != null && x1 !== x0) px = Math.abs(x1 - x0);
                    if (seg.frac) {
                      const span = x1 != null && x1 !== x0 ? (x1 - x0) : px;
                      x1 = x0 + span * seg.frac;
                    }
                    if (x1 == null) continue;
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

  // 봉마다 짧은 가로. 이웃과 잇지 않는다.
  function dashes(frac) {
    return hlines((points, step) => {
      const segs = [];
      for (let i = 0; i < points.length; i++) {
        const a = points[i];
        const b = points[i + 1];
        if (!a || !Number.isFinite(a.value)) continue;
        const near = b && (b.time - a.time) > 0 && (b.time - a.time) <= step * 1.5;
        segs.push({
          t0: a.time,
          t1: near ? b.time : a.time + step,
          frac,
          v: a.value,
          color: a.color,
          width: a.width,
        });
      }
      return segs;
    });
  }

  // 같은 값이 이웃 봉에서 이어질 때만 긴 가로. 빈 구간과 값 변화는 끊는다.
  function holds() {
    return hlines((points, step) => {
      const segs = [];
      for (let i = 0; i < points.length; i++) {
        const a = points[i];
        const b = points[i + 1];
        if (!a || !Number.isFinite(a.value)) continue;
        const near = b && Number.isFinite(b.value) && (b.time - a.time) <= step * 1.5;
        if (near && a.value === b.value) {
          segs.push({ t0: a.time, t1: b.time, v: a.value, color: a.color, width: a.width });
        } else {
          segs.push({ t0: a.time, t1: a.time + step, v: a.value, color: a.color, width: a.width });
        }
      }
      return segs;
    });
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

  function posHandle(chart, which) {
    const scale = which === 2 ? "fxpos2" : "fxpos1";
    const pickPos = (ind) => {
      const os = ind && ind.os;
      return os && (which === 2 ? os.pos2 : os.pos1);
    };
    const line = (layer, value, color, width) => ({
      layer, series: mark(chart, scale, ticks()), value,
      width: width || ((r) => (r && r.openW) || 1),
      color: typeof color === "function" ? color : () => color,
    });
    const wide = (pick) => (r) => (pick(r) > 0 ? pick(r) : 1);
    const lines = [
      line("open", (r) => (r.side !== 0 ? r.open : null), (r) => css(r.openRgb || 0x0000ff), (r) => r.openW || 1),
      line("mfe", (r) => (r.side !== 0 && r.mfeW > 0 ? r.mfe : null), (r) => css(r.mfeRgb || 0xff0000), wide((r) => r.mfeW)),
      line("mae", (r) => (r.side !== 0 && r.maeW > 0 ? r.mae : null), (r) => css(r.maeRgb || 0x0000ff), wide((r) => r.maeW)),
      line("exit", (r) => (r.exitOn ? r.exit : null), "#ff0000", () => 1),
      line("closedLong", (r) => (r.long ? r.long : null), (r) => css(r.longRgb || 0xff7f00), () => 2),
      line("closedShort", (r) => (r.short ? r.short : null), "#6a8f23", () => 1),
      line("p4", (r) => (r.side > 0 && r.keepMfeOn ? r.keepMfe : null), "#9aa0a6", () => 1),
      line("p5", (r) => (r.side > 0 && r.keepMaeOn ? r.keepMae : null), "#6b7280", () => 1),
      line("flips", (r) => (r.flips > 0 ? r.flips : null), "#b0b0b0", () => 1),
      line("danger", (r) => (r.danger > 0 ? r.danger : null), "#ff00ff", () => 2),
      line("total", (r) => (r.totalOn ? r.total : null), "#ff00ff", () => 2),
    ];
    const layers = {
      open: true, mfe: true, mae: true, exit: true, closedLong: true, closedShort: true,
      p4: true, p5: true, flips: true, danger: true, total: true,
    };
    const all = lines.map((ln) => ln.series);
    function setAxis(id) {
      const own = id === "right";
      const next = own ? "right" : scale;
      for (const s of all) {
        if (s.applyOptions) s.applyOptions({ priceScaleId: next });
      }
      if (typeof chart.priceScale === "function") {
        chart.priceScale(next).applyOptions({
          scaleMargins: { top: 0.78, bottom: 0 }, autoScale: true, visible: own,
        });
      }
    }
    setAxis(null);
    let ctx = null;
    function draw() {
      for (const ln of lines) {
        ln.on = () => layers[ln.layer];
        paintMarks(ln, ctx, (ind) => {
          const p = pickPos(ind);
          return p || null;
        });
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

  // 봉 색은 페인트바를 켰을 때만. 둘 이상이면 VolBurst, 1분봉용, 통합판정강조 순으로 하나만 쓴다.
  const BAR_PAINT_IDS = ["vb_paint", "fx_paint_os", "fx_paint_v12"];

  function barPaintOn(entry) {
    return !!entry && (typeof entry.handle?.wantsBar !== "function" || entry.handle.wantsBar());
  }

  function barPaintRgb(active, ind) {
    if (!active || !ind) return 0;
    if (barPaintOn(active.get("vb_paint")) && Number(ind.vb)) return Number(ind.vb) >>> 0;
    if (barPaintOn(active.get("fx_paint_os")) && Number(ind.os?.osPaint)) return Number(ind.os.osPaint) >>> 0;
    if (barPaintOn(active.get("fx_paint_v12")) && Number(ind.os?.paintRgb)) return Number(ind.os.paintRgb) >>> 0;
    return 0;
  }

  return {
    parse, judgeHandle, packHandle, flatHandle, flatHandleV2, adjHandle, adjHandleOf,
    ecHandle, sigHandle, pnlHandle, posHandle, paintHandle, swingHandle, orderName, exitName, pairLabel, groupName,
    labelMarks, BAR_PAINT_IDS, barPaintRgb,
  };
})();

if (typeof globalThis !== "undefined") globalThis.OsLayers = OsLayers;
