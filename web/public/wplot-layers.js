// 위클리 합산수익률·프라이스링크. 값은 엔진 pair.plots가 넣는다.
// 합산수익률은 HTS처럼 0에서 선 막대다. 영점기준은 자홍 수평선이다.
// 영점기준은 상대·선물이 없어도 자기 봉 구간에 그린다. 하단 별도 칸이다.
// 그리지 않은 봉은 빼 둔다.
// 브라우저에서는 전역 WplotLayers, node:test에서는 globalThis.WplotLayers.
"use strict";

const WplotLayers = (() => {
  function pushAsc(out, time, value) {
    if (!Number.isFinite(time) || !Number.isFinite(value)) return;
    if (out.length && time <= out[out.length - 1].time) return;
    out.push({ time, value });
  }

  function expandRuns(runs) {
    const out = [];
    if (!Array.isArray(runs)) return out;
    for (const run of runs) {
      if (!Array.isArray(run) || run.length < 2) continue;
      const t0 = Number(run[0]);
      if (!Number.isFinite(t0)) continue;
      for (let i = 1; i < run.length; i++) pushAsc(out, t0 + (i - 1) * 60, Number(run[i]));
    }
    return out;
  }

  function expandSpans(spans) {
    const out = [];
    if (!Array.isArray(spans)) return out;
    for (const sp of spans) {
      if (!Array.isArray(sp) || sp.length < 3) continue;
      const t0 = Number(sp[0]);
      const t1 = Number(sp[1]);
      const value = Number(sp[2]);
      pushAsc(out, t0, value);
      if (t1 > t0) pushAsc(out, t1, value);
    }
    return out;
  }

  // 가격 눈금 아래 약 28%를 지표 칸으로 비운다. 네 지표가 그 칸을 같이 쓴다.
  const PANE = { top: 0.74, bottom: 0.02 };
  const SPECS = {
    w_ret_long: {
      scale: "wret",
      margins: PANE,
      lines: [
        { id: "ret", kind: "run", key: "lr", plot: "histogram", color: "#4040a0", title: "첫만남합산수익률" },
        { id: "zero", kind: "zero", plot: "line", color: "#fc40fc", title: "영점기준" },
      ],
    },
    w_ret_short: {
      scale: "wret",
      margins: PANE,
      lines: [
        { id: "ret", kind: "run", key: "sr", plot: "histogram", color: "#4040a0", title: "양매도합산수익률" },
        { id: "zero", kind: "zero", plot: "line", color: "#fc40fc", title: "영점기준" },
      ],
    },
    // 가격 눈금 위의 수평 점선. HTS Plot1(D3_첫만남가격)과 같은 자리다.
    w_link_long: {
      scale: "right",
      lineStyle: 1,
      lines: [{ id: "link", kind: "span", key: "ll", color: "#f08c00", title: "D3_첫만남가격" }],
    },
    w_link_short: {
      scale: "right",
      lineStyle: 1,
      lines: [{ id: "link", kind: "span", key: "sl", color: "#1c7ed6", title: "D3_첫만남가격" }],
    },
  };

  // 가격 눈금만 값을 100으로 나눈다. 합산수익률 눈금은 수식 값 그대로다.
  function chartPoints(points, onPriceScale) {
    return points.map((p) => ({
      time: p.time,
      value: onPriceScale ? p.value * 100 : p.value,
    }));
  }

  function pointsFor(specLine, layers, payload, own) {
    if (!layers[specLine.id]) return [];
    if (specLine.kind === "zero") {
      const src = own && Number.isFinite(Number(own.t0)) && Number(own.t0) > 0 ? own : payload;
      if (!src) return [];
      const t0 = Number(src.t0);
      const t1 = Number(src.t1);
      if (!Number.isFinite(t0) || t0 <= 0) return [];
      const out = [];
      pushAsc(out, t0, 0);
      if (Number.isFinite(t1)) pushAsc(out, t1, 0);
      return out;
    }
    if (!payload) return [];
    if (specLine.kind === "span") return expandSpans(payload[specLine.key]);
    return expandRuns(payload[specLine.key]);
  }

  // 지표가 켜진 동안 가격 봉을 위로 올려 하단 칸을 비운다. 끄면 원래 여백으로 되돌린다.
  const rightPad = new WeakMap();
  function holdRight(chart) {
    if (typeof chart.priceScale !== "function") return;
    let slot = rightPad.get(chart);
    if (!slot) {
      const cur = chart.priceScale("right").options();
      slot = {
        n: 0,
        margins: {
          top: cur?.scaleMargins?.top ?? 0.2,
          bottom: cur?.scaleMargins?.bottom ?? 0.1,
        },
      };
      rightPad.set(chart, slot);
    }
    if (slot.n === 0) {
      chart.priceScale("right").applyOptions({ scaleMargins: { top: 0.06, bottom: 0.3 } });
    }
    slot.n += 1;
  }
  function releaseRight(chart) {
    const slot = rightPad.get(chart);
    if (!slot || typeof chart.priceScale !== "function") return;
    slot.n -= 1;
    if (slot.n > 0) return;
    rightPad.delete(chart);
    chart.priceScale("right").applyOptions({ scaleMargins: slot.margins });
  }

  function addPlotSeries(chart, spec, ln) {
    const common = {
      color: ln.color,
      priceScaleId: spec.scale,
      priceLineVisible: false,
      title: ln.title,
    };
    if (ln.plot === "histogram") {
      return chart.addHistogramSeries({
        ...common,
        base: 0,
        lastValueVisible: true,
        priceFormat: { type: "price", precision: 2, minMove: 0.01 },
      });
    }
    return chart.addLineSeries({
      ...common,
      lineWidth: 1,
      lineStyle: spec.lineStyle || 0,
      lastValueVisible: spec.scale !== "right" && ln.kind !== "zero",
    });
  }

  function create(id) {
    const spec = SPECS[id];
    return {
      id,
      createHandle(chart) {
        const layers = {};
        for (const ln of spec.lines) layers[ln.id] = true;
        const held = spec.scale !== "right";
        if (held) holdRight(chart);
        const series = spec.lines.map((ln) => ({
          ...ln,
          series: addPlotSeries(chart, spec, ln),
        }));
        if (spec.margins && typeof chart.priceScale === "function") {
          chart.priceScale(spec.scale).applyOptions({ scaleMargins: spec.margins });
        }
        let payload = null;
        let own = null;
        function rebuild() {
          for (const ln of series) {
            try {
              ln.series.setData(chartPoints(pointsFor(ln, layers, payload, own), spec.scale === "right"));
            } catch (err) {
              console.error(err);
              ln.series.setData([]);
            }
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
          setPlots(data) {
            payload = data;
            rebuild();
          },
          applyLive() {},
          applySeed(ctx) {
            const seq = ctx && Array.isArray(ctx.barSeq) ? ctx.barSeq : null;
            own = seq && seq.length ? { t0: Number(seq[0]), t1: Number(seq[seq.length - 1]) } : null;
            rebuild();
          },
          clear() {
            payload = null;
            own = null;
            rebuild();
          },
          destroy() {
            for (const ln of series) {
              try {
                chart.removeSeries(ln.series);
              } catch (err) {
                console.error(err);
              }
            }
            if (held) releaseRight(chart);
          },
        };
      },
    };
  }

  return {
    expandRuns,
    expandSpans,
    chartPoints,
    pointsFor,
    specFor(id) {
      return SPECS[id] || null;
    },
    RetLong: create("w_ret_long"),
    RetShort: create("w_ret_short"),
    LinkLong: create("w_link_long"),
    LinkShort: create("w_link_short"),
  };
})();

if (typeof globalThis !== "undefined") globalThis.WplotLayers = WplotLayers;
