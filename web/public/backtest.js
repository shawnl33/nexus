"use strict";

let rows = [[frame()]];
let selected = rows[0][0];
const charts = new Map();
const chartObservers = [];
let lastResult = null;

function frame() {
  return { id: Math.random().toString(36).slice(2), symbol: "ESZ26", panes: [BtSide.blankPane()] };
}

function paint() {
  for (const ro of chartObservers) ro.disconnect();
  chartObservers.length = 0;
  const grid = document.getElementById("grid");
  grid.replaceChildren();
  charts.clear();
  for (const row of rows) {
    const el = document.createElement("div");
    el.className = "row";
    for (const fr of row) {
      const box = document.createElement("div");
      box.className = "frame" + (fr === selected ? " sel" : "");
      box.addEventListener("mousedown", (e) => {
        if (e.target.closest(".chart")) e.stopPropagation();
      });
      box.addEventListener("click", (e) => {
        if (e.target.closest("input, button, label, .chart")) return;
        if (selected === fr) return;
        selected = fr;
        for (const node of document.querySelectorAll(".frame")) node.classList.remove("sel");
        box.classList.add("sel");
        syncPairFields();
      });
      const bar = document.createElement("div");
      bar.className = "frame-bar";
      const sym = document.createElement("input");
      sym.value = fr.symbol;
      sym.addEventListener("click", (e) => e.stopPropagation());
      sym.addEventListener("change", () => {
        fr.symbol = sym.value.trim().toUpperCase();
        BtSide.syncSymbol(fr);
      });
      const add = document.createElement("button");
      add.type = "button";
      add.textContent = "+ 차트";
      add.addEventListener("click", (e) => {
        e.stopPropagation();
        fr.panes.push(BtSide.blankPane());
        paint();
      });
      const menu = document.createElement("button");
      menu.type = "button";
      menu.className = "panel-toggle" + (fr.panes.some((p) => p.panelOpen) ? " on" : "");
      menu.textContent = "☰";
      menu.title = "사이드바 열기/닫기";
      menu.addEventListener("click", (e) => {
        e.stopPropagation();
        const open = !fr.panes.some((p) => p.panelOpen);
        for (const p of fr.panes) p.panelOpen = open;
        box.querySelectorAll(".ind-panel").forEach((el) => { el.hidden = !open; });
        menu.classList.toggle("on", open);
      });
      bar.append(sym, add, menu);
      const panes = document.createElement("div");
      panes.className = "panes";
      fr.panes.forEach((pane, idx) => {
        const p = document.createElement("div");
        p.className = "pane";
        const panel = document.createElement("div");
        panel.className = "ind-panel";
        panel.hidden = !pane.panelOpen;
        const chart = document.createElement("div");
        chart.className = "chart";
        p.append(panel, chart);
        panes.append(p);
        chart.addEventListener("mousedown", (e) => e.stopPropagation());
        chart.addEventListener("click", (e) => e.stopPropagation());
        const slot = { el: chart, pane, symbol: () => fr.symbol, view: null, bars: null, trades: null };
        pane.onStyle = () => redrawSlot(slot);
        charts.set(fr.id + ":" + idx, slot);
        BtSide.mount(pane, panel, fr);
        const ro = new ResizeObserver(() => {
          const live = slot.view?.chart;
          if (!live) return;
          const w = chart.clientWidth;
          const h = chart.clientHeight;
          if (w > 0 && h > 0) live.applyOptions({ width: w, height: h });
        });
        ro.observe(chart);
        chartObservers.push(ro);
      });
      box.append(bar, panes);
      el.append(box);
    }
    grid.append(el);
  }
  if (lastResult) showResult(lastResult);
  syncPairFields();
}

function redrawSlot(slot) {
  slot.view = draw(slot.el, slot.bars, slot.trades, slot.pane.barStyle);
  if (slot.view) BtSide.attach(slot.pane, slot.view.chart, slot.view.series);
}

