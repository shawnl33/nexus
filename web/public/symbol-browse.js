// 종목 찾기 팝업. 주식은 검색, 지수는 해외선물 카드 위·코스피200 월물 아래,
// 옵션은 만기 칩과 행사가 체인이다. DOM 없는 분류는 node:test가 본다.
"use strict";

const SymbolBrowse = (() => {
  const OVS_ORDER = ["ES", "NQ", "YM", "RTY", "CL", "GC"];

  function parseOptName(name) {
    const text = String(name || "").trim();
    const m = text.match(/^([CP])\s+(.+)$/);
    if (!m) return null;
    const parts = m[2].trim().split(/\s+/);
    if (parts.length < 2) return null;
    const strikeText = parts[parts.length - 1].replace(/,/g, "");
    const strike = Number(strikeText);
    if (!Number.isFinite(strike)) return null;
    const head = parts.slice(0, -1);
    const expiry = head.length === 1 && /^\d{4}$/.test(head[0]) ? head[0] : head.join(" ");
    if (!expiry) return null;
    return { cp: m[1], expiry, strike };
  }

  function futMonthLabel(name) {
    const m = String(name || "").trim().match(/^F\s+(\d{2})(\d{2})$/);
    return m ? `20${m[1]}-${m[2]}` : String(name || "").trim();
  }

  function futMonthKey(name) {
    const m = String(name || "").trim().match(/^F\s+(\d{4})$/);
    return m ? m[1] : "";
  }

  function expiryIsMonth(key) {
    return /^\d{4}$/.test(key);
  }

  function expiryLabel(key) {
    if (expiryIsMonth(key)) return `20${key.slice(0, 2)}-${key.slice(2)}`;
    return key;
  }

  function sortExpiries(keys) {
    return (keys || []).slice().sort((a, b) => {
      const ma = expiryIsMonth(a);
      const mb = expiryIsMonth(b);
      if (ma !== mb) return ma ? 1 : -1;
      return a < b ? -1 : a > b ? 1 : 0;
    });
  }

  function ovsPrefix(shcode) {
    const s = String(shcode || "").toUpperCase();
    const m = s.match(/^(.*?)[FGHJKMNQUVXZ]\d{1,2}$/);
    return m && m[1] ? m[1] : s;
  }

  function ovsRank(shcode) {
    const i = OVS_ORDER.indexOf(ovsPrefix(shcode));
    return i < 0 ? OVS_ORDER.length : i;
  }

  function nearestStrike(strikes, price) {
    let best = null;
    let dist = Infinity;
    for (const strike of strikes || []) {
      if (!Number.isFinite(strike) || !Number.isFinite(price)) continue;
      const d = Math.abs(strike - price);
      if (best == null || d < dist || (d === dist && strike < best)) {
        dist = d;
        best = strike;
      }
    }
    return best;
  }

  function chainRows(items) {
    const by = new Map();
    for (const it of items || []) {
      const parsed = parseOptName(it.name);
      const strike = parsed ? parsed.strike : Number(it.strike);
      const cp = parsed ? parsed.cp : it.cp;
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

  function el(tag, cls, text) {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text != null) node.textContent = text;
    return node;
  }

  function mount(parent) {
    const root = el("div", "sym-browse");
    root.hidden = true;
    const card = el("div", "sym-browse-card");
    const head = el("div", "sym-browse-head");
    const tabs = ["stock", "index", "opt"].map((id) => {
      const btn = el("button", "sym-tab", id === "stock" ? "주식" : id === "index" ? "지수" : "옵션");
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
    let tab = "stock";
    const chains = new Map();
    let expiries = null;
    let underlying = null;

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

    function paintStock() {
      body.replaceChildren();
      const input = el("input", "sym-browse-q");
      input.placeholder = "코드 또는 종목명";
      input.autocomplete = "off";
      const list = el("div", "sym-browse-list");
      body.append(input, list);
      let timer = null;
      let seq = 0;
      async function run() {
        const query = input.value.trim();
        const my = ++seq;
        if (!query) {
          list.replaceChildren();
          return;
        }
        try {
          const payload = await loadMarket({ q: query, kind: "0", limit: "30" });
          if (my !== seq) return;
          list.replaceChildren();
          const items = payload.items || [];
          if (items.length === 0) {
            list.append(el("div", "empty", "일치하는 종목 없음"));
            return;
          }
          for (const it of items) {
            const row = el("button", "sym-browse-row");
            row.type = "button";
            row.append(el("span", "code", it.shcode), el("span", "name", it.name));
            row.onclick = () => pick(it.shcode, it.name);
            list.append(row);
          }
        } catch (err) {
          if (my !== seq) return;
          list.replaceChildren(el("div", "empty", err.message));
        }
      }
      input.addEventListener("input", () => {
        clearTimeout(timer);
        timer = setTimeout(run, 200);
      });
      input.focus();
    }

    function cardButton(title, code, name) {
      const btn = el("button", "sym-card");
      btn.type = "button";
      btn.append(el("b", "", title), el("small", "", code));
      btn.onclick = () => pick(code, name || title);
      return btn;
    }

    async function paintIndex() {
      showMessage("불러오는 중");
      try {
        const [ovs, fut] = await Promise.all([
          loadMarket({ kind: "2", limit: "200" }),
          loadMarket({ kind: "1", limit: "80" }),
        ]);
        body.replaceChildren();
        const ovsTitle = el("h3", "", "해외선물");
        const ovsGrid = el("div", "sym-card-grid");
        const ovsItems = (ovs.items || []).slice().sort((a, b) => {
          const d = ovsRank(a.shcode) - ovsRank(b.shcode);
          return d !== 0 ? d : String(a.shcode).localeCompare(String(b.shcode));
        });
        for (const it of ovsItems) ovsGrid.append(cardButton(it.name || it.shcode, it.shcode, it.name));
        if (ovsItems.length === 0) ovsGrid.append(el("p", "sym-browse-msg", "해외선물 목록이 없습니다"));
        const futTitle = el("h3", "", "코스피200 선물");
        const futGrid = el("div", "sym-card-grid");
        const futItems = (fut.items || []).slice().sort((a, b) => {
          const ka = futMonthKey(a.name);
          const kb = futMonthKey(b.name);
          if (ka && kb && ka !== kb) return ka < kb ? -1 : 1;
          if (ka !== kb) return ka ? -1 : 1;
          return String(a.shcode).localeCompare(String(b.shcode));
        });
        for (const it of futItems) {
          futGrid.append(cardButton(futMonthLabel(it.name), it.shcode, it.name));
        }
        if (futItems.length === 0) futGrid.append(el("p", "sym-browse-msg", "코스피200 선물 목록이 없습니다"));
        body.append(ovsTitle, ovsGrid, futTitle, futGrid);
      } catch (err) {
        showMessage(err.message);
      }
    }

    function sideButton(it, cls) {
      if (!it) {
        const gap = el("span", `opt-side ${cls} empty`, "");
        return gap;
      }
      const btn = el("button", `opt-side ${cls}`, it.name || it.shcode);
      btn.type = "button";
      btn.onclick = () => pick(it.shcode, it.name);
      return btn;
    }

    function paintChain(items, atm) {
      const old = body.querySelector(".opt-chain");
      if (old) old.remove();
      const chain = el("div", "opt-chain");
      const rows = chainRows(items);
      if (rows.length === 0) {
        chain.append(el("p", "sym-browse-msg", "이 만기의 옵션이 없습니다"));
      }
      for (const row of rows) {
        const line = el("div", "opt-row");
        line.dataset.strike = String(row.strike);
        const atmHere = atm != null && row.strike === atm;
        if (atmHere) line.classList.add("atm");
        const k = el("span", "opt-k");
        k.append(document.createTextNode(String(row.strike)));
        if (atmHere) {
          k.append(el("em", "atm-tag", "ATM"));
          k.title = underlying == null ? "ATM" : `코스피200 선물 ${underlying.toFixed(2)}`;
        }
        line.append(sideButton(row.call, "call"), k, sideButton(row.put, "put"));
        chain.append(line);
      }
      body.append(chain);
      return chain;
    }

    function centerRow(row) {
      const scroller = body;
      const fit = () => {
        const s = scroller.getBoundingClientRect();
        const r = row.getBoundingClientRect();
        scroller.scrollTop += (r.top + r.height / 2) - (s.top + s.height / 2);
      };
      requestAnimationFrame(() => requestAnimationFrame(fit));
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
      const price = await kospi200Price();
      const rows = chainRows(items);
      const atm = nearestStrike(rows.map((r) => r.strike), price);
      const chain = paintChain(items, atm);
      const box = body.querySelector(".sym-strike");
      if (box && box.value.trim()) scrollStrike(chain, box.value);
      else if (atm != null) {
        const row = chain.querySelector(".opt-row.atm");
        if (row) centerRow(row);
      }
    }

    async function kospi200Price() {
      if (underlying != null) return underlying;
      const listed = await loadMarket({ kind: "1", limit: "80" });
      const front = (listed.items || [])
        .filter((it) => futMonthKey(it.name))
        .sort((a, b) => (futMonthKey(a.name) < futMonthKey(b.name) ? -1 : 1))[0];
      if (!front) return null;
      const res = await fetch(`/api/chart?shcode=${encodeURIComponent(front.shcode)}&back_index=0`);
      const data = await res.json().catch(() => ({}));
      const bars = data.payload?.bars;
      if (!Array.isArray(bars) || bars.length === 0) return null;
      const close = Number(bars[bars.length - 1][4]);
      if (!Number.isFinite(close)) return null;
      underlying = close / 100;
      return underlying;
    }

    function scrollStrike(chain, raw) {
      const want = Number(String(raw).replace(/,/g, ""));
      if (!Number.isFinite(want)) return;
      let best = null;
      let bestDist = Infinity;
      for (const row of chain.querySelectorAll(".opt-row")) {
        const strike = Number(row.dataset.strike);
        const dist = Math.abs(strike - want);
        if (dist < bestDist) {
          bestDist = dist;
          best = row;
        }
      }
      if (best) best.scrollIntoView({ block: "center" });
    }

    async function paintOpt() {
      showMessage("불러오는 중");
      try {
        if (!expiries) {
          const payload = await loadMarket({ kind: "3", limit: "1" });
          expiries = sortExpiries(payload.expiries || []);
        }
        body.replaceChildren();
        if (expiries.length === 0) {
          showMessage("옵션 목록이 없습니다");
          return;
        }
        const chips = el("div", "sym-exp-row");
        const strike = el("input", "sym-strike");
        strike.placeholder = "행사가";
        strike.inputMode = "decimal";
        const headRow = el("div", "opt-tools");
        headRow.append(chips, strike);
        body.append(headRow);
        for (const key of expiries) {
          const btn = el("button", "sym-exp", expiryLabel(key));
          btn.type = "button";
          btn.dataset.expiry = key;
          btn.onclick = () => {
            showExpiry(key).catch((err) => showMessage(err.message));
          };
          chips.append(btn);
        }
        strike.addEventListener("input", () => {
          const chain = body.querySelector(".opt-chain");
          if (chain) scrollStrike(chain, strike.value);
        });
        await showExpiry(expiries[0]);
      } catch (err) {
        showMessage(err.message);
      }
    }

    function paint() {
      for (const btn of tabs) btn.classList.toggle("on", btn.dataset.tab === tab);
      if (tab === "stock") paintStock();
      else if (tab === "index") paintIndex();
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

  return {
    parseOptName, futMonthLabel, futMonthKey, expiryLabel, sortExpiries, ovsPrefix, ovsRank,
    nearestStrike, chainRows, mount,
  };
})();

if (typeof globalThis !== "undefined") {
  globalThis.SymbolBrowse = SymbolBrowse;
}
