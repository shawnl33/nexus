// 선 시리즈의 연결선을 끄고, 같은 값이 이어진 구간만 수평선으로 그린다.
// 값이 바뀌거나 봉이 비면 잇지 않는다.
"use strict";

const HorizLines = (() => {
  function pointsOf(series) {
    const raw = series.data;
    if (typeof raw === "function") return raw.call(series) || [];
    return Array.isArray(raw) ? raw : [];
  }

  function styleOf(series, color, width) {
    const opt = typeof series.options === "function" ? series.options() : series.options;
    const nextColor = opt && opt.color ? opt.color : color;
    const nextWidth = opt && Number.isFinite(opt.lineWidth) ? opt.lineWidth : width;
    return { color: nextColor, width: nextWidth };
  }

  function primitive(color, width) {
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
                const points = pointsOf(series);
                let step = 60;
                for (let i = 1; i < points.length; i++) {
                  const d = points[i].time - points[i - 1].time;
                  if (d > 0 && d < step) step = d;
                }
                const runs = [];
                let run = null;
                let prev = null;
                for (const p of points) {
                  if (!p || !Number.isFinite(p.value)) {
                    run = null;
                    prev = null;
                    continue;
                  }
                  const adjacent = run && prev && (p.time - prev.time) <= step * 1.5 && p.value === run.value;
                  if (!adjacent) {
                    run = { t0: p.time, t1: p.time, value: p.value };
                    runs.push(run);
                  } else {
                    run.t1 = p.time;
                  }
                  prev = p;
                }
                const style = styleOf(series, color, width);
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const xOf = (t) => chart.timeScale().timeToCoordinate(t);
                  ctx.beginPath();
                  ctx.strokeStyle = style.color;
                  ctx.lineWidth = Math.max(1, style.width) * hr;
                  for (const seg of runs) {
                    const x0m = xOf(seg.t0);
                    const y = series.priceToCoordinate(seg.value);
                    if (x0m == null || y == null) continue;
                    const x0 = x0m * hr;
                    const py = y * vr;
                    let x1;
                    if (seg.t0 === seg.t1) {
                      const xNext = xOf(seg.t0 + step);
                      const span = xNext == null ? 8 : (xNext - x0m) * 0.8;
                      x1 = x0 + span * hr;
                    } else {
                      const x1m = xOf(seg.t1);
                      if (x1m == null) continue;
                      x1 = x1m * hr;
                    }
                    ctx.moveTo(x0, py);
                    ctx.lineTo(x1, py);
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

  // 회귀선. 이전 점과 이번 점을 한 선분으로 잇는다. 색과 굵기는 이번 봉 것이다.
  function segments() {
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
                if (!series || !chart || points.length < 2) return;
                const segs = [];
                for (let i = 1; i < points.length; i++) {
                  const a = points[i - 1];
                  const b = points[i];
                  segs.push({ t0: a.time, v0: a.value, t1: b.time, v1: b.value, color: b.color, width: b.width });
                }
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const xOf = (t) => chart.timeScale().timeToCoordinate(t);
                  for (const seg of segs) {
                    const x0 = xOf(seg.t0);
                    const x1 = xOf(seg.t1);
                    const y0 = series.priceToCoordinate(seg.v0);
                    const y1 = series.priceToCoordinate(seg.v1);
                    if (x0 == null || x1 == null || y0 == null || y1 == null) continue;
                    ctx.beginPath();
                    ctx.strokeStyle = seg.color;
                    ctx.lineCap = "butt";
                    ctx.lineJoin = "round";
                    ctx.lineWidth = Math.max(1, seg.width) * vr;
                    ctx.moveTo(x0 * hr, y0 * vr);
                    ctx.lineTo(x1 * hr, y1 * vr);
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

  // 같은 값·같은 색이 이어진 구간만 수평선. 값이 바뀌면 잇지 않는다.
  function flat() {
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
                if (!series || !chart || points.length < 2) return;
                let step = 60;
                for (let i = 1; i < points.length; i++) {
                  const d = points[i].time - points[i - 1].time;
                  if (d > 0 && d < step) step = d;
                }
                const segs = [];
                for (let i = 1; i < points.length; i++) {
                  const a = points[i - 1];
                  const b = points[i];
                  if (!Number.isFinite(a.value) || !Number.isFinite(b.value)) continue;
                  if ((b.time - a.time) > step * 1.5) continue;
                  if (a.value !== b.value || a.color !== b.color) continue;
                  segs.push({ t0: a.time, t1: b.time, v: a.value, color: b.color, width: b.width });
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
                    ctx.strokeStyle = seg.color;
                    ctx.lineCap = "butt";
                    ctx.lineJoin = "round";
                    ctx.lineWidth = Math.max(1, seg.width) * vr;
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

  const PLUS_ARM = 3;
  function plus(color) {
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
                if (!series || !chart) return;
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const arm = PLUS_ARM * hr;
                  const xOf = (t) => chart.timeScale().timeToCoordinate(t);
                  for (const p of points) {
                    if (!p || !Number.isFinite(p.value)) continue;
                    const mx = xOf(p.time);
                    const my = series.priceToCoordinate(p.value);
                    if (mx == null || my == null) continue;
                    const x = mx * hr;
                    const y = my * vr;
                    ctx.beginPath();
                    ctx.strokeStyle = p.color || color;
                    ctx.lineWidth = 2 * hr;
                    ctx.moveTo(x - arm, y);
                    ctx.lineTo(x + arm, y);
                    ctx.moveTo(x, y - arm);
                    ctx.lineTo(x, y + arm);
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

  // 봉마다 채운 원. 지름은 굵기. 점 사이는 잇지 않는다.
  function dots() {
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
                if (!series || !chart) return;
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const xOf = (t) => chart.timeScale().timeToCoordinate(t);
                  for (const p of points) {
                    if (!p || !Number.isFinite(p.value)) continue;
                    const mx = xOf(p.time);
                    const my = series.priceToCoordinate(p.value);
                    if (mx == null || my == null) continue;
                    const radius = Math.max(1, p.width || 1) * 0.5 * hr;
                    ctx.beginPath();
                    ctx.fillStyle = p.color || "#d7dde8";
                    ctx.arc(mx * hr, my * vr, radius, 0, Math.PI * 2);
                    ctx.fill();
                  }
                });
              },
            };
          },
        }];
      },
    };
  }

  // 채운 삼각 화살표. up은 위, down은 아래. 점 사이는 잇지 않는다.
  // each가 켜지면 값이 있는 봉마다 그린다. 꺼지면 연속 구간의 첫 봉만 그린다.
  function arrows(direction, color, each) {
    const H = 7;
    const W = 5;
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
                const points = pointsOf(series);
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  const hr = scope.horizontalPixelRatio;
                  const vr = scope.verticalPixelRatio;
                  const h = H * vr;
                  const w = W * hr;
                  const xOf = (t) => chart.timeScale().timeToCoordinate(t);
                  const paint = styleOf(series, color, 1).color;
                  let prevOn = false;
                  for (const p of points) {
                    const on = !!(p && Number.isFinite(p.value));
                    const start = on && (each || !prevOn);
                    prevOn = on;
                    if (!start) continue;
                    const mx = xOf(p.time);
                    const my = series.priceToCoordinate(p.value);
                    if (mx == null || my == null) continue;
                    const x = mx * hr;
                    const y = my * vr;
                    ctx.beginPath();
                    ctx.fillStyle = p.color || paint;
                    if (direction === "down") {
                      ctx.moveTo(x, y + h);
                      ctx.lineTo(x - w, y - h);
                      ctx.lineTo(x + w, y - h);
                    } else {
                      ctx.moveTo(x, y - h);
                      ctx.lineTo(x - w, y + h);
                      ctx.lineTo(x + w, y + h);
                    }
                    ctx.closePath();
                    ctx.fill();
                  }
                });
              },
            };
          },
        }];
      },
    };
  }

  return { primitive, segments, flat, plus, dots, arrows };
})();

if (typeof globalThis !== "undefined") globalThis.HorizLines = HorizLines;
