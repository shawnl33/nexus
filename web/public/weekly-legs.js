// 위클리 프라이스링크에 고른 상대·선물. 합산수익률은 같은 쪽을 쓰고,
// 국내선물 Data2는 그 선물 종목을 쓴다. 브라우저 전역 WeeklyLegs, node:test도 같다.
"use strict";

const WeeklyLegs = (() => {
  const LONG = "w_link_long";
  const SHORT = "w_link_short";

  function clean(code) {
    return String(code || "").trim().toUpperCase();
  }

  function empty() {
    return { [LONG]: { opp: "", fut: "" }, [SHORT]: { opp: "", fut: "" } };
  }

  function copy(legs) {
    const base = empty();
    for (const id of [LONG, SHORT]) {
      base[id] = {
        opp: clean(legs?.[id]?.opp),
        fut: clean(legs?.[id]?.fut),
      };
    }
    return base;
  }

  function setLeg(legs, id, role, code) {
    const next = copy(legs);
    if (id !== LONG && id !== SHORT) return next;
    if (role !== "opp" && role !== "fut") return next;
    const row = next[id];
    const c = clean(code);
    row[role] = c;
    const other = role === "opp" ? "fut" : "opp";
    if (c && row[other] === c) row[other] = "";
    return next;
  }

  function legForIndicator(legs, id) {
    const key = id === "w_ret_long" || id === LONG ? LONG
      : id === "w_ret_short" || id === SHORT ? SHORT
        : "";
    if (!key) return { opp: "", fut: "" };
    return { opp: clean(legs?.[key]?.opp), fut: clean(legs?.[key]?.fut) };
  }

  function futForData2(legs) {
    return clean(legs?.[LONG]?.fut) || clean(legs?.[SHORT]?.fut);
  }

  function forgetCode(legs, code) {
    const c = clean(code);
    let next = copy(legs);
    for (const id of [LONG, SHORT]) {
      if (next[id].opp === c) next = setLeg(next, id, "opp", "");
      if (next[id].fut === c) next = setLeg(next, id, "fut", "");
    }
    return next;
  }

  // 넣은 순서: 지수, 콜, 풋. 양매수는 콜이 자기 차트이고 풋이 상대.
  // 양매도는 풋이 자기 차트이고 콜이 상대. 선물은 둘 다 지수.
  function roles(codes) {
    const list = [];
    for (const code of Array.isArray(codes) ? codes : []) {
      const c = clean(code);
      if (c && !list.includes(c)) list.push(c);
    }
    return { fut: list[0] || "", call: list[1] || "", put: list[2] || "" };
  }

  function fromSymbolOrder(codes) {
    const r = roles(codes);
    let legs = empty();
    legs = setLeg(legs, LONG, "fut", r.fut);
    legs = setLeg(legs, LONG, "opp", r.put);
    legs = setLeg(legs, SHORT, "fut", r.fut);
    legs = setLeg(legs, SHORT, "opp", r.call);
    return legs;
  }

  function selfFor(codes, side) {
    const r = roles(codes);
    return side < 0 ? r.put : r.call;
  }

  // basis "C"면 콜이 Data1, "P"면 풋이 Data1. 상대는 반대 옵션이고 주문은 Data1 하나다.
  function optionCharts(codes, letters, basis) {
    const list = [];
    const kind = [];
    for (let i = 0; i < (Array.isArray(codes) ? codes.length : 0); i++) {
      const c = clean(codes[i]);
      if (!c || list.includes(c)) continue;
      list.push(c);
      const letter = letters && letters[i];
      kind.push(letter === "C" || letter === "P" ? letter : "");
    }
    let fut = "";
    let call = "";
    let put = "";
    for (let i = 0; i < list.length; i++) {
      if (kind[i] === "C" && !call) call = list[i];
      else if (kind[i] === "P" && !put) put = list[i];
      else if (!fut) fut = list[i];
    }
    if (!fut || !call || !put) return [];
    const self = basis === "P" ? put : call;
    const opp = self === call ? put : call;
    return [{ self, opp, fut }];
  }

  return { empty, copy, setLeg, legForIndicator, futForData2, forgetCode, fromSymbolOrder, selfFor, optionCharts };
})();

if (typeof globalThis !== "undefined") globalThis.WeeklyLegs = WeeklyLegs;
