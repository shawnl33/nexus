// 대시보드 프론트엔드 (계획서 §18).
// C가 계산한 값을 표시만 한다. 지표·점수를 재계산하지 않는다.
// 칸(pane)마다 차트 1개 + 종목 1개를 두고, 종목별 데이터 캐시는 feed.js가 격리한다.
// 지표는 렌더러(RENDERERS)를 칸에 활성화해 표시하고, 칸 도구줄의 지표 칩/레이어 칩은
// 엔진 스냅샷의 indicators 매니페스트에서 만든다.
// 기본 상태 = 칸 1개 + 종목 미선택(빈 차트에 종목 입력만). 단, 시딩 시 엔진이 관측 중인
// 종목이 하나뿐이면 그 종목을 칸 1에 자동 설정한다 (기존 사용자 흐름 보호).

"use strict";

const WS_URL = `ws://${location.host}/ws`;

// ---- 종목별 데이터 피드 (feed.js) ----
// 캐시는 종목(shcode)별로 격리된다. status의 payload.shcode로 캐시를 골라 갱신한다.
const feed = Feed.create();
let engineWatches = []; // /api/status가 알려준 관측 종목 목록 (구 엔진은 shcode 1개 폴백)

// 거래소 시간은 항상 KST(UTC+9, 서머타임 없음) — 라이브러리 기본 UTC 표시를 KST로 맞춘다
const KST_OFFSET_SEC = 9 * 3600;
function kstParts(timeSec) {
  const d = new Date((Number(timeSec) + KST_OFFSET_SEC) * 1000);
  return { y: d.getUTCFullYear(), mo: d.getUTCMonth() + 1, d: d.getUTCDate(), hh: d.getUTCHours(), mm: d.getUTCMinutes(), ss: d.getUTCSeconds() };
}
const pad2 = (n) => String(n).padStart(2, "0");

