// 종목 입력 Enter가 고를 후보. DOM 없는 순수 로직.
"use strict";

const SymbolPick = (() => {
  function same(value, query) {
    return String(value ?? "").trim().toUpperCase() === query;
  }

  // 코드가 같으면 그 종목, 이름이 같으면 그 종목, 아니면 목록의 첫 후보.
  function pick(query, items) {
    const q = String(query ?? "").trim().toUpperCase();
    if (!q || !Array.isArray(items) || items.length === 0) return null;
    return items.find((it) => same(it.shcode, q))
      || items.find((it) => same(it.name, q))
      || items[0];
  }

  function activeIndex(query, items) {
    const hit = pick(query, items);
    if (!hit || !Array.isArray(items)) return -1;
    const i = items.indexOf(hit);
    return i < 0 ? -1 : i;
  }

  // 위아래 키. 목록 밖으로 나가지 않는다. 후보가 없으면 -1.
  function move(index, delta, count) {
    if (!count || count < 1) return -1;
    const cur = Number.isInteger(index) ? index : 0;
    return Math.min(count - 1, Math.max(0, cur + delta));
  }

  return { pick, activeIndex, move };
})();

if (typeof globalThis !== "undefined") {
  globalThis.SymbolPick = SymbolPick;
}
