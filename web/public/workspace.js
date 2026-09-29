// 화면틀 v2 직렬화/파싱 — DOM 없는 순수 함수 (node:test 단위 테스트 대상).
// 화면틀에는 레이아웃·칸별 지표 집합(레이어 설정 포함)·종목 바인딩만 저장한다
// (계획서 §18: 전략 자동 시작·주문 상태는 넣지 않는다).
// 브라우저에서는 전역 Workspace, node:test에서는 globalThis.Workspace로 쓴다.
"use strict";

const Workspace = (() => {
  const SCHEMA_VERSION = 2;
  const MIN_HEIGHT = 0.1; // 칸 최소 높이 비율 (app.js MIN_PANE_FRAC와 동일하게 유지)

  // panesState: [{ height, indicators: [{ id, layers: { layerId: bool } }] }]
  function serialize(name, symbol, panesState) {
    return {
      schema_version: SCHEMA_VERSION,
      name,
      current_symbol: symbol,
      panels: (panesState ?? []).map((p) => ({
        height: p.height,
        indicators: (p.indicators ?? []).map((i) => ({
          id: i.id,
          layers: { ...(i.layers ?? {}) },
        })),
      })),
    };
  }

  // v2 검증·정규화. 구 스키마(schema_version != 2)면 null을 돌려준다.
  // isKnownIndicator(id): 알 수 없는 지표는 걸러낸다 (엔진/프론트 버전 차이 흡수).
  // 결과: { symbol, panels: [{ height, indicators: [{ id, layers }] }] } — panels는 최소 1칸.
  function parse(data, isKnownIndicator) {
    if (!data || data.schema_version !== SCHEMA_VERSION) return null;
    const panels = [];
    for (const p of Array.isArray(data.panels) ? data.panels : []) {
      const h = Number(p?.height);
      const height = Number.isFinite(h) ? Math.min(1, Math.max(MIN_HEIGHT, h)) : 1;
      const indicators = [];
      for (const i of Array.isArray(p?.indicators) ? p.indicators : []) {
        if (!i || typeof i.id !== "string") continue;
        if (isKnownIndicator && !isKnownIndicator(i.id)) continue;
        const layers = {};
        for (const [k, v] of Object.entries(i.layers ?? {})) layers[k] = !!v;
        indicators.push({ id: i.id, layers });
      }
      panels.push({ height, indicators });
    }
    if (panels.length === 0) panels.push({ height: 1, indicators: [] }); // 빈 화면틀 = 맨 차트 1칸
    return {
      symbol: typeof data.current_symbol === "string" ? data.current_symbol : "",
      panels,
    };
  }

  // 저장된 종목 바인딩 복원: 전환 콜백(switchSymbol)은 입력창 값을 읽으므로
  // 반드시 입력창을 저장 종목으로 맞춘 뒤 호출한다. 전환 불필요(같은 종목/저장값 없음)면 false.
  function restoreSymbol(parsed, input, switchSymbol) {
    const target = typeof parsed?.symbol === "string" ? parsed.symbol.trim() : "";
    if (!target || target === input.value.trim()) return false;
    input.value = target;
    switchSymbol();
    return true;
  }

  return { SCHEMA_VERSION, MIN_HEIGHT, serialize, parse, restoreSymbol };
})();

if (typeof globalThis !== "undefined") {
  globalThis.Workspace = Workspace;
}
