// 체결이 없는 분은 데이터 없음이 아니다. 균일 분 그리드의 빈 칸은 그 분에 봉이 없다는
// 뜻이라 사선과 "데이터 없음"을 그리지 않는다. 가격도 잇지 않는다.
// 마지막 봉 오른쪽의 빈 차트 여백은 시리즈 항목이 아니므로 여기서 다루지 않는다.
"use strict";

const GapShade = (() => {
  const LABEL = "";
  const LABEL_MIN_PX = 40;

  // series.data()는 whitespace를 빼서 돌려준다. 논리 인덱스는 빈 칸을 유지하므로
  // 그 칸을 가격 없는 항목으로 바꿔 whitespaceRuns에 넘긴다.
  function logicalRows(series) {
    if (!series || typeof series.barsInLogicalRange !== "function" || typeof series.dataByIndex !== "function") {
      return [];
    }
    const span = 1000000;
    const info = series.barsInLogicalRange({ from: 0, to: span });
    if (!info || !Number.isFinite(info.barsAfter)) return [];
    const last = span + info.barsAfter;
    if (last < 0 || last > span) return [];
    const rows = [];
    for (let i = 0; i <= last; i++) {
      const point = series.dataByIndex(i);
      const priced = point != null && (point.open != null || point.close != null || point.value != null);
      rows.push(priced ? { time: i, open: 0 } : { time: i });
    }
    return rows;
  }

  function primitive() {
    let chart = null;
    let series = null;
    // 사선은 배경 다음, 봉보다 먼저 그린다. 위에 두면 다른 시리즈의 봉을 덮는다.
    // 글자는 맨 위에 두어 봉과 격자가 있어도 읽힌다.
    const shadeView = {
      zOrder() {
        return "bottom";
      },
      renderer() {
        return {
          draw(target) {
            paint(target, false);
          },
        };
      },
    };
    const labelView = {
      zOrder() {
        return "top";
      },
      renderer() {
        return {
          draw(target) {
            paint(target, true);
          },
        };
      },
    };

    function paint(target, labels) {
      if (!chart || !series || typeof Gaps === "undefined") return;
      const opt = typeof series.options === "function" ? series.options() : null;
      if (opt && opt.visible === false) return;
      const runs = Gaps.whitespaceRuns(logicalRows(series));
      if (!runs.length) return;
      const ts = chart.timeScale();
      const spacing = ts.options().barSpacing;
      target.useBitmapCoordinateSpace((scope) => {
        const ctx = scope.context;
        const hr = scope.horizontalPixelRatio;
        const vr = scope.verticalPixelRatio;
        const viewW = scope.mediaSize.width;
        const height = scope.bitmapSize.height;
        for (const run of runs) {
          const x1 = ts.logicalToCoordinate(run.start);
          const x2 = ts.logicalToCoordinate(run.end);
          if (x1 == null || x2 == null) continue;
          const left = x1 - spacing / 2;
          const right = x2 + spacing / 2;
          if (right <= 0 || left >= viewW) continue;
          const L = Math.round(left * hr);
          const R = Math.round(right * hr);
          if (!labels || !LABEL) {
            continue;
          }
          const visL = Math.max(left, 0);
          const visR = Math.min(right, viewW);
          if (visR - visL < LABEL_MIN_PX) continue;
          const cx = Math.round(((visL + visR) / 2) * hr);
          const cy = Math.round(height / 2);
          ctx.save();
          ctx.fillStyle = "rgba(209, 212, 220, 0.9)";
          ctx.font = `${Math.round(12 * vr)}px system-ui, sans-serif`;
          ctx.textAlign = "center";
          ctx.textBaseline = "middle";
          ctx.fillText(LABEL, cx, cy);
          ctx.restore();
        }
      });
    }

    return {
      attached(param) {
        chart = param.chart;
        series = param.series;
      },
      detached() {
        chart = null;
        series = null;
      },
      updateAllViews() {},
      paneViews() {
        return [shadeView, labelView];
      },
    };
  }

  return { primitive, LABEL, LABEL_MIN_PX };
})();

if (typeof globalThis !== "undefined") {
  globalThis.GapShade = GapShade;
}