// 표시용 가격 포맷: 엔진 값은 raw(실제×100)이므로 ÷100. 소수 자리는 틱으로 결정한다
// (선물 tick 5 raw = 0.05pt → 2자리, 주식 tick 100 raw = 1원 → 0자리). 틱은 종목별 캐시 값.
function fmtPrice(raw, tickRaw = 5) {
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

// ---- 패널 매니저 ----
// Pane: { id, el, toolsEl, pickerEl, chipsEl, chart, candleSeries, heightFrac, syncHandle,
//         symbol, symName, selSeq, searchSeq, searchTimer,
//         symInput, symNameEl, symResults,
//         active: Map<indId, { renderer, handle, layers: {layerId: bool} }> }
// symbol이 ""이면 미선택: 빈 차트에 종목 입력만 보인다 (지표 칩 없음).

const RENDERERS = { mirae_v16: MiraeLayers.MiraeRenderer, sma: SmaLayers.SmaRenderer };

// 지표 매니페스트 (엔진 스냅샷의 indicators 배열) — 시딩 전에는 비어 있다
let indicatorManifest = [];

const panesEl = document.getElementById("panes");
const panes = [];
let nextPaneId = 1;
const MIN_PANE_FRAC = 0.1; // 드래그로 줄일 수 있는 칸 최소 높이 비율

// shcode 없는 메시지(구 엔진·리플레이)의 행선지: 칸 1 종목 → 엔진 첫 관측 종목 → 기본("") 캐시.
// 기본 캐시는 미선택 칸이 본다 — 리플레이처럼 종목을 고를 수 없는 엔진의 기존 동작(전 칸 표시)을 지킨다.
function legacyShcode() {
  return panes[0]?.symbol || engineWatches[0] || "";
}

// 칸이 실제로 보는 캐시: 선택 종목의 캐시. 미선택 칸은 shcode 없는 피드의 기본 캐시다.
function paneCache(pane) {
  return feed.get(pane.symbol || legacyShcode());
}

// 렌더러에 건네는 종목별 컨텍스트. 캐시에 한 번 붙여 재사용한다
// (렌더러가 마지막 ctx를 보관해 레이어 토글 시 재구축에 쓴다 — 참조가 고정되어야 한다).
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

// 칸 간 시간축·크로스헤어 동기화 (pane-sync.js). 종목이 달라도 전 칸에 걸린다.
// 크로스헤어 가로선 값은 칸별 getPrice로 자기 종목 캐시에서 찾는다.
const paneSync = PaneSync.create();

function chartOptions(pane) {
  return {
    layout: { background: { color: "#131722" }, textColor: "#d1d4dc" },
    grid: { vertLines: { color: "#1e2530" }, horzLines: { color: "#1e2530" } },
    localization: {
      locale: "ko-KR",
      priceFormatter: (p) => fmtPrice(p, paneCache(pane)?.tickRaw ?? 5),
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
  const pane = {
    id: nextPaneId++, el: div, toolsEl: tools, heightFrac,
    symbol: "", symName: "", selSeq: 0, searchSeq: 0, searchTimer: null,
    active: new Map(), chart: null, candleSeries: null, syncHandle: null,
    pickerEl: null, chipsEl: null, symInput: null, symNameEl: null, symResults: null,
  };
  pane.chart = LightweightCharts.createChart(div, chartOptions(pane));
  pane.candleSeries = pane.chart.addCandlestickSeries({
    upColor: "#ef5350", downColor: "#2962ff",
    borderUpColor: "#ef5350", borderDownColor: "#2962ff",
    wickUpColor: "#ef5350", wickDownColor: "#2962ff",
  });
  pane.syncHandle = paneSync.add(pane.chart, pane.candleSeries, {
    // 크로스헤어 가로선 값과 시간축 전파의 클램프/최신 창 계산은 이 칸 자기 종목의 캐시 기준이다
    getPrice: (t) => paneCache(pane)?.bars.get(t)?.close,
    getLength: () => feed.get(pane.symbol)?.barSeq.length ?? 0,
  });
  buildPanePicker(pane); // 종목 입력은 칸 도구줄 맨 앞에 1회 만든다 (칩 재구성과 무관)
  const chips = document.createElement("span");
  chips.className = "chips";
  tools.append(chips);
  pane.chipsEl = chips;
  buildPaneTools(pane);
  panes.push(pane);
  return pane;
}

function removePane(pane, { release = true } = {}) {
  const i = panes.indexOf(pane);
  if (i < 0) return;
  panes.splice(i, 1);
  pane.selSeq++; // 진행 중인 종목 선택의 늦은 완료를 폐기한다
  clearTimeout(pane.searchTimer);
  pane.active.clear();
  paneSync.remove(pane.syncHandle); // 차트 제거 전에 동기화 구독부터 뗀다 (리스너 누수 방지)
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
  if (release) releaseSymbol(pane.symbol); // 다른 칸이 안 보면 엔진 watch도 해지한다
}

// 더 이상 어느 칸도 보지 않는 종목을 정리한다: 엔진 watch를 해지하고 로컬 캐시를 지운다.
// (화면 구독 자원 정리 — 전략 거래 대상과는 무관하다, 계획서 §18)
async function releaseSymbol(shcode) {
  if (!shcode || panes.some((p) => p.symbol === shcode)) return;
  seedInflight.delete(shcode); // 진행 중 시딩은 고아 캐시를 채우고 렌더 없이 끝난다
  feed.drop(shcode);
  const token = await apiToken();
  if (!token) return;
  try {
    const res = await fetch("/api/symbols/unwatch", {
      method: "POST",
      headers: { "content-type": "application/json", "x-trader-token": token },
      body: JSON.stringify({ shcode }),
    });
    if (res.ok) {
      engineWatches = engineWatches.filter((s) => s !== shcode);
    } else if (res.status === 403) {
      resetToken();
    }
    // 그 외 거부(마지막 watch 해지 불가 등)는 화면 동작에 영향이 없으므로 조용히 넘긴다
  } catch { /* 엔진 미응답이면 엔진 재시작 시 관측 목록이 초기화되며 정리된다 */ }
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

// ---- 칸별 종목 선택 ----
// 칸 도구줄의 종목 입력 + 검색 드롭다운 (/api/market?q= 프록시).
// 선택하면 /api/symbols/watch → /api/chart?shcode= 시딩 순으로 진행하고,
// 이후 그 칸은 그 shcode의 status만 반영한다.

function buildPanePicker(pane) {
  const picker = document.createElement("span");
  picker.className = "sym-picker";
  const input = document.createElement("input");
  input.size = 10;
  input.placeholder = "코드/종목명";
  input.autocomplete = "off";
  input.title = "이 칸의 종목 (코드 또는 종목명, Enter로 적용)";
  const name = document.createElement("span");
  name.className = "badge sym-name";
  name.hidden = true;
  const results = document.createElement("div");
  results.className = "sym-results";
  results.hidden = true;
  picker.append(input, name, results);
  pane.toolsEl.append(picker);
  pane.pickerEl = picker;
  pane.symInput = input;
  pane.symNameEl = name;
  pane.symResults = results;

  input.addEventListener("input", () => onPaneSymbolInput(pane));
  input.addEventListener("keydown", (ev) => {
    if (ev.key === "Enter") selectPaneSymbol(pane, input.value);
    if (ev.key === "Escape") hidePaneResults(pane);
  });
}

// 입력창·종목명 배지를 pane 상태에 맞춘다 (선택 실패 시 원복에도 쓴다)
function syncPaneSymbolUi(pane) {
  if (pane.symInput.value !== pane.symbol) pane.symInput.value = pane.symbol;
  const label = pane.symName || feed.get(pane.symbol)?.name || "";
  pane.symNameEl.textContent = label;
  pane.symNameEl.hidden = !label;
}

function hidePaneResults(pane) {
  pane.symResults.hidden = true;
  pane.symResults.replaceChildren();
}

function showPaneResults(pane, items, seq) {
  if (seq !== pane.searchSeq) return; // 최신 검색만 반영
  pane.symResults.replaceChildren();
  if (items.length === 0) {
    const div = document.createElement("div");
    div.className = "empty";
    div.textContent = "일치하는 종목 없음";
    pane.symResults.append(div);
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
    row.onclick = () => selectPaneSymbol(pane, it.shcode, it.name);
    pane.symResults.append(row);
  }
  pane.symResults.hidden = false;
}

function onPaneSymbolInput(pane) {
  clearTimeout(pane.searchTimer);
  const q = pane.symInput.value.trim();
  if (!q) return hidePaneResults(pane);
  pane.searchTimer = setTimeout(async () => {
    const seq = ++pane.searchSeq;
    try {
      const res = await fetch(`/api/market?q=${encodeURIComponent(q)}&limit=20`);
      if (!res.ok) return hidePaneResults(pane);
      const data = await res.json();
      showPaneResults(pane, data.payload?.items ?? [], seq);
    } catch {
      /* 검색 실패는 드롭다운만 닫는다 */
    }
  }, 200);
}

// watch 요청 공통부 (선택·화면틀 복원·스트림 리셋 재구독이 함께 쓴다)
async function watchSymbol(shcode) {
  const token = await apiToken();
  if (!token) return { ok: false, error: "no_token" };
  let res;
  try {
    res = await fetch("/api/symbols/watch", {
      method: "POST",
      headers: { "content-type": "application/json", "x-trader-token": token },
      body: JSON.stringify({ shcode }),
    });
  } catch {
    return { ok: false, error: "network" };
  }
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    if (res.status === 403) resetToken();
    return { ok: false, error: data.error_code ?? data.error ?? res.status };
  }
  return { ok: true, name: data.payload?.name ?? "", generation: data.payload?.generation };
}

// 칸에 종목을 설정한다: watch → 캐시에 반영 → 시딩(완료 시 그 종목의 모든 칸을 다시 그림).
// 실패하면 칸은 기존 종목과 화면을 그대로 유지한다 (입력창만 현재 종목으로 되돌린다).
// selSeq는 빠른 연속 선택 시 늦은 완료를 폐기한다.
async function selectPaneSymbol(pane, shcode, name) {
  shcode = String(shcode ?? "").trim();
  hidePaneResults(pane);
  if (!shcode || shcode === pane.symbol) {
    syncPaneSymbolUi(pane);
    return;
  }
  const seq = ++pane.selSeq;
  const w = await watchSymbol(shcode);
  if (seq !== pane.selSeq) return; // 그 사이 다른 선택이 시작됐다
  if (!w.ok) {
    syncPaneSymbolUi(pane);
    return alert(`종목 관측 실패 (${shcode}): ${w.error}`);
  }
  const prev = pane.symbol;
  pane.symbol = shcode;
  pane.symName = name ?? "";
  clearPaneData(pane); // 이전 종목의 잔여 표시를 지운다
  const cache = feed.forSymbol(shcode);
  if (w.name) {
    cache.name = w.name;
    pane.symName = w.name;
  }
  // 엔진이 알려준 현 세대를 바닥으로 깐다 — 이보다 낮은 세대의 늦은 메시지가 리셋을 일으키지 않게
  if (typeof w.generation === "number") feed.noteGeneration(cache, w.generation);
  if (!engineWatches.includes(shcode)) engineWatches.push(shcode);
  syncPaneSymbolUi(pane);
  buildPaneTools(pane); // 지표 칩이 보이기 시작한다
  updateBadgeVisibility();
  // 이전 종목은 새 watch가 붙은 뒤에 해제한다 (엔진의 마지막-watch 해지 거부를 피한다)
  if (prev) releaseSymbol(prev);
  await seedSymbol(shcode);
}

function clearPaneData(pane) {
  pane.candleSeries.setData([]);
  for (const { handle } of pane.active.values()) handle.clear();
}

function clearSymbolPanes(shcode) {
  for (const pane of panes) {
    if (pane.symbol === shcode) clearPaneData(pane);
  }
}

// 칸 칩 줄: [없음] [지표 칩…] [활성 지표의 레이어 칩…] [×]
// 종목 미선택 칸은 종목 입력과 [×]만 보인다 (빈 차트 원칙 — 지표 칩은 종목이 있어야 동작한다).
// 지표 칩은 매니페스트에서 만들고, 레이어 칩은 켜진 지표의 layers(defaultOn 반영)에서 만든다.
function buildPaneTools(pane) {
  const chips = pane.chipsEl;
  chips.replaceChildren();

  if (pane.symbol) {
    const noneChip = document.createElement("button");
    noneChip.className = `chip${pane.active.size === 0 ? " on" : ""}`;
    noneChip.textContent = "없음";
    noneChip.title = "이 칸의 지표를 모두 끈다";
    noneChip.onclick = () => {
      for (const id of [...pane.active.keys()]) deactivateIndicator(pane, id);
      buildPaneTools(pane);
      updateBadgeVisibility();
    };
    chips.append(noneChip);

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
      chips.append(chip);
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
        chips.append(chip);
      }
    }
  }

  const del = document.createElement("button");
  del.className = "chip del";
  del.textContent = "×";
  del.title = "이 차트 삭제";
  del.onclick = () => removePane(pane);
  chips.append(del);
}

// 지표 렌더러를 칸에 활성화하고 종목 캐시로 백필한다.
// savedLayers(화면틀)가 있으면 그 값을, 없으면 매니페스트 defaultOn을 적용한다.
// 종목 미선택(화면틀 복원의 watch 대기 등)이면 백필은 시딩 완료 시 렌더가 대신한다.
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
  const cache = pane.symbol ? feed.get(pane.symbol) : undefined;
  if (cache) handle.applySeed(ctxFor(cache));
}

