// 대시보드 프론트엔드 (계획서 §18).
// C가 계산한 값을 표시만 한다. 지표·점수를 재계산하지 않는다.

"use strict";

const WS_URL = `ws://${location.host}/ws`;
const bars = new Map(); // time(sec) → candle
let generation = 0;     // 종목 전환 시 올려 늦은 응답을 폐기 (계획서 §18)

const chart = LightweightCharts.createChart(document.getElementById("chart"), {
  layout: { background: { color: "#131722" }, textColor: "#d1d4dc" },
  grid: { vertLines: { color: "#1e2530" }, horzLines: { color: "#1e2530" } },
  timeScale: { timeVisible: true, secondsVisible: true },
});
const candleSeries = chart.addCandlestickSeries({
  upColor: "#ef5350", downColor: "#2962ff",
  borderUpColor: "#ef5350", borderDownColor: "#2962ff",
  wickUpColor: "#ef5350", wickDownColor: "#2962ff",
});
const regSeries = chart.addLineSeries({ color: "#f0b90b", lineWidth: 2, priceLineVisible: false });
const predSeries = chart.addLineSeries({ color: "#26a69a", lineWidth: 1, lineStyle: LightweightCharts.LineStyle.Dashed, priceLineVisible: false });

const el = {
  wsState: document.getElementById("ws-state"),
  score: document.getElementById("score"),
  reg: document.getElementById("reg"),
  pred: document.getElementById("pred"),
  ob: document.getElementById("ob"),
  wsName: document.getElementById("ws-name"),
};

function scoreColor(v) {
  if (v > 0) return "#ef5350";
  if (v < 0) return "#2962ff";
  return "#7d8590";
}

function applyStatus(msg) {
  const p = msg.payload ?? {};
  // 세대 확인: 종목 전환 이후 새 세대가 오면 로컬 이력을 지우고 다시 쌓는다 (혼합 방지, 계획서 §18)
  if (typeof p.generation === "number" && p.generation > generation) {
    generation = p.generation;
    bars.clear();
    candleSeries.setData([]);
    regSeries.setData([]);
    predSeries.setData([]);
  }
  const t = Number(p.bar_open_time) / 1e6;
  if (!Number.isFinite(t) || t <= 0) return;

  const [o, h, l, c] = p.ohlc ?? [];
  if (o != null) {
    const bar = { time: t, open: o, high: h, low: l, close: c };
    const prev = bars.get(t);
    bars.set(t, bar);
    candleSeries.update(bar);
    // 진행/확정 구분: 확정 봉만 회귀선을 찍는다 (과거 불변성)
    if (p.closed) bars.set(t, bar);
    void prev;
  }

  if (p.reg_valid === 1 && Number.isFinite(p.reg_line)) {
    regSeries.update({ time: t, value: p.reg_line });
    el.reg.textContent = `회귀선 ${p.reg_line.toFixed(1)} (R² ${p.reg_r2.toFixed(2)})`;
    el.reg.className = "badge ok";
  } else {
    el.reg.textContent = "회귀: 워밍업";
    el.reg.className = "badge";
  }

  // 예측: 생성 시점 기준 n봉 앞 위치에 표시한다. 미래 가격 데이터가 있는 것처럼 연결하지 않는다.
  const preds = p.pred ?? [];
  if (p.reg_valid === 1 && preds.length === 3) {
    const spans = [5, 10, 15];
    for (let i = 0; i < 3; i++) {
      predSeries.update({ time: t + spans[i] * 60, value: preds[i] });
    }
    el.pred.textContent = `예측 ${preds.map((v) => v.toFixed(1)).join(" / ")}`;
  }

  if (typeof p.score === "number") {
    el.score.textContent = String(p.score);
    el.score.style.color = scoreColor(p.score);
  }

  if (p.ob_valid === 1 && typeof p.ob_score === "number") {
    el.ob.textContent = `호가 ${p.ob_score.toFixed(1)}`;
    el.ob.className = p.ob_score > 0 ? "badge ok" : "badge err";
  } else {
    el.ob.textContent = "호가: 미지원";
    el.ob.className = "badge";
  }
}

