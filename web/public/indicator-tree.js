// 칸 지표 패널의 트리 구성 — DOM 없는 순수 함수 (node:test 단위 테스트 대상).
// 매니페스트(엔진 스냅샷의 indicators 배열)의 지표를 성격별 카테고리로 묶는
// 프론트 매핑만 담당한다. 체크 상태(pane.active·layers)는 app.js가 트리를
// 그릴 때 칸 상태에서 직접 읽으므로 이 모듈은 정적 구조만 만든다.
// 브라우저에서는 전역 IndicatorTree, node:test에서는 globalThis.IndicatorTree로 쓴다.
"use strict";

const IndicatorTree = (() => {
  // 카테고리 프론트 매핑: 새 지표가 늘어나면 해당 카테고리의 members에 id를 추가한다.
  // 어떤 카테고리에도 속하지 않는 지표는 마지막 카테고리("기타")에 모인다 —
  // CATEGORIES의 마지막 항목이 항상 미분류 기본 그룹이어야 한다.
  const CATEGORIES = [
    { id: "mirae", name: "미래곡선", members: ["mirae_v16", "fx_mirae_v1", "fx_mirae_v3", "fx_curve_os", "fx_judge_v3", "fx_pack_v4"] },
    { id: "pgap", name: "지속목표차", members: ["fx_pgap3", "fx_pgap5", "fx_rgap", "fx_mgap", "fx_ugap"] },
    { id: "ymae", name: "양매수", members: ["fx_ymae", "fx_sniper", "fx_snco", "fx_pvc", "fx_data2"] },
    { id: "ma", name: "이동평균", members: ["sma"] },
    { id: "weekly", name: "위클리", members: ["w_ret_long", "w_ret_short", "w_link_long", "w_link_short", "ks_data2"] },
    { id: "misc", name: "기타", members: [] }, // 미분류 지표의 기본 그룹 (항상 마지막)
  ];

  const MISC_ID = CATEGORIES[CATEGORIES.length - 1].id;

  // 매니페스트 → 트리 구조: [{ id, name, indicators: [매니페스트 메타…] }].
  // isKnown(id)가 있으면 이 프론트가 모르는 지표는 걸러낸다 (app.js는 RENDERERS 기준 —
  // 칩 시절 buildPaneTools의 `if (!RENDERERS[meta.id]) continue`와 같은 계약).
  // 지표가 하나도 없는 카테고리는 빼고, 카테고리 안에서는 매니페스트 순서를 유지한다.
  // 돌려주는 indicators는 매니페스트 메타 객체 그대로다 (읽기 전용으로 쓴다).
  function buildTree(manifest, isKnown) {
    const items = (Array.isArray(manifest) ? manifest : [])
      .filter((m) => m && typeof m.id === "string")
      .filter((m) => !isKnown || isKnown(m.id));
    const catOf = new Map();
    for (const cat of CATEGORIES) for (const id of cat.members) catOf.set(id, cat.id);
    const groups = new Map(CATEGORIES.map((c) => [c.id, []]));
    for (const m of items) groups.get(catOf.get(m.id) ?? MISC_ID).push(m);
    return CATEGORIES
      .filter((c) => groups.get(c.id).length > 0)
      .map((c) => ({ id: c.id, name: c.name, indicators: groups.get(c.id) }));
  }

  return { CATEGORIES, buildTree };
})();

if (typeof globalThis !== "undefined") {
  globalThis.IndicatorTree = IndicatorTree;
}