function deactivateIndicator(pane, indId) {
  const entry = pane.active.get(indId);
  if (!entry) return;
  pane.active.delete(indId);
  if (typeof entry.handle.destroy === "function") entry.handle.destroy();
  else entry.handle.clear();
}

// 헤더 배지의 출처: mirae_v16이 켜진 첫 칸이 보는 캐시
function firstMiraePane() {
  return panes.find((pane) => pane.active.has("mirae_v16"));
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
  el.miraeBadges.hidden = !firstMiraePane();
  if (!el.miraeBadges.hidden) restoreHeaderBadges();
}

// 시딩/지표 활성화 직후: mirae_v16이 켜진 첫 칸의 캐시에서 마지막 봉 값으로 배지를 복원한다
function restoreHeaderBadges() {
  const pane = firstMiraePane();
  const cache = pane ? paneCache(pane) : undefined;
  if (!cache) return;
  const lastT = cache.barSeq[cache.barSeq.length - 1];
  const ind = lastT === undefined ? undefined : cache.barInd.get(lastT);
  if (!ind) return;
  if (Number.isFinite(ind.score)) {
    el.score.textContent = String(ind.score);
    el.score.style.color = scoreTextColor(ind.score);
  }
  updateFinalBadge(ind.finalValid, ind.finalState);
  // 회귀선 배지는 라이브 경로(applyStatus의 p.reg_line)와 같은 출처를 쓴다
  if (ind.regValid && Number.isFinite(ind.regLine)) {
    el.reg.textContent = `회귀선 ${fmtPrice(ind.regLine, cache.tickRaw)} (R² ${ind.r2.toFixed(2)})`;
    el.reg.className = "badge ok";
    if (Array.isArray(ind.pred) && ind.pred.every(Number.isFinite)) {
      el.pred.textContent = `예측 ${ind.pred.map((v) => fmtPrice(v, cache.tickRaw)).join(" / ")}`;
    }
  }
  if (ind.obValid) {
    el.ob.textContent = `호가 ${ind.obScore.toFixed(1)}`;
    el.ob.className = ind.obScore > 0 ? "badge ok" : "badge err";
  }
}

