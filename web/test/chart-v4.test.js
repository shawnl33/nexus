// 경량 차트 v5에는 addLineSeries가 없다. 지표는 v4 이름으로 시리즈를 만든다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/chart-v4.js");

test("addLineSeries는 LineSeries로 addSeries를 부른다", () => {
  const calls = [];
  const chart = {
    addSeries(ctor, options) {
      calls.push([ctor, options]);
      return { ok: true };
    },
    timeScale() { return "scale"; },
  };
  const wrapped = globalThis.v4Chart(chart, { LineSeries: "LINE", HistogramSeries: "HIST" });
  const series = wrapped.addLineSeries({ color: "#fff" });
  assert.equal(series.ok, true);
  assert.deepEqual(calls, [["LINE", { crosshairMarkerVisible: false, color: "#fff" }]]);
  wrapped.addHistogramSeries();
  assert.equal(calls[1][0], "HIST");
  assert.equal(calls[1][1].crosshairMarkerVisible, false);
  assert.equal(wrapped.timeScale(), "scale");
});
