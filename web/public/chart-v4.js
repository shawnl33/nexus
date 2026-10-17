// 경량 차트 v5는 addSeries(생성자)만 있다. 지표는 addLineSeries 같은 v4 이름을 쓴다.
"use strict";

const V4_SERIES = {
  addLineSeries: "LineSeries",
  addAreaSeries: "AreaSeries",
  addBarSeries: "BarSeries",
  addCandlestickSeries: "CandlestickSeries",
  addHistogramSeries: "HistogramSeries",
  addBaselineSeries: "BaselineSeries",
};

function v4Chart(chart, lib) {
  return new Proxy(chart, {
    get(target, prop) {
      const name = V4_SERIES[prop];
      if (name) {
        const ctor = lib[name];
        return (options) => target.addSeries(ctor, { crosshairMarkerVisible: false, ...(options || {}) });
      }
      if (prop === "addCustomSeries") {
        return (def, options) => target.addCustomSeries(def, { crosshairMarkerVisible: false, ...(options || {}) });
      }
      const value = target[prop];
      return typeof value === "function" ? value.bind(target) : value;
    },
  });
}

if (typeof globalThis !== "undefined") globalThis.v4Chart = v4Chart;