function applyStatus(msg) {
  const p = msg.payload ?? {};
  // 라우팅: 엔진이 status 끝에 실어 보낸 shcode가 행선지다. shcode 없는 메시지
  // (구 엔진·리플레이)는 기존 동작대로 기본 캐시에 쌓고 그 캐시를 보는 칸(미선택 칸)에 반영한다.
  const sh = typeof p.shcode === "string" && p.shcode !== "" ? p.shcode : legacyShcode();
  const cache = feed.forSymbol(sh);
  // 세대 교체: 이 종목의 이력만 비우고 새로 쌓는다 (shcode+generation 조합, 계획서 §18)
  if (feed.noteGeneration(cache, p.generation)) {
    feed.reset(cache);
    clearSymbolPanes(sh);
  }
  const t = Number(p.bar_open_time) / 1e6;
  if (!Number.isFinite(t) || t <= 0) return;

  const [o, h, l, c] = p.ohlc ?? [];
  if (o != null) {
    feed.noteBar(cache, t, { time: t, open: o, high: h, low: l, close: c });
    for (const pane of panes) {
      if (pane.symbol === sh) pane.candleSeries.update(cache.bars.get(t));
    }
  }
  const ind = MiraeLayers.barIndFromPayload(p);
  ind.score = Number.isFinite(p.score) ? p.score : NaN;
  ind.pred = Array.isArray(p.pred) ? p.pred : undefined;
  ind.resid = p.resid ?? 0;
  ind.pvol = p.pvol ?? 0;
  cache.barInd.set(t, ind);
  if (typeof p.tick === "number" && Number.isFinite(p.tick) && p.tick > 0) cache.tickRaw = p.tick;

  // ⑥⑦ 봉별 아이템은 렌더러 on/off와 무관하게 캐시에 기록한다 (복원 대비)
  const pos = cache.barPos.get(t);
  const memItem = MiraeLayers.memItemFromPayload(t, p.mem, feed.recentBars(cache, pos, 5));
  if (memItem !== undefined) ind.memItem = memItem;
  const pstItem = MiraeLayers.pstItemFromPayload(t, p.pst, feed.recentBars(cache, pos, 5));
  if (pstItem !== undefined) ind.pstItem = pstItem;

  // 지표 표시는 이 종목을 보는 칸의 활성 렌더러만 담당한다
  const ctx = ctxFor(cache);
  for (const pane of panes) {
    if (pane.symbol !== sh) continue;
    for (const { handle } of pane.active.values()) handle.applyLive(p, ctx);
  }

  // 헤더 배지는 mirae_v16이 켜진 첫 칸이 이 종목을 볼 때만 갱신한다
  const mp = firstMiraePane();
  if (!mp || paneCache(mp) !== cache) return;
  const preds = p.pred ?? [];
  if (ind.regValid && Number.isFinite(ind.regFlat)) {
    el.reg.textContent = `회귀선 ${fmtPrice(p.reg_line, cache.tickRaw)} (R² ${ind.r2.toFixed(2)})`;
    el.reg.className = "badge ok";
    if (preds.length === 3 && preds.every(Number.isFinite)) {
      el.pred.textContent = `예측 ${preds.map((v) => fmtPrice(v, cache.tickRaw)).join(" / ")}`;
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

// 시딩이 끝난 종목의 캐시로 그 종목을 보는 모든 칸을 다시 그린다
function renderSymbolPanes(shcode) {
  const cache = feed.get(shcode);
  if (!cache) return;
  const rows = cache.barSeq.map((t) => cache.bars.get(t)).filter(Boolean);
  const ctx = ctxFor(cache);
  for (const pane of panes) {
    if (pane.symbol !== shcode) continue;
    pane.candleSeries.setData(rows);
    // 시딩 직후에는 그 칸이 자기 최신 범위로 돌아온다. 이 이벤트는 꼬리 기준 전파 규칙으로
    // 다른 칸에도 자기 최신 창으로 전파된다 (pane-sync propagateRange 참조)
    if (rows.length) pane.chart.timeScale().scrollToRealTime();
    for (const { handle } of pane.active.values()) handle.applySeed(ctx);
  }
  restoreHeaderBadges();
}

// 과거 봉 시딩: PUB/SUB는 과거 메시지를 보존하지 않으므로 스냅샷을 가져온다.
// 같은 종목의 동시 시딩은 하나로 합친다 (칸 여러 개가 같은 종목을 고를 수 있다).
const seedInflight = new Map(); // shcode → 진행 중 Promise
function seedSymbol(shcode) {
  const running = seedInflight.get(shcode);
  if (running) return running;
  const p = seedSymbolNow(shcode).finally(() => {
    if (seedInflight.get(shcode) === p) seedInflight.delete(shcode);
  });
  seedInflight.set(shcode, p);
  return p;
}

// 링 전체(최대 2일치)를 페이지로 나눠 가져와 종목 캐시에 합친다.
// 가져오는 동안 리셋(세대 교체·엔진 재시작)이 끼어들면(seedToken 변경) 그 응답은
// 리셋 이전 기준이므로 폐기하고 새 기준으로 다시 가져온다 — 시딩을 기다리는 쪽이
// 무효한 결과를 받아 빈 차트로 남지 않게 한다.
async function seedSymbolNow(shcode) {
  const cache = feed.forSymbol(shcode);
  for (let attempt = 0; attempt < 3; attempt++) {
    const seedTok = cache.seedToken;
    try {
      const all = [];
      const memEvents = [];
      const pstEvents = [];
      let back = 0;
      for (let pages = 0; pages < 16; pages++) {
        const res = await fetch(`/api/chart?shcode=${encodeURIComponent(shcode)}&back_index=${back}`);
        if (!res.ok) return;
        const data = await res.json();
        const p = data.payload ?? {};
        feed.noteGeneration(cache, p.generation);
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
      if (cache.seedToken !== seedTok) continue; // 시딩 중 리셋 — 새 기준으로 다시 가져온다
      feed.reset(cache); // 시딩 중 라이브로 쌓인 봉과 혼합하지 않는다 (스냅샷 기준으로 다시 쌓음)
      all.sort((a, b) => a.time - b.time);
      const dedup = all.filter((b, i) => i === 0 || b.time !== all[i - 1].time);
      for (const b of dedup) feed.noteBar(cache, b.time, b);

      // 봉별 지표 캐시 복원: 스냅샷의 ind 배열로 종목 캐시를 채운다
      for (const b of dedup) {
        const d = MiraeLayers.parseInd(b.ind);
        if (!d) continue;
        if (Number.isFinite(d.tick) && d.tick > 0) cache.tickRaw = d.tick;
        const ind = MiraeLayers.barIndFromInd(d);
        ind.score = d.score;
        ind.pred = d.pred;
        ind.resid = d.resid;
        ind.pvol = d.pvol;
        cache.barInd.set(b.time, ind);
      }
      // ⑥⑦ 이벤트 → 봉별 아이템으로 변환해 캐시에 심는다 (렌더러가 applySeed에서 복원)
      for (const item of buildMemItems(dedup, memEvents)) {
        const ind = cache.barInd.get(item.time);
        if (ind) ind.memItem = item;
      }
      for (const item of buildPstItems(dedup, pstEvents)) {
        const ind = cache.barInd.get(item.time);
        if (ind) ind.pstItem = item;
      }

      renderSymbolPanes(shcode);
      return;
    } catch { /* 시딩 실패는 라이브 스트림으로 진행 */ }
    return;
  }
}

// 지표 매니페스트를 저장하고 모든 칸의 도구줄을 다시 만든다.
// 내용이 같으면 건너뛴다 — 재구성이 다른 칸의 종목 입력 중 포커스를 뺏지 않게.
function setIndicatorManifest(list) {
  const next = list.filter((m) => m && typeof m.id === "string");
  if (JSON.stringify(next) === JSON.stringify(indicatorManifest)) return;
  indicatorManifest = next;
  for (const pane of panes) buildPaneTools(pane);
}

// 엔진의 관측 종목 목록을 갱신한다 (구 엔진은 watches 없이 shcode 1개만 온다)
async function refreshEngineWatches() {
  try {
    const res = await fetch("/api/status");
    if (!res.ok) return;
    const p = (await res.json()).payload ?? {};
    if (Array.isArray(p.watches)) {
      engineWatches = p.watches.filter((s) => typeof s === "string" && s !== "");
    } else if (typeof p.shcode === "string" && p.shcode !== "") {
      engineWatches = [p.shcode];
    }
  } catch { /* 엔진 미응답이면 이전 목록을 유지한다 */ }
}

// 엔진 재시작/순번 공백: 모든 종목 캐시를 비우고 칸 종목을 다시 watch→시딩한다
// (재시작한 엔진은 관측 목록을 잃으므로 watch부터 다시 한다. 혼합 표시 방지)
async function onStreamReset() {
  for (const sh of feed.symbols()) feed.reset(feed.get(sh));
  for (const pane of panes) clearPaneData(pane);
  await refreshEngineWatches();
  const targets = [...new Set(panes.map((p) => p.symbol).filter(Boolean))];
  for (const sh of targets) {
    const w = await watchSymbol(sh);
    if (!w.ok) continue;
    const cache = feed.forSymbol(sh);
    if (w.name) cache.name = w.name;
    if (typeof w.generation === "number") feed.noteGeneration(cache, w.generation);
  }
  // 칸 1이 미선택이고 엔진이 종목 하나만 관측 중이면 자동 설정한다 (기존 흐름 보호)
  if (engineWatches.length === 1 && panes[0] && !panes[0].symbol) {
    await selectPaneSymbol(panes[0], engineWatches[0]); // watch+시딩 포함
  }
  await Promise.all(targets.map((sh) => seedSymbol(sh)));
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
      onStreamReset();
    }
    try {
      applyStatus(data.message ?? {});
    } catch {
      /* 시딩 직후 과거 봉의 늦은 갱신 등 표시상 무해한 순서 오류는 무시한다 */
    }
  };
}

// ---- 화면틀 v2 ----
// 화면틀에는 레이아웃·칸별 지표 집합(레이어 설정)·칸별 종목 바인딩만 저장한다.
// 전략 자동 시작·주문 상태는 넣지 않는다 (계획서 §18).
// 직렬화/검증은 workspace.js의 순수 함수가 담당한다 (node:test 대상).

function collectWorkspace() {
  return Workspace.serialize(el.wsName.value.trim(),
    panes.map((pane) => ({
      height: pane.heightFrac,
      symbol: pane.symbol,
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

// 화면틀 v2 적용: 칸을 재구성하고 칸별 종목은 watch→시딩으로 복원한다 (비동기 진행).
// symbol 없는 칸(구 화면틀)은 parse 단에서 current_symbol로 폴백되어 들어온다.
// 사라진 종목의 unwatch는 새 칸의 watch가 끝난 뒤에 한다 (엔진의 마지막-watch 해지 거부 회피).
async function applyWorkspace(parsed) {
  const before = new Set(panes.map((p) => p.symbol).filter(Boolean));
  while (panes.length) removePane(panes[panes.length - 1], { release: false });
  const jobs = [];
  for (const spec of parsed.panels) {
    const pane = createPane(spec.height);
    for (const ind of spec.indicators) activateIndicator(pane, ind.id, ind.layers);
    buildPaneTools(pane);
    if (spec.symbol) jobs.push(selectPaneSymbol(pane, spec.symbol).catch(() => {}));
  }
  normalizeHeights();
  rebuildResizeBars();
  updateBadgeVisibility();
  await Promise.all(jobs);
  const after = new Set(panes.map((p) => p.symbol).filter(Boolean));
  for (const sh of before) {
    if (!after.has(sh)) await releaseSymbol(sh);
  }
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
  // 칸별 watch→시딩과 사라진 종목의 unwatch는 백그라운드로 진행한다.
  applyWorkspace(parsed).catch(() => {});
  alert(`화면틀 '${name}' 적용`);
}

// 칸 추가: 기존 칸 높이를 비율대로 줄여 새 칸 자리를 만든다 (새 칸은 종목 미선택)
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

// 드롭다운 바깥 클릭은 열린 검색 결과를 닫는다
document.addEventListener("click", (ev) => {
  for (const pane of panes) {
    if (!pane.symResults.hidden && !pane.pickerEl.contains(ev.target)) hidePaneResults(pane);
  }
});

// 기본 구성: 칸 1개 + 종목 미선택 (빈 차트). 단, 엔진이 관측 중인 종목이 하나뿐이면
// 그 종목을 칸 1에 자동 설정한다 (기존 사용자 흐름 보호). 둘 이상이면 자동 선택하지 않는다.
async function bootstrap() {
  createPane(1);
  updateBadgeVisibility();
  await refreshEngineWatches();
  if (engineWatches.length === 1 && panes[0] && !panes[0].symbol) {
    await selectPaneSymbol(panes[0], engineWatches[0]);
  }
  connect();
}
bootstrap();
