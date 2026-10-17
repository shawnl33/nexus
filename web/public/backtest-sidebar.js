// 백테스트 칸의 왼쪽 사이드바. 실시간 화면과 같은 원본 트리, 체크, 레이어, 변수, 분봉 모양이다.
// 지표 값은 엔진 차트 스냅샷을 종목 캐시에 넣고, 실시간과 같은 렌더러가 그린다.
"use strict";

const BtSide = (() => {
  const RENDERERS = {
    mirae_v16: MiraeLayers.MiraeRenderer,
    sma: SmaLayers.SmaRenderer,
    fx_mirae_v1: FxLayers.FxRenderer,
    fx_mirae_v3: Fx3Layers.Fx3Renderer,
    fx_pgap3: PgapLayers.make("fx_pgap3", 0),
    fx_pgap5: PgapLayers.make("fx_pgap5", 1),
    fx_rgap: PgapLayers.makeSingle("fx_rgap", "rgap"),
    fx_mgap: PgapLayers.makeSingle("fx_mgap", "mgap"),
    fx_ugap: PgapLayers.makeUnion("fx_ugap"),
    fx_ymae: YmaeLayers.YmaeRenderer,
    fx_sniper: Data2Layers.Data2Renderer,
    fx_curve_os: { createHandle: (chart) => CuLayers.createHandle(chart) },
    fx_dnbrk: { createHandle: (chart) => CuLayers.brkHandle(chart) },
    fx_snco: { createHandle: (chart) => SncoLayers.createHandle(chart) },
    fx_judge: { createHandle: (chart) => OsLayers.judgeHandle(chart) },
    fx_pack_v4: { createHandle: (chart) => OsLayers.packHandle(chart) },
    fx_flat_v4: { createHandle: (chart) => OsLayers.flatHandle(chart) },
    fx_flat_v2: { createHandle: (chart) => OsLayers.flatHandleV2(chart) },
    fx_adj_v6: { createHandle: (chart) => OsLayers.adjHandle(chart) },
    fx_adj_v1: { createHandle: (chart) => OsLayers.adjHandleOf(chart, "fxadj1", "w1") },
    fx_adj_v2: { createHandle: (chart) => OsLayers.adjHandleOf(chart, "fxadj2", "w2") },
    fx_adj_v4: { createHandle: (chart) => OsLayers.adjHandleOf(chart, "fxadj4", "w4") },
    fx_adj_v5: { createHandle: (chart) => OsLayers.adjHandleOf(chart, "fxadj5", "w5") },
    fx_ec19: { createHandle: (chart) => OsLayers.ecHandle(chart, false) },
    fx_ec20: { createHandle: (chart) => OsLayers.ecHandle(chart, true) },
    fx_paint_v12: { createHandle: () => OsLayers.paintHandle() },
    fx_paint_os: { createHandle: () => OsLayers.paintHandle() },
    vb_paint: { createHandle: () => OsLayers.paintHandle() },
    fx_sig_v6: { createHandle: (chart) => OsLayers.sigHandle(chart) },
    fx_sig_pair: { createHandle: (chart) => OsLayers.sigHandle(chart, "pair") },
    fx_sig_zd: { createHandle: (chart) => OsLayers.sigHandle(chart, "zd") },
    fx_pnl: { createHandle: (chart) => OsLayers.pnlHandle(chart) },
    fx_pos_v1: { createHandle: (chart) => OsLayers.posHandle(chart, 1) },
    fx_pos_v2: { createHandle: (chart) => OsLayers.posHandle(chart, 2) },
    fx_pvc: PvcLayers.PvcRenderer,
    fx_data2: Data2Layers.Data2Renderer,
    ks_data2: Data2Layers.Data2Renderer,
    w_ret_long: WplotLayers.RetLong,
    w_ret_short: WplotLayers.RetShort,
    w_link_long: WplotLayers.LinkLong,
    w_link_short: WplotLayers.LinkShort,
  };
  const WPLOT = ["w_ret_long", "w_ret_short", "w_link_long", "w_link_short"];
  const FRAME_VARS = new Set(["fx_sig_v6", "pair-long", "pair-short", "fx_pnl"]);
  const STYLES = [
    { id: "none", name: "없음" },
    { id: "candle", name: "캔들바" },
    { id: "outline", name: "테두리" },
    { id: "bar", name: "바" },
    { id: "line", name: "종가선" },
  ];
  const GEAR = `<svg viewBox="0 0 24 24" width="14" height="14" aria-hidden="true"><path fill="currentColor" d="M19.14 12.94c.04-.31.06-.63.06-.94s-.02-.63-.06-.94l2.03-1.58a.5.5 0 0 0 .12-.64l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.2 7.2 0 0 0-1.63-.94l-.36-2.54a.5.5 0 0 0-.5-.42h-3.84a.5.5 0 0 0-.5.42l-.36 2.54c-.59.22-1.13.54-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.71 8.84a.5.5 0 0 0 .12.64l2.03 1.58c-.04.31-.06.63-.06.94s.02.63.06.94l-2.03 1.58a.5.5 0 0 0-.12.64l1.92 3.32c.13.22.39.31.6.22l2.39-.96c.5.4 1.04.72 1.63.94l.36 2.54c.05.24.26.42.5.42h3.84c.24 0 .45-.18.5-.42l.36-2.54c.59-.22 1.13-.54 1.63-.94l2.39.96c.22.09.47 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.64l-2.03-1.58zM12 15.5A3.5 3.5 0 1 1 12 8.5a3.5 3.5 0 0 1 0 7z"/></svg>`;

  const feed = Feed.create();
  let catalog = { dirs: [] };
  let manifest = [];
  const loading = new Map();

  function blankPane() {
    return {
      barStyle: "candle",
      panelOpen: true,
      treeFold: {},
      wanted: new Map(),
      active: new Map(),
      systems: new Set(),
      systemVars: {},
      systemBasis: {},
      linkLegs: WeeklyLegs.empty(),
      chart: null,
      candleSeries: null,
      onStyle: null,
    };
  }

  function sameSession(src, cur) {
    if (!src) return false;
    if (!Number.isFinite(src.day) || !Number.isFinite(cur.day)) return true;
    return src.day === cur.day;
  }

  function ctxFor(cache) {
    if (!cache.ctx) {
      cache.ctx = {
        bars: cache.bars, barInd: cache.barInd, barSeq: cache.barSeq, barPos: cache.barPos,
        tickRaw: () => cache.tickRaw,
        sameSession,
        recentBars: (pos, n) => feed.recentBars(cache, pos, n),
      };
    }
    return cache.ctx;
  }

  function bag(frame, id) {
    for (const pane of frame?.panes || []) {
      const found = pane.systemVars?.[id];
      if (found && Object.keys(found).length) return found;
    }
    return {};
  }

  function writeBag(frame, id, next) {
    for (const pane of frame?.panes || []) {
      if (!pane.systemVars) pane.systemVars = {};
      pane.systemVars[id] = { ...next };
    }
  }

  async function loadCatalog() {
    for (const url of ["/api/yeslang", "/yl-catalog.json"]) {
      try {
        const res = await fetch(url);
        if (!res.ok) continue;
        const data = await res.json();
        if (!YlTree.hasEntries(data)) continue;
        catalog = data;
        return;
      } catch { /* 다음 목록 */ }
    }
  }

  function rememberBars(cache, rows) {
    rows.sort((a, b) => a.time - b.time);
    const dedup = rows.filter((b, i) => i === 0 || b.time !== rows[i - 1].time);
    feed.reset(cache);
    for (const b of dedup) feed.noteBar(cache, b.time, b);
    for (const b of dedup) {
      const d = MiraeLayers.parseInd(b.ind);
      if (!d) continue;
      if (Number.isFinite(d.tick) && d.tick > 0) cache.tickRaw = d.tick;
      const ind = MiraeLayers.barIndFromInd(d);
      ind.score = d.score;
      ind.pred = d.pred;
      ind.resid = d.resid;
      ind.pvol = d.pvol;
      ind.fx3 = Fx3Layers.parseFx3(b.fx3);
      ind.pgap = PgapLayers.parsePgap(b.pgap);
      ind.rgap = PgapLayers.parseSide(b.rgap);
      ind.mgap = PgapLayers.parseSide(b.mgap);
      ind.ymae = YmaeLayers.parseYmae(b.ymae);
      ind.sniper = SniperLayers.parseSniper(b.sniper);
      ind.os = OsLayers.parse(b.os);
      ind.cu = CuLayers.parse(b.cu);
      ind.snco = SncoLayers.parse(b.snco);
      ind.pvc = PvcLayers.parsePvc(b.pvc);
      cache.barInd.set(b.time, ind);
    }
    cache.ready = true;
  }

  function prefetch(symbol) {
    const code = (symbol || "").trim().toUpperCase();
    if (!code) return Promise.resolve(null);
    const cache = feed.forSymbol(code);
    if (cache.ready) return Promise.resolve(cache);
    if (loading.has(code)) return loading.get(code);
    const job = (async () => {
      const rows = [];
      let back = 0;
      for (let page = 0; page < 40; page++) {
        const res = await fetch(`/api/chart?shcode=${encodeURIComponent(code)}&back_index=${back}`);
        if (!res.ok) break;
        const data = await res.json();
        const p = data.payload;
        if (!p || !Array.isArray(p.bars)) break;
        if (page === 0 && Array.isArray(p.indicators)) manifest = p.indicators.filter((m) => m && typeof m.id === "string");
        const n = p.bars.length;
        for (let i = 0; i < n; i++) {
          const [t, o, h, l, c] = p.bars[i];
          rows.push({
            time: Number(t) / 1e6, open: o, high: h, low: l, close: c,
            ind: (p.ind || [])[i], fx3: (p.fx3 || [])[i], pgap: (p.pgap || [])[i],
            rgap: (p.rgap || [])[i], mgap: (p.mgap || [])[i], ymae: (p.ymae || [])[i],
            sniper: (p.sniper || [])[i], pvc: (p.pvc || [])[i], os: (p.os || [])[i],
            cu: (p.cu || [])[i], snco: (p.snco || [])[i],
          });
        }
        if (!p.next_back_index) break;
        back = p.next_back_index;
      }
      if (rows.length) rememberBars(cache, rows);
      return cache;
    })().finally(() => loading.delete(code));
    loading.set(code, job);
    return job;
  }

  function activate(pane, id, saved) {
    if (!pane.chart || pane.active.has(id)) return;
    const renderer = RENDERERS[id];
    if (!renderer) return;
    const handle = renderer.createHandle(pane.chart, pane.candleSeries);
    if (pane.barStyle === "none" && typeof handle.setAxis === "function") handle.setAxis("right");
    const meta = manifest.find((m) => m.id === id);
    const layers = {};
    for (const layer of meta?.layers ?? []) layers[layer.id] = saved?.[layer.id] ?? (layer.defaultOn !== false);
    for (const [k, v] of Object.entries(saved ?? {})) layers[k] = !!v;
    pane.wanted.set(id, layers);
    pane.active.set(id, { handle, layers });
    if (typeof handle.setLayers === "function") handle.setLayers(layers);
    const cache = feed.get(pane.symbol || "");
    if (cache?.ready && id !== "fx_data2" && id !== "ks_data2") {
      try { handle.applySeed(ctxFor(cache)); } catch (err) { console.error(err); }
    }
    if (WPLOT.includes(id)) refreshWeekly(pane);
  }

  function deactivate(pane, id) {
    pane.wanted.delete(id);
    const entry = pane.active.get(id);
    pane.active.delete(id);
    if (!entry) return;
    try {
      if (typeof entry.handle.destroy === "function") entry.handle.destroy();
      else entry.handle.clear();
    } catch { /* 차트를 이미 지운 뒤 */ }
    if (WPLOT.includes(id)) refreshWeekly(pane);
  }

  function attach(pane, chart, series) {
    pane.chart = chart;
    pane.candleSeries = series;
    pane.active = new Map();
    for (const [id, layers] of pane.wanted) activate(pane, id, layers);
    refreshWeekly(pane);
  }

  function sourceTitle(item) {
    const path = `${item.dir}/${item.label}`;
    return item.port?.note ? `${path}. ${item.port.note}` : path;
  }

  function build(pane) {
    const panel = pane.panelEl;
    if (!panel) return;
    const index = pane.frame?.panes?.indexOf(pane) ?? 0;
    panel.replaceChildren();
    panel.append(chartControls(pane));
    if (!pane.symbol) {
      const hint = document.createElement("div");
      hint.className = "ind-hint";
      hint.textContent = "종목을 선택하면 원본 항목을 고를 수 있습니다";
      panel.append(hint);
      return;
    }
    for (const dir of YlTree.build(catalog)) panel.append(sourceDir(pane, dir, index));
    const covered = YlTree.indicatorIds();
    for (const meta of manifest) {
      if (!meta || covered.has(meta.id) || !(meta.id in RENDERERS)) continue;
      panel.append(indNode(pane, meta, null, false));
    }
  }

  function chartControls(pane) {
    const box = document.createElement("div");
    box.className = "ind-chart-tools";
    const label = document.createElement("label");
    label.className = "bar-style";
    const name = document.createElement("span");
    name.textContent = pane.symbol || "분봉";
    name.title = "이 칸의 봉";
    const sel = document.createElement("select");
    for (const style of STYLES) {
      const opt = document.createElement("option");
      opt.value = style.id;
      opt.textContent = style.name;
      sel.append(opt);
    }
    sel.value = pane.barStyle || "candle";
    sel.title = "이 칸의 분봉 표시. 테두리는 캔들 속을 비운다. 없음이면 지표만 남는다";
    sel.addEventListener("click", (e) => e.stopPropagation());
    sel.onchange = () => {
      pane.barStyle = sel.value;
      if (pane.chart) {
        for (const { handle } of pane.active.values()) {
          if (typeof handle.setAxis === "function") handle.setAxis(sel.value === "none" ? "right" : null);
        }
      }
      if (typeof pane.onStyle === "function") pane.onStyle();
    };
    label.append(name, sel);
    box.append(label);
    return box;
  }

  function sourceDir(pane, dir, index) {
    const node = document.createElement("div");
    node.className = "ind-cat";
    const locked = !YlTree.selectable(dir.id, index);
    if (locked) node.classList.add("locked");
    const folded = !!pane.treeFold[dir.id];
    const head = document.createElement("div");
    head.className = "ind-cat-head";
    head.textContent = `${folded ? "▸" : "▾"} ${dir.name}`;
    head.title = locked ? "이 화면틀의 첫 번째 차트에서만 고를 수 있습니다" : (folded ? "펼치기" : "접기");
    head.onclick = () => {
      pane.treeFold[dir.id] = !folded;
      build(pane);
    };
    node.append(head);
    if (!folded) {
      const body = document.createElement("div");
      body.className = "ind-cat-body";
      for (const item of dir.items) body.append(sourceItem(pane, item, locked));
      node.append(body);
    }
    return node;
  }

  function sourceItem(pane, item, locked) {
    if (item.port?.kind === "system") return systemNode(pane, item, locked);
    if (item.port?.kind === "indicator" && item.port.id in RENDERERS) {
      const meta = manifest.find((m) => m.id === item.port.id) || { id: item.port.id, layers: [] };
      return indNode(pane, meta, item, locked);
    }
    const row = document.createElement("div");
    row.className = "ind-row src-plain";
    row.textContent = item.label;
    row.title = `${sourceTitle(item)}. 차트에 따로 연결되지 않은 원본입니다`;
    return row;
  }

  function systemNode(pane, item, locked) {
    const node = document.createElement("div");
    const row = document.createElement("div");
    row.className = locked ? "ind-row locked" : "ind-row";
    const label = document.createElement("label");
    const box = document.createElement("input");
    box.type = "checkbox";
    box.checked = !locked && pane.systems.has(item.port.id);
    box.disabled = !!locked;
    box.onchange = () => {
      if (locked) { box.checked = false; return; }
      if (box.checked) pane.systems.add(item.port.id);
      else pane.systems.delete(item.port.id);
      refreshWeekly(pane);
      build(pane);
      notify(pane);
    };
    const name = document.createElement("span");
    name.className = "src-name";
    name.textContent = item.label;
    name.title = locked ? "이 화면틀의 첫 번째 차트에서만 고를 수 있습니다" : `${sourceTitle(item)}. 종목 순서: 지수, 콜, 풋`;
    label.append(box, name);
    row.append(label, gearButton(pane, item, locked));
    node.append(row);
    return node;
  }

  function gearButton(pane, item, locked) {
    const gear = document.createElement("button");
    gear.type = "button";
    gear.className = "src-gear";
    gear.title = locked ? "이 화면틀의 첫 번째 차트에서만 고를 수 있습니다" : "변수 설정";
    gear.setAttribute("aria-label", "변수 설정");
    gear.disabled = !!locked;
    gear.innerHTML = GEAR;
    gear.onclick = (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      if (!locked) openVars(pane, item);
    };
    return gear;
  }

  function indNode(pane, meta, source, locked) {
    const node = document.createElement("div");
    node.className = "ind-item";
    const row = document.createElement("div");
    row.className = locked ? "ind-row locked" : "ind-row";
    const label = document.createElement("label");
    const box = document.createElement("input");
    box.type = "checkbox";
    box.checked = !locked && pane.wanted.has(meta.id);
    box.disabled = !!locked;
    box.addEventListener("click", (e) => e.stopPropagation());
    box.onchange = async () => {
      if (locked) { box.checked = false; return; }
      if (box.checked) {
        pane.wanted.set(meta.id, pane.wanted.get(meta.id) || null);
        if (pane.symbol) await prefetch(pane.symbol);
        if (pane.chart) activate(pane, meta.id, pane.wanted.get(meta.id));
      } else {
        deactivate(pane, meta.id);
      }
      if (WPLOT.includes(meta.id)) refreshWeekly(pane);
      build(pane);
      if (meta.id === "fx_sig_v6") notify(pane);
    };
    const name = document.createElement("span");
    name.className = "src-name";
    name.textContent = source?.label || meta.name || meta.id;
    name.title = locked ? "이 화면틀의 첫 번째 차트에서만 고를 수 있습니다" : (source ? sourceTitle(source) : (meta.name || meta.id));
    label.append(box, name);
    row.append(label);
    if (source?.port?.inputs) row.append(gearButton(pane, source, locked));
    node.append(row);
    const entry = pane.active.get(meta.id);
    const layers = entry?.layers || pane.wanted.get(meta.id);
    if (pane.wanted.has(meta.id) && meta.layers?.length) {
      const boxWrap = document.createElement("div");
      boxWrap.className = "ind-layers";
      for (const layer of meta.layers) {
        const lrow = document.createElement("label");
        lrow.className = "ind-row layer";
        const lbox = document.createElement("input");
        lbox.type = "checkbox";
        lbox.checked = !layers || layers[layer.id] !== false;
        lbox.disabled = !!locked;
        lbox.onchange = () => {
          const next = { ...(pane.wanted.get(meta.id) || {}) };
          next[layer.id] = lbox.checked;
          pane.wanted.set(meta.id, next);
          const live = pane.active.get(meta.id);
          if (live) {
            live.layers[layer.id] = lbox.checked;
            live.handle.setLayers({ [layer.id]: lbox.checked });
          }
        };
        lrow.append(lbox, document.createTextNode(layer.name ?? layer.id));
        boxWrap.append(lrow);
      }
      node.append(boxWrap);
    }
    return node;
  }

  function openVars(pane, item) {
    const id = item.port.id;
    const fields = YlTree.inputsFor(id);
    document.getElementById("sysvar-back")?.remove();
    const back = document.createElement("div");
    back.className = "sysvar-back";
    back.id = "sysvar-back";
    const box = document.createElement("div");
    box.className = "sysvar-box";
    box.setAttribute("role", "dialog");
    box.setAttribute("aria-label", "시스템 변수 설정");
    const title = document.createElement("div");
    title.className = "sysvar-title";
    title.textContent = `시스템 트레이딩 설정 - ${item.label}`;
    const note = document.createElement("div");
    note.className = "sysvar-note";
    note.textContent = id === "fx_pnl"
      ? "이 화면틀의 시그널 포지션을 쓰고, 수량은 틀 안의 차트가 같이 씁니다"
      : "시스템의 변수를 설정합니다";
    const table = document.createElement("div");
    const pair = id === "pair-long" || id === "pair-short";
    const basis = document.createElement("select");
    if (pair) {
      const line = document.createElement("label");
      line.className = "sysvar-line";
      const span = document.createElement("span");
      span.textContent = "기준";
      for (const [value, text] of [["C", "콜"], ["P", "풋"]]) {
        const opt = document.createElement("option");
        opt.value = value;
        opt.textContent = text;
        basis.append(opt);
      }
      basis.value = pane.systemBasis?.[id] === "P" ? "P" : "C";
      line.append(span, basis);
      table.append(line);
    }
    const head = document.createElement("div");
    head.className = "sysvar-head";
    head.innerHTML = "<span>변수이름</span><span>변수값</span>";
    table.append(head);
    const saved = FRAME_VARS.has(id) ? bag(pane.frame, id) : (pane.systemVars?.[id] || {});
    const signalBag = bag(pane.frame, "fx_sig_v6");
    const inputs = [];
    for (const [name, fallback] of fields) {
      const line = document.createElement("label");
      line.className = "sysvar-line";
      const lab = document.createElement("span");
      lab.textContent = name;
      const input = document.createElement("input");
      input.type = "number";
      input.step = "any";
      input.value = String(id === "fx_pnl"
        ? YlTree.sharedValue(signalBag, bag(pane.frame, "fx_pnl"), name, fallback)
        : (Number.isFinite(Number(saved[name])) ? Number(saved[name]) : fallback));
      input.dataset.name = name;
      line.append(lab, input);
      table.append(line);
      inputs.push(input);
    }
    const actions = document.createElement("div");
    actions.className = "sysvar-actions";
    const ok = document.createElement("button");
    ok.type = "button";
    ok.textContent = "확인";
    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.textContent = "취소";
    actions.append(ok, cancel);
    box.append(title, note, table, actions);
    back.append(box);
    const close = () => back.remove();
    cancel.onclick = close;
    back.addEventListener("mousedown", (ev) => { if (ev.target === back) close(); });
    ok.onclick = () => {
      const next = {};
      for (const input of inputs) {
        const n = Number(input.value);
        if (!Number.isFinite(n)) { input.focus(); return; }
        next[input.dataset.name] = n;
      }
      if (FRAME_VARS.has(id)) writeBag(pane.frame, id, next);
      else pane.systemVars[id] = next;
      if (pair) pane.systemBasis[id] = basis.value === "P" ? "P" : "C";
      if (id === "fx_sig_v6" && Number.isFinite(next["수량"])) {
        const qty = document.getElementById("qty");
        if (qty) qty.value = String(next["수량"]);
      }
      close();
      refreshWeekly(pane);
    };
    document.body.append(back);
    inputs[0]?.focus();
    inputs[0]?.select();
  }

  async function refreshWeekly(pane) {
    if (!pane?.chart) return;
    const ids = WPLOT.filter((id) => pane.wanted.has(id) || pane.active.has(id));
    const systems = ["pair-long", "pair-short"].filter((id) => pane.systems.has(id));
    if (!ids.length && !systems.length) return;
    const codes = [pane.symbol].filter(Boolean);
    const legs = WeeklyLegs.fromSymbolOrder(codes);
    pane.linkLegs = legs;
    for (const id of ids) {
      const side = id.endsWith("short") ? -1 : 1;
      const leg = WeeklyLegs.legForIndicator(legs, id.includes("link") ? id : (side < 0 ? "w_link_short" : "w_link_long"));
      const self = WeeklyLegs.selfFor(codes, side);
      if (self.length < 4 || leg.opp.length < 4 || leg.fut.length < 4) continue;
      try {
        const q = new URLSearchParams({ self, opp: leg.opp, fut: leg.fut });
        const res = await fetch(`/api/pair/plots?${q}`);
        const data = await res.json();
        const entry = pane.active.get(id);
        if (res.ok && entry && typeof entry.handle.setPlots === "function") entry.handle.setPlots(data.payload || null);
      } catch (err) { console.error(err); }
    }
  }

  function mount(pane, panel, frame) {
    pane.panelEl = panel;
    pane.frame = frame;
    pane.symbol = frame.symbol;
    panel._btPane = pane;
    build(pane);
    if (pane.symbol) prefetch(pane.symbol).then(() => {
      if (pane.chart) attach(pane, pane.chart, pane.candleSeries);
    });
  }

  function syncSymbol(frame) {
    for (const pane of frame.panes || []) {
      pane.symbol = frame.symbol;
      pane.frame = frame;
      build(pane);
    }
    if (frame.symbol) {
      prefetch(frame.symbol).then(() => {
        for (const pane of frame.panes || []) {
          if (pane.chart) attach(pane, pane.chart, pane.candleSeries);
        }
      });
    }
  }

  let onChoice = null;

  function notify(pane) {
    if (typeof onChoice === "function") onChoice(pane?.frame);
  }

  function chosen(frame) {
    const pane = frame?.panes?.[0];
    const ids = [];
    if (pane?.systems?.has("pair-long")) ids.push("pair-long");
    if (pane?.systems?.has("pair-short")) ids.push("pair-short");
    if (pane?.wanted?.has("fx_sig_v6")) ids.push("fx_sig_v6");
    if (pane?.wanted?.has("fx_sig_pair")) ids.push("fx_sig_pair");
    if (pane?.wanted?.has("fx_sig_zd")) ids.push("fx_sig_zd");
    return { ids, pair: ids.some((id) => id.startsWith("pair")) };
  }

  loadCatalog().then(() => {
    for (const pane of document.querySelectorAll(".ind-panel")) {
      const owner = pane._btPane;
      if (owner) build(owner);
    }
  });

  return {
    blankPane, mount, attach, syncSymbol, prefetch, STYLES, chosen,
    setOnChoice(fn) { onChoice = fn; },
  };
})();

if (typeof globalThis !== "undefined") globalThis.BtSide = BtSide;
