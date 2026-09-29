// 대시보드 프론트엔드 (계획서 §18).
// C가 계산한 값을 표시만 한다. 지표·점수를 재계산하지 않는다.
// 패널 매니저: 칸(pane)마다 차트 1개를 두고, 지표는 렌더러(RENDERERS)를
// 칸에 활성화해 표시한다. 칸 도구줄의 지표 칩/레이어 칩은 엔진 스냅샷의
// indicators 매니페스트에서 생성한다. 기본 상태 = 칸 1개, 지표 없음(맨 차트).

"use strict";

const WS_URL = `ws://${location.host}/ws`;
let generation = 0;     // 종목 전환 시 올려 늦은 응답을 폐기 (계획서 §18)

// ---- 공유 데이터 피드 ----
// 모든 패널이 같은 캐시를 본다. 렌더러에는 feedCtx로 전달한다.
const bars = new Map();   // time(sec) → candle
const barInd = new Map(); // time → { predDir[3], regValid, r2, regFlat, finalValid, finalState, day,
                          //          mktValid, mkt, obValid, obScore, smaValid, sma[3],
                          //          score, pred, resid, pvol, memItem, pstItem }
const barSeq = [];        // 시각 오름차순 목록
const barPos = new Map(); // time → barSeq 인덱스
let tickRaw = 5;          // raw 단위 틱 크기 (엔진 tick 키가 갱신; 선물 5, 주식 100)

// 거래소 시간은 항상 KST(UTC+9, 서머타임 없음) — 라이브러리 기본 UTC 표시를 KST로 맞춘다
const KST_OFFSET_SEC = 9 * 3600;
function kstParts(timeSec) {
  const d = new Date((Number(timeSec) + KST_OFFSET_SEC) * 1000);
  return { y: d.getUTCFullYear(), mo: d.getUTCMonth() + 1, d: d.getUTCDate(), hh: d.getUTCHours(), mm: d.getUTCMinutes(), ss: d.getUTCSeconds() };
}
const pad2 = (n) => String(n).padStart(2, "0");

// 표시용 가격 포맷: 엔진 값은 raw(실제×100)이므로 ÷100. 소수 자리는 틱으로 결정
// (선물 tick 5 raw = 0.05pt → 2자리, 주식 tick 100 raw = 1원 → 0자리)
function fmtPrice(raw) {
  if (!Number.isFinite(raw)) return "-";
  const v = raw / 100;
  return tickRaw >= 100
    ? v.toLocaleString("ko-KR", { maximumFractionDigits: 0 })
    : v.toFixed(2);
}

// ④ 세션 가드 (원본 v16:224-226): 10봉 전 봉이 다른 세션이면 결과 띠 무효(회색)
function sameSession(src, cur) {
  if (!src) return false;
  if (!Number.isFinite(src.day) || !Number.isFinite(cur.day)) return true; // day 미제공 구형 엔진 호환
  return src.day === cur.day;
}

function noteBar(t, bar) {
  bars.set(t, bar);
  if (barPos.has(t)) return;
  // 라이브는 대부분 뒤에 붙는다. 늦은 정정 등 순서 역행만 이진 삽입으로 처리한다.
  if (barSeq.length === 0 || t > barSeq[barSeq.length - 1]) {
    barPos.set(t, barSeq.length);
    barSeq.push(t);
    return;
  }
  let lo = 0, hi = barSeq.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (barSeq[mid] < t) lo = mid + 1; else hi = mid;
  }
  barSeq.splice(lo, 0, t);
  for (let i = lo; i < barSeq.length; i++) barPos.set(barSeq[i], i);
}
// barSeq[pos]까지 최근 n개 봉 (오름차순) — ⑥⑦ 5봉 규칙에 사용
function recentBars(pos, n) {
  const out = [];
  if (pos === undefined) return out;
  for (let i = Math.max(0, pos - n + 1); i <= pos; i++) {
    const b = bars.get(barSeq[i]);
    if (b) out.push(b);
  }
  return out;
}

