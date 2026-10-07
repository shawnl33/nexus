// #우드스탁_스나이퍼스코프_해외선물_CO_V3.
// 표현은 #우드스탁_스나이퍼스코프_해외선물_Data2 와 같다.
// 삼선 비율은 봉마다 색·굵기 점, 회귀·마켓·가격·거래량 비율은 같은 값만 가로선,
// 고정 높이 신호는 짧은 가로선, Plot29·30은 점.
// 눈금은 HTS처럼 0이 아래, 100이 위이고 그 밖 신호는 가장자리에 붙는다.
// Plot33(스코프종합상태, 굵기 0)은 그리지 않는다.
// 엔진 snco 배열은 [plot, value, rgb, width].
"use strict";

const SncoLayers = (() => {
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

  function shown(list, id) {
    const row = (list || []).find((p) => p.id === id);
    if (!row || !(row.width > 0)) return null;
    return row;
  }

  // Data2 scopeFrom 과 같은 칸. 없는 표시는 null.
  function viewFrom(list) {
    const p10 = shown(list, 10);
    const p21 = shown(list, 21);
    const p29 = shown(list, 29);
    const p30 = shown(list, 30);
    let signal = null;
    if (p29) {
      const rgb = p29.rgb >>> 0;
      signal = rgb === 0x008000 ? "both" : rgb === 0x0000ff ? "up" : "down";
    }
    let emphasis = null;
    if (p30) {
      const rgb = p30.rgb >>> 0;
      if (rgb === 0xffff00) emphasis = "yellow";
      else if (rgb === 0x00ffff) emphasis = "cyan";
      else if (rgb === 0xffffff) emphasis = "white";
    }
    const level = (id) => {
      const row = shown(list, id);
      return row ? row.value : null;
    };
    return {
      sam: p10 ? p10.value : null,
      color: p10 ? p10.rgb : 0xdcdcdc,
      samWidth: p10 ? p10.width : 0,
      reg: level(11),
      market: level(12),
      price: level(13),
      vol: level(31),
      posHi: p21 && p21.value > 0 ? p21.value : null,
      posLo: p21 && p21.value < 0 ? p21.value : null,
      up: level(22),
      down: level(23),
      compound: level(24),
      squeezeUp: level(25),
      squeezeDn: level(26),
      holdUp: level(27),
      holdDn: level(28),
      both: level(32),
      signal,
      signalY: p29 ? p29.value : null,
      emphasis,
      emphasisY: p30 ? p30.value : null,
    };
  }

  const MARKS = [
    { key: "posHi", layer: "range", color: "#0000ff" },
    { key: "posLo", layer: "range", color: "#ff0000" },
    { key: "up", layer: "range", color: "#b40000" },
    { key: "down", layer: "range", color: "#000096" },
    { key: "compound", layer: "range", color: "#ff8c00" },
    { key: "squeezeUp", layer: "range", color: "#ff00ff" },
    { key: "squeezeDn", layer: "range", color: "#00a0a0" },
    { key: "holdUp", layer: "scope", color: "#ff0000" },
    { key: "holdDn", layer: "scope", color: "#0000ff" },
    { key: "both", layer: "both", color: "#ff8c00" },
  ];
  const DOTS = [
    { key: "signal", y: "signalY", layer: "signal", match: "up", color: "#0000ff", radius: 3 },
    { key: "signal", y: "signalY", layer: "signal", match: "down", color: "#ff0000", radius: 3 },
    { key: "signal", y: "signalY", layer: "signal", match: "both", color: "#008000", radius: 3 },
    { key: "emphasis", y: "emphasisY", layer: "emphasis", match: "yellow", color: "#ffff00", radius: 2 },
    { key: "emphasis", y: "emphasisY", layer: "emphasis", match: "cyan", color: "#00ffff", radius: 2 },
    { key: "emphasis", y: "emphasisY", layer: "emphasis", match: "white", color: "#ffffff", radius: 2 },
  ];

  function createHandle(chart) {
    const layers = {
      three: true, reg: true, market: true, price: true, vol: true,
      signal: true, emphasis: true, range: true, scope: true, both: true,
    };
    const specs = [
      { layer: "three", key: "sam", color: "#d7dde8", width: 2 },
      { layer: "reg", key: "reg", color: "#c9a227", width: 2 },
      { layer: "market", key: "market", color: "#8ecae6", width: 2 },
      { layer: "price", key: "price", color: "#808080", width: 1 },
      { layer: "vol", key: "vol", color: "#0000ff", width: 1 },
    ];
    const lines = specs.map((d) => ({
      ...d,
      series: chart.addLineSeries({
        color: d.color,
        lineWidth: d.width,
        priceScaleId: "snco",
        priceLineVisible: false,
        lastValueVisible: false,
        lineVisible: false,
        pointMarkersVisible: false,
      }),
    }));
    for (const ln of lines) {
      const pen = ln.key === "sam"
        ? globalThis.HorizLines.dots(0.5)
        : globalThis.HorizLines.primitive(ln.color, ln.width);
      ln.series.attachPrimitive(pen);
      ln.pen = pen;
    }
    const marks = MARKS.map((d) => ({
      ...d,
      series: chart.addLineSeries({
        color: d.color,
        lineWidth: 1,
        priceScaleId: "snco",
        priceLineVisible: false,
        lastValueVisible: false,
        lineVisible: false,
      }),
    }));
    for (const mk of marks) {
      mk.series.attachPrimitive(globalThis.HorizLines.primitive(mk.color, 1));
    }
    const dots = DOTS.map((d) => ({
      ...d,
      series: chart.addLineSeries({
        color: d.color,
        lineWidth: 1,
        priceScaleId: "snco",
        priceLineVisible: false,
        lastValueVisible: false,
        lineVisible: false,
        pointMarkersVisible: true,
        pointMarkersRadius: d.radius,
      }),
    }));
    const all = [...lines, ...marks, ...dots];
    function setAxis(id) {
      // 봉이 없는 칸은 오른쪽 축이 이 지표 눈금이다. 숫자를 따로 붙이지 않는다.
      const scale = id || "snco";
      const ownAxis = scale === "right";
      for (const s of all) {
        if (!s.series.applyOptions) continue;
        s.series.applyOptions({
          priceScaleId: scale,
          autoscaleInfoProvider: () => ({
            priceRange: { minValue: -8, maxValue: 108 },
          }),
        });
      }
      if (typeof chart.priceScale === "function") {
        chart.priceScale(scale).applyOptions(ownAxis
          ? { scaleMargins: { top: 0.06, bottom: 0.06 }, autoScale: true, visible: true }
          : { scaleMargins: { top: 0.74, bottom: 0.02 }, autoScale: true });
      }
    }
    setAxis("snco");
    let cache = null;
    let synced = false;
    function rowAt(t) {
      return viewFrom(cache?.barInd?.get(t)?.snco);
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
              pts.push({ time: t, value: row.sam, color: css(row.color), width: row.samWidth });
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
        if (layers[mk.layer]) {
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
        if (layers[dot.layer]) {
          for (const t of seq) {
            const row = rowAt(t);
            const y = row ? row[dot.y] : null;
            if (row && row[dot.key] === dot.match && y != null) data.push({ time: t, value: y });
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
      for (const mk of marks) {
        if (!layers[mk.layer]) continue;
        const x = row ? row[mk.key] : null;
        mk.series.update(x != null ? { time: t, value: x } : { time: t });
      }
      for (const dot of dots) {
        if (!layers[dot.layer]) continue;
        {
          const y = row ? row[dot.y] : null;
          const on = row && row[dot.key] === dot.match && y != null;
          const last = lastOf(dot.series);
          if (on) dot.series.update({ time: t, value: y });
          else if (last && last.time === t && Number.isFinite(last.value)) dot.series.update({ time: t });
        }
      }
    }
    return {
      setLayers(map) {
        if (!map) return;
        let changed = false;
        if ("ratio" in map) {
          const on = !!map.ratio;
          for (const k of ["three", "reg", "market", "price", "vol", "signal", "emphasis"]) layers[k] = on;
          changed = true;
        }
        if ("marks" in map) {
          const on = !!map.marks;
          for (const k of ["range", "scope", "both"]) layers[k] = on;
          changed = true;
        }
        for (const k of Object.keys(layers)) {
          if (k in map) {
            layers[k] = !!map[k];
            changed = true;
          }
        }
        if (changed) rebuild();
      },
      applyLive(_p, c) {
        if (c) cache = c;
        if (!synced) { rebuild(); return; }
        liveTail();
      },
      setAxis,
      applySeed(c) { if (c) cache = c; rebuild(); },
      clear() {
        cache = null;
        synced = false;
        for (const ln of lines) {
          ln.pts = [];
          if (ln.pen && ln.pen.setPoints) ln.pen.setPoints([]);
          ln.series.setData([]);
        }
        for (const mk of marks) mk.series.setData([]);
        for (const dot of dots) dot.series.setData([]);
      },
      destroy() {
        for (const s of all) chart.removeSeries(s.series);
      },
    };
  }

  return { parse, css, viewFrom, createHandle };
})();

if (typeof globalThis !== "undefined") globalThis.SncoLayers = SncoLayers;
