// 해외선물 미래곡선 V1 렌더러 — 패널 매니저(app.js)의 렌더러 계약 구현체.
// 계산은 엔진(fx_mirae_v1)이 하고, 이 파일은 상태 스트림의 fx 키와
// 스냅샷 ind[32..56]을 barInd.fxMask/fx[24]로 읽는다.
// 지난 상승·하락의 최고·최저만 봉마다 +. 0.382/0.5/0.618은 그리지 않는다.
// 나머지 선은 값을 잇지 않고, 같은 값이 이어진 구간만 수평선으로 그린다.
// 브라우저에서는 전역 FxLayers, node:test에서는 globalThis.FxLayers.
"use strict";

const FxLayers = (() => {
  // Plot 순서 = 원본 #WSF_해외선물미래곡선V1 (docs/display_payload.md).
  // 색은 원본 RGB. 단계화는 가격 축을 누르지 않게 아래 스케일(fxscore)에 둔다.
  const PLOTS = [
    { layer: "score", scale: "fxscore", color: "#ff6464", width: 2 },
    { layer: "reg", color: "#d7dde8", width: 3 },
    { layer: "market", color: "#7eb6ff", width: 3 },
    { layer: "persist", color: "#6e6e6e", width: 2 },
    { layer: "persist", color: "#8c6428", width: 2 },
    { layer: "persist", color: "#965096", width: 2 },
    { layer: "persist", color: "#1e8c8c", width: 2 },
    { layer: "persist", color: "#5a46b4", width: 2 },
    { layer: "swingUp", color: "#ff7f00", width: 2 },
    { layer: "swingUp", color: "#ff7f00", width: 2 },
    { layer: "swingUp", color: "#ff7f00", width: 2 },
    { layer: "swingUp", color: "#ff7f00", width: 2 },
    { layer: "swingUp", color: "#ff7f00", width: 2 },
    { layer: "swingDn", color: "#008000", width: 2 },
    { layer: "swingDn", color: "#008000", width: 2 },
    { layer: "swingDn", color: "#008000", width: 2 },
    { layer: "swingDn", color: "#008000", width: 2 },
    { layer: "swingDn", color: "#008000", width: 2 },
    { layer: "synth", color: "#f0821e", width: 3 },
    { layer: "synth", color: "#f0821e", width: 1 },
    { layer: "synth", color: "#8c46be", width: 3 },
    { layer: "synth", color: "#8c46be", width: 1 },
    { layer: "synth", color: "#1482b4", width: 3 },
    { layer: "synth", color: "#1482b4", width: 1 },
  ];

  function shown(ind, k) {
    if (!ind) return false;
    const bit = (Number(ind.fxMask) >>> k) & 1;
    return bit === 1 && Number.isFinite(ind.fx?.[k]);
  }

  function horizontalPrimitive(color, width) {
    return globalThis.HorizLines.primitive(color, width);
  }

  // 차트 라이브러리에 + 모양이 없어서, 숨긴 선의 값 위에 가로·세로를 긋는다.
  const PLUS_ARM = 3;
  function plusPrimitive(color) {
    let series = null;
    let chart = null;
    return {
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
                if (!series || !chart) return;
                const points = typeof series.data === "function" ? series.data() : [];
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const arm = PLUS_ARM * hr;
                  ctx.beginPath();
                  ctx.strokeStyle = color;
                  ctx.lineWidth = 2 * hr;
                  for (const p of points) {
                    if (!p || !Number.isFinite(p.value)) continue;
                    const mx = chart.timeScale().timeToCoordinate(p.time);
                    const my = series.priceToCoordinate(p.value);
                    if (mx == null || my == null) continue;
                    const x = mx * hr;
                    const y = my * vr;
                    ctx.moveTo(x - arm, y);
                    ctx.lineTo(x + arm, y);
                    ctx.moveTo(x, y - arm);
                    ctx.lineTo(x, y + arm);
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

  const FxRenderer = {
    id: "fx_mirae_v1",
    createHandle(chart, candleSeries) {
      void candleSeries;
      const layers = {
        score: false, reg: true, market: true, persist: true,
        swingUp: true, swingDn: true, synth: true,
      };
      // 단계화·마켓중심은 같은 값만 수평으로 끊는다. 회귀선만 점과 점을 선분으로 잇는다.
      const stageOf = [
        { id: "p4", color: "#dc0000", test: (s) => s >= 4 },
        { id: "p2", color: "#ff6446", test: (s) => s >= 2 },
        { id: "p1", color: "#ffb9b9", test: (s) => s === 1 },
        { id: "n4", color: "#0000b4", test: (s) => s <= -4 },
        { id: "n2", color: "#3c82ff", test: (s) => s <= -2 },
        { id: "n1", color: "#b4d2ff", test: (s) => s === -1 },
        { id: "z", color: "#969696", test: () => true },
      ];
      function stageColor(stage) {
        const n = Number(stage);
        for (const s of stageOf) if (s.test(n)) return s.color;
        return "#969696";
      }
      function regWidth(r2) {
        const n = Number(r2);
        if (n >= 0.70) return 6;
        if (n >= 0.4) return 4;
        return 2;
      }
      const lines = PLOTS.map((d, k) => {
        if (k > 2) {
          // 0.382, 0.5, 0.618 은 십자 안쪽 실선이었다. 그리지 않는다.
          if (k === 10 || k === 11 || k === 12 || k === 15 || k === 16 || k === 17) {
            return { ...d, colored: false, skip: true, series: null };
          }
          const cross = d.layer === "swingUp" || d.layer === "swingDn";
          const series = chart.addLineSeries({
            color: d.color,
            lineWidth: d.width,
            priceScaleId: d.scale || "right",
            priceLineVisible: false,
            lastValueVisible: false,
            lineStyle: 0,
            lineType: 0,
            lineVisible: false,
            pointMarkersVisible: false,
          });
          series.attachPrimitive(cross ? plusPrimitive(d.color) : horizontalPrimitive(d.color, d.width));
          return { ...d, colored: false, series };
        }
        if (k === 1) {
          const series = chart.addLineSeries({
            color: d.color,
            lineWidth: d.width,
            priceScaleId: "right",
            priceLineVisible: false,
            lastValueVisible: false,
            lineVisible: false,
          });
          const pen = globalThis.HorizLines.segments();
          series.attachPrimitive(pen);
          return { ...d, reg: true, series, pen };
        }
        const series = {};
        for (const s of stageOf) {
          series[s.id] = chart.addLineSeries({
            color: s.color,
            lineWidth: d.width,
            priceScaleId: d.scale || "right",
            priceLineVisible: false,
            lastValueVisible: false,
            lineVisible: false,
          });
          series[s.id].attachPrimitive(horizontalPrimitive(s.color, d.width));
        }
        return { ...d, stage: true, series };
      });
      if (typeof chart.priceScale === "function") {
        chart.priceScale("fxscore").applyOptions({ scaleMargins: { top: 0.84, bottom: 0.02 } });
      }
      let ctx = null;
      let synced = false;

      function lastOf(series) {
        const raw = typeof series.data === "function" ? series.data() : series.data;
        return Array.isArray(raw) && raw.length ? raw[raw.length - 1] : null;
      }

      function rebuild() {
        lines.forEach((ln, k) => {
          if (ln.skip) return;
          if (ln.reg) {
            const pts = [];
            if (layers.reg && ctx) {
              for (const t of ctx.barSeq) {
                const ind = ctx.barInd.get(t);
                if (!shown(ind, k)) continue;
                pts.push({
                  time: t,
                  value: ind.fx[k],
                  color: stageColor(ind.fx?.[0]),
                  width: regWidth(ind.r2),
                });
              }
            }
            ln.pts = pts;
            ln.pen.setPoints(pts);
            ln.series.setData(pts.map((p) => ({ time: p.time, value: p.value })));
            return;
          }
          if (ln.stage) {
            const buckets = Object.fromEntries(stageOf.map((s) => [s.id, []]));
            if (layers[ln.layer] && ctx) {
              let open = null;
              for (const t of ctx.barSeq) {
                const ind = ctx.barInd.get(t);
                if (!shown(ind, k)) {
                  if (open) buckets[open].push({ time: t });
                  open = null;
                  continue;
                }
                const sid = stageOf.find((s) => s.test(Number(ind.fx?.[0]))).id;
                if (open && open !== sid) buckets[open].push({ time: t });
                buckets[sid].push({ time: t, value: ind.fx[k] });
                open = sid;
              }
            }
            for (const s of stageOf) ln.series[s.id].setData(buckets[s.id]);
            return;
          }
          {
            const data = [];
            if (layers[ln.layer] && ctx) {
              for (const t of ctx.barSeq) {
                const ind = ctx.barInd.get(t);
                data.push(shown(ind, k) ? { time: t, value: ind.fx[k] } : { time: t });
              }
            }
            ln.series.setData(data);
          }
        });
        synced = true;
      }

      // 시딩으로 깔아 둔 이력 뒤에 마지막 봉만 붙인다. 봉마다 전체를 setData 하면
      // 마커가 늘수록 틱 처리가 느려져 화면 시각이 서버보다 늦어진다.
      function liveTail() {
        const seq = ctx && ctx.barSeq;
        const t = seq && seq.length ? seq[seq.length - 1] : NaN;
        if (!Number.isFinite(t)) return;
        const ind = ctx.barInd.get(t);
        for (let k = 0; k < lines.length; k++) {
          const ln = lines[k];
          if (ln.skip || !ln.series) continue;
          if (ln.reg) {
            if (!layers.reg) continue;
            const pts = ln.pts || (ln.pts = []);
            const last = pts[pts.length - 1];
            if (last && last.time > t) { synced = false; rebuild(); return; }
            if (shown(ind, k)) {
              const p = {
                time: t, value: ind.fx[k],
                color: stageColor(ind.fx?.[0]), width: regWidth(ind.r2),
              };
              if (last && last.time === t) pts[pts.length - 1] = p;
              else pts.push(p);
              ln.series.update({ time: t, value: p.value });
            } else {
              if (last && last.time === t) pts.pop();
              ln.series.update({ time: t });
            }
            ln.pen.setPoints(pts);
            continue;
          }
          if (ln.stage) {
            if (!layers[ln.layer]) continue;
            const on = shown(ind, k);
            const sid = on ? stageOf.find((s) => s.test(Number(ind.fx?.[0]))).id : null;
            for (const s of stageOf) {
              const series = ln.series[s.id];
              const last = lastOf(series);
              if (s.id === sid) series.update({ time: t, value: ind.fx[k] });
              else if (last && last.time === t && Number.isFinite(last.value)) series.update({ time: t });
            }
            continue;
          }
          if (!layers[ln.layer]) continue;
          if (shown(ind, k)) ln.series.update({ time: t, value: ind.fx[k] });
          else ln.series.update({ time: t });
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
        applyLive(_p, c) {
          ctx = c;
          if (!synced) { rebuild(); return; }
          liveTail();
        },
        applySeed(c) {
          ctx = c;
          rebuild();
        },
        clear() {
          synced = false;
          for (const ln of lines) {
            if (ln.skip) continue;
            if (ln.reg) { ln.pts = []; ln.pen.setPoints([]); }
            if (ln.stage) for (const s of Object.values(ln.series)) s.setData([]);
            else ln.series.setData([]);
          }
        },
        destroy() {
          for (const ln of lines) {
            if (ln.skip) continue;
            if (ln.stage) for (const s of Object.values(ln.series)) chart.removeSeries(s);
            else chart.removeSeries(ln.series);
          }
        },
      };
    },
  };

  return { PLOTS, FxRenderer };
})();

if (typeof globalThis !== "undefined") globalThis.FxLayers = FxLayers;