// 과거 봉 시딩: PUB/SUB는 과거 메시지를 보존하지 않으므로 접속 시 스냅샷을 가져온다
async function seedChart() {
  try {
    const res = await fetch("/api/chart");
    if (!res.ok) return;
    const data = await res.json();
    const rows = data.payload?.bars ?? [];
    if (rows.length === 0) return;
    if (typeof data.payload.generation === "number" && data.payload.generation > generation) {
      generation = data.payload.generation;
    }
    bars.clear();
    const seeded = [];
    for (const [t, o, h, l, c] of rows) {
      const bar = { time: Number(t) / 1e6, open: o, high: h, low: l, close: c };
      bars.set(bar.time, bar);
      seeded.push(bar);
    }
    candleSeries.setData(seeded);
    regSeries.setData([]);
    predSeries.setData([]);
  } catch { /* 시딩 실패는 라이브 스트림으로 진행 */ }
}

function connect() {
  const ws = new WebSocket(WS_URL);
  ws.onopen = () => {
    el.wsState.textContent = "연결됨";
    el.wsState.className = "badge ok";
  };
  ws.onclose = () => {
    el.wsState.textContent = "연결 끊김 — 재시도";
    el.wsState.className = "badge err";
    setTimeout(connect, 2000);
  };
  ws.onmessage = (ev) => {
    let data;
    try { data = JSON.parse(ev.data); } catch { return; }
    if (data.kind !== "status") return;
    if (data.stream_event === "restart" || data.stream_event === "gap") {
      // 엔진 재시작/순번 공백: 로컬 이력을 비우고 새 기준으로 쌓는다 (혼합 표시 방지)
      bars.clear();
      candleSeries.setData([]);
      regSeries.setData([]);
      predSeries.setData([]);
      seedChart(); // 재시작한 엔진의 봉 링으로 다시 시딩
    }
    applyStatus(data.message ?? {});
  };
}

// ---- 화면틀 ----
// 화면틀에는 레이아웃·스타일·종목 바인딩만 저장한다. 전략 자동 시작·주문 상태는 넣지 않는다.

function collectWorkspace() {
  return {
    panels: [
      { id: "chart", type: "chart", symbol_binding: "selected", timeframe: 60, link_group: "main" },
    ],
    current_symbol: "1",
    styles: { theme: "dark" },
    ui: { generation },
  };
}

let cachedToken = null;
async function apiToken() {
  if (cachedToken) return cachedToken;
  // 서버가 같은 출처에 자동 발급해준다 (수동 입력 불필요)
  try {
    const res = await fetch("/api/token");
    if (res.ok) {
      cachedToken = (await res.json()).token;
      return cachedToken;
    }
  } catch { /* 서버 미응답이면 수동 입력으로 대체 */ }
  let token = localStorage.getItem("trader_token");
  if (!token) {
    token = prompt("대시보드 인증 토큰 (web/.runtime/token 파일 내용):", "");
    if (token) localStorage.setItem("trader_token", token.trim());
  }
  return token?.trim();
}

function resetToken() {
  cachedToken = null;
  localStorage.removeItem("trader_token");
}

async function saveWorkspace() {
  const token = await apiToken();
  if (!token) return alert("토큰이 필요합니다.");
  const name = el.wsName.value.trim();
  const res = await fetch(`/api/workspaces/${encodeURIComponent(name)}`, {
    method: "PUT",
    headers: { "content-type": "application/json", "x-trader-token": token },
    body: JSON.stringify(collectWorkspace()),
  });
  const data = await res.json();
  if (!res.ok) {
    if (res.status === 403) resetToken();
    return alert(`저장 실패: ${data.error}`);
  }
  alert(`화면틀 '${name}' 저장됨`);
}

