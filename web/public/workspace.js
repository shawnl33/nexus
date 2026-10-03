// 화면틀 v2 직렬화/파싱 — DOM 없는 순수 함수 (node:test 단위 테스트 대상).
// 화면틀에는 레이아웃·칸별 지표 집합(레이어 설정 포함)·칸별 종목 바인딩만 저장한다
// (계획서 §18: 전략 자동 시작·주문 상태는 넣지 않는다).
// 문서는 열 수·열 너비·화면틀 목록이다. frames가 없는 스키마 2는 화면틀 하나, 열 하나다.
// 브라우저에서는 전역 Workspace, node:test에서는 globalThis.Workspace로 쓴다.
"use strict";

const Workspace = (() => {
  const SCHEMA_VERSION = 2;
  const MIN_HEIGHT = 0.1; // 칸 최소 높이 비율 (app.js MIN_PANE_FRAC와 동일하게 유지)

  // 합이 1. 비유한 값은 0으로 친다. 합이 0 이하면 균등. 빈 배열은 빈 배열.
  function normalizeWeights(weights) {
    if (!weights.length) return [];
    const nums = weights.map((w) => Number(w));
    const total = nums.reduce((s, w) => s + (Number.isFinite(w) ? w : 0), 0);
    if (!(total > 0)) return nums.map(() => 1 / nums.length);
    return nums.map((w) => (Number.isFinite(w) ? w : 0) / total);
  }

  // 합을 1로 맞춘 뒤 각 몫에 n/(n+1)을 곱하고 1/(n+1)을 붙인다. 빈 배열은 [1]에서 시작한다.
  function scaleAdd(weights) {
    const base = normalizeWeights(weights.length ? weights : [1]);
    const n = base.length;
    const factor = n / (n + 1);
    return [...base.map((w) => w * factor), 1 / (n + 1)];
  }

  // 행이 둘 이상이면 그 행, 행이 하나이고 열이 둘 이상이면 그 열, 1×1은 닫지 않는다.
  function frameClose(rowCount, colCount) {
    if (rowCount >= 2) return "row";
    if (rowCount === 1 && colCount >= 2) return "col";
    return "none";
  }

  function equalWeights(cols) {
    return Array.from({ length: cols }, () => 1 / cols);
  }

  // 길이·양수·합이 아니면 균등. 쓸 수 있으면 합이 1이 되도록 나눈다.
  function weightsOrEqual(given, cols) {
    if (!Array.isArray(given) || given.length !== cols) return equalWeights(cols);
    const nums = given.map((w) => Number(w));
    if (nums.some((w) => !(w > 0))) return equalWeights(cols);
    const total = nums.reduce((s, w) => s + w, 0);
    if (!(total > 0)) return equalWeights(cols);
    return nums.map((w) => w / total);
  }

  function clampHeight(h) {
    const n = Number(h);
    return Number.isFinite(n) ? Math.min(1, Math.max(MIN_HEIGHT, n)) : 1;
  }

  function panelRecord(p) {
    return {
      height: p.height,
      symbol: typeof p.symbol === "string" ? p.symbol : "",
      panelOpen: p.panelOpen !== false, // 지표 패널 접힘만 false로 남긴다 (기본 열림)
      data2: typeof p.data2 === "string" ? p.data2 : "",
      candles: p.barStyle ? p.barStyle !== "none" : p.candles !== false,
      barStyle: ["none", "candle", "outline", "bar", "line"].includes(p.barStyle)
        ? p.barStyle
        : (p.candleBody === "outline" ? "outline" : (p.candles === false ? "none" : "candle")),
      indicators: (p.indicators ?? []).map((i) => ({
        id: i.id,
        layers: { ...(i.layers ?? {}) },
      })),
    };
  }

  // panels[i].symbol이 없는 구 화면틀은 current_symbol로 폴백한다.
  // panelOpen이 없으면 열림. 결과가 0칸이면 빈 차트 하나.
  function readPanels(panels, fallbackSymbol, isKnownIndicator) {
    const out = [];
    for (const p of Array.isArray(panels) ? panels : []) {
      const height = clampHeight(p?.height);
      const symbol = typeof p?.symbol === "string" ? p.symbol : fallbackSymbol;
      const indicators = [];
      for (const i of Array.isArray(p?.indicators) ? p.indicators : []) {
        if (!i || typeof i.id !== "string") continue;
        if (isKnownIndicator && !isKnownIndicator(i.id)) continue;
        const layers = {};
        for (const [k, v] of Object.entries(i.layers ?? {})) layers[k] = !!v;
        indicators.push({ id: i.id, layers });
      }
      const data2 = typeof p?.data2 === "string" ? p.data2 : "";
      let barStyle = ["none", "candle", "outline", "bar", "line"].includes(p?.barStyle)
        ? p.barStyle
        : (p?.candles === false ? "none" : "candle");
      if (barStyle === "candle" && p?.candleBody === "outline") barStyle = "outline";
      out.push({
        height, symbol, panelOpen: p?.panelOpen !== false, data2,
        candles: barStyle !== "none", barStyle, indicators,
      });
    }
    if (out.length === 0) {
      out.push({ height: 1, symbol: fallbackSymbol, panelOpen: true, data2: "", candles: true, barStyle: "candle", indicators: [] });
    }
    return out;
  }

  // frames: [{ height, panels }]. grid를 생략하면 cols 1, colWeights [1].
  // current_symbol은 첫 화면틀의 첫 칸 종목이다 (구 독자 호환).
  function serialize(name, frames, grid) {
    const cols = Number.isInteger(grid?.cols) && grid.cols >= 1 ? grid.cols : 1;
    const colWeights = weightsOrEqual(grid?.colWeights, cols);
    const outFrames = (frames ?? []).map((f) => ({
      height: f?.height,
      panels: (f?.panels ?? []).map((p) => panelRecord(p)),
    }));
    return {
      schema_version: SCHEMA_VERSION,
      name,
      current_symbol: outFrames[0]?.panels?.[0]?.symbol ?? "",
      cols,
      colWeights,
      frames: outFrames,
    };
  }

  // v2 검증·정규화. 구 스키마(schema_version != 2)면 null을 돌려준다.
  // frames가 비어 있지 않으면 그 격자를 읽고, 아니면 문서 panels를 화면틀 하나의 panels로 읽는다.
  // 결과: { symbol, cols, colWeights, frames: [{ height, panels }] }
  function parse(data, isKnownIndicator) {
    if (!data || data.schema_version !== SCHEMA_VERSION) return null;
    const fallbackSymbol = typeof data.current_symbol === "string" ? data.current_symbol : "";
    const useFrames = Array.isArray(data.frames) && data.frames.length > 0;
    const raw = useFrames
      ? data.frames.map((f) => ({ height: f?.height, panels: readPanels(f?.panels, fallbackSymbol, isKnownIndicator) }))
      : [{ height: 1, panels: readPanels(data.panels, fallbackSymbol, isKnownIndicator) }];
    let cols = Number.isInteger(data.cols) && data.cols >= 1 ? data.cols : 1;
    let keepWeights = true;
    if (raw.length % cols !== 0) {
      cols = 1;
      keepWeights = false;
    }
    const frames = [];
    const rows = raw.length / cols;
    for (let r = 0; r < rows; r++) {
      const height = clampHeight(raw[r * cols]?.height);
      for (let c = 0; c < cols; c++) frames.push({ height, panels: raw[r * cols + c].panels });
    }
    return {
      symbol: fallbackSymbol,
      cols,
      colWeights: keepWeights ? weightsOrEqual(data.colWeights, cols) : equalWeights(cols),
      frames,
    };
  }

  return { SCHEMA_VERSION, MIN_HEIGHT, serialize, parse, normalizeWeights, scaleAdd, frameClose };
})();

if (typeof globalThis !== "undefined") {
  globalThis.Workspace = Workspace;
}