// 렌더러에 건네는 공유 컨텍스트. 참조는 고정하고 내용만 갱신한다
// (렌더러가 마지막 ctx를 보관해 레이어 토글 시 재구축에 쓴다).
const feedCtx = {
  bars, barInd, barSeq, barPos,
  tickRaw: () => tickRaw,
  sameSession,
  recentBars,
};

// ---- 패널 매니저 ----
// Pane: { id, el, toolsEl, chart, candleSeries, heightFrac,
//         active: Map<indId, { renderer, handle, layers: {layerId: bool} }> }

const RENDERERS = { mirae_v16: MiraeLayers.MiraeRenderer, sma: SmaLayers.SmaRenderer };

// 지표 매니페스트 (엔진 스냅샷의 indicators 배열) — 시딩 전에는 비어 있다
let indicatorManifest = [];

const panesEl = document.getElementById("panes");
const panes = [];
let nextPaneId = 1;
const MIN_PANE_FRAC = 0.1; // 드래그로 줄일 수 있는 칸 최소 높이 비율

function chartOptions() {
  return {
    layout: { background: { color: "#131722" }, textColor: "#d1d4dc" },
    grid: { vertLines: { color: "#1e2530" }, horzLines: { color: "#1e2530" } },
    localization: {
      locale: "ko-KR",
      priceFormatter: (p) => fmtPrice(p),
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
  };
}

// 칸 높이는 #panes 대비 % (pane.heightFrac 0..1). 인접 칸 사이의 리사이즈바를
// 드래그해 조절한다. 높이를 바꾼 뒤에는 차트 크기를 다시 맞춘다.
function syncPaneSize(pane) {
  pane.el.style.height = `${pane.heightFrac * 100}%`;
  pane.chart.resize(pane.el.clientWidth, pane.el.clientHeight);
}

function createPane(heightFrac = 1) {
  const div = document.createElement("div");
  div.className = "pane";
  div.style.height = `${heightFrac * 100}%`;
  const tools = document.createElement("div");
  tools.className = "tools";
  div.append(tools);
  panesEl.append(div);
  const chart = LightweightCharts.createChart(div, chartOptions());
  const candleSeries = chart.addCandlestickSeries({
    upColor: "#ef5350", downColor: "#2962ff",
    borderUpColor: "#ef5350", borderDownColor: "#2962ff",
    wickUpColor: "#ef5350", wickDownColor: "#2962ff",
  });
  const pane = { id: nextPaneId++, el: div, toolsEl: tools, chart, candleSeries,
                 heightFrac, active: new Map() };
  panes.push(pane);
  // 공유 캐시가 이미 있으면 새 칸에 그대로 백필한다 (화면틀 적용·칸 추가 시 재시딩 불필요)
  if (barSeq.length) candleSeries.setData(barSeq.map((t) => bars.get(t)).filter(Boolean));
  buildPaneTools(pane);
  return pane;
}

function removePane(pane) {
  const i = panes.indexOf(pane);
  if (i < 0) return;
  panes.splice(i, 1);
  pane.active.clear();
  pane.chart.remove();
  pane.el.remove();
  // 남은 칸이 빠진 높이를 비율대로 나눠 갖는다
  const total = panes.reduce((s, p) => s + p.heightFrac, 0);
  if (panes.length && total > 0) {
    for (const p of panes) {
      p.heightFrac /= total;
      syncPaneSize(p);
    }
  }
  rebuildResizeBars();
  updateBadgeVisibility();
}

// 인접 칸 경계의 리사이즈바를 현재 칸 목록에 맞춰 다시 단다
function rebuildResizeBars() {
  panesEl.querySelectorAll(".resizebar").forEach((e) => e.remove());
  for (let i = 0; i + 1 < panes.length; i++) {
    const bar = document.createElement("div");
    bar.className = "resizebar";
    bar.textContent = "⠿";
    bar.title = "드래그로 크기 조절";
    attachResize(bar, panes[i], panes[i + 1]);
    panes[i].el.append(bar); // 위 칸의 아래쪽 가장자리에 겹쳐 둔다
  }
}

function attachResize(bar, above, below) {
  bar.addEventListener("mousedown", (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    const totalPx = panesEl.clientHeight;
    if (!totalPx) return;
    const startY = ev.clientY;
    const a0 = above.heightFrac, b0 = below.heightFrac;
    const move = (e2) => {
      const d = (e2.clientY - startY) / totalPx;
      const sum = a0 + b0;
      const a = Math.min(Math.max(a0 + d, MIN_PANE_FRAC), sum - MIN_PANE_FRAC);
      above.heightFrac = a;
      below.heightFrac = sum - a;
      syncPaneSize(above);
      syncPaneSize(below);
    };
    const up = () => {
      removeEventListener("mousemove", move);
      removeEventListener("mouseup", up);
    };
    addEventListener("mousemove", move);
    addEventListener("mouseup", up);
  });
}

// 칸 도구줄: [없음] [지표 칩…] [활성 지표의 레이어 칩…] [×]
// 지표 칩은 매니페스트에서 만들고, 레이어 칩은 켜진 지표의 layers(defaultOn 반영)에서 만든다.
function buildPaneTools(pane) {
  const tools = pane.toolsEl;
  tools.replaceChildren();

  const noneChip = document.createElement("button");
  noneChip.className = `chip${pane.active.size === 0 ? " on" : ""}`;
  noneChip.textContent = "없음";
  noneChip.title = "이 칸의 지표를 모두 끈다";
  noneChip.onclick = () => {
    for (const id of [...pane.active.keys()]) deactivateIndicator(pane, id);
    buildPaneTools(pane);
    updateBadgeVisibility();
  };
  tools.append(noneChip);

  for (const meta of indicatorManifest) {
    if (!RENDERERS[meta.id]) continue; // 이 프론트가 모르는 지표는 건너뛴다
    const on = pane.active.has(meta.id);
    const chip = document.createElement("button");
    chip.className = `chip${on ? " on" : ""}`;
    chip.textContent = meta.name ?? meta.id;
    chip.onclick = () => {
      if (pane.active.has(meta.id)) deactivateIndicator(pane, meta.id);
      else activateIndicator(pane, meta.id);
      buildPaneTools(pane);
      updateBadgeVisibility();
    };
    tools.append(chip);
  }

  for (const [indId, entry] of pane.active) {
    const meta = indicatorManifest.find((m) => m.id === indId);
    for (const layer of meta?.layers ?? []) {
      const on = entry.layers[layer.id] !== false;
      const chip = document.createElement("button");
      chip.className = `chip layer${on ? " on" : ""}`;
      chip.textContent = layer.name ?? layer.id;
      chip.title = `${meta.name ?? indId} 레이어`;
      chip.onclick = () => {
        const next = entry.layers[layer.id] === false;
        entry.layers[layer.id] = next;
        entry.handle.setLayers({ [layer.id]: next });
        chip.classList.toggle("on", next);
      };
      tools.append(chip);
    }
  }

  const del = document.createElement("button");
  del.className = "chip del";
  del.textContent = "×";
  del.title = "이 차트 삭제";
  del.onclick = () => removePane(pane);
  tools.append(del);
}

// 지표 렌더러를 칸에 활성화하고 현재 캐시로 백필한다.
// savedLayers(화면틀)가 있으면 그 값을, 없으면 매니페스트 defaultOn을 적용한다.
function activateIndicator(pane, indId, savedLayers) {
  if (pane.active.has(indId)) return;
  const renderer = RENDERERS[indId];
  if (!renderer) return;
  const handle = renderer.createHandle(pane.chart, pane.candleSeries);
  const meta = indicatorManifest.find((m) => m.id === indId);
  const layers = {};
  for (const l of meta?.layers ?? []) layers[l.id] = savedLayers?.[l.id] ?? (l.defaultOn !== false);
  for (const [k, v] of Object.entries(savedLayers ?? {})) layers[k] = !!v; // 매니페스트에 없는 저장 키도 보존
  pane.active.set(indId, { renderer, handle, layers });
  handle.setLayers(layers);
  handle.applySeed(feedCtx);
}

function deactivateIndicator(pane, indId) {
  const entry = pane.active.get(indId);
  if (!entry) return;
  pane.active.delete(indId);
  if (typeof entry.handle.destroy === "function") entry.handle.destroy();
  else entry.handle.clear();
}

function anyMiraeActive() {
  return panes.some((pane) => pane.active.has("mirae_v16"));
}

function resetIndicators() {
  bars.clear();
  barInd.clear();
  barSeq.length = 0;
  barPos.clear();
  tickRaw = 5;
  for (const pane of panes) {
    pane.candleSeries.setData([]);
    for (const { handle } of pane.active.values()) handle.clear();
  }
}

const el = {
  wsState: document.getElementById("ws-state"),
  score: document.getElementById("score"),
  reg: document.getElementById("reg"),
  pred: document.getElementById("pred"),
  ob: document.getElementById("ob"),
  final: document.getElementById("final"),
  wsName: document.getElementById("ws-name"),
  miraeBadges: document.getElementById("mirae-badges"),
};

function scoreTextColor(v) {
  if (v > 0) return "#ef5350";
  if (v < 0) return "#2962ff";
  return "#7d8590";
}

// ⑤ 매매 상태 배지 (운영최종상태 −2..+2)
function updateFinalBadge(valid, state) {
  if (!valid) {
    el.final.textContent = "상태: 워밍업";
    el.final.style.color = "";
    return;
  }
  el.final.textContent = state >= 2 ? "매수강" : state === 1 ? "매수"
    : state <= -2 ? "매도강" : state === -1 ? "매도" : "관망";
  el.final.style.color = state >= 1 ? "#ef5350" : state <= -1 ? "#6aa9ff" : "#7d8590";
}

// 헤더 지표 배지는 미래곡선이 한 칸이라도 켜져 있을 때만 보인다
function updateBadgeVisibility() {
  el.miraeBadges.hidden = !anyMiraeActive();
  if (!el.miraeBadges.hidden) restoreHeaderBadges();
}

// 시딩/지표 활성화 직후: 마지막 봉의 캐시 값으로 배지를 복원한다
function restoreHeaderBadges() {
  const lastT = barSeq[barSeq.length - 1];
  const ind = lastT === undefined ? undefined : barInd.get(lastT);
  if (!ind || !anyMiraeActive()) return;
  if (Number.isFinite(ind.score)) {
    el.score.textContent = String(ind.score);
    el.score.style.color = scoreTextColor(ind.score);
  }
  updateFinalBadge(ind.finalValid, ind.finalState);
  // 회귀선 배지는 라이브 경로(applyStatus의 p.reg_line)와 같은 출처를 쓴다
  if (ind.regValid && Number.isFinite(ind.regLine)) {
    el.reg.textContent = `회귀선 ${fmtPrice(ind.regLine)} (R² ${ind.r2.toFixed(2)})`;
    el.reg.className = "badge ok";
    if (Array.isArray(ind.pred) && ind.pred.every(Number.isFinite)) {
      el.pred.textContent = `예측 ${ind.pred.map((v) => fmtPrice(v)).join(" / ")}`;
    }
  }
  if (ind.obValid) {
    el.ob.textContent = `호가 ${ind.obScore.toFixed(1)}`;
    el.ob.className = ind.obScore > 0 ? "badge ok" : "badge err";
  }
}

function applyStatus(msg) {
  const p = msg.payload ?? {};
  // 세대 확인: 종목 전환 이후 새 세대가 오면 로컬 이력을 지우고 다시 쌓는다 (혼합 방지, 계획서 §18)
  if (typeof p.generation === "number" && p.generation > generation) {
    generation = p.generation;
    resetIndicators();
  }
  const t = Number(p.bar_open_time) / 1e6;
  if (!Number.isFinite(t) || t <= 0) return;

  const [o, h, l, c] = p.ohlc ?? [];
  if (o != null) {
    noteBar(t, { time: t, open: o, high: h, low: l, close: c });
    for (const pane of panes) pane.candleSeries.update(bars.get(t));
  }
  const ind = MiraeLayers.barIndFromPayload(p);
  ind.score = Number.isFinite(p.score) ? p.score : NaN;
  ind.pred = Array.isArray(p.pred) ? p.pred : undefined;
  ind.resid = p.resid ?? 0;
  ind.pvol = p.pvol ?? 0;
  barInd.set(t, ind);
  if (typeof p.tick === "number" && Number.isFinite(p.tick) && p.tick > 0) tickRaw = p.tick;

  // ⑥⑦ 봉별 아이템은 렌더러 on/off와 무관하게 캐시에 기록한다 (복원 대비)
  const pos = barPos.get(t);
  const memItem = MiraeLayers.memItemFromPayload(t, p.mem, recentBars(pos, 5));
  if (memItem !== undefined) ind.memItem = memItem;
  const pstItem = MiraeLayers.pstItemFromPayload(t, p.pst, recentBars(pos, 5));
  if (pstItem !== undefined) ind.pstItem = pstItem;

  // 지표 표시는 각 칸의 활성 렌더러가 담당한다
  for (const pane of panes) {
    for (const { handle } of pane.active.values()) handle.applyLive(p, feedCtx);
  }

  // 헤더 배지는 미래곡선이 한 칸이라도 켜져 있을 때만 갱신한다
  if (!anyMiraeActive()) return;
  const preds = p.pred ?? [];
  if (ind.regValid && Number.isFinite(ind.regFlat)) {
    el.reg.textContent = `회귀선 ${fmtPrice(p.reg_line)} (R² ${ind.r2.toFixed(2)})`;
    el.reg.className = "badge ok";
    if (preds.length === 3 && preds.every(Number.isFinite)) {
      el.pred.textContent = `예측 ${preds.map((v) => fmtPrice(v)).join(" / ")}`;
    }
  } else {
    el.reg.textContent = "회귀: 워밍업";
    el.reg.className = "badge";
  }
  if (typeof p.score === "number") {
    el.score.textContent = String(p.score);
    el.score.style.color = scoreTextColor(p.score);
  }
  updateFinalBadge(ind.finalValid, ind.finalState);
  if (p.ob_valid === 1 && typeof p.ob_score === "number") {
    el.ob.textContent = `호가 ${p.ob_score.toFixed(1)}`;
    el.ob.className = p.ob_score > 0 ? "badge ok" : "badge err";
  } else {
    el.ob.textContent = "호가: 미지원";
    el.ob.className = "badge";
  }
}

// ⑥ 이벤트([time, valid, dir, price, t1..3, u1..3, l1..3, ...])로 봉별 수평 세그먼트 복원
function buildMemItems(dedup, events) {
  const sorted = events
    .map((e) => ({ time: Number(e[0]) / 1e6, valid: e[1] === 1, dir: e[2], price: e[3],
                    t: e.slice(4, 7), u: e.slice(7, 10), l: e.slice(10, 13) }))
    .filter((e) => Number.isFinite(e.time))
    .sort((a, b) => a.time - b.time);
  const items = [];
  let ei = -1;
  for (let i = 0; i < dedup.length; i++) {
    const t = dedup[i].time;
    while (ei + 1 < sorted.length && sorted[ei + 1].time <= t) ei++;
    if (ei < 0 || !sorted[ei].valid) continue;
    const ev = sorted[ei];
    const upd = ev.time === t; // 갱신 봉에는 범위선 숨김
    const flags = upd ? { showU: false, showL: false }
      : MiraeLayers.rangeFlags(recentFromRows(dedup, i, 5), ev.t[2]);
    items.push({ time: t, value: ev.t[1], dir: ev.dir, price: ev.price,
                 t: ev.t, u: ev.u, l: ev.l, showU: flags.showU, showL: flags.showL, upd });
  }
  return items;
}

// ⑦ 이벤트([time, valid, dir, t1..3, u1..3, l1..3])로 봉별 수평 세그먼트 복원
function buildPstItems(dedup, events) {
  const sorted = events
    .map((e) => ({ time: Number(e[0]) / 1e6, valid: e[1] === 1, dir: e[2],
                    t: e.slice(3, 6), u: e.slice(6, 9), l: e.slice(9, 12) }))
    .filter((e) => Number.isFinite(e.time))
    .sort((a, b) => a.time - b.time);
  const items = [];
  let ei = -1;
  for (let i = 0; i < dedup.length; i++) {
    const t = dedup[i].time;
    while (ei + 1 < sorted.length && sorted[ei + 1].time <= t) ei++;
    if (ei < 0 || !sorted[ei].valid) continue;
    const ev = sorted[ei];
    const flags = MiraeLayers.rangeFlags(recentFromRows(dedup, i, 5), ev.t[2]);
    items.push({ time: t, value: ev.t[1], dir: ev.dir,
                 t: ev.t, u: ev.u, l: ev.l, showU: flags.showU, showL: flags.showL });
  }
  return items;
}

// 시딩용: dedup 행 기준 최근 n개 (오름차순)
function recentFromRows(rows, i, n) {
  return rows.slice(Math.max(0, i - n + 1), i + 1);
}

// 과거 봉 시딩: PUB/SUB는 과거 메시지를 보존하지 않으므로 접속 시 스냅샷을 가져온다.
// 링 전체(최대 2일치)를 페이지로 나눠 가져와 합친다.
async function seedChart() {
  try {
    const all = [];
    const memEvents = [];
    const pstEvents = [];
    let back = 0;
    for (let pages = 0; pages < 16; pages++) {
      const res = await fetch(`/api/chart?back_index=${back}`);
      if (!res.ok) return;
      const data = await res.json();
      const p = data.payload ?? {};
      if (typeof p.generation === "number" && p.generation > generation) {
        generation = p.generation;
      }
      // 지표 매니페스트: 첫 페이지에서 한 번 받아 칸 도구줄을 구성한다
      if (pages === 0 && Array.isArray(p.indicators)) setIndicatorManifest(p.indicators);
      const rows = p.bars ?? [];
      const inds = p.ind ?? [];
      for (let ri = 0; ri < rows.length; ri++) {
        const [t, o, h, l, c] = rows[ri];
        all.push({ time: Number(t) / 1e6, open: o, high: h, low: l, close: c, ind: inds[ri] });
      }
      // ⑥⑦ 갱신·저장 이벤트 (희소) — 페이지 경계에서 중복되지 않게 시각으로 모은다
      for (const e of p.mem ?? []) memEvents.push(e);
      for (const e of p.pst ?? []) pstEvents.push(e);
      if (!p.next_back_index) break;
      back = p.next_back_index;
    }
    if (all.length === 0) return;
    all.sort((a, b) => a.time - b.time);
    const dedup = all.filter((b, i) => i === 0 || b.time !== all[i - 1].time);
    resetIndicators();
    for (const b of dedup) noteBar(b.time, b);
    for (const pane of panes) pane.candleSeries.setData(dedup);

    // 봉별 지표 캐시 복원: 스냅샷의 ind 배열로 공유 캐시를 채운다
    for (const b of dedup) {
      const d = MiraeLayers.parseInd(b.ind);
      if (!d) continue;
      if (Number.isFinite(d.tick) && d.tick > 0) tickRaw = d.tick;
      const ind = MiraeLayers.barIndFromInd(d);
      ind.score = d.score;
      ind.pred = d.pred;
      ind.resid = d.resid;
      ind.pvol = d.pvol;
      barInd.set(b.time, ind);
    }
    // ⑥⑦ 이벤트 → 봉별 아이템으로 변환해 캐시에 심는다 (렌더러가 applySeed에서 복원)
    for (const item of buildMemItems(dedup, memEvents)) {
      const ind = barInd.get(item.time);
      if (ind) ind.memItem = item;
    }
    for (const item of buildPstItems(dedup, pstEvents)) {
      const ind = barInd.get(item.time);
      if (ind) ind.pstItem = item;
    }

    // 각 칸의 활성 렌더러가 캐시에서 전체를 다시 그린다
    for (const pane of panes) {
      for (const { handle } of pane.active.values()) handle.applySeed(feedCtx);
    }

    // 마지막 봉의 값으로 배지를 복원한다 (미래곡선이 켜진 칸이 있을 때만)
    restoreHeaderBadges();
  } catch { /* 시딩 실패는 라이브 스트림으로 진행 */ }
}

// 지표 매니페스트를 저장하고 모든 칸의 도구줄을 다시 만든다
function setIndicatorManifest(list) {
  indicatorManifest = list.filter((m) => m && typeof m.id === "string");
  for (const pane of panes) buildPaneTools(pane);
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
      resetIndicators();
      seedChart(); // 재시작한 엔진의 봉 링으로 다시 시딩
    }
    try {
      applyStatus(data.message ?? {});
    } catch {
      /* 시딩 직후 과거 봉의 늦은 갱신 등 표시상 무해한 순서 오류는 무시한다 */
    }
  };
}