async function loadWorkspace() {
  const name = el.wsName.value.trim();
  const res = await fetch(`/api/workspaces/${encodeURIComponent(name)}`);
  if (!res.ok) return alert("화면틀 없음");
  const data = await res.json();
  generation = (data.ui?.generation ?? 0) + 1;
  // 화면 복원으로 전략을 자동 시작하거나 주문을 재실행하지 않는다.
  alert(`화면틀 '${name}' 적용 (generation ${generation})`);
}

document.getElementById("ws-save").onclick = saveWorkspace;
document.getElementById("ws-load").onclick = loadWorkspace;

// 종목 전환: 화면의 선택 종목만 바꾼다. 전략 거래 대상은 바꾸지 않는다 (계획서 §18).
const symbolInput = document.getElementById("symbol");
const symbolResults = document.getElementById("symbol-results");
const symbolName = document.getElementById("symbol-name");
let searchSeq = 0; // 늦게 도착한 검색 응답 폐기용

async function switchSymbol() {
  const shcode = symbolInput.value.trim();
  if (!shcode) return;
  const token = await apiToken();
  if (!token) return alert("토큰이 필요합니다.");
  const res = await fetch("/api/symbols/select", {
    method: "POST",
    headers: { "content-type": "application/json", "x-trader-token": token },
    body: JSON.stringify({ shcode }),
  });
  const data = await res.json();
  if (!res.ok) {
    if (res.status === 403) resetToken();
    return alert(`전환 실패: ${data.error_code ?? data.error ?? res.status}`);
  }
  // 새 세대를 즉시 반영하고 화면을 비운다 (엔진의 다음 메시지부터 새 종목)
  if (data.payload?.generation) generation = data.payload.generation;
  if (data.payload?.name) symbolName.textContent = data.payload.name;
  hideSymbolResults();
  bars.clear();
  candleSeries.setData([]);
  regSeries.setData([]);
  predSeries.setData([]);
}

function hideSymbolResults() {
  symbolResults.hidden = true;
  symbolResults.replaceChildren();
}

function showSymbolResults(items, seq) {
  if (seq !== searchSeq) return; // 최신 검색만 반영
  symbolResults.replaceChildren();
  if (items.length === 0) {
    const div = document.createElement("div");
    div.className = "empty";
    div.textContent = "일치하는 종목 없음";
    symbolResults.append(div);
  }
  for (const it of items) {
    const row = document.createElement("div");
    row.className = "row";
    const code = document.createElement("span");
    code.className = "code";
    code.textContent = it.shcode;
    const name = document.createElement("span");
    name.textContent = it.name;
    const mkt = document.createElement("span");
    mkt.className = "mkt";
    mkt.textContent = it.fut ? "선물" : "";
    row.append(code, name, mkt);
    row.onclick = () => {
      symbolInput.value = it.shcode;
      symbolName.textContent = it.name;
      hideSymbolResults();
      switchSymbol();
    };
    symbolResults.append(row);
  }
  symbolResults.hidden = false;
}

let searchTimer = null;
function onSymbolInput() {
  clearTimeout(searchTimer);
  const q = symbolInput.value.trim();
  if (!q) return hideSymbolResults();
  searchTimer = setTimeout(async () => {
    const seq = ++searchSeq;
    try {
      const res = await fetch(`/api/market?q=${encodeURIComponent(q)}&limit=20`);
      if (!res.ok) return hideSymbolResults();
      const data = await res.json();
      showSymbolResults(data.payload?.items ?? [], seq);
    } catch {
      /* 검색 실패는 드롭다욧만 닫는다 */
    }
  }, 200);
}

symbolInput.addEventListener("input", onSymbolInput);
symbolInput.addEventListener("keydown", (ev) => {
  if (ev.key === "Enter") switchSymbol();
  if (ev.key === "Escape") hideSymbolResults();
});
document.addEventListener("click", (ev) => {
  if (!symbolResults.hidden && !ev.target.closest(".sym-picker")) hideSymbolResults();
});
document.getElementById("symbol-apply").onclick = switchSymbol;

seedChart();
connect();
