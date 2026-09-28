// 대시보드 프론트엔드 (계획서 §18).
// C가 계산한 값을 표시만 한다. 지표·점수를 재계산하지 않는다.

"use strict";

const WS_URL = `ws://${location.host}/ws`;
const bars = new Map(); // time(sec) → candle
let generation = 0;     // 종목 전환 시 올려 늦은 응답을 폐기 (계획서 §18)

// 거래소 시간은 항상 KST(UTC+9, 서머타임 없음) — 라이브러리 기본 UTC 표시를 KST로 맞춘다
const KST_OFFSET_SEC = 9 * 3600;
function kstParts(timeSec) {
  const d = new Date((Number(timeSec) + KST_OFFSET_SEC) * 1000);
  return { y: d.getUTCFullYear(), mo: d.getUTCMonth() + 1, d: d.getUTCDate(), hh: d.getUTCHours(), mm: d.getUTCMinutes(), ss: d.getUTCSeconds() };
}
const pad2 = (n) => String(n).padStart(2, "0");

const chart = LightweightCharts.createChart(document.getElementById("chart"), {
  layout: { background: { color: "#131722" }, textColor: "#d1d4dc" },
  grid: { vertLines: { color: "#1e2530" }, horzLines: { color: "#1e2530" } },
  localization: {
    locale: "ko-KR",
    timeFormatter: (t) => {
      const p = kstParts(t);
      return `${p.y}-${pad2(p.mo)}-${pad2(p.d)} ${pad2(p.hh)}:${pad2(p.mm)}:${pad2(p.ss)}`;
    },
  },
  timeScale: {
    timeVisible: true, secondsVisible: true,
    tickMarkFormatter: (t, tickMarkType) => {
      const p = kstParts(t);
      if (tickMarkType <= 1) return `${p.y}-${pad2(p.mo)}`;
      if (tickMarkType === 2) return `${pad2(p.mo)}-${pad2(p.d)}`;
      if (tickMarkType === 3) return `${pad2(p.hh)}:${pad2(p.mm)}`;
      return `${pad2(p.hh)}:${pad2(p.mm)}:${pad2(p.ss)}`;
    },
  },
});
const candleSeries = chart.addCandlestickSeries({
  upColor: "#ef5350", downColor: "#2962ff",
  borderUpColor: "#ef5350", borderDownColor: "#2962ff",
  wickUpColor: "#ef5350", wickDownColor: "#2962ff",
});
const regSeries = chart.addLineSeries({ color: "#f0b90b", lineWidth: 2, priceLineVisible: false });
// 예측은 원본처럼 지평(5/10/15봉)별 3개 트랙으로 분리한다 — 하나로 합치면 지그재그로 보인다
const predStyles = [
  { color: "#26a69a", lineStyle: LightweightCharts.LineStyle.Dashed },
  { color: "#4db6ac", lineStyle: LightweightCharts.LineStyle.Dotted },
  { color: "#80cbc4", lineStyle: LightweightCharts.LineStyle.SparseDotted },
];
const predSeries = predStyles.map((s) =>
  chart.addLineSeries({ color: s.color, lineWidth: 1, lineStyle: s.lineStyle, priceLineVisible: false }));

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
    for (const s of predSeries) s.setData([]);
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
  // 지평(5/10/15봉)별로 원본처럼 별개 트랙에 찍는다.
  const preds = p.pred ?? [];
  if (p.reg_valid === 1 && preds.length === 3) {
    const spans = [5, 10, 15];
    for (let i = 0; i < 3; i++) {
      predSeries[i].update({ time: t + spans[i] * 60, value: preds[i] });
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

// 과거 봉 시딩: PUB/SUB는 과거 메시지를 보존하지 않으므로 접속 시 스냅샷을 가져온다.
// 링 전체(최대 2일치)를 페이지로 나눠 가져와 합친다.
async function seedChart() {
  try {
    const all = [];
    let back = 0;
    for (let pages = 0; pages < 16; pages++) {
      const res = await fetch(`/api/chart?back_index=${back}`);
      if (!res.ok) return;
      const data = await res.json();
      const p = data.payload ?? {};
      if (typeof p.generation === "number" && p.generation > generation) {
        generation = p.generation;
      }
      const rows = p.bars ?? [];
      const inds = p.ind ?? [];
      for (let ri = 0; ri < rows.length; ri++) {
        const [t, o, h, l, c] = rows[ri];
        all.push({ time: Number(t) / 1e6, open: o, high: h, low: l, close: c, ind: inds[ri] });
      }
      if (!p.next_back_index) break;
      back = p.next_back_index;
    }
    if (all.length === 0) return;
    all.sort((a, b) => a.time - b.time);
    const dedup = all.filter((b, i) => i === 0 || b.time !== all[i - 1].time);
    bars.clear();
    for (const b of dedup) bars.set(b.time, b);
    candleSeries.setData(dedup);

    // 봉별 지표 복원: 스냅샷의 ind 배열로 과거 구간의 회귀선·예측선·배지를 다시 그린다
    const regData = [];
    const predTracks = [[], [], []]; // 지평(5/10/15봉)별 트랙 — 원본의 3개 분리 표시
    const spans = [5, 10, 15];
    for (const b of dedup) {
      const ind = b.ind;
      if (!ind || ind[1] !== 1) continue; // reg_valid 아닌 봉은 건너뜀
      if (Number.isFinite(ind[2])) regData.push({ time: b.time, value: ind[2] });
      for (let i = 0; i < 3; i++) {
        if (Number.isFinite(ind[4 + i])) {
          predTracks[i].push({ time: b.time + spans[i] * 60, value: ind[4 + i] });
        }
      }
    }
    regSeries.setData(regData);
    // setData는 시간 오름차순·중복 불가 — 각 트랙을 정렬하고 같은 시각은 최신 봉의 값을 남긴다
    for (let i = 0; i < 3; i++) {
      const track = predTracks[i];
      track.sort((a, b) => a.time - b.time);
      const dedupT = track.filter((p, k) => k === track.length - 1 || p.time !== track[k + 1].time);
      predSeries[i].setData(dedupT);
    }

    // 마지막 봉의 값으로 배지를 복원한다
    const last = dedup[dedup.length - 1]?.ind;
    if (last) {
      el.score.textContent = String(last[7]);
      el.score.style.color = scoreColor(last[7]);
      if (last[1] === 1) {
        el.reg.textContent = `회귀선 ${last[2].toFixed(1)} (R² ${last[3].toFixed(2)})`;
        el.reg.className = "badge ok";
        el.pred.textContent = `예측 ${last[4].toFixed(1)} / ${last[5].toFixed(1)} / ${last[6].toFixed(1)}`;
      }
      if (last[8] === 1) {
        el.ob.textContent = `호가 ${last[9].toFixed(1)}`;
        el.ob.className = last[9] > 0 ? "badge ok" : "badge err";
      }
    }
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
      for (const s of predSeries) s.setData([]);
      seedChart(); // 재시작한 엔진의 봉 링으로 다시 시딩
    }
    try {
      applyStatus(data.message ?? {});
    } catch {
      /* 시딩 직후 과거 봉의 늦은 갱신 등 표시상 무해한 순서 오류는 무시한다 */
    }
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
  for (const s of predSeries) s.setData([]);
  seedChart(); // 엔진이 새 종목을 백필해 두었으므로 스냅샷으로 채운다
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