// ---- 화면틀 v2 ----
// 화면틀에는 레이아웃·칸별 지표 집합(레이어 설정)·종목 바인딩만 저장한다.
// 전략 자동 시작·주문 상태는 넣지 않는다 (계획서 §18).
// 직렬화/검증은 workspace.js의 순수 함수가 담당한다 (node:test 대상).

function collectWorkspace() {
  return Workspace.serialize(el.wsName.value.trim(), symbolInput.value.trim(),
    panes.map((pane) => ({
      height: pane.heightFrac,
      indicators: [...pane.active.entries()].map(([id, entry]) => ({ id, layers: { ...entry.layers } })),
    })));
}

// 칸 높이 합을 1로 맞춘다 (불러온 화면틀의 height는 상대 비율로만 쓴다)
function normalizeHeights() {
  if (!panes.length) return;
  const total = panes.reduce((s, p) => s + p.heightFrac, 0);
  for (const p of panes) p.heightFrac = total > 0 ? p.heightFrac / total : 1 / panes.length;
  for (const p of panes) syncPaneSize(p);
}

// 화면틀 v2 적용: 공유 데이터(봉/지표 캐시)는 유지하고 칸만 재구성한다.
// 새 칸은 createPane이 캐시에서 캔들을 백필하고 activateIndicator가 applySeed로 복원한다.
function applyWorkspace(parsed) {
  while (panes.length) removePane(panes[panes.length - 1]);
  for (const spec of parsed.panels) {
    const pane = createPane(spec.height);
    for (const ind of spec.indicators) activateIndicator(pane, ind.id, ind.layers);
    buildPaneTools(pane);
  }
  normalizeHeights();
  rebuildResizeBars();
  updateBadgeVisibility();
  // 저장된 종목 바인딩이 현재와 다르면 입력창을 저장 종목으로 맞추고 전환한다
  // (switchSymbol은 입력창 값을 읽는다 — 대입이 먼저다). 전환은 새 스냅샷으로 다시 시딩한다.
  Workspace.restoreSymbol(parsed, symbolInput, switchSymbol);
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
  const parsed = Workspace.parse(data, (id) => id in RENDERERS);
  if (!parsed) return alert("구 버전 화면틀은 적용할 수 없습니다 — 기본 상태를 유지합니다");
  // 화면 복원으로 전략을 자동 시작하거나 주문을 재실행하지 않는다.
  applyWorkspace(parsed);
  alert(`화면틀 '${name}' 적용`);
}

// 칸 추가: 기존 칸 높이를 비율대로 줄여 새 칸 자리를 만든다
function addPane() {
  const newFrac = 1 / (panes.length + 1);
  const scale = 1 - newFrac; // 기존 칸 heightFrac 합은 ≈1
  for (const p of panes) {
    p.heightFrac *= scale;
    syncPaneSize(p);
  }
  createPane(newFrac);
  rebuildResizeBars();
}

document.getElementById("pane-add").onclick = addPane;
document.getElementById("ws-save").onclick = saveWorkspace;
document.getElementById("ws-load").onclick = loadWorkspace;

// 창 크기가 바뀌면 각 칸의 차트 크기를 다시 맞춘다
addEventListener("resize", () => {
  for (const pane of panes) pane.chart.resize(pane.el.clientWidth, pane.el.clientHeight);
});

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
  resetIndicators();
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

// 기본 구성: 칸 1개, 지표 없음 (맨 차트). 지표는 칸 도구줄에서 켠다.
createPane(1);
updateBadgeVisibility();

seedChart();
connect();
