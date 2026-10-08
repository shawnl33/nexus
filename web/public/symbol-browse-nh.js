// NH 종목 찾기. 분봉 조회가 되는 종목만 보여 준다.
// 선물은 분봉이 되는 CME와 CBOE. 옵션은 OPRA O_SPW O_SPX O_NDX.
"use strict";

const NH_OPTION_PRODUCTS = ["O_SPW", "O_SPX", "O_NDX"];

const SymbolBrowseNh = (() => {
  function el(tag, cls, text) {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text != null) node.textContent = text;
    return node;
  }

  function chainRows(items) {
    const by = new Map();
    for (const it of items || []) {
      const strike = Number(it.strike);
      const cp = it.cp;
      if (!Number.isFinite(strike) || (cp !== "C" && cp !== "P")) continue;
      let row = by.get(strike);
      if (!row) {
        row = { strike, call: null, put: null };
        by.set(strike, row);
      }
      if (cp === "C") row.call = it;
      else row.put = it;
    }
    return [...by.values()].sort((a, b) => b.strike - a.strike);
  }

  function splitExpiry(key) {
    const m = String(key || "").match(/^(.*)\s+(\d{8}|\d{6})$/);
    if (!m) return { product: String(key || ""), date: "" };
    return { product: m[1], date: m[2] };
  }

  function dateLabel(date) {
    if (/^\d{8}$/.test(date)) return `${date.slice(0, 4)}-${date.slice(4, 6)}-${date.slice(6)}`;
    if (/^\d{6}$/.test(date)) return `${date.slice(0, 4)}-${date.slice(4)}`;
    return date;
  }

  function mount(parent) {
    const root = el("div", "sym-browse sym-browse-nh");
    root.hidden = true;
    const card = el("div", "sym-browse-card");
    const head = el("div", "sym-browse-head");
    head.append(el("span", "sym-browse-tag", "NH"));
    const tabs = [
      ["fut", "선물"],
      ["opt", "옵션"],
    ].map(([id, label]) => {
      const btn = el("button", "sym-tab", label);
      btn.type = "button";
      btn.dataset.tab = id;
      head.append(btn);
      return btn;
    });
    const close = el("button", "sym-browse-x", "×");
    close.type = "button";
    close.title = "닫기";
    head.append(close);
    const body = el("div", "sym-browse-body");
    card.append(head, body);
    root.append(card);
    parent.append(root);

    let onPick = null;
    let tab = "opt";
    const chains = new Map();
    let expiryRows = null;

    function shut() {
      root.hidden = true;
      onPick = null;
    }

    function pick(code, name) {
      const fn = onPick;
      shut();
      if (fn) fn(code, name);
    }

    async function loadMarket(params) {
      const q = new URLSearchParams(params);
      const res = await fetch(`/api/market?${q}`);
      const data = await res.json().catch(() => ({}));
      if (!res.ok) {
        const why = data.error_code === "registry_unavailable"
          ? "종목 목록을 아직 받지 못했습니다"
          : "종목 목록을 받지 못했습니다";
        throw new Error(why);
      }
      return data.payload || {};
    }

    function showMessage(text) {
      body.replaceChildren(el("p", "sym-browse-msg", text));
    }

    function cardButton(title, code, name) {
      const btn = el("button", "sym-card");
      btn.type = "button";
      btn.append(el("b", "", title), el("small", "", code));
      btn.onclick = () => pick(code, name || title);
      return btn;
    }

    async function paintFut() {
      showMessage("불러오는 중");
      try {
        const [cme, cboe] = await Promise.all([
          loadMarket({ kind: "4", limit: "200" }),
          loadMarket({ kind: "2", limit: "200" }),
        ]);
        body.replaceChildren();
        const cmeTitle = el("h3", "", "CME 선물");
        const cmeGrid = el("div", "sym-card-grid");
        const cmeItems = cme.items || [];
        for (const it of cmeItems) cmeGrid.append(cardButton(it.name || it.shcode, it.shcode, it.name));
        if (cmeItems.length === 0) cmeGrid.append(el("p", "sym-browse-msg", "CME 선물이 없습니다"));
        const cboeTitle = el("h3", "", "CBOE 선물");
        const cboeGrid = el("div", "sym-card-grid");
        const cboeItems = cboe.items || [];
        for (const it of cboeItems) cboeGrid.append(cardButton(it.name || it.shcode, it.shcode, it.name));
        if (cboeItems.length === 0) cboeGrid.append(el("p", "sym-browse-msg", "CBOE 선물이 없습니다"));
        body.append(cmeTitle, cmeGrid, cboeTitle, cboeGrid);
      } catch (err) {
        showMessage(err.message);
      }
    }

    function sideButton(it, cls) {
      if (!it) return el("span", `opt-side ${cls} empty`, "");
      const btn = el("button", `opt-side ${cls}`, it.shcode);
      btn.type = "button";
      btn.title = it.name || it.shcode;
      btn.onclick = () => pick(it.shcode, it.name);
      return btn;
    }

    function paintChain(items) {
      const old = body.querySelector(".opt-chain");
      if (old) old.remove();
      const chain = el("div", "opt-chain");
      const rows = chainRows(items);
      if (rows.length === 0) chain.append(el("p", "sym-browse-msg", "이 만기의 옵션이 없습니다"));
      for (const row of rows) {
        const line = el("div", "opt-row");
        line.dataset.strike = String(row.strike);
        const k = el("span", "opt-k", String(row.strike));
        line.append(sideButton(row.call, "call"), k, sideButton(row.put, "put"));
        chain.append(line);
      }
      body.append(chain);
      return chain;
    }

    function scrollStrike(chain, raw) {
      const want = Number(String(raw).replace(/,/g, ""));
      if (!Number.isFinite(want)) return;
      let best = null;
      let bestDist = Infinity;
      for (const row of chain.querySelectorAll(".opt-row")) {
        const dist = Math.abs(Number(row.dataset.strike) - want);
        if (dist < bestDist) {
          bestDist = dist;
          best = row;
        }
      }
      if (best) best.scrollIntoView({ block: "center" });
    }

    async function showExpiry(key) {
      body.querySelectorAll(".sym-exp").forEach((btn) => {
        btn.classList.toggle("on", btn.dataset.expiry === key);
      });
      let items = chains.get(key);
      if (!items) {
        const payload = await loadMarket({ kind: "3", expiry: key, limit: "1024" });
        items = payload.items || [];
        chains.set(key, items);
      }
      const chain = paintChain(items);
      const box = body.querySelector(".sym-strike");
      if (box && box.value.trim()) scrollStrike(chain, box.value);
    }

    async function paintOpt() {
      showMessage("불러오는 중");
      try {
        if (!expiryRows) {
          const payload = await loadMarket({ kind: "3", limit: "1" });
          const grouped = new Map();
          for (const key of payload.expiries || []) {
            const part = splitExpiry(key);
            if (!NH_OPTION_PRODUCTS.includes(part.product)) continue;
            if (!grouped.has(part.product)) grouped.set(part.product, []);
            grouped.get(part.product).push({ key, date: part.date });
          }
          expiryRows = [...grouped.entries()].sort((a, b) => {
            const ia = NH_OPTION_PRODUCTS.indexOf(a[0]);
            const ib = NH_OPTION_PRODUCTS.indexOf(b[0]);
            return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib);
          });
        }
        body.replaceChildren();
        if (expiryRows.length === 0) {
          showMessage("옵션 목록이 없습니다");
          return;
        }
        const products = el("div", "sym-exp-row");
        const dates = el("div", "sym-exp-row");
        const strike = el("input", "sym-strike");
        strike.placeholder = "행사가";
        strike.inputMode = "decimal";
        body.append(products, dates, el("div", "opt-tools"));
        body.querySelector(".opt-tools").append(strike);
        let product = expiryRows.some((row) => row[0] === "O_SPW") ? "O_SPW" : expiryRows[0][0];
        function paintDates() {
          dates.replaceChildren();
          const row = expiryRows.find((item) => item[0] === product);
          const list = row ? row[1] : [];
          for (const item of list) {
            const btn = el("button", "sym-exp", dateLabel(item.date) || item.key);
            btn.type = "button";
            btn.dataset.expiry = item.key;
            btn.onclick = () => {
              showExpiry(item.key).catch((err) => showMessage(err.message));
            };
            dates.append(btn);
          }
          if (list[0]) showExpiry(list[0].key).catch((err) => showMessage(err.message));
        }
        for (const [name] of expiryRows) {
          const btn = el("button", "sym-exp", name);
          btn.type = "button";
          btn.onclick = () => {
            product = name;
            products.querySelectorAll("button").forEach((node) => node.classList.toggle("on", node === btn));
            paintDates();
          };
          if (name === product) btn.classList.add("on");
          products.append(btn);
        }
        strike.addEventListener("input", () => {
          const chain = body.querySelector(".opt-chain");
          if (chain) scrollStrike(chain, strike.value);
        });
        paintDates();
      } catch (err) {
        showMessage(err.message);
      }
    }

    function paint() {
      for (const btn of tabs) btn.classList.toggle("on", btn.dataset.tab === tab);
      if (tab === "fut") paintFut();
      else paintOpt();
    }

    for (const btn of tabs) {
      btn.onclick = () => {
        tab = btn.dataset.tab;
        paint();
      };
    }
    close.onclick = shut;
    root.addEventListener("mousedown", (ev) => {
      if (ev.target === root) shut();
    });
    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape" && !root.hidden) {
        ev.preventDefault();
        ev.stopPropagation();
        shut();
      }
    });

    return {
      open(fn) {
        onPick = fn;
        root.hidden = false;
        paint();
      },
      close: shut,
      isOpen() {
        return !root.hidden;
      },
    };
  }

  return { splitExpiry, dateLabel, chainRows, mount };
})();

if (typeof globalThis !== "undefined") {
  globalThis.SymbolBrowseNh = SymbolBrowseNh;
}