function draw(host, bars, trades, barStyle) {
  host.replaceChildren();
  if (!window.LightweightCharts) return null;
  const style = BtSide.STYLES.some((s) => s.id === barStyle) ? barStyle : "candle";
  const kind = style === "line" ? "line" : style === "bar" ? "bar" : "candle";
  const hollow = style === "outline";
  const kstText = (t) => {
    const d = new Date((Number(t) + 9 * 3600) * 1000);
    const p = (n) => String(n).padStart(2, "0");
    return `${d.getUTCFullYear()}-${p(d.getUTCMonth() + 1)}-${p(d.getUTCDate())} ${p(d.getUTCHours())}:${p(d.getUTCMinutes())}`;
  };
  const priceText = (p) => (Number(p) / 100).toLocaleString("ko-KR", { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const chart = LightweightCharts.createChart(host, {
    width: Math.max(host.clientWidth, 1),
    height: Math.max(host.clientHeight, 1),
    layout: { background: { color: "#131722" }, textColor: "#d1d4dc" },
    grid: { vertLines: { color: "#1e2430" }, horzLines: { color: "#1e2430" } },
    localization: {
      locale: "ko-KR",
      timeFormatter: kstText,
      priceFormatter: style === "none" ? (p) => String(Math.round(Number(p) * 100) / 100) : priceText,
    },
    timeScale: { timeVisible: true, secondsVisible: false, tickMarkFormatter: (t) => kstText(t) },
  });
  const series = kind === "line"
    ? chart.addLineSeries({ color: "#d1d4dc", lineWidth: 2 })
    : kind === "bar"
      ? chart.addBarSeries({ upColor: "#ef5350", downColor: "#2962ff" })
      : chart.addCandlestickSeries({
        upColor: hollow ? "rgba(0,0,0,0)" : "#26a69a",
        downColor: hollow ? "rgba(0,0,0,0)" : "#ef5350",
        borderUpColor: "#26a69a",
        borderDownColor: "#ef5350",
        wickUpColor: "#26a69a",
        wickDownColor: "#ef5350",
        borderVisible: hollow,
      });
  const seen = new Set();
  const data = [];
  const raw = (v) => Number(v) * 100;
  for (const b of bars || []) {
    if (!Number.isFinite(b.t) || seen.has(b.t)) continue;
    seen.add(b.t);
    data.push(kind === "line"
      ? { time: b.t, value: raw(b.c) }
      : { time: b.t, open: raw(b.o), high: raw(b.h), low: raw(b.l), close: raw(b.c) });
  }
  data.sort((a, b) => a.time - b.time);
  if (data.length) series.setData(data);
  if (style !== "none" && kind !== "line" && trades) {
    series.setMarkers(trades.filter((t) => t.status === "체결" && seen.has(t.t)).map((t) => ({
      time: t.t,
      position: t.name.includes("청산") ? "aboveBar" : "belowBar",
      color: t.name.includes("청산") ? "#ff8c00" : "#d4ff4a",
      shape: t.name.includes("청산") ? "arrowDown" : "arrowUp",
      text: t.name,
    })));
  }
  series.applyOptions({ visible: style !== "none" });
  chart.timeScale().fitContent();
  const w = host.clientWidth;
  const h = host.clientHeight;
  if (w > 0 && h > 0) chart.applyOptions({ width: w, height: h });
  return { chart, series };
}

function showResult(payload) {
  lastResult = payload;
  tradesTable(payload.trades || []);
  for (const slot of charts.values()) {
    if (slot.symbol() !== payload.symbol) continue;
    slot.bars = payload.bars;
    slot.trades = payload.trades;
    redrawSlot(slot);
  }
}

function kstCell(cell) {
  const t = cell.getValue();
  if (!Number.isFinite(t)) return "";
  return new Date(t * 1000).toLocaleString("ko-KR", { timeZone: "Asia/Seoul", hour12: false });
}

function blankCell(cell) {
  const v = cell.getValue();
  return v == null || v === "" ? "" : v;
}

function pnlCell(cell) {
  const v = cell.getValue();
  if (v == null || v === "") return "";
  const el = document.createElement("span");
  el.className = "pos";
  el.textContent = String(v);
  return el;
}

const tradeTable = new Tabulator("#trades-grid", {
  height: "100%",
  data: [],
  layout: "fitColumns",
  placeholder: "거래내역이 없습니다",
  columns: [
    { title: "시각", field: "t", sorter: "number", minWidth: 168, formatter: kstCell },
    { title: "주문 이름", field: "name", minWidth: 100 },
    { title: "상태", field: "status", width: 80 },
    { title: "가격", field: "price", sorter: "number", hozAlign: "right", width: 100, formatter: blankCell },
    { title: "수량", field: "qty", sorter: "number", hozAlign: "right", width: 72 },
    { title: "손익", field: "pnl", sorter: "number", hozAlign: "right", width: 80, formatter: pnlCell },
  ],
});

function tradesTable(trades) {
  tradeTable.setData((trades || []).map((t) => ({
    t: t.t,
    name: t.name,
    status: t.status,
    price: t.price == null ? null : t.price,
    qty: t.qty,
    pnl: t.pnl == null ? null : t.pnl,
  })));
}

const tradesEl = document.getElementById("trades");
const MIN_TRADES = 32;
const MIN_CHART = 120;
document.getElementById("trades-resize").addEventListener("mousedown", (ev) => {
  ev.preventDefault();
  const startY = ev.clientY;
  const startH = tradesEl.getBoundingClientRect().height;
  const move = (e) => {
    const room = document.getElementById("grid").getBoundingClientRect().height + startH - MIN_CHART;
    const next = Math.min(Math.max(startH + (startY - e.clientY), MIN_TRADES), Math.max(MIN_TRADES, room));
    tradesEl.style.height = `${next}px`;
    tradeTable.setHeight(tradesEl.clientHeight);
  };
  const up = () => {
    removeEventListener("mousemove", move);
    removeEventListener("mouseup", up);
  };
  addEventListener("mousemove", move);
  addEventListener("mouseup", up);
});

function syncPairFields() {
  document.body.classList.toggle("pair", BtSide.chosen(selected).pair);
}

BtSide.setOnChoice((frame) => {
  if (frame === selected) syncPairFields();
});

document.getElementById("screen-new").addEventListener("click", () => {
  rows = [[frame()]];
  selected = rows[0][0];
  paint();
});
document.getElementById("row-add").addEventListener("click", () => {
  const fr = frame();
  rows.push([fr]);
  selected = fr;
  paint();
});
document.getElementById("col-add").addEventListener("click", () => {
  const row = rows[rows.length - 1];
  const fr = frame();
  row.push(fr);
  selected = fr;
  paint();
});

document.getElementById("run").addEventListener("click", async () => {
  const msg = document.getElementById("msg");
  const choice = BtSide.chosen(selected);
  if (choice.ids.length === 0) {
    msg.textContent = "사이드바에서 신호를 켜 주세요";
    return;
  }
  if (choice.ids.length > 1) {
    msg.textContent = "신호는 하나만 켜 주세요";
    return;
  }
  msg.textContent = "실행 중";
  const body = {
    signal: choice.ids[0],
    start: document.getElementById("start").value,
    end: document.getElementById("end").value,
    qty: Number(document.getElementById("qty").value),
    symbol: selected.symbol,
    base: document.getElementById("base").value.trim(),
    opp: document.getElementById("opp").value.trim(),
    fut: document.getElementById("fut").value.trim(),
  };
  try {
    const token = (await (await fetch("/api/token")).json()).token;
    const res = await fetch("/api/backtest", {
      method: "POST",
      headers: { "content-type": "application/json", "x-trader-token": token },
      body: JSON.stringify(body),
    });
    const reply = await res.json();
    const payload = typeof reply.payload === "string" ? JSON.parse(reply.payload) : (reply.payload || reply);
    if (!res.ok || payload.error) {
      msg.textContent = payload.error || reply.error_code || "실행 실패";
      return;
    }
    msg.textContent = `${(payload.trades || []).length}건`;
    showResult(payload);
  } catch (e) {
    msg.textContent = String(e);
  }
});

const todayKst = new Intl.DateTimeFormat("en-CA", {
  timeZone: "Asia/Seoul",
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
}).format(new Date());
document.getElementById("start").value = todayKst;
document.getElementById("end").value = todayKst;

paint();
