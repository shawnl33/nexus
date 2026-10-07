// 대시보드 프론트엔드 (계획서 §18).
// C가 계산한 값을 표시만 한다. 지표·점수를 재계산하지 않는다.
// 화면틀마다 종목 입력이 하나이고, 그 안의 칸은 그 종목을 같이 본다.
// 칸마다 차트 1개. 종목별 데이터 캐시는 feed.js가 격리한다.
// 지표는 렌더러(RENDERERS)를 칸에 활성화해 표시하고, 칸 왼쪽의 접이식 지표 패널
// (카테고리 ▸ 지표 체크박스 ▸ 레이어 체크박스 트리)은 엔진 스냅샷의 indicators
// 매니페스트에서 만든다 (트리 분류는 indicator-tree.js).
// 기본 상태 = 화면틀 하나 + 빈 차트. 단, 시딩 시 엔진이 관측 중인
// 종목이 하나뿐이면 그 종목을 그 칸에 자동 설정한다 (기존 사용자 흐름 보호).

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
// Pane: { id, el, toolsEl, panelEl, chartEl, chart, candleSeries, heightFrac, syncHandle,
//         symbol, symName, selSeq, selTarget, panelOpen, treeFold,
//         active: Map<indId, { renderer, handle, layers: {layerId: bool} }> }
// symbol이 ""이면 미선택. 종목 입력·검색은 칸이 아니라 화면틀 도구줄에 하나다.
// panelOpen은 칸별 지표 패널 접기/펼치기(화면틀 직렬화 대상), treeFold는 트리
// 카테고리의 접힘 상태로 칸 UI 로컬이다 (직렬화하지 않는다).
// selTarget은 진행 중인 선택의 목표 종목 — 늦은 선택 완료를 폐기할 때 그 종목의
// watch를 해지해도 되는지(WatchGuard.staleWatchLeaks) 판정에 쓴다.

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
  fx_snco: { createHandle: (chart) => SncoLayers.createHandle(chart) },
  fx_judge_v3: { createHandle: (chart) => OsLayers.judgeHandle(chart) },
  fx_pack_v4: { createHandle: (chart) => OsLayers.packHandle(chart) },
  fx_pvc: PvcLayers.PvcRenderer,
  fx_data2: Data2Layers.Data2Renderer,
  ks_data2: Data2Layers.Data2Renderer,
  w_ret_long: WplotLayers.RetLong,
  w_ret_short: WplotLayers.RetShort,
  w_link_long: WplotLayers.LinkLong,
  w_link_short: WplotLayers.LinkShort,
};

const WPLOT_IDS = ["w_ret_long", "w_ret_short", "w_link_long", "w_link_short"];

// 지표 매니페스트 (엔진 스냅샷의 indicators 배열) — 시딩 전에는 비어 있다
let indicatorManifest = [];

const panesEl = document.getElementById("panes");
const panes = []; // 화면틀 행 우선 평탄 목록. 배열 정체성은 유지하고 내용만 바꾼다.
let nextPaneId = 1;
const MIN_PANE_FRAC = 0.1; // 드래그로 줄일 수 있는 행·열·칸 최소 비율
let gridRows = []; // { el, heightFrac, frames }
let colWeights = [1];
let currentFrame = null;
let currentPane = null;

function allFrames() {
  return gridRows.flatMap((row) => row.frames);
}

function syncPaneList() {
  const next = allFrames().flatMap((frame) => frame.panes);
  panes.splice(0, panes.length, ...next);
}

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

// 시간축·크로스헤어는 화면틀마다 PaneSync 하나다. 다른 화면틀로는 넘어가지 않는다.
// 크로스헤어 가로선 값은 칸별 getPrice로 자기 종목 캐시에서 찾는다.

// 프로그램적 시간축 변경이 다른 칸으로 번지지 않게 그 칸의 범위 이벤트를 뮤트한다
// (전파 계약은 pane-sync.js 헤더 참조). 범위 이벤트는 동기 호출 안과 뒤따르는 rAF
// 프레임에 걸쳐 나오므로(실측), 뮤트는 프레임이 지난 뒤에 푼다 — 라이브 1봉 적용용.
function mutePaneRange(pane) {
  const sync = pane.frame.sync;
  sync.mute(pane.syncHandle);
  requestAnimationFrame(() => requestAnimationFrame(() => sync.unmute(pane.syncHandle)));
}

// 시딩 적용용 뮤트: scrollToRealTime은 400ms 스크롤 애니메이션이라 프레임마다 범위
// 이벤트를 흘리므로(실측), 애니메이션이 끝날 때까지 뮤트를 유지한다.
function mutePaneRangeForSeeding(pane) {
  const sync = pane.frame.sync;
  sync.mute(pane.syncHandle);
  setTimeout(() => sync.unmute(pane.syncHandle), 450); // 400ms 애니메이션 + 여유
}

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
      // fixLeftEdge/fixRightEdge는 끈다 (기본 false): 칸 간 엄밀 시각 정렬(pane-sync.js
      // 헤더 참조)은 대상 칸 데이터 밖의 빈 영역까지 범위를 적용해야 하는데, 이 옵션이
      // 켜져 있으면 프로그램적 setVisibleLogicalRange도 클램프된다 — 2026-09-30 라이브
      // 실측: 602개 항목 시리즈에 {from:570,to:650}을 적용하면 읽기 값이
      // {from:521,to:601}로 시프트된다. 클램프된 에코는 pane-sync의 expectEcho(적용
      // 값과 정확히 같은 에코만 삼킴)와 어긋나 재전파 루프를 일으킨다. 끄면 데이터 밖
      // 소수·음수 범위({from:570.5,to:650.25}, {from:-40.5,to:30.25})도 요청 값 그대로
      // 왕복한다 (③ 광선은 캔버스에 오른쪽 끝까지 그리므로 이 옵션과 무관하게 보인다).
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

// 칸 높이는 그 화면틀 본문 대비 % (pane.heightFrac 0..1). 인접 칸 사이의 리사이즈바를
// 드래그해 조절한다. 높이를 바꾼 뒤에는 차트 크기를 다시 맞춘다.
// 차트 크기의 기준은 차트 호스트(.chart-host)다 — 지표 패널을 접으면 칸 폭은
// 그대로여도 호스트 폭이 늘어나므로, 패널 접기/펼치기에서도 이 함수를 부른다.
function syncPaneSize(pane) {
  pane.el.style.height = `${pane.heightFrac * 100}%`;
  pane.chart.resize(pane.chartEl.clientWidth, pane.chartEl.clientHeight);
}

function resizeFrameCharts(frames) {
  for (const frame of frames) {
    if (!frame) continue;
    for (const pane of frame.panes) {
      pane.chart.resize(pane.chartEl.clientWidth, pane.chartEl.clientHeight);
    }
  }
}

// 칸 폭이 나중에 잡히거나 패널을 접으면 봉 간격이 달라진다. 그 화면틀만 다시 맞춘다.
let alignRaf = 0;
const pendingAlignFrames = new Set();
const holdAlignFrames = new Set();
const paneResize = new ResizeObserver((entries) => {
  for (const entry of entries) {
    const pane = panes.find((p) => p.chartEl === entry.target);
    if (pane?.frame && !holdAlignFrames.has(pane.frame)) pendingAlignFrames.add(pane.frame);
  }
  cancelAnimationFrame(alignRaf);
  alignRaf = requestAnimationFrame(() => {
    const frames = [...pendingAlignFrames];
    pendingAlignFrames.clear();
    for (const frame of frames) alignFramePanes(frame, false);
  });
});

function makeRow(heightFrac) {
  const el = document.createElement("div");
  el.className = "pane-row";
  el.style.height = `${heightFrac * 100}%`;
  panesEl.append(el);
  const row = { el, heightFrac, frames: [] };
  gridRows.push(row);
  return row;
}

function updateFrameCloseButtons() {
  const mode = Workspace.frameClose(gridRows.length, colWeights.length);
  for (const frame of allFrames()) {
    frame.closeEl.hidden = mode === "none";
    frame.closeEl.title = mode === "row" ? "이 행 삭제" : "이 열 삭제";
  }
}

function createFrame(row) {
  const el = document.createElement("div");
  el.className = "frame";
  const tools = document.createElement("div");
  tools.className = "frame-tools";
  const addBtn = document.createElement("button");
  addBtn.textContent = "+ 차트";
  addBtn.title = "이 화면틀에 차트를 추가한다";
  const menuBtn = document.createElement("button");
  menuBtn.className = "panel-toggle on";
  menuBtn.textContent = "☰";
  menuBtn.title = "사이드바 열기/닫기";
  const closeBtn = document.createElement("button");
  closeBtn.className = "frame-close";
  closeBtn.textContent = "×";
  const bodyEl = document.createElement("div");
  bodyEl.className = "frame-body";
  el.append(tools, bodyEl);
  row.el.append(el);
  const frame = {
    el, bodyEl, closeEl: closeBtn, panelToggleEl: menuBtn, row,
    sync: PaneSync.create(), panes: [], currentPane: null,
    pickerEl: null, symInput: null, symResults: null, chipsEl: null,
    searchSeq: 0, searchTimer: null, symActive: -1,
    symCandidates: null, symCandidateQuery: "", symSearch: null,
    overlays: [], overlayNames: {}, overlayColors: {}, overlayStyles: {}, overlayScale: "shared",
    mainColor: "",
    overlayTargets: new Set(),
    replacingMain: false,
    pairOpp: "", pairOppName: "", pairFut: "", pairFutName: "",
  };
  tools.append(addBtn, menuBtn, buildFramePicker(frame), closeBtn);
  addBtn.onclick = () => addChart(frame);
  menuBtn.onclick = () => setFramePanels(frame, !frame.panes.some((p) => p.panelOpen));
  closeBtn.onclick = () => closeFrame(frame);
  row.frames.push(frame);
  const col = row.frames.length - 1;
  frame.el.style.width = `${(colWeights[col] ?? 0) * 100}%`;
  return frame;
}

function layoutGrid() {
  const rowH = Workspace.normalizeWeights(gridRows.map((row) => row.heightFrac));
  gridRows.forEach((row, i) => { row.heightFrac = rowH[i]; });
  colWeights = Workspace.normalizeWeights(colWeights);
  for (const row of gridRows) {
    row.el.style.height = `${row.heightFrac * 100}%`;
    row.frames.forEach((frame, c) => {
      frame.el.style.width = `${colWeights[c] * 100}%`;
      const paneH = Workspace.normalizeWeights(frame.panes.map((p) => p.heightFrac));
      frame.panes.forEach((pane, i) => {
        pane.heightFrac = paneH[i] ?? 1;
        syncPaneSize(pane);
      });
    });
  }
  rebuildResizeBars();
  updateFrameCloseButtons();
}

// 이름도 비어 있고 차트도 빈 하나면 새화면이 이미 그 상태다.
function screenIsFresh() {
  if (el.wsName.value.trim()) return false;
  if (gridRows.length !== 1 || colWeights.length !== 1) return false;
  const frames = allFrames();
  if (frames.length !== 1 || frames[0].panes.length !== 1) return false;
  const frame = frames[0];
  if (frame.overlays?.length) return false;
  const pane = frame.panes[0];
  return !pane.symbol && !pane.data2 && pane.active.size === 0 && pane.systems.size === 0;
}

// 저장 파일은 건드리지 않는다. 화면만 빈 차트 하나로 되돌리고 이름은 비운다.
// force는 로딩 취소다. 덮개가 확인창을 가리므로 다시 묻지 않는다.
async function newScreen(opts) {
  if (!opts?.force && !screenIsFresh() && !confirm("현재 화면을 지우고 빈 차트로 시작할까요?")) return;
  const before = new Set();
  for (const pane of panes) {
    if (pane.symbol) before.add(pane.symbol);
    if (pane.data2) before.add(pane.data2);
  }
  for (const frame of allFrames()) {
    for (const sh of frame.overlays || []) before.add(sh);
  }
  destroyGrid();
  const row = makeRow(1);
  const frame = createFrame(row);
  createPane(frame, 1);
  currentFrame = frame;
  layoutGrid();
  updateBadgeVisibility();
  el.wsName.value = "";
  for (const sh of before) await releaseSymbol(sh);
}

function addFrameRow() {
  const heights = Workspace.scaleAdd(gridRows.map((row) => row.heightFrac));
  gridRows.forEach((row, i) => { row.heightFrac = heights[i]; });
  const row = makeRow(heights[heights.length - 1]);
  let frame = null;
  for (let c = 0; c < colWeights.length; c++) {
    frame = createFrame(row);
    createPane(frame, 1);
  }
  layoutGrid();
  currentFrame = frame;
}

function addFrameCol() {
  colWeights = Workspace.scaleAdd(colWeights);
  let frame = null;
  for (const row of gridRows) {
    frame = createFrame(row);
    createPane(frame, 1);
  }
  layoutGrid();
  currentFrame = frame;
}

function createPane(frame, heightFrac = 1) {
  const div = document.createElement("div");
  div.className = "pane";
  div.style.height = `${heightFrac * 100}%`;
  // 칸 구조: 겹친 ×(이 차트 삭제) + 본문([지표 패널 | 차트] 수평 분할).
  // 사이드바 ☰는 화면틀 도구줄의 +차트 옆이다. 종목 입력도 화면틀에 하나다.
  const tools = document.createElement("div");
  tools.className = "tools";
  const body = document.createElement("div");
  body.className = "pane-body";
  const panel = document.createElement("div");
  panel.className = "ind-panel";
  const chartHost = document.createElement("div");
  chartHost.className = "chart-host";
  body.append(panel, chartHost);
  div.append(tools, body);
  frame.bodyEl.append(div);
  const pane = {
    id: nextPaneId++, el: div, toolsEl: tools, panelEl: panel, chartEl: chartHost, heightFrac,
    frame,
    symbol: "", symName: "", selSeq: 0, selTarget: "",
    active: new Map(), chart: null, candleSeries: null, syncHandle: null,
    overlaySeries: new Map(),
    panelOpen: true, treeFold: {}, data2: "",
    linkLegs: WeeklyLegs.empty(),
    systems: new Set(),
    systemVars: {},
    systemBasis: {},
    barStyle: "candle", barDraw: "candle", overlayStyles: {},
    pairSide: 1,
    pairOpp: frame.pairOpp || "", pairOppName: frame.pairOppName || "",
    pairFut: frame.pairFut || "", pairFutName: frame.pairFutName || "",
    pairEvents: [], pairStatus: "", pairLast: null, pairBusy: false,
  };
  pane.chart = LightweightCharts.createChart(chartHost, chartOptions(pane));
  hideCrosshairMarkers(pane.chart);
  if (chartLoads > 0) lockChartInput(pane.chart, true);
  paneResize.observe(chartHost);
  pane.candleSeries = makePriceSeries(pane.chart, "candle", false);
  pane.syncHandle = frame.sync.add(pane.chart, pane.candleSeries, {
    // 크로스헤어 가로선 값과 시간축 전파의 꼬리 판정·변환은 이 칸 자기 종목의 캐시 기준이다.
    // getPrice는 캐시의 봉 맵만 본다 — whitespace(구멍) 시각에는 항목이 없어 undefined가 나오고,
    // pane-sync는 그 칸에 가로선을 그리지 않는다 (구멍 위 크로스헤어는 세로선만).
    getPrice: (t) => paneCache(pane)?.bars.get(t)?.close,
    // 시리즈 길이 = 봉 수 + whitespace(구멍) 포인트 수: setVisibleLogicalRange의 논리 인덱스는
    // whitespace를 포함한 시리즈 기준이라 봉 수만 재면 구멍 수만큼 어긋난다.
    // wsCount는 renderSymbolPanes(시딩)와 rebuildPaneCandles(정정)가 같은 withWhitespace
    // 결과로 갱신한다 (정합 고정).
    // 라이브 꼬리는 봉·whitespace(균일 분 그리드 채움)가 붙을 때 barSeq/wsCount와 시리즈가
    // 함께 자라므로(noteBar 주석 참조) 이 합계식이 그대로 맞는다.
    // 시간축 전파에서 이 길이는 데이터 없는 칸 스킵과 레거시 폴백(시각 미제공 칸)에만
    // 쓴다 — 창 변환은 getTimes 기준 (pane-sync.js 헤더의 엄밀 시각 정렬 참조).
    getLength: () => {
      const c = feed.get(pane.symbol);
      return c ? c.barSeq.length + (c.wsCount ?? 0) : 0;
    },
    // 시리즈(봉 + whitespace)의 항목별 시각(초) 오름차순 — pane-sync가 발생 칸의 오른쪽
    // 끝 논리 인덱스를 시각으로 환산하고 대상 칸에서 그 시각의 인덱스를 찾는 기준이다.
    // 종목마다 봉 수·구멍 수가 달라 논리 인덱스로 맞추면 칸마다 다른 시각을 보게 되므로
    // 시각이 공통 기준이다.
    getTimes: () => feed.get(pane.symbol)?.seriesTimes,
    // 차트 영역(.chart-host)의 px 폭 — 새 시간축 규칙(같은 px 봉 간격 + 같은 오른쪽 끝
    // 시각)의 줌 기준. 지표 패널 접기/펼치기로 폭이 바뀌므로 호출 시점에 잰다.
    getWidth: () => pane.chartEl.clientWidth,
  });
  buildPaneToolButtons(pane); // ×(이 차트 삭제)는 1회 만든다. ☰는 화면틀에 있다.
  buildPaneTools(pane); // 지표 패널의 트리를 채운다
  // 차트는 칸 높이가 잡히기 전에 만들어진다. 생성 직후 호스트 크기로 맞춘다.
  syncPaneSize(pane);
  frame.panes.push(pane);
  syncPaneList();
  pane.el.addEventListener("pointerdown", () => {
    currentPane = pane;
    frame.currentPane = pane;
    currentFrame = frame;
  });
  mountFrameOverlays(pane);
  syncFramePanelToggle(frame);
  return pane;
}

// ×(이 차트 삭제)만 차트 모서리에 둔다. 트리 재구성 때 다시 만들지 않는다.
function buildPaneToolButtons(pane) {
  const del = document.createElement("button");
  del.className = "chip del";
  del.textContent = "×";
  del.title = "이 차트 삭제";
  del.onclick = () => deleteChart(pane);
  pane.toolsEl.append(del);
}

// 칸별 지표 패널 접기/펼치기. 접으면 차트가 칸 전체 폭을 쓴다 — 호스트 폭이 바뀌므로
// 차트 크기를 다시 맞춘다 (패널 DOM은 숨길 뿐, 트리 상태는 유지된다)
const BAR_STYLES = [
  { id: "none", name: "없음" },
  { id: "candle", name: "캔들바" },
  { id: "outline", name: "테두리" },
  { id: "bar", name: "바" },
  { id: "line", name: "종가선" },
];

function pricePoint(row, draw) {
  if (!row || row.open == null) return { time: row.time };
  if (draw === "line") return { time: row.time, value: row.close };
  return row;
}

// 미래곡선 해외가 켜진 칸은 봉마다 단계 색으로 강도를 칠한다.
function curveStageTint(pane, time) {
  if (!pane?.active?.has("fx_curve_os")) return "";
  const list = paneCache(pane)?.barInd?.get(time)?.cu;
  if (!Array.isArray(list)) return "";
  const row = list.find((r) => r.id === 1) || list.find((r) => r.id === 2) || list.find((r) => r.id === 3);
  if (!row || !Number.isFinite(Number(row.rgb))) return "";
  return "#" + (Number(row.rgb) >>> 0).toString(16).padStart(6, "0");
}

function candlePoint(pane, row, draw) {
  const point = pricePoint(row, draw || pane.barDraw);
  const tint = curveStageTint(pane, row && row.time);
  if (!tint || point.open == null) return point;
  if ((draw || pane.barDraw) === "line") return { ...point, color: tint };
  if (pane.barStyle === "outline") return { ...point, borderColor: tint, wickColor: tint };
  return { ...point, color: tint, borderColor: tint, wickColor: tint };
}

const CANDLE_UP = "#ef5350";
const CANDLE_DN = "#2962ff";

function candleBodyWidth(barSpacing, pixelRatio) {
  if (barSpacing >= 2.5 && barSpacing <= 4) return Math.floor(3 * pixelRatio);
  const coeff = 1 - 0.2 * Math.atan(Math.max(4, barSpacing) - 4) / (Math.PI * 0.5);
  const scaled = Math.floor(barSpacing * pixelRatio);
  return Math.max(Math.floor(pixelRatio), Math.min(Math.floor(barSpacing * coeff * pixelRatio), scaled));
}

function candleBorderPrimitive() {
  const WIDTH = 2;
  let chart = null;
  let series = null;
  return {
    attached(param) {
      chart = param.chart;
      series = param.series;
    },
    detached() {
      chart = null;
      series = null;
    },
    updateAllViews() {},
    paneViews() {
      return [{
        renderer() {
          return {
            draw(target) {
              if (!chart || !series) return;
              const opt = typeof series.options === "function" ? series.options() : null;
              if (opt && opt.visible === false) return;
              const raw = series.data;
              const bars = typeof raw === "function" ? raw.call(series) : [];
              if (!bars.length) return;
              target.useBitmapCoordinateSpace((scope) => {
                const ctx = scope.context;
                const hr = scope.horizontalPixelRatio;
                const vr = scope.verticalPixelRatio;
                const spacing = chart.timeScale().options().barSpacing;
                const bw = candleBodyWidth(spacing, hr);
                const border = Math.max(1, Math.round(WIDTH * hr));
                const upTint = opt?.borderUpColor || CANDLE_UP;
                const downTint = opt?.borderDownColor || CANDLE_DN;
                for (const bar of bars) {
                  if (bar.open == null || bar.close == null) continue;
                  const x = chart.timeScale().timeToCoordinate(bar.time);
                  const yo = series.priceToCoordinate(bar.open);
                  const yc = series.priceToCoordinate(bar.close);
                  if (x == null || yo == null || yc == null) continue;
                  const left = Math.round(x * hr) - Math.floor(bw * 0.5);
                  const right = left + bw - 1;
                  let top = Math.round(Math.min(yo, yc) * vr);
                  let bottom = Math.round(Math.max(yo, yc) * vr);
                  if (bottom - top < border) bottom = top + border;
                  ctx.beginPath();
                  ctx.strokeStyle = bar.borderColor || bar.color || (bar.close >= bar.open ? upTint : downTint);
                  ctx.lineWidth = border;
                  const inset = border / 2;
                  ctx.strokeRect(
                    left + inset,
                    top + inset,
                    Math.max(border, right - left + 1 - border),
                    Math.max(border, bottom - top + 1 - border),
                  );
                }
              });
            },
          };
        },
      }];
    },
  };
}

function candleColors(body) {
  const hollow = body === "outline";
  return {
    upColor: hollow ? "rgba(0,0,0,0)" : CANDLE_UP,
    downColor: hollow ? "rgba(0,0,0,0)" : CANDLE_DN,
    borderVisible: true,
    borderUpColor: CANDLE_UP,
    borderDownColor: CANDLE_DN,
    wickUpColor: CANDLE_UP,
    wickDownColor: CANDLE_DN,
  };
}

// 크로스헤어 마커는 라이브러리 기본값이라 값이 있는 시리즈마다 찍힌다.
// 어느 요소에 남길지는 나중에 정한다. 그때까지 새로 만드는 시리즈는 모두 끈다.
function hideCrosshairMarkers(chart) {
  const wrap = (name, optIndex) => {
    const orig = chart[name];
    if (typeof orig !== "function") return;
    chart[name] = (...args) => {
      args[optIndex] = { ...(args[optIndex] || {}), crosshairMarkerVisible: false };
      return orig.apply(chart, args);
    };
  };
  for (const name of ["addLineSeries", "addAreaSeries", "addBarSeries", "addCandlestickSeries", "addHistogramSeries", "addBaselineSeries"]) {
    wrap(name, 0);
  }
  wrap("addCustomSeries", 1);
}

function makePriceSeries(chart, draw, hollow) {
  const series = draw === "bar"
    ? chart.addBarSeries({
      upColor: CANDLE_UP, downColor: CANDLE_DN,
      thinBars: false,
    })
    : draw === "line"
      ? chart.addLineSeries({
        color: "#d1d4dc", lineWidth: 2,
        priceLineVisible: true, lastValueVisible: true,
      })
      : chart.addCandlestickSeries(candleColors(hollow ? "outline" : "fill"));
  attachGapShade(series);
  return series;
}

function attachGapShade(series) {
  if (!series || typeof series.attachPrimitive !== "function" || typeof GapShade === "undefined") return;
  series.attachPrimitive(GapShade.primitive());
}

function syncCandleBorder(pane, hollow) {
  if (!pane.candleSeries || pane.barDraw !== "candle") {
    pane.candleBorder = null;
    return;
  }
  if (hollow && !pane.candleBorder) {
    pane.candleBorder = candleBorderPrimitive();
    pane.candleSeries.attachPrimitive(pane.candleBorder);
  } else if (!hollow && pane.candleBorder) {
    if (typeof pane.candleSeries.detachPrimitive === "function") {
      pane.candleSeries.detachPrimitive(pane.candleBorder);
    }
    pane.candleBorder = null;
  }
}

function setBarStyle(pane, style) {
  const next = BAR_STYLES.some((s) => s.id === style) ? style : "candle";
  const draw = next === "none" || next === "outline" ? "candle" : next;
  const hollow = next === "outline";
  pane.barStyle = next;
  if (pane.barDraw !== draw) {
    const cache = feed.get(pane.symbol);
    const bars = cache ? cache.barSeq.map((t) => cache.bars.get(t)).filter(Boolean) : [];
    const rows = bars.length ? Gaps.withWhitespace(bars).map((r) => candlePoint(pane, r, draw)) : [];
    mutePaneRange(pane);
    if (pane.candleSeries) pane.chart.removeSeries(pane.candleSeries);
    pane.candleBorder = null;
    pane.candleSeries = makePriceSeries(pane.chart, draw, hollow);
    pane.barDraw = draw;
    if (pane.syncHandle) pane.syncHandle.candleSeries = pane.candleSeries;
    if (rows.length) pane.candleSeries.setData(rows);
  }
  applyMainBarColors(pane);
  if (draw === "candle") syncCandleBorder(pane, hollow);
  pane.candleSeries.applyOptions({
    visible: next !== "none",
    priceScaleId: next === "none" ? "off" : "right",
  });
  // 분봉이 없으면 오른쪽 눈금은 지표 비율이다. 가격용 ÷100을 쓰면 80이 0.80으로 보인다.
  pane.chart.applyOptions({
    localization: {
      priceFormatter: next === "none"
        ? (p) => (Number.isFinite(p) ? String(Math.round(p * 100) / 100) : "-")
        : (p) => fmtPrice(p, paneCache(pane)?.tickRaw ?? 5),
    },
  });
  for (const { handle } of pane.active.values()) {
    if (typeof handle.setAxis === "function") handle.setAxis(next === "none" ? "right" : null);
  }
  if (pane.barSelect) pane.barSelect.value = next;
  applyPairMarkers(pane);
}

function syncFramePanelToggle(frame) {
  const btn = frame?.panelToggleEl;
  if (!btn) return;
  btn.classList.toggle("on", frame.panes.some((p) => p.panelOpen));
}

// 이 화면틀의 사이드바를 한 번에 연다/닫는다. 칸마다 줄을 두지 않기 위한 버튼이다.
function setFramePanels(frame, open) {
  if (!frame) return;
  const view = captureAlignView(frame);
  holdAlignFrames.add(frame);
  for (const pane of frame.panes) {
    pane.panelOpen = open;
    pane.panelEl.hidden = !open;
    syncPaneSize(pane);
  }
  syncFramePanelToggle(frame);
  requestAnimationFrame(() => requestAnimationFrame(() => {
    if (frame.panes.length && view) applyAlignView(frame, view, false);
    requestAnimationFrame(() => holdAlignFrames.delete(frame));
  }));
}

function setPanelOpen(pane, open) {
  // 접기 전에 화면틀 창을 잡는다. 폭이 바뀐 뒤 라이브러리가 그 칸만 봉 간격을
  // 고치므로, 접기 전의 오른쪽 끝 시각과 px 봉 간격을 두 프레임 뒤에 다시 깐다.
  // 그 사이 ResizeObserver가 어긋난 간격을 확정하지 않게 이 틀의 맞춤을 보류한다.
  const view = pane.frame ? captureAlignView(pane.frame) : null;
  if (pane.frame) holdAlignFrames.add(pane.frame);
  pane.panelOpen = open;
  pane.panelEl.hidden = !open;
  syncFramePanelToggle(pane.frame);
  syncPaneSize(pane);
  if (!pane.frame) return;
  requestAnimationFrame(() => requestAnimationFrame(() => {
    if (pane.frame.panes.includes(pane) && view) applyAlignView(pane.frame, view, false);
    requestAnimationFrame(() => holdAlignFrames.delete(pane.frame));
  }));
}

// 복사 원본은 이 화면틀에서 마지막으로 다룬 칸이고, 없으면 맨 아래 칸이다.
// 보이는 창은 칸을 만들기 전에 잡아 둔다. 시딩 뒤에 alignFramePanes를 부르면
// 그 사이 범위를 다시 읽어 새 칸의 최신 봉이 틀 전체를 덮는다.
async function addChart(frame) {
  const src = frame.currentPane && frame.panes.includes(frame.currentPane)
    ? frame.currentPane
    : frame.panes[frame.panes.length - 1];
  const view = captureAlignView(frame);
  const next = Workspace.scaleAdd(frame.panes.map((p) => p.heightFrac));
  frame.panes.forEach((p, i) => { p.heightFrac = next[i]; });
  const pane = createPane(frame, next[next.length - 1]);
  pane.overlayStyles = { ...(src?.overlayStyles || {}) };
  repaintPaneOverlays(pane);
  buildPaneTools(pane);
  frame.currentPane = pane;
  currentPane = pane;
  currentFrame = frame;
  layoutGrid();
  if (!src?.symbol) return;
  try {
    await withChartLoad(async () => {
      await selectPaneSymbol(pane, src.symbol, src.symName);
      // 시딩 중에 칸이나 틀이 지워지면 떼인 차트에 지표를 올리지 않는다.
      if (!frame.panes.includes(pane)) return;
      if (pane.symbol !== src.symbol) return;
      for (const [id, entry] of src.active) activateIndicator(pane, id, entry.layers);
      pane.systems = new Set(src.systems || []);
      pane.systemVars = JSON.parse(JSON.stringify(src.systemVars || {}));
      pane.systemBasis = { ...(src.systemBasis || {}) };
      pane.linkLegs = WeeklyLegs.copy(src.linkLegs);
      refreshWeeklyPlots(pane);
      syncFrameData2(frame);
      setBarStyle(pane, src.barStyle || "candle");
      buildPaneTools(pane);
    });
  } finally {
    if (view) applyAlignView(frame, view, true);
  }
}

// 마지막 칸의 ×: 칸은 남기고 종목·지표·Data2만 지운다. 패널은 열고 캔들바로 둔다.
// barDraw가 이미 캔들이면 setBarStyle만으로는 봉이 안 지워지므로 clearPaneData가 먼저다.
function clearPaneContent(pane) {
  pane.selSeq++;
  releaseFrameSearch(pane.frame);
  clearFrameOverlays(pane.frame);
  const prev = pane.symbol;
  const prevData2 = pane.data2;
  pane.symbol = "";
  pane.symName = "";
  pane.selTarget = "";
  pane.data2 = "";
  clearPaneData(pane);
  pane.systems.clear();
  for (const id of [...pane.active.keys()]) deactivateIndicator(pane, id);
  refreshWeeklyPlots(pane);
  setBarStyle(pane, "candle");
  setPanelOpen(pane, true);
  syncFrameSymbolUi(pane);
  buildPaneTools(pane);
  updateBadgeVisibility();
  releaseSymbol(prev);
  if (prevData2 && prevData2 !== prev) releaseSymbol(prevData2);
}

function deleteChart(pane) {
  const frame = pane.frame;
  if (!frame) return;
  if (frame.panes.length <= 1) {
    clearPaneContent(pane);
    return;
  }
  // removePane이 현재 칸을 비운 뒤에는 지운 칸과 비교가 성립하지 않는다.
  const wasCurrent = frame.currentPane === pane;
  removePane(pane);
  if (wasCurrent || !frame.currentPane) {
    frame.currentPane = frame.panes[frame.panes.length - 1] || null;
  }
  if (!currentPane) currentPane = frame.currentPane;
  layoutGrid();
}

function closeFrame(frame) {
  const mode = Workspace.frameClose(gridRows.length, colWeights.length);
  if (mode === "none") return;
  if (mode === "row") removeFrameRow(frame.row);
  else removeFrameCol(frame.row.frames.indexOf(frame));
}

function removeFrameRow(row) {
  const held = [];
  for (const frame of row.frames) {
    releaseFrameSearch(frame);
    held.push(...takeFrameOverlays(frame));
  }
  const doomed = row.frames.flatMap((frame) => [...frame.panes]);
  for (const pane of doomed) destroyPane(pane);
  row.el.remove();
  gridRows = gridRows.filter((r) => r !== row);
  for (const sh of held) releaseSymbol(sh);
  const heights = Workspace.normalizeWeights(gridRows.map((r) => r.heightFrac));
  gridRows.forEach((r, i) => { r.heightFrac = heights[i]; });
  if (currentFrame && !allFrames().includes(currentFrame)) currentFrame = allFrames()[0] || null;
  layoutGrid();
}

function removeFrameCol(col) {
  if (!Number.isInteger(col) || col < 0 || col >= colWeights.length) return;
  const held = [];
  for (const row of gridRows) {
    const frame = row.frames[col];
    releaseFrameSearch(frame);
    held.push(...takeFrameOverlays(frame));
    for (const pane of [...frame.panes]) destroyPane(pane);
    frame.el.remove();
    row.frames.splice(col, 1);
  }
  colWeights.splice(col, 1);
  colWeights = Workspace.normalizeWeights(colWeights);
  for (const sh of held) releaseSymbol(sh);
  if (currentFrame && !allFrames().includes(currentFrame)) currentFrame = allFrames()[0] || null;
  layoutGrid();
}

// 행·열을 닫을 때는 칸마다 높이를 다시 나누거나 경계 손잡이를 다시 달지 않는다.
function destroyPane(pane, { release = true } = {}) {
  const frame = pane.frame;
  if (!frame) return;
  const i = frame.panes.indexOf(pane);
  if (i < 0) return;
  frame.panes.splice(i, 1);
  syncFramePanelToggle(frame);
  if (frame.currentPane === pane) frame.currentPane = null;
  syncPaneList();
  paneResize.unobserve(pane.chartEl);
  if (currentPane === pane) currentPane = null;
  pane.selSeq++; // 진행 중인 종목 선택의 늦은 완료를 폐기한다
  pane.active.clear();
  if (pane.syncHandle) frame.sync.remove(pane.syncHandle); // 차트 제거 전에 동기화 구독부터 뗀다
  pane.chart.remove();
  pane.el.remove();
  updateBadgeVisibility();
  // syncPaneList 뒤다. 다른 칸의 종목·Data2·진행 중 선택은 releaseSymbol이 유지한다.
  if (release) {
    releaseSymbol(pane.symbol);
    if (pane.data2 && pane.data2 !== pane.symbol) releaseSymbol(pane.data2);
  }
}

function removePane(pane, { release = true } = {}) {
  const frame = pane.frame;
  if (!frame || frame.panes.indexOf(pane) < 0) return;
  destroyPane(pane, { release });
  // 남은 칸 높이는 그 화면틀 안에서만 비율대로 나눈다
  const paneH = Workspace.normalizeWeights(frame.panes.map((p) => p.heightFrac));
  frame.panes.forEach((p, idx) => {
    p.heightFrac = paneH[idx];
    syncPaneSize(p);
  });
  rebuildResizeBars();
}

// 더 이상 어느 칸도 보지 않는 종목을 정리한다: 엔진 watch를 해지하고 로컬 캐시를 지운다.
// (화면 구독 자원 정리 — 전략 거래 대상과는 무관하다, 계획서 §18)
// 해지 요청은 종목별 큐에 넣어 이 종목의 watch와 순서를 맞춘다: 독립 fetch의 엔진 도착
// 순서는 보장되지 않아, 먼저 낸 unwatch가 뒤에 낸 watch보다 늦게 도착하면 막 채택한
// 종목을 끊는다 (칸 삭제·stale 폐기 경로 모두 같은 클래스). 큐에서 기다리는 동안 이
// 종목을 채택하는 선택이 붙었으면(symbol/selTarget) 해지를 건너뛴다.
function symbolHeld(shcode) {
  if (!WatchGuard.staleWatchLeaks(shcode, panes)) return true;
  for (const frame of allFrames()) {
    if (frame.overlays?.includes(shcode)) return true;
    if (frame.overlayTargets?.has(shcode)) return true;
  }
  return false;
}

async function releaseSymbol(shcode) {
  // 종목으로 보는 칸, Data2, 겹침 종목, 진행 중 선택이 있으면 캐시도 watch도 유지한다.
  if (!shcode || symbolHeld(shcode)) return;
  seedInflight.delete(shcode); // 진행 중 시딩은 고아 캐시를 채우고 렌더 없이 끝난다
  feed.drop(shcode);
  await symbolOpQueue.enqueue(shcode, async () => {
    if (symbolHeld(shcode)) return;
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
  });
}

function weightSlot(index) {
  return {
    index,
    get weight() { return colWeights[index]; },
    set weight(v) { colWeights[index] = v; },
  };
}

// 화면틀 안 칸 경계, 행 경계, 열 경계(열마다 하나)를 다시 단다.
function rebuildResizeBars() {
  panesEl.querySelectorAll(".resizebar").forEach((e) => e.remove());
  for (const frame of allFrames()) {
    const list = frame.panes;
    for (let i = 0; i + 1 < list.length; i++) {
      const bar = document.createElement("div");
      bar.className = "resizebar";
      bar.textContent = "⠿";
      bar.title = "드래그로 크기 조절";
      attachResize(bar, list[i], list[i + 1], frame.bodyEl, "pane");
      list[i].el.append(bar);
    }
  }
  for (let r = 0; r + 1 < gridRows.length; r++) {
    const bar = document.createElement("div");
    bar.className = "resizebar row";
    bar.textContent = "⠿";
    bar.title = "드래그로 크기 조절";
    attachResize(bar, gridRows[r], gridRows[r + 1], panesEl, "row");
    gridRows[r].el.append(bar);
  }
  for (let c = 0; c + 1 < colWeights.length; c++) {
    const bar = document.createElement("div");
    bar.className = "resizebar col";
    bar.title = "드래그로 크기 조절";
    const left = colWeights.slice(0, c + 1).reduce((s, w) => s + w, 0);
    bar.style.left = `${left * 100}%`;
    attachResize(bar, weightSlot(c), weightSlot(c + 1), panesEl, "col");
    panesEl.append(bar);
  }
}

// measureEl은 비율의 기준 상자다. 칸 경계는 화면틀 본문, 행·열 경계는 #panes.
function attachResize(bar, above, below, measureEl, kind) {
  bar.addEventListener("mousedown", (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    const vertical = kind !== "col";
    const totalPx = vertical ? measureEl.clientHeight : measureEl.clientWidth;
    if (!totalPx) return;
    const start = vertical ? ev.clientY : ev.clientX;
    const a0 = vertical ? above.heightFrac : above.weight;
    const b0 = vertical ? below.heightFrac : below.weight;
    const move = (e2) => {
      const cur = vertical ? e2.clientY : e2.clientX;
      const d = (cur - start) / totalPx;
      const sum = a0 + b0;
      const a = Math.min(Math.max(a0 + d, MIN_PANE_FRAC), sum - MIN_PANE_FRAC);
      const b = sum - a;
      if (kind === "col") {
        above.weight = a;
        below.weight = b;
        const left = colWeights.slice(0, above.index + 1).reduce((s, w) => s + w, 0);
        bar.style.left = `${left * 100}%`;
        for (const row of gridRows) {
          const leftFrame = row.frames[above.index];
          const rightFrame = row.frames[below.index];
          if (leftFrame) leftFrame.el.style.width = `${a * 100}%`;
          if (rightFrame) rightFrame.el.style.width = `${b * 100}%`;
          resizeFrameCharts([leftFrame, rightFrame].filter(Boolean));
        }
        return;
      }
      above.heightFrac = a;
      below.heightFrac = b;
      if (kind === "row") {
        above.el.style.height = `${a * 100}%`;
        below.el.style.height = `${b * 100}%`;
        resizeFrameCharts(above.frames);
        resizeFrameCharts(below.frames);
        return;
      }
      syncPaneSize(above);
      syncPaneSize(below);
    };
    const up = () => {
      removeEventListener("mousemove", move);
      removeEventListener("mouseup", up);
      layoutGrid();
    };
    addEventListener("mousemove", move);
    addEventListener("mouseup", up);
  });
}

// ---- 화면틀 종목 선택 ----
// 화면틀 도구줄의 종목 입력은 하나다. 검색 드롭다운은 /api/market?q= 프록시.
// 고르면 그 틀의 칸에 적용하고, 같은 틀의 나머지 칸도 그 종목을 따른다.

function frameSymbolPane(frame) {
  if (!frame) return null;
  if (frame.currentPane && frame.panes.includes(frame.currentPane)) return frame.currentPane;
  return frame.panes[0] || null;
}

function buildFramePicker(frame) {
  const picker = document.createElement("span");
  picker.className = "sym-picker";
  const input = document.createElement("input");
  input.size = 10;
  input.placeholder = "코드/종목명";
  input.autocomplete = "off";
  input.title = "이 화면틀의 메인 종목 (코드 또는 종목명, Enter로 적용)";
  const chips = document.createElement("span");
  chips.className = "sym-chips";
  const results = document.createElement("div");
  results.className = "sym-results";
  results.hidden = true;
  const browseBtn = document.createElement("button");
  browseBtn.type = "button";
  browseBtn.className = "sym-browse-btn";
  browseBtn.title = "종목 찾기";
  browseBtn.innerHTML = '<svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true"><circle cx="7" cy="7" r="4.2" fill="none" stroke="currentColor" stroke-width="1.6"/><path d="M10.4 10.4 L14 14" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/></svg>';
  browseBtn.onclick = () => openSymbolBrowse(frame);
  picker.append(chips, input, browseBtn, results);
  frame.pickerEl = picker;
  frame.chipsEl = chips;
  frame.symInput = input;
  frame.symResults = results;

  // 보이는 글자는 CSS text-transform으로 대문자다. input 중에 value를 다시
  // 넣고 커서를 옮기면, Shift를 누른 채 칠 때 크롬이 글자를 A와 B로만 넣는다.
  input.addEventListener("focus", () => { currentFrame = frame; });
  input.addEventListener("input", () => onFrameSymbolInput(frame));
  input.addEventListener("keydown", (ev) => {
    if (ev.key === "ArrowDown" || ev.key === "ArrowUp") {
      if (moveSymbolHighlight(frame, ev.key === "ArrowDown" ? 1 : -1)) ev.preventDefault();
    }
    if (ev.key === "Enter") {
      ev.preventDefault();
      confirmFrameSymbol(frame);
    }
    if (ev.key === "Escape") {
      frame.replacingMain = false;
      hideFrameResults(frame);
      syncFrameChips(frame);
    }
  });
  syncFrameChips(frame);
  return picker;
}

let symbolBrowse = null;
function openSymbolBrowse(frame) {
  hideFrameResults(frame);
  if (!symbolBrowse) symbolBrowse = SymbolBrowse.mount(document.body);
  symbolBrowse.open((code, name) => applyFrameCode(frame, code, name));
}

function releaseFrameSearch(frame) {
  if (!frame) return;
  clearTimeout(frame.searchTimer);
  frame.searchTimer = null;
  hideFrameResults(frame);
}

// 화면틀 칩에 종목번호와 종목명을 맞춘다. 입력창은 다음 종목을 받는 칸이라 여기서 값을 덮지 않는다.
function syncFrameSymbolUi(pane) {
  if (pane?.frame) syncFrameChips(pane.frame);
}

function syncFrameChips(frame) {
  if (!frame?.chipsEl) return;
  const pane = frameSymbolPane(frame);
  const main = pane?.symbol || "";
  const mainName = (pane?.symbol ? pane.symName : "") || feed.get(main)?.name || "";
  frame.symInput.placeholder = frame.replacingMain ? "메인 변경" : (main ? "종목 추가" : "코드/종목명");
  frame.symInput.title = frame.replacingMain
    ? "새 메인 종목. Enter로 바꾸면 겹친 종목은 남습니다"
    : (main
      ? "겹칠 종목 (코드 또는 종목명, Enter로 차트 위에 올린다)"
      : "이 화면틀의 메인 종목 (코드 또는 종목명, Enter로 적용)");
  frame.chipsEl.replaceChildren();
  if (main) {
    const chip = document.createElement("button");
    chip.type = "button";
    chip.className = `sym-chip main${frame.replacingMain ? " replacing" : ""}`;
    chip.append(symbolChipText(main, mainName));
    const mainRole = orderChipRole(frame, main);
    if (mainRole) {
      const badge = document.createElement("span");
      badge.className = "sym-chip-role";
      badge.textContent = mainRole;
      chip.append(badge);
    }
    if (frame.mainColor && !frame.replacingMain) chip.style.borderColor = frame.mainColor;
    chip.title = `${mainName ? `${mainName}. ` : ""}클릭하면 메인 종목을 바꿉니다`;
    chip.onclick = () => {
      frame.replacingMain = !frame.replacingMain;
      syncFrameChips(frame);
      frame.symInput.focus();
    };
    frame.chipsEl.append(chip);
  }
  for (const code of frame.overlays) {
    const chip = document.createElement("span");
    chip.className = "sym-chip";
    chip.style.borderColor = frame.overlayColors[code] || "";
    const overlayName = frame.overlayNames[code] || feed.get(code)?.name || "";
    chip.title = overlayName || code;
    const label = symbolChipText(code, overlayName);
    const role = orderChipRole(frame, code);
    const close = document.createElement("button");
    close.type = "button";
    close.className = "x";
    close.textContent = "×";
    close.title = "이 겹침 종목을 지운다";
    close.onclick = () => removeFrameOverlay(frame, code);
    chip.append(label);
    if (role) {
      const badge = document.createElement("span");
      badge.className = "sym-chip-role";
      badge.textContent = role;
      chip.append(badge);
    }
    chip.append(close);
    frame.chipsEl.append(chip);
  }
  if (frame.overlays.length) {
    const scale = document.createElement("select");
    scale.className = "overlay-scale";
    scale.title = "추가 종목 눈금. 각자 가격은 왼쪽이 추가 종목, 오른쪽이 메인입니다";
    for (const item of [
      { id: "price", name: "각자 가격" },
      { id: "ratio", name: "100 비율" },
      { id: "shared", name: "같은 눈금" },
    ]) {
      const opt = document.createElement("option");
      opt.value = item.id;
      opt.textContent = item.name;
      scale.append(opt);
    }
    scale.value = overlayScaleMode(frame);
    scale.onmousedown = (ev) => ev.stopPropagation();
    scale.onchange = () => setOverlayScale(frame, scale.value);
    frame.chipsEl.append(scale);
  }
}

function symbolChipText(code, name) {
  const wrap = document.createElement("span");
  wrap.className = "sym-chip-text";
  wrap.append(document.createTextNode(code));
  const trimmed = String(name || "").trim();
  if (!trimmed || trimmed === code) return wrap;
  const nm = document.createElement("span");
  nm.className = "sym-chip-name";
  nm.textContent = trimmed;
  wrap.append(nm);
  return wrap;
}

function hideFrameResults(frame) {
  if (!frame?.symResults) return;
  frame.symActive = -1;
  frame.symResults.hidden = true;
  frame.symResults.replaceChildren();
}

function paintSymbolHighlight(frame) {
  const rows = frame.symResults.querySelectorAll(".row");
  rows.forEach((row, i) => {
    const on = i === frame.symActive;
    row.classList.toggle("active", on);
    if (on) row.scrollIntoView({ block: "nearest" });
  });
}

function moveSymbolHighlight(frame, delta) {
  const items = frame.symCandidates || [];
  if (frame.symResults.hidden || items.length === 0) return false;
  frame.symActive = SymbolPick.move(frame.symActive, delta, items.length);
  paintSymbolHighlight(frame);
  return true;
}

function showFrameResults(frame, items, seq) {
  if (seq !== frame.searchSeq) return; // 최신 검색만 반영
  frame.symCandidates = items;
  frame.symActive = SymbolPick.activeIndex(frame.symInput.value, items);
  frame.symResults.replaceChildren();
  if (items.length === 0) {
    const div = document.createElement("div");
    div.className = "empty";
    div.textContent = "일치하는 종목 없음";
    frame.symResults.append(div);
  }
  items.forEach((it, i) => {
    const row = document.createElement("div");
    row.className = "row";
    const code = document.createElement("span");
    code.className = "code";
    code.textContent = it.shcode;
    const name = document.createElement("span");
    name.textContent = it.name;
    const mkt = document.createElement("span");
    mkt.className = "mkt";
    // fut: 0=주식, 1=국내선물, 2=해외선물 (엔진 market.instruments)
    mkt.textContent = it.fut === 2 ? "해외" : it.fut === 3 ? "옵션" : it.fut ? "선물" : "";
    row.append(code, name, mkt);
    row.onmouseenter = () => {
      frame.symActive = i;
      paintSymbolHighlight(frame);
    };
    row.onclick = () => applyFrameCode(frame, it.shcode, it.name);
    frame.symResults.append(row);
  });
  frame.symResults.hidden = false;
  paintSymbolHighlight(frame);
}

function onFrameSymbolInput(frame) {
  clearTimeout(frame.searchTimer);
  const q = frame.symInput.value.trim().toUpperCase();
  if (!q) {
    frame.symCandidates = [];
    frame.symCandidateQuery = "";
    return hideFrameResults(frame);
  }
  frame.searchTimer = setTimeout(() => loadSymbolCandidates(frame), 200);
}

// 지금 입력과 같은 검색이 이미 끝나 있으면 그 결과를 쓰고, 아니면 바로 받아 온다.
// Enter는 디바운스를 기다리지 않는다.
function loadSymbolCandidates(frame) {
  const q = frame.symInput.value.trim().toUpperCase();
  if (!q) {
    frame.symCandidates = [];
    frame.symCandidateQuery = "";
    hideFrameResults(frame);
    return Promise.resolve([]);
  }
  if (frame.symCandidateQuery === q && frame.symSearch) return frame.symSearch;
  clearTimeout(frame.searchTimer);
  frame.searchTimer = null;
  const seq = ++frame.searchSeq;
  frame.symCandidateQuery = q;
  frame.symSearch = (async () => {
    try {
      const res = await fetch(`/api/market?q=${encodeURIComponent(q)}&limit=20`);
      const data = await res.json().catch(() => ({}));
      if (seq !== frame.searchSeq) return frame.symCandidates || [];
      if (!res.ok) {
        const why = data.error_code === "registry_unavailable"
          ? "종목 목록을 아직 받지 못했습니다"
          : "종목 검색 실패";
        showFrameResults(frame, [], seq);
        if (seq === frame.searchSeq && frame.symResults.firstChild) {
          frame.symResults.firstChild.textContent = why;
        }
        return [];
      }
      const items = data.payload?.items ?? [];
      showFrameResults(frame, items, seq);
      return items;
    } catch {
      return frame.symCandidates || [];
    }
  })();
  return frame.symSearch;
}

// Enter와 검색 목록 클릭이 같은 규칙이다. 메인이 없거나 메인 칩을 눌러 둔 상태면
// 메인 종목을 바꾸고, 이미 메인이 있으면 겹침 종목으로 올린다.
async function applyFrameCode(frame, code, pickedName) {
  const pane = frameSymbolPane(frame);
  if (!pane) return;
  hideFrameResults(frame);
  frame.symInput.value = "";
  code = String(code ?? "").trim().toUpperCase();
  if (!code) {
    frame.replacingMain = false;
    syncFrameChips(frame);
    return;
  }
  const main = pane.symbol;
  if (!main || frame.replacingMain) {
    frame.replacingMain = false;
    if (frame.overlays.includes(code)) {
      frame.overlays.splice(frame.overlays.indexOf(code), 1);
      frame.overlayTargets.delete(code);
      delete frame.overlayNames[code];
      delete frame.overlayColors[code];
      delete frame.overlayStyles[code];
      for (const other of frame.panes) dropOverlaySeries(other, code);
      forgetPairCode(frame, code);
      syncFrameData2(frame);
    }
    await selectPaneSymbol(pane, code, pickedName);
    syncFrameChips(frame);
    return;
  }
  if (code === main) {
    syncFrameChips(frame);
    return;
  }
  await addFrameOverlay(frame, code, pickedName);
}

async function confirmFrameSymbol(frame) {
  const pane = frameSymbolPane(frame);
  if (!pane) return;
  clearTimeout(frame.searchTimer);
  frame.searchTimer = null;
  const typed = frame.symInput.value;
  const items = await loadSymbolCandidates(frame);
  const hit = items[frame.symActive] || SymbolPick.pick(typed, items);
  await applyFrameCode(frame, hit ? hit.shcode : typed, hit?.name || "");
}

// watch 요청 공통부 (선택·화면틀 복원·스트림 리셋 재구독이 함께 쓴다).
// 종목별 큐(WatchGuard.createOpQueue)로 직렬화한다: 같은 종목의 unwatch가
// 뒤에 시작된 watch보다 엔진에 늦게 도착하는 경주를 막는다 (releaseSymbol 참조).
const symbolOpQueue = WatchGuard.createOpQueue();
function watchSymbol(shcode) {
  const epoch = loadEpoch;
  return symbolOpQueue.enqueue(shcode, async () => {
    if (!loadStill(epoch)) return { ok: false, error: "cancelled" };
    const token = await apiToken();
    if (!token) return { ok: false, error: "no_token" };
    if (!loadStill(epoch)) return { ok: false, error: "cancelled" };
    let res;
    try {
      res = await fetch("/api/symbols/watch", {
        method: "POST",
        headers: { "content-type": "application/json", "x-trader-token": token },
        body: JSON.stringify({ shcode }),
        signal: loadAbort.signal,
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
  });
}

// 한 화면틀의 칸은 같은 종목을 본다. 방금 고른 칸의 종목·종목명을 같은 틀의 나머지 칸에 복사한다.
// 시딩 전에 부른다. 그러면 seedSymbol이 그 종목의 칸을 한 번에 다시 그린다.
function adoptFrameSymbol(pane) {
  const frame = pane.frame;
  if (!frame || !pane.symbol) return false;
  let changed = false;
  for (const other of frame.panes) {
    if (other === pane) continue;
    if (other.symbol === pane.symbol) {
      if (other.symName !== pane.symName) other.symName = pane.symName;
      continue;
    }
    changed = true;
    other.selSeq++;
    const prev = other.symbol;
    other.symbol = pane.symbol;
    other.symName = pane.symName;
    other.selTarget = "";
    clearPairSim(other);
    clearPaneData(other);
    buildPaneTools(other);
    if (prev) releaseSymbol(prev);
  }
  if (changed) updateBadgeVisibility();
  syncFrameSymbolUi(pane);
  return changed;
}

// 칸에 종목을 설정한다: watch → 캐시에 반영 → 시딩(완료 시 그 종목의 모든 칸을 다시 그림).
// 같은 화면틀의 다른 칸도 그 종목으로 맞춘다. 다른 화면틀은 바꾸지 않는다.
// 실패하면 칸은 기존 종목과 화면을 그대로 유지한다 (입력창만 현재 종목으로 되돌린다).
// selSeq는 빠른 연속 선택 시 늦은 완료를 폐기한다. 폐기되는 선택은 엔진이 이미 그 종목을
// watch했을 수 있으므로, 아무도 이어받지 않은 watch이면 해지한다 (누수 방지). 선택 실패
// 시에도 같은 판정으로 정리한다 — 다른 선택의 stale 완료가 이 선택의 selTarget을 보고
// 해지를 보류했던 watch가 실패와 함께 주인을 잃는 경우가 있다.
async function selectPaneSymbol(pane, shcode, name) {
  shcode = String(shcode ?? "").trim().toUpperCase();
  hideFrameResults(pane.frame);
  if (!shcode) {
    syncFrameSymbolUi(pane);
    return;
  }
  if (shcode === pane.symbol) {
    forgetPairCode(pane.frame, shcode);
    syncFrameSymbolUi(pane);
    if (adoptFrameSymbol(pane)) await seedSymbol(shcode);
    return;
  }
  const seq = ++pane.selSeq;
  pane.selTarget = shcode; // 진행 중 선택 목표 — stale 폐기 시 watch 해지 판정에 쓴다
  let fail = "";
  await withChartLoad(async () => {
    const w = await watchSymbol(shcode);
    if (seq !== pane.selSeq) {
      // 그 사이 다른 선택이 시작됐다. 늦게 붙은 watch는 어느 칸도 안 보고 다른 진행 중
      // 선택도 노리지 않으면 그대로 새어 나간다 — 해지한다 (인계된 watch는 건드리지 않는다).
      // w.ok가 false여도 엔진엔 적용되고 응답만 유실됐을 수 있으므로 같은 판정으로 정리한다
      // (관측 중이 아닌 종목의 unwatch는 엔진이 거절해 무해하다)
      if (WatchGuard.staleWatchLeaks(shcode, panes)) releaseSymbol(shcode);
      finishSeedIfIdle(shcode);
      return;
    }
    if (!w.ok) {
      pane.selTarget = "";
      syncFrameSymbolUi(pane);
      // 채택 실패로 이 종목에 주인이 남지 않았으면 남은 watch를 해지한다
      // (stale 폐기 경로와 같은 판정 — 인계받은 watch가 있으면 건드리지 않는다)
      if (WatchGuard.staleWatchLeaks(shcode, panes)) releaseSymbol(shcode);
      fail = `종목 관측 실패 (${shcode}): ${w.error}`;
      finishSeedIfIdle(shcode);
      return;
    }
    const prev = pane.symbol;
    pane.symbol = shcode;
    pane.symName = name ?? "";
    clearPairSim(pane);
    forgetPairCode(pane.frame, shcode);
    syncPairLegs(pane.frame);
    clearPaneData(pane); // 이전 종목의 잔여 표시를 지운다
    const cache = feed.forSymbol(shcode);
    if (w.name) {
      cache.name = w.name;
      pane.symName = w.name;
    }
    // 엔진이 알려준 현 세대를 바닥으로 깐다 — 이보다 낮은 세대의 늦은 메시지가 리셋을 일으키지 않게
    if (typeof w.generation === "number") feed.noteGeneration(cache, w.generation);
    if (!engineWatches.includes(shcode)) engineWatches.push(shcode);
    syncFrameSymbolUi(pane);
    buildPaneTools(pane); // 지표 트리가 보이기 시작한다
    updateBadgeVisibility();
    adoptFrameSymbol(pane);
    // 이전 종목은 새 watch가 붙은 뒤에 해제한다 (엔진의 마지막-watch 해지 거부를 피한다)
    if (prev) releaseSymbol(prev);
    await seedSymbol(shcode);
    if (seq !== pane.selSeq) return;
    syncFrameData2(pane.frame);
  });
  if (fail) alert(fail);
}

// 스나이퍼 Data2의 참조는 이 화면틀의 두 번째 종목(첫 겹침)이다.
function frameSecondSymbol(frame) {
  return frame?.overlays?.[0] || "";
}

function syncFrameData2(frame) {
  if (!frame) return;
  const code = frameSecondSymbol(frame);
  for (const pane of frame.panes || []) {
    pane.data2 = code;
    const entry = pane.active.get("fx_data2");
    if (!entry) continue;
    try {
      if (!code) entry.handle.clear();
      else entry.handle.setSource(feed.get(code) || null);
    } catch (err) {
      console.error(`[${code}] Data2 반영 실패`, err);
    }
  }
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

// 칸 왼쪽 패널: 분봉 모양, 그 아래 예스랭귀지 원본 폴더와 파일 이름(확장자 없음).
// 종목 미선택 칸은 안내 문구만 보인다. 체크 상태는 pane.active·systems 를 다시 읽는다.
let ylCatalog = { dirs: [] };

function buildPaneTools(pane) {
  const panel = pane.panelEl;
  panel.replaceChildren();

  panel.append(buildChartControls(pane));
  if (!pane.symbol) {
    const hint = document.createElement("div");
    hint.className = "ind-hint";
    hint.textContent = "종목을 선택하면 원본 항목을 고를 수 있습니다";
    panel.append(hint);
    return;
  }

  for (const dir of YlTree.build(ylCatalog)) panel.append(buildSourceDir(pane, dir));
  const covered = YlTree.indicatorIds();
  for (const meta of indicatorManifest) {
    if (!meta || covered.has(meta.id) || !(meta.id in RENDERERS)) continue;
    panel.append(buildIndNode(pane, meta));
  }
}

async function loadYlCatalog() {
  // /api/yeslang 은 원본 디렉터리를 그대로 읽는다. 라우트가 없거나 폴더가 비어
  // 있으면 같은 내용의 정적 목록으로 넘어간다. 빈 dirs 를 성공으로 받으면
  // 배포본 사이드바에서 예스트레이더 항목이 사라진다.
  for (const url of ["/api/yeslang", "/yl-catalog.json"]) {
    try {
      const res = await fetch(url);
      if (!res.ok) continue;
      const data = await res.json();
      if (!YlTree.hasEntries(data)) continue;
      ylCatalog = data;
      for (const pane of panes) buildPaneTools(pane);
      return;
    } catch (err) {
      console.error("원본 목록을 읽지 못했습니다", url, err);
    }
  }
}

function optionLetter(name) {
  const s = String(name || "").trim();
  if (/^P\b/.test(s)) return "P";
  if (/^C\b/.test(s)) return "C";
  return "";
}

function codeName(pane, code) {
  if (!code) return "";
  if (code === pane.symbol) return pane.symName || feed.get(code)?.name || "";
  return pane.frame?.overlayNames?.[code] || feed.get(code)?.name || "";
}

// 주문은 시스템을 올린 옵션 차트(Data1)에만 그린다. 콜 차트면 콜, 풋 차트면 풋.
function pairOptionCharts(pane, systemId) {
  const codes = frameSymbolCodes(pane.frame);
  const letters = codes.map((code) => optionLetter(codeName(pane, code)));
  const basis = pane.systemBasis?.[systemId] === "P" ? "P" : "C";
  return WeeklyLegs.optionCharts(codes, letters, basis);
}

function paintPairSeries(pane, series, key, marks) {
  if (!series || typeof series.setMarkers !== "function") return;
  if (!pane.pairLabelBy) pane.pairLabelBy = new Map();
  // 눈금·분봉 모양이 바뀌면 시리즈가 새로 생긴다. 글자는 그 시리즈에 다시 붙인다.
  let slot = pane.pairLabelBy.get(key);
  if (!slot || slot.series !== series) {
    if (slot?.labels && slot.series && typeof slot.series.detachPrimitive === "function") {
      try { slot.series.detachPrimitive(slot.labels); } catch { /* 이미 제거된 시리즈 */ }
    }
    slot = { series, labels: PairMarkers.labels() };
    pane.pairLabelBy.set(key, slot);
    if (typeof series.attachPrimitive === "function") series.attachPrimitive(slot.labels);
  }
  slot.labels.setMarks(marks);
  series.setMarkers(PairMarkers.seriesMarkers(marks));
}

function applyPairMarkers(pane) {
  const markers = PairMarkers.fromEvents(pane.pairEvents);
  const groups = new Map();
  for (const mark of markers) {
    const key = mark.symbol || pane.symbol || "";
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(mark);
  }
  try {
    paintPairSeries(pane, pane.candleSeries, pane.symbol || "", groups.get(pane.symbol || "") || []);
    for (const [code, series] of pane.overlaySeries || []) {
      paintPairSeries(pane, series, code, groups.get(code) || []);
    }
  } catch (err) {
    console.error("페어 신호 표시 실패", err);
  }
}

function clearPairSim(pane) {
  pane.pairEvents = [];
  pane.pairStatus = "";
  pane.pairLast = null;
  applyPairMarkers(pane);
}

function frameSymbolCodes(frame) {
  const main = frameSymbolPane(frame)?.symbol || "";
  const codes = [];
  if (main) codes.push(main);
  for (const code of frame?.overlays || []) {
    if (code && !codes.includes(code)) codes.push(code);
  }
  return codes;
}

function framePairOrderOn(frame) {
  return (frame?.panes || []).some((pane) =>
    (pane.systems && pane.systems.size > 0) || WPLOT_IDS.some((id) => pane.active.has(id)));
}

// 페어를 쓰는 화면틀만 첫 종목부터 지수, 콜, 풋으로 보여 준다.
function orderChipRole(frame, code) {
  if (!code || !framePairOrderOn(frame)) return "";
  const codes = frameSymbolCodes(frame);
  if (codes[0] === code) return "지수";
  if (codes[1] === code) return "콜";
  if (codes[2] === code) return "풋";
  return "";
}

function syncPairLegs(frame) {
  if (!frame) return;
  const legs = WeeklyLegs.fromSymbolOrder(frameSymbolCodes(frame));
  for (const pane of frame.panes || []) {
    pane.linkLegs = WeeklyLegs.copy(legs);
    refreshWeeklyPlots(pane);
    refreshKsData2(pane);
  }
  syncFrameChips(frame);
}

function forgetPairCode(frame, code) {
  if (!frame || !code) return;
  if (frame.pairOpp === code) {
    frame.pairOpp = "";
    frame.pairOppName = "";
  }
  if (frame.pairFut === code) {
    frame.pairFut = "";
    frame.pairFutName = "";
  }
  let changed = false;
  for (const pane of frame.panes || []) {
    if (!pane.linkLegs) continue;
    const next = WeeklyLegs.forgetCode(pane.linkLegs, code);
    const prevLong = WeeklyLegs.legForIndicator(pane.linkLegs, "w_link_long");
    const prevShort = WeeklyLegs.legForIndicator(pane.linkLegs, "w_link_short");
    const nextLong = WeeklyLegs.legForIndicator(next, "w_link_long");
    const nextShort = WeeklyLegs.legForIndicator(next, "w_link_short");
    if (prevLong.opp === nextLong.opp && prevLong.fut === nextLong.fut &&
        prevShort.opp === nextShort.opp && prevShort.fut === nextShort.fut) {
      continue;
    }
    pane.linkLegs = next;
    changed = true;
    refreshWeeklyPlots(pane);
    refreshKsData2(pane);
  }
  if (changed) syncFrameChips(frame);
}

function paneWantsWeekly(pane) {
  return WPLOT_IDS.some((id) => pane.active.has(id)) || (pane.systems && pane.systems.size > 0);
}

function setWeeklySide(pane, ids, payload) {
  for (const id of ids) {
    const entry = pane.active.get(id);
    if (entry && typeof entry.handle.setPlots === "function") entry.handle.setPlots(payload);
  }
}

function refreshWeeklyPlots(pane) {
  if (!pane) return;
  clearTimeout(pane.wplotTimer);
  if (!paneWantsWeekly(pane)) {
    pane.pairEvents = [];
    applyPairMarkers(pane);
    return;
  }
  pane.wplotTimer = setTimeout(() => {
    loadWeeklyPlots(pane);
  }, 200);
}

function refreshFrameWeekly(frame) {
  for (const pane of panes) if (pane.frame === frame) refreshWeeklyPlots(pane);
}

function refreshWeeklyForSymbol(shcode) {
  for (const pane of panes) {
    const long = WeeklyLegs.legForIndicator(pane.linkLegs, "w_link_long");
    const short = WeeklyLegs.legForIndicator(pane.linkLegs, "w_link_short");
    if (pane.symbol === shcode || long.opp === shcode || long.fut === shcode ||
        short.opp === shcode || short.fut === shcode) {
      refreshWeeklyPlots(pane);
    }
    if (long.fut === shcode || short.fut === shcode) refreshKsData2(pane);
  }
}

async function loadWeeklyPlots(pane) {
  const codes = frameSymbolCodes(pane.frame);
  const sides = [
    { leg: "w_link_long", ids: ["w_ret_long", "w_link_long"], side: 1, systemId: "pair-long" },
    { leg: "w_link_short", ids: ["w_ret_short", "w_link_short"], side: -1, systemId: "pair-short" },
  ];
  const wanted = sides.filter((side) =>
    side.ids.some((id) => pane.active.has(id)) || pane.systems?.has(side.systemId));
  if (!wanted.length) {
    pane.pairEvents = [];
    applyPairMarkers(pane);
    return;
  }
  // 지수·콜·풋이 다 붙기 전의 갱신은 선을 지우지 않는다. 지우면 영점만 남고
  // 나중에 도착한 막대 응답은 번호가 밀려 버려진다.
  const readySides = wanted.filter((side) => {
    const leg = WeeklyLegs.legForIndicator(WeeklyLegs.fromSymbolOrder(codes), side.leg);
    const self = WeeklyLegs.selfFor(codes, side.side);
    side.legNow = leg;
    side.self = self;
    side.plotted = side.ids.some((id) => pane.active.has(id));
    side.systemOn = !!pane.systems?.has(side.systemId);
    return self.length >= 4 && leg.opp.length >= 4 && leg.fut.length >= 4;
  });
  if (!readySides.length) return;
  const seq = (pane.wplotSeq = (pane.wplotSeq || 0) + 1);
  const marks = [];
  await Promise.all(readySides.map(async (side) => {
    const leg = side.legNow;
    const self = side.self;
    const plotted = side.plotted;
    const systemOn = side.systemOn;
    const jobs = [];
    if (plotted) {
      jobs.push((async () => {
        try {
          const q = new URLSearchParams({ self, opp: leg.opp, fut: leg.fut });
          const res = await fetch(`/api/pair/plots?${q}`);
          const data = await res.json().catch(() => ({}));
          if (pane.wplotSeq !== seq) return;
          if (!res.ok || data.status === "rejected" || data.error) {
            setWeeklySide(pane, side.ids, null);
            return;
          }
          setWeeklySide(pane, side.ids, data.payload || null);
        } catch (err) {
          console.error(err);
          if (pane.wplotSeq === seq) setWeeklySide(pane, side.ids, null);
        }
      })());
    }
    if (systemOn) {
      const charts = pairOptionCharts(pane, side.systemId);
      for (const chart of charts) {
        jobs.push((async () => {
          try {
            const q = new URLSearchParams({
              side: String(side.side), self: chart.self, opp: chart.opp, fut: chart.fut,
            });
            const vars = pane.systemVars?.[side.systemId];
            if (vars && Object.keys(vars).length) q.set("cfg", JSON.stringify(vars));
            const res = await fetch(`/api/pair/sim?${q}`);
            const data = await res.json().catch(() => ({}));
            if (pane.wplotSeq !== seq) return;
            const payload = data.payload && typeof data.payload === "object" ? data.payload : null;
            const events = res.ok && payload && Array.isArray(payload.events) ? payload.events : [];
            for (const ev of events) {
              if (ev.p) continue;
              marks.push({ t: ev.t, k: ev.k, side: side.side, n: ev.n, q: ev.q, symbol: chart.self });
            }
          } catch (err) {
            console.error(err);
          }
        })());
      }
    }
    await Promise.all(jobs);
  }));
  if (pane.wplotSeq !== seq) return;
  pane.pairEvents = marks;
  applyPairMarkers(pane);
}

// 왼쪽 패널 위: 이 칸의 분봉 모양을 고른다. 칸을 더 여는 것은 위쪽 행추가·열추가다.
function buildChartControls(pane) {
  const box = document.createElement("div");
  box.className = "ind-chart-tools";
  const label = document.createElement("label");
  label.className = "bar-style";
  const name = document.createElement("span");
  name.textContent = pane.symbol || "분봉";
  if (pane.symbol) {
    const mainName = pane.symName || feed.get(pane.symbol)?.name || "";
    name.title = mainName ? `${mainName}. 이 칸의 봉` : "이 칸의 봉";
    if (pane.frame?.mainColor) name.style.color = pane.frame.mainColor;
  }
  label.append(name);
  const sel = document.createElement("select");
  for (const s of BAR_STYLES) {
    const opt = document.createElement("option");
    opt.value = s.id;
    opt.textContent = s.name;
    sel.append(opt);
  }
  sel.value = pane.barStyle || "candle";
  sel.title = "이 칸의 분봉 표시. 테두리는 캔들 속을 비운다. 없음이면 지표만 남는다";
  sel.onchange = () => setBarStyle(pane, sel.value);
  pane.barSelect = sel;
  label.append(sel);
  const styles = document.createElement("div");
  styles.className = "bar-styles";
  styles.append(label);
  for (const code of pane.frame?.overlays || []) {
    const row = document.createElement("label");
    row.className = "bar-style";
    const name = document.createElement("span");
    name.textContent = code;
    const overlayName = pane.frame.overlayNames?.[code] || "";
    name.title = overlayName ? `${overlayName}. 이 칸의 봉` : "이 칸의 봉";
    const color = pane.frame.overlayColors?.[code];
    if (color) name.style.color = color;
    const ov = document.createElement("select");
    for (const item of BAR_STYLES) {
      const opt = document.createElement("option");
      opt.value = item.id;
      opt.textContent = item.name;
      ov.append(opt);
    }
    ov.value = overlayStyle(pane, code);
    ov.title = "이 칸에서 이 종목의 봉. 없음이면 이 종목 봉을 숨긴다";
    ov.onchange = () => setOverlayStyle(pane, code, ov.value);
    row.append(name, ov);
    styles.append(row);
  }
  box.append(styles);
  return box;
}

// 원본 폴더 하나. 접힘 상태는 pane.treeFold에 칸 UI 로컬로 둔다.
function buildSourceDir(pane, dir) {
  const node = document.createElement("div");
  node.className = "ind-cat";
  const folded = !!pane.treeFold[dir.id];
  const head = document.createElement("div");
  head.className = "ind-cat-head";
  head.textContent = `${folded ? "▸" : "▾"} ${dir.name}`;
  head.title = folded ? "펼치기" : "접기";
  head.onclick = () => {
    pane.treeFold[dir.id] = !folded;
    buildPaneTools(pane);
  };
  node.append(head);
  if (!folded) {
    const body = document.createElement("div");
    body.className = "ind-cat-body";
    for (const item of dir.items) body.append(buildSourceItem(pane, item));
    node.append(body);
  }
  return node;
}

function sourceTitle(item) {
  const path = `${item.dir}/${item.label}`;
  return item.port?.note ? `${path}. ${item.port.note}` : path;
}

function buildSourceItem(pane, item) {
  if (item.port?.kind === "system") return buildSystemNode(pane, item);
  if (item.port?.kind === "indicator" && item.port.id in RENDERERS) {
    const meta = indicatorManifest.find((m) => m.id === item.port.id) || { id: item.port.id, layers: [] };
    return buildIndNode(pane, meta, item);
  }
  const row = document.createElement("div");
  row.className = "ind-row src-plain";
  row.textContent = item.label;
  row.title = `${sourceTitle(item)}. 차트에 따로 연결되지 않은 원본입니다`;
  return row;
}

function buildSystemNode(pane, item) {
  const node = document.createElement("div");
  node.className = "ind-item";
  const row = document.createElement("div");
  row.className = "ind-row";
  const label = document.createElement("label");
  const box = document.createElement("input");
  box.type = "checkbox";
  box.checked = pane.systems.has(item.port.id);
  box.onchange = () => {
    if (box.checked) pane.systems.add(item.port.id);
    else pane.systems.delete(item.port.id);
    syncPairLegs(pane.frame);
    buildPaneTools(pane);
  };
  const name = document.createElement("span");
  name.className = "src-name";
  name.textContent = item.label;
  name.title = `${sourceTitle(item)}. 종목 순서: 지수, 콜, 풋`;
  label.append(box, name);
  const gear = document.createElement("button");
  gear.type = "button";
  gear.className = "src-gear";
  gear.title = "변수 설정";
  gear.setAttribute("aria-label", "변수 설정");
  gear.innerHTML = GEAR_SVG;
  gear.onclick = (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    openSystemVars(pane, item);
  };
  row.append(label, gear);
  node.append(row);
  return node;
}

const GEAR_SVG = `<svg viewBox="0 0 24 24" width="14" height="14" aria-hidden="true"><path fill="currentColor" d="M19.14 12.94c.04-.31.06-.63.06-.94s-.02-.63-.06-.94l2.03-1.58a.5.5 0 0 0 .12-.64l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.2 7.2 0 0 0-1.63-.94l-.36-2.54a.5.5 0 0 0-.5-.42h-3.84a.5.5 0 0 0-.5.42l-.36 2.54c-.59.22-1.13.54-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.71 8.84a.5.5 0 0 0 .12.64l2.03 1.58c-.04.31-.06.63-.06.94s.02.63.06.94l-2.03 1.58a.5.5 0 0 0-.12.64l1.92 3.32c.13.22.39.31.6.22l2.39-.96c.5.4 1.04.72 1.63.94l.36 2.54c.05.24.26.42.5.42h3.84c.24 0 .45-.18.5-.42l.36-2.54c.59-.22 1.13-.54 1.63-.94l2.39.96c.22.09.47 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.64l-2.03-1.58zM12 15.5A3.5 3.5 0 1 1 12 8.5a3.5 3.5 0 0 1 0 7z"/></svg>`;

function systemVarValues(pane, id) {
  const saved = pane.systemVars?.[id] || {};
  const out = {};
  for (const [name, fallback] of YlTree.inputsFor(id)) {
    const n = Number(saved[name]);
    out[name] = Number.isFinite(n) ? n : fallback;
  }
  return out;
}

function openSystemVars(pane, item) {
  const id = item.port.id;
  const fields = YlTree.inputsFor(id);
  document.getElementById("sysvar-back")?.remove();
  const back = document.createElement("div");
  back.id = "sysvar-back";
  back.className = "sysvar-back";
  const box = document.createElement("div");
  box.className = "sysvar-box";
  box.setAttribute("role", "dialog");
  box.setAttribute("aria-label", "시스템 변수 설정");
  const title = document.createElement("div");
  title.className = "sysvar-title";
  title.textContent = `시스템 트레이딩 설정 - ${item.label}`;
  const note = document.createElement("div");
  note.className = "sysvar-note";
  note.textContent = "시스템의 변수를 설정합니다";
  const table = document.createElement("div");
  table.className = "sysvar-table";
  const basisLine = document.createElement("label");
  basisLine.className = "sysvar-line";
  const basisName = document.createElement("span");
  basisName.textContent = "기준";
  const basis = document.createElement("select");
  basis.dataset.name = "기준";
  for (const opt of [["C", "콜"], ["P", "풋"]]) {
    const o = document.createElement("option");
    o.value = opt[0];
    o.textContent = opt[1];
    basis.append(o);
  }
  basis.value = pane.systemBasis?.[id] === "P" ? "P" : "C";
  basisLine.append(basisName, basis);
  const head = document.createElement("div");
  head.className = "sysvar-head";
  head.innerHTML = "<span>변수이름</span><span>변수값</span>";
  table.append(basisLine, head);
  const values = systemVarValues(pane, id);
  const inputs = [];
  for (const [name] of fields) {
    const line = document.createElement("label");
    line.className = "sysvar-line";
    const lab = document.createElement("span");
    lab.textContent = name;
    const input = document.createElement("input");
    input.type = "number";
    input.step = "any";
    input.value = String(values[name]);
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
  back.addEventListener("mousedown", (ev) => {
    if (ev.target === back) close();
  });
  ok.onclick = () => {
    const next = {};
    for (const input of inputs) {
      const n = Number(input.value);
      if (!Number.isFinite(n)) {
        input.focus();
        return;
      }
      next[input.dataset.name] = n;
    }
    if (!pane.systemVars) pane.systemVars = {};
    if (!pane.systemBasis) pane.systemBasis = {};
    pane.systemVars[id] = next;
    pane.systemBasis[id] = basis.value === "P" ? "P" : "C";
    close();
    refreshWeeklyPlots(pane);
    if (el.wsName.value.trim()) saveWorkspace({ quiet: true });
  };
  document.body.append(back);
  back.tabIndex = -1;
  back.onkeydown = (ev) => {
    if (ev.key === "Escape") close();
  };
  inputs[0]?.focus();
  inputs[0]?.select();
}

// 트리 지표 노드: 지표 체크박스 + (켜져 있으면) 그 지표의 레이어 체크박스들.
// 지표 체크는 activateIndicator/deactivateIndicator를 그대로 부르고 트리를 재구성한다
// (칩 시절과 같은 경로). 레이어 체크는 handle.setLayers만 반영한다 — 체크박스 자체가
// 상태 표시라 트리 재구성은 필요 없다 (칩 시절 classList.toggle과 같은 계약).
function buildIndNode(pane, meta, source) {
  const node = document.createElement("div");
  node.className = "ind-item";
  const row = document.createElement("label");
  row.className = "ind-row";
  const box = document.createElement("input");
  box.type = "checkbox";
  box.checked = pane.active.has(meta.id);
  box.onchange = () => {
    if (box.checked) activateIndicator(pane, meta.id);
    else deactivateIndicator(pane, meta.id);
    if (WPLOT_IDS.includes(meta.id)) syncPairLegs(pane.frame);
    buildPaneTools(pane);
    updateBadgeVisibility();
  };
  const name = document.createElement("span");
  name.className = "src-name";
  name.textContent = source?.label || meta.name || meta.id;
  if (source) name.title = sourceTitle(source);
  row.append(box, name);
  node.append(row);

  const entry = pane.active.get(meta.id);
  if (entry) {
    const layersBox = document.createElement("div");
    layersBox.className = "ind-layers";
    for (const layer of meta?.layers ?? []) {
      const lrow = document.createElement("label");
      lrow.className = "ind-row layer";
      const lbox = document.createElement("input");
      lbox.type = "checkbox";
      lbox.checked = entry.layers[layer.id] !== false;
      lbox.onchange = () => {
        entry.layers[layer.id] = lbox.checked;
        entry.handle.setLayers({ [layer.id]: lbox.checked });
      };
      lrow.append(lbox, document.createTextNode(layer.name ?? layer.id));
      layersBox.append(lrow);
    }
    node.append(layersBox);
  }
  return node;
}

// 지표 렌더러를 칸에 활성화하고 종목 캐시로 백필한다.
// savedLayers(화면틀)가 있으면 그 값을, 없으면 매니페스트 defaultOn을 적용한다.
// 종목 미선택(화면틀 복원의 watch 대기 등)이면 백필은 시딩 완료 시 렌더가 대신한다.
function activateIndicator(pane, indId, savedLayers) {
  if (pane.active.has(indId)) return;
  const renderer = RENDERERS[indId];
  if (!renderer) return;
  const handle = renderer.createHandle(pane.chart, pane.candleSeries);
  if (pane.barStyle === "none" && typeof handle.setAxis === "function") handle.setAxis("right");
  const meta = indicatorManifest.find((m) => m.id === indId);
  const layers = {};
  for (const l of meta?.layers ?? []) layers[l.id] = savedLayers?.[l.id] ?? (l.defaultOn !== false);
  for (const [k, v] of Object.entries(savedLayers ?? {})) layers[k] = !!v; // 매니페스트에 없는 저장 키도 보존
  pane.active.set(indId, { renderer, handle, layers });
  handle.setLayers(layers);
  if (indId === "fx_data2") {
    syncFrameData2(pane.frame);
  } else if (indId === "ks_data2") {
    const fut = WeeklyLegs.futForData2(pane.linkLegs);
    if (fut) handle.setSource(feed.get(fut));
  } else {
    const cache = pane.symbol ? feed.get(pane.symbol) : undefined;
    if (cache) handle.applySeed(ctxFor(cache));
  }
  if (indId === "fx_curve_os" && pane.symbol) {
    const cache = feed.get(pane.symbol);
    if (cache) rebuildPaneCandles(pane, cache);
  }
  if (WPLOT_IDS.includes(indId)) refreshWeeklyPlots(pane);
}

function deactivateIndicator(pane, indId) {
  const entry = pane.active.get(indId);
  if (!entry) return;
  pane.active.delete(indId);
  if (typeof entry.handle.destroy === "function") entry.handle.destroy();
  else entry.handle.clear();
  if (indId === "fx_curve_os" && pane.symbol) {
    const cache = feed.get(pane.symbol);
    if (cache) rebuildPaneCandles(pane, cache);
  }
  if (WPLOT_IDS.includes(indId)) refreshWeeklyPlots(pane);
}

// 헤더 배지의 출처: mirae_v16이 켜진 첫 칸이 보는 캐시
function firstMiraePane() {
  return panes.find((pane) => pane.active.has("mirae_v16"));
}

const el = {
  wsState: document.getElementById("ws-state"),
  mode: document.getElementById("mode"),
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
    clearOverlayLines(sh);
    // 세대 상승은 엔진 쪽 재구성 신호다 (종목 전환·RT 캐치업 병합). 비운 뒤 스냅샷을
    // 다시 가져와야 병합된 과거 봉(캐치업)까지 화면에 반영된다 — 라이브만으로는 구멍이 남는다.
    // 겹침 종목도 같은 캐시를 쓰므로 같이 다시 받는다.
    if (symbolHeld(sh)) seedSymbol(sh);
  }
  const t = Number(p.bar_open_time) / 1e6;
  if (!Number.isFinite(t) || t <= 0) return;

  const [o, h, l, c] = p.ohlc ?? [];
  // noteBar 코드: 0 기존 봉 갱신 / 1 꼬리 추가 / 2 중간 삽입(늦은 정정·구멍 채움),
  // -1은 이 틱에 봉이 없음(ohlc 미포함). 꼬리 판정은 캐시 기준이라 봉 없는 틱에서도 성립한다.
  // prevTail은 noteBar가 seriesTimes를 밀기 전의 마지막 시리즈 항목(봉 또는 whitespace)
  // 시각 — 꼬리 추가 시 사이에 넣을 균일 분 그리드 whitespace 목록을 만드는 기준이다.
  const prevTail = cache.seriesTimes[cache.seriesTimes.length - 1];
  const barCode = o != null ? feed.noteBar(cache, t, { time: t, open: o, high: h, low: l, close: c }) : -1;
  const trimmed = feed.trimTo(cache, barCap);
  // 균일 분 그리드(1칸=1분): 꼬리 추가(code 1)로 직전 항목과 1분 초과 공백이 생기면
  // 사이 매분의 whitespace 행 — noteBar가 seriesTimes에 넣은 것과 같은 목록이다.
  // 칸 반영 시 새 봉 앞에 같은 순서로 update()한다 (아래 isTail 분기 주석 참조).
  const liveWs = [];
  if (barCode === 1 && prevTail !== undefined) {
    for (let wt = prevTail + 60; wt < t; wt += 60) liveWs.push({ time: wt });
  }
  const isTail = cache.barSeq[cache.barSeq.length - 1] === t;
  if (barCode >= 0) {
    // 시딩 중인 종목의 칸에는 라이브를 그리지 않는다 — 봉은 캐시에 쌓이고 시딩 끝의
    // renderSymbolPanes가 통째로 그린다. 비운 차트에 1봉만 그리면 범위가 [0,1]로
    // 찌그러지고 그 이벤트가 동기화를 타고 다른 칸을 데이터 맨 앞으로 점프시킨다.
    const seeding = seedInflight.has(sh);
    for (const pane of panes) {
      if (pane.symbol !== sh || seeding) continue;
      // 칸별 격리: 한 칸의 렌더 실패가 다른 칸·지표 적용을 중단시키지 않게 try/catch로
      // 감싸고 콘솔에 남긴다 (과거에는 update() throw가 onmessage에서 조용히 삼켜져
      // 캐시와 차트가 영구 발산했다 — 2026-09-30 실측).
      try {
        if (trimmed) {
          rebuildPaneCandles(pane, cache);
        } else if (isTail) {
          // 균일 분 그리드 유지: 꼬리 봉이 직전 시리즈 항목과 1분 초과로 떨어져 오면
          // (무틱 분·세션 경계·주말) 사이 매분 whitespace를 먼저 붙이고 새 봉을 붙인다.
          // 무틱 분은 "아직 안 온 것"이 아니라 다음 봉이 왔을 때 확정되므로 이 시점에
          // 채운다. 엔진이 무거래 봉을 스스로 채우면(no_trade FILL) 간격이 정확히
          // 60초라 liveWs가 비어 자연히 멱등이다 (현재 엔진은 SKIP — src/app/main.c).
          // 꼬리 갱신(code 0)은 범위 이벤트가 없고(실측) 꼬리 추가(code 1, whitespace
          // 포함)만 범위가 밀리므로, 추가일 때만 이 칸을 뮤트한다 — 틱마다 뮤트 창이
          // 열리며 사용자의 줌/스크롤 이벤트를 삼키는 일을 피한다.
          if (barCode === 1) {
            mutePaneRange(pane);
            for (const w of liveWs) pane.candleSeries.update(pricePoint(w, pane.barDraw));
          }
          pane.candleSeries.update(candlePoint(pane, cache.bars.get(t)));
        } else {
          // 늦은 정정/구멍 채움(과거 시각): candleSeries.update()는 시리즈 마지막보다
          // 과거 시각에 throw("Cannot update oldest data")하므로 캐시에서 다시 깐다.
          // 재구성의 withWhitespace가 균일 분 그리드로 다시 채우므로 채운 분의
          // whitespace는 저절로 캔들로 교체된다 — 엔진 gaps 구간 수술은 폐기했다
          // (표시 경로가 엔진 gaps를 더 쓰지 않는다, gaps.js 헤더 참조).
          rebuildPaneCandles(pane, cache);
        }
      } catch (err) {
        console.error(`[${sh}] 캔들 반영 실패 t=${t}`, err);
      }
    }
    if (!seeding) paintOverlayLive(sh, cache, { trimmed, barCode, t, isTail });
  }
  const ind = MiraeLayers.barIndFromPayload(p);
  ind.fx3 = Fx3Layers.parseFx3(p.fx3);
  ind.pgap = PgapLayers.parsePgap(p.pgap);
  ind.rgap = PgapLayers.parseSide(p.rgap);
  ind.mgap = PgapLayers.parseSide(p.mgap);
  ind.ymae = YmaeLayers.parseYmae(p.ymae);
  ind.sniper = SniperLayers.parseSniper(p.sniper);
  ind.os = OsLayers.parse(p.os);
  ind.cu = CuLayers.parse(p.cu);
  ind.snco = SncoLayers.parse(p.snco);
  ind.pvc = PvcLayers.parsePvc(p.pvc);
  ind.score = Number.isFinite(p.score) ? p.score : NaN;
  ind.pred = Array.isArray(p.pred) ? p.pred : undefined;
  ind.resid = p.resid ?? 0;
  ind.pvol = p.pvol ?? 0;
  cache.barInd.set(t, ind);
  if (!seedInflight.has(sh)) {
    const painted = cache.bars.get(t);
    if (painted) {
      for (const pane of panes) {
        if (pane.symbol !== sh || !pane.active.has("fx_curve_os") || !pane.candleSeries) continue;
        try { pane.candleSeries.update(candlePoint(pane, painted)); }
        catch { /* 과거 시각은 아래 재구성이 맡는다 */ }
      }
    }
  }
  if (typeof p.tick === "number" && Number.isFinite(p.tick) && p.tick > 0) cache.tickRaw = p.tick;

  // ⑥⑦ 봉별 아이템은 렌더러 on/off와 무관하게 캐시에 기록한다 (복원 대비)
  const pos = cache.barPos.get(t);
  const memItem = MiraeLayers.memItemFromPayload(t, p.mem, feed.recentBars(cache, pos, 5));
  if (memItem !== undefined) ind.memItem = memItem;
  const pstItem = MiraeLayers.pstItemFromPayload(t, p.pst, feed.recentBars(cache, pos, 5));
  if (pstItem !== undefined) ind.pstItem = pstItem;

  // 지표 표시는 이 종목을 보는 칸의 활성 렌더러만 담당한다 (시딩 중인 칸은 건너뛴다 —
  // 비운 차트에 라이브 지표를 그리는 것도 같은 클래스의 오염이다)
  const ctx = ctxFor(cache);
  const seedingNow = seedInflight.has(sh);
  for (const pane of panes) {
    if (pane.symbol !== sh || seedingNow) continue;
    for (const [indId, { handle }] of pane.active) {
      if (indId === "fx_data2") continue;
      if (indId === "ks_data2") continue;
      // 지표 시리즈도 칸별로 격리한다 — 한 지표의 실패가 다른 지표·칸으로 번지지 않게.
      try {
        // 정정(과거 시각) 틱에는 지표 시리즈의 update()도 같은 throw가 나므로
        // (mirae-layers reg/score/band/mktband, sma-layers — 2026-09-30 실측),
        // 캐시에서 전체를 다시 그리는 시딩 경로로 처리한다. 꼬리는 기존처럼 라이브 반영.
        if (trimmed || !isTail) handle.applySeed(ctx);
        else handle.applyLive(p, ctx);
      } catch (err) {
        console.error(`[${sh}] 지표(${indId}) 반영 실패 t=${t}`, err);
      }
    }
  }
  for (const pane of panes) {
    if (pane.data2 !== sh || pane.symbol === sh || seedingNow) continue;
    const entry = pane.active.get("fx_data2");
    if (!entry) continue;
    try {
      if (trimmed || !isTail) entry.handle.applySeed(ctx);
      else entry.handle.applyLive(p, ctx);
    } catch (err) {
      console.error(`[${sh}] Data2 반영 실패 t=${t}`, err);
    }
  }
  for (const pane of panes) {
    if (seedingNow) continue;
    if (WeeklyLegs.futForData2(pane.linkLegs) !== sh) continue;
    const entry = pane.active.get("ks_data2");
    if (!entry) continue;
    try {
      if (trimmed || !isTail) entry.handle.applySeed(ctx);
      else entry.handle.applyLive(p, ctx);
    } catch (err) {
      console.error(`[${sh}] 국내선물 Data2 반영 실패 t=${t}`, err);
    }
  }

  // 헤더 배지는 최신(꼬리) 봉의 값으로만 갱신한다 — 과거 봉 정정 틱으로 덮어쓰지 않는다
  if (!isTail) return;

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

// 시딩 직후 초기 표시 범위(봉 수). 원본 미래곡선 차트는 하루 전체(396봉)를 한 화면에
// 보여주므로 그에 맞춘다. scrollToRealTime은 현재 봉 간격을 유지한 채 최신으로 갈 뿐이라
// 첫 화면이 과도하게 확대되어 보였다 — 명식 범위 지정으로 봉 간격을 이 폭에 맞춘다.
const INITIAL_VISIBLE_BARS = 380;

// 차트가 다루는 1분봉 상한. 엔진 저장 상한(BB_STORE_MAX)과 같은 범위다.
const BAR_CAP_MIN = 16;
const BAR_CAP_MAX = 8192;
const BAR_CAP_DEFAULT = 2560;
const BAR_CAP_KEY = "nexus.barCap";

function readBarCap() {
  const raw = localStorage.getItem(BAR_CAP_KEY);
  if (raw == null || raw === "") return BAR_CAP_DEFAULT;
  const n = Number(raw);
  if (!Number.isInteger(n)) return BAR_CAP_DEFAULT;
  return Math.min(BAR_CAP_MAX, Math.max(BAR_CAP_MIN, n));
}

let barCap = readBarCap();

function clampBarCap(n) {
  if (!Number.isInteger(n)) return null;
  return Math.min(BAR_CAP_MAX, Math.max(BAR_CAP_MIN, n));
}

// 늦은 정정·구멍 채움(과거 시각 봉)의 칸 반영: lightweight-charts update()는 시리즈
// 마지막보다 과거 시각에 throw하므로("Cannot update oldest data", 2026-09-30 실측),
// 이 칸의 캔들 시리즈를 캐시에서 통째로 다시 깐다. 재구성으로 봉/whitespace 수가 바뀌어
// 논리 인덱스가 어긋나므로, 보이는 창은 먼저 시각(getVisibleRange)으로 받아 두고 새
// 시리즈에서 PaneSync.logicalAt으로 같은 시각의 인덱스를 구해 복원한다 — 정수 스냅이
// 아니라 소수 인덱스 그대로라 사용자가 보던 시계 창이 정확히 유지된다.
// pane-sync 기준값(wsCount/seriesTimes)도 setData에 넘긴 같은 rows에서 다시 세운다
// (renderSymbolPanes와 같은 계약). 이벤트는 이 칸을 뮤트해 다른 칸으로 번지지 않게 한다 —
// renderSymbolPanes와 달리 스크롤 애니메이션이 없어 프레임 뮤트(mutePaneRange)로 충분하다.
function rebuildPaneCandles(pane, cache) {
  const bars = cache.barSeq.map((t) => cache.bars.get(t)).filter(Boolean);
  const rows = Gaps.withWhitespace(bars); // 균일 분 그리드 — renderSymbolPanes와 같은 계약
  const range = pane.chart.timeScale().getVisibleRange(); // 시각 창 (데이터 없으면 null)
  mutePaneRange(pane);
  pane.candleSeries.setData(rows.map((r) => candlePoint(pane, r)));
  applyPairMarkers(pane);
  cache.wsCount = rows.length - bars.length;
  cache.seriesTimes = rows.map((r) => r.time);
  if (range && rows.length) {
    const times = cache.seriesTimes;
    const from = PaneSync.logicalAt(times, Number(range.from));
    const to = PaneSync.logicalAt(times, Number(range.to));
    // 역전(from > to) 같은 비정상 케이스만 가드한다 — 그 외에는 이전 시계 창 그대로
    if (from <= to) pane.chart.timeScale().setVisibleLogicalRange({ from, to });
  }
}

// 시딩이 끝난 종목의 캐시로 그 종목을 보는 모든 칸을 다시 그린다
function renderSymbolPanes(shcode) {
  const cache = feed.get(shcode);
  if (!cache) return;
  const bars = cache.barSeq.map((t) => cache.bars.get(t)).filter(Boolean);
  // 균일 분 그리드(1칸=1분): 연속 봉 사이의 1분 초과 공백을 전부 분당 1칸
  // whitespace({time}만 있는 항목)로 펼쳐 캔들 사이에 섞는다 (gaps.js) — 밤·주말·
  // 무틱 공백을 가리지 않으므로 모든 칸이 같은 분 그리드에 오고, 창 가장자리가 같은
  // 시계 창이면 칸마다 같은 시각의 x가 픽셀 단위로 일치한다 (엄밀 시각 정렬의 기반).
  // 엔진 gaps 페이로드는 이 채움의 부분집합이라 표시 경로에서는 쓰지 않는다 — 종목별
  // 구멍 채움 정책 차이로 1칸의 시간이 구간마다 달라 칸 간 x가 어긋났기 때문이다
  // (gaps.js 헤더의 2026-10-01 실측 참조). 라이브 꼬리는 noteBar/applyStatus가 같은
  // 규칙으로 채운다 (applyStatus 주석 참조).
  const rows = Gaps.withWhitespace(bars);
  // 보이는 창은 setData 전에, 그 칸이 속한 화면틀에서만 읽는다. seriesTimes를 바꾼 뒤
  // 논리 인덱스를 읽으면 이전 창과 어긋나고, 다른 화면틀의 창을 가져오면 시간축이 섞인다.
  const frameViews = new Map();
  for (const pane of panes) {
    if (pane.symbol !== shcode || !pane.frame || frameViews.has(pane.frame)) continue;
    frameViews.set(pane.frame, captureFrameSeedView(pane.frame));
  }
  // pane-sync getLength의 시리즈 길이(봉 + whitespace)와 같은 기준 — 반드시 여기서 갱신한다
  cache.wsCount = rows.length - bars.length;
  // pane-sync getTimes의 시각 기준 — 시리즈(봉 + whitespace)의 인덱스와 1:1로 맞닿아야 하므로
  // setData에 넘길 같은 rows에서 만든다. 이후 라이브 꼬리는 noteBar가 함께 민다.
  cache.seriesTimes = rows.map((r) => r.time);
  const ctx = ctxFor(cache);
  const times = cache.seriesTimes;
  for (const pane of panes) {
    if (pane.symbol !== shcode) continue;
    // 시딩 적용(setData + 초기 범위 지정)은 프로그램적 변경 — 그 칸이 자기 최신 범위로
    // 돌아가며 내는 범위 이벤트가 같은 화면틀의 다른 칸 탐색 위치를 빼앗지 않게 뮤트한다.
    mutePaneRangeForSeeding(pane);
    try {
      pane.candleSeries.setData(rows.map((r) => candlePoint(pane, r)));
    } catch (err) {
      console.error(`[${shcode}] 캔들 시딩 실패`, err);
      continue;
    }
    // 겹침선은 이 칸 메인 분 그리드에만 올린다. 보이는 창을 지정하기 전에 깔아야
    // 선이 되돌린 이전 창이 시딩 창을 덮지 않는다.
    // 겹침 시리즈를 다시 만든 뒤에 주문 글자를 붙여야 화살표만 남지 않는다.
    for (const code of pane.frame?.overlays ?? []) {
      const overlayCache = feed.get(code);
      if (!overlayCache) continue;
      ensureOverlaySeries(pane, code);
      setOverlayData(pane, code, overlayCache);
    }
    applyPairMarkers(pane);
    const width = pane.chartEl.clientWidth;
    const view = pane.frame ? frameViews.get(pane.frame) : null;
    if (view && view.spacingPx > 0 && times.length && width > 0) {
      const toM = PaneSync.logicalAt(times, view.rightTime);
      const fromM = toM - (width / view.spacingPx - 1);
      if (Number.isFinite(fromM) && Number.isFinite(toM) && fromM <= toM) {
        pane.chart.timeScale().setVisibleLogicalRange({ from: fromM, to: toM });
      }
    } else if (rows.length) {
      const from = Math.max(0, rows.length - INITIAL_VISIBLE_BARS);
      const to = rows.length - 1;
      if (from <= to) pane.chart.timeScale().setVisibleLogicalRange({ from, to });
    }
    applyPaneSeed(pane, ctx);
  }
  refreshData2(shcode);
  refreshKsData2ForSymbol(shcode);
  paintOverlaySymbol(shcode);
  restoreHeaderBadges();
  refreshWeeklyForSymbol(shcode);
}

// 이 칸의 지표를 종목 캐시로 채운다. fx_data2는 참조 종목 캐시를 쓰므로
// 여기 넣지 않는다 — 넣으면 메인 시딩이 참조 그림을 덮어 지운다.
// 한 지표가 실패해도 같은 칸의 나머지와 다른 칸은 계속 그린다.
function applyPaneSeed(pane, ctx) {
  for (const [indId, { handle }] of pane.active) {
    if (indId === "fx_data2") continue;
    if (indId === "ks_data2") continue;
    try {
      handle.applySeed(ctx);
    } catch (err) {
      console.error(`[${pane.symbol}] 지표(${indId}) 시딩 실패`, err);
    }
  }
}

// 참조 종목 시딩이 끝나면, 그 종목을 Data2로 보는 칸의 비율선을 다시 그린다.
// 겹친 종목은 상승·하락이 둘 다 선명한 쌍이다.
// 넣은 순서: 연두/짙은초록, 빨강/노랑, 하늘/남색.
const SYMBOL_PAIRS = [
  { up: "#d4ff4a", down: "#0e7a32" },
  { up: "#df0202", down: "#ffcc00" },
  { up: "#00ccff", down: "#0000ce" },
  { up: "#ff6d00", down: "#ffab40" },
  { up: "#e040fb", down: "#7c4dff" },
  { up: "#ff4081", down: "#f48fb1" },
  { up: "#c6ff00", down: "#aeea00" },
  { up: "#18ffff", down: "#00b8d4" },
];

function symbolDown(color) {
  const pair = SYMBOL_PAIRS.find((p) => p.up === color);
  return pair ? pair.down : color;
}

function symbolPaint(color, hollow) {
  const down = symbolDown(color);
  return {
    upColor: hollow ? "rgba(0,0,0,0)" : color,
    downColor: hollow ? "rgba(0,0,0,0)" : down,
    borderVisible: true,
    borderUpColor: color,
    borderDownColor: down,
    wickUpColor: color,
    wickDownColor: down,
  };
}

function takeOverlayColor(frame) {
  const used = new Set(Object.values(frame.overlayColors || {}));
  if (frame.mainColor) used.add(frame.mainColor);
  const ups = SYMBOL_PAIRS.map((p) => p.up);
  return ups.find((c) => !used.has(c)) || ups[used.size % ups.length];
}

function ensureMainColor(frame) {
  if (!frame?.overlays?.length) {
    if (frame) frame.mainColor = "";
    return "";
  }
  if (!frame.mainColor) frame.mainColor = takeOverlayColor(frame);
  return frame.mainColor;
}

// 겹침이 있으면 메인 봉도 종목색이다. 없으면 원래 빨강/파랑으로 되돌린다.
function applyMainBarColors(pane) {
  const series = pane?.candleSeries;
  if (!series) return;
  const color = pane.frame?.overlays?.length ? ensureMainColor(pane.frame) : "";
  const hollow = pane.barStyle === "outline";
  const draw = pane.barDraw || "candle";
  if (!color) {
    if (draw === "bar") series.applyOptions({ upColor: CANDLE_UP, downColor: CANDLE_DN });
    else if (draw === "line") series.applyOptions({ color: "#d1d4dc" });
    else series.applyOptions(candleColors(hollow ? "outline" : "fill"));
    return;
  }
  if (draw === "line") series.applyOptions({ color });
  else if (draw === "bar") series.applyOptions({ upColor: color, downColor: symbolDown(color) });
  else series.applyOptions(symbolPaint(color, hollow));
}

function syncMainBarColors(frame) {
  if (!frame) return;
  ensureMainColor(frame);
  for (const pane of frame.panes) applyMainBarColors(pane);
}

function overlayStyle(pane, code) {
  const style = pane?.overlayStyles?.[code] ?? pane?.frame?.overlayStyles?.[code];
  return BAR_STYLES.some((s) => s.id === style) ? style : "candle";
}

function overlayScaleMode(frame) {
  const mode = frame?.overlayScale;
  if (mode === "price" || mode === "ratio" || mode === "shared") return mode;
  return "shared";
}

// 각자 가격: 첫 추가 종목은 왼쪽 눈금, 그 다음 종목은 자기 가격 범위(눈금 칸은 좌우뿐).
// 100 비율은 왼쪽 공통 눈금. 같은 눈금은 메인과 같은 오른쪽 가격 눈금.
function overlayPriceScaleId(frame, shcode) {
  const mode = overlayScaleMode(frame);
  if (mode === "shared") return "right";
  if (mode === "ratio") return "left";
  return frame?.overlays?.[0] === shcode ? "left" : `ov:${shcode}`;
}

// 차트 가격 눈금은 원값을 100으로 나눠 보여 준다. 100 비율만 비율에 100을 곱해 100으로 보이게 한다.
// gridTimes가 있으면 그 분 그리드에만 찍는다. 겹침 종목만 가진 시각을 시간축에 넣으면
// 논리 인덱스가 메인 봉과 어긋나 화면틀 안의 스크롤이 밀린다.
function overlaySeriesRows(pane, shcode, cache) {
  if (!cache) return [];
  const bars = cache.barSeq.map((t) => cache.bars.get(t)).filter(Boolean);
  const line = overlayStyle(pane, shcode) === "line";
  const ratio = overlayScaleMode(pane.frame) === "ratio";
  const rows = ratio
    ? OverlayRatio.ohlc(bars).map((p) => ({
        time: p.time,
        open: p.open * 100,
        high: p.high * 100,
        low: p.low * 100,
        close: p.close * 100,
      }))
    : bars.flatMap((bar) => {
        const time = Number(bar.time);
        const close = Number(bar.close);
        if (!Number.isFinite(time) || !Number.isFinite(close)) return [];
        const num = (v) => {
          const n = Number(v);
          return Number.isFinite(n) ? n : close;
        };
        return [{ time, open: num(bar.open), high: num(bar.high), low: num(bar.low), close }];
      });
  const gridTimes = overlayGridTimes(pane);
  const times = gridTimes?.length ? gridTimes : rows.map((p) => p.time);
  const byTime = new Map(rows.map((p) => [p.time, p]));
  return times.map((time) => {
    const p = byTime.get(time);
    if (!p) return { time };
    if (line) return { time, value: p.close };
    return { time, open: p.open, high: p.high, low: p.low, close: p.close };
  });
}

function overlayGridTimes(pane) {
  const symbol = pane.symbol || frameSymbolPane(pane.frame)?.symbol || "";
  const times = feed.get(symbol)?.seriesTimes;
  return Array.isArray(times) && times.length ? times : null;
}

function syncOverlayScale(pane) {
  const on = (pane.overlaySeries?.size ?? 0) > 0 && overlayScaleMode(pane.frame) !== "shared";
  try {
    pane.chart.priceScale("left").applyOptions({ visible: on, borderVisible: false });
  } catch { /* 왼쪽 눈금을 쓰기 전이면 넘긴다 */ }
}

function ensureOverlaySeries(pane, shcode) {
  if (!pane.overlaySeries) pane.overlaySeries = new Map();
  if (!pane.overlayDrawn) pane.overlayDrawn = new Map();
  const frame = pane.frame;
  const style = overlayStyle(pane, shcode);
  if (style === "none") {
    dropOverlaySeries(pane, shcode);
    return null;
  }
  const scaleId = overlayPriceScaleId(frame, shcode);
  const drawnKey = `${style}|${overlayScaleMode(frame)}|${scaleId}`;
  const existing = pane.overlaySeries.get(shcode);
  if (existing && pane.overlayDrawn.get(shcode) === drawnKey) return existing;
  if (existing) dropOverlaySeries(pane, shcode);
  if (!frame.overlayColors[shcode]) frame.overlayColors[shcode] = takeOverlayColor(frame);
  const color = frame.overlayColors[shcode];
  const ownAxis = scaleId !== "left" && scaleId !== "right";
  const common = {
    priceScaleId: scaleId,
    priceLineVisible: ownAxis,
    lastValueVisible: true,
    title: shcode,
  };
  const hollow = style === "outline";
  const down = symbolDown(color);
  const series = style === "line"
    ? pane.chart.addLineSeries({ ...common, color, lineWidth: 2 })
    : style === "bar"
      ? pane.chart.addBarSeries({ ...common, upColor: color, downColor: down, thinBars: false })
      : pane.chart.addCandlestickSeries({ ...common, ...symbolPaint(color, hollow) });
  attachGapShade(series);
  pane.overlaySeries.set(shcode, series);
  pane.overlayDrawn.set(shcode, drawnKey);
  syncOverlayScale(pane);
  return series;
}

function dropOverlaySeries(pane, shcode) {
  const series = pane.overlaySeries?.get(shcode);
  if (!series) return;
  pane.overlaySeries.delete(shcode);
  pane.overlayDrawn?.delete(shcode);
  try { pane.chart.removeSeries(series); } catch { /* 차트가 이미 닫힌 경우 */ }
  syncOverlayScale(pane);
}

function setOverlayData(pane, shcode, cache) {
  const series = pane.overlaySeries?.get(shcode);
  if (!series) return;
  const lr = pane.chart.timeScale().getVisibleLogicalRange();
  mutePaneRange(pane);
  try {
    series.setData(overlaySeriesRows(pane, shcode, cache));
  } catch (err) {
    console.error(`[${shcode}] 겹침 분봉 반영 실패`, err);
    return;
  }
  if (lr && Number.isFinite(lr.from) && Number.isFinite(lr.to) && lr.from <= lr.to) {
    pane.chart.timeScale().setVisibleLogicalRange(lr);
  }
}

function mountFrameOverlays(pane) {
  const frame = pane.frame;
  if (!frame) return;
  for (const sh of frame.overlays) {
    ensureOverlaySeries(pane, sh);
    const cache = feed.get(sh);
    if (cache) setOverlayData(pane, sh, cache);
  }
  applyMainBarColors(pane);
  if (pane.pairEvents?.length) applyPairMarkers(pane);
}

function paintOverlaySymbol(shcode) {
  const cache = feed.get(shcode);
  if (!cache) return;
  for (const pane of panes) {
    if (!pane.frame?.overlays?.includes(shcode)) continue;
    ensureOverlaySeries(pane, shcode);
    setOverlayData(pane, shcode, cache);
    if (pane.pairEvents?.length) applyPairMarkers(pane);
  }
}

function clearOverlayLines(shcode) {
  for (const pane of panes) {
    const series = pane.overlaySeries?.get(shcode);
    if (!series) continue;
    try { series.setData([]); } catch { /* 닫힌 차트 */ }
  }
}

function paintOverlayLive(shcode, cache, info) {
  const targets = panes.filter((pane) => pane.frame?.overlays?.includes(shcode) && pane.overlaySeries?.has(shcode));
  if (!targets.length) return;
  for (const pane of targets) {
    const series = pane.overlaySeries.get(shcode);
    const points = overlaySeriesRows(pane, shcode, cache);
    const last = points[points.length - 1];
    const hasBar = last && (last.value != null || last.close != null);
    const canUpdate = !info.trimmed && info.isTail && hasBar && last.time === info.t;
    if (!canUpdate) {
      setOverlayData(pane, shcode, cache);
      continue;
    }
    try {
      series.update(last);
    } catch {
      setOverlayData(pane, shcode, cache);
    }
  }
}

function takeFrameOverlays(frame) {
  const codes = [...(frame?.overlays ?? [])];
  if (!frame) return codes;
  frame.overlays = [];
  frame.overlayTargets?.clear();
  frame.overlayNames = {};
  frame.overlayColors = {};
  frame.overlayStyles = {};
  for (const pane of frame.panes || []) pane.overlayStyles = {};
  syncFrameData2(frame);
  return codes;
}

function repaintFrameOverlays(frame) {
  if (!frame) return;
  for (const pane of frame.panes) {
    for (const code of frame.overlays) {
      ensureOverlaySeries(pane, code);
      const cache = feed.get(code);
      if (cache) setOverlayData(pane, code, cache);
    }
    syncOverlayScale(pane);
    if (pane.pairEvents?.length) applyPairMarkers(pane);
  }
}

function repaintPaneOverlays(pane) {
  const frame = pane?.frame;
  if (!frame) return;
  for (const code of frame.overlays) {
    ensureOverlaySeries(pane, code);
    const cache = feed.get(code);
    if (cache) setOverlayData(pane, code, cache);
  }
  syncOverlayScale(pane);
  if (pane.pairEvents?.length) applyPairMarkers(pane);
}

function setOverlayStyle(pane, code, style) {
  const frame = pane?.frame;
  if (!frame?.overlays?.includes(code)) return;
  if (!BAR_STYLES.some((s) => s.id === style)) return;
  if (!pane.overlayStyles) pane.overlayStyles = {};
  if (pane.overlayStyles[code] === style) return;
  pane.overlayStyles[code] = style;
  ensureOverlaySeries(pane, code);
  const cache = feed.get(code);
  if (cache) setOverlayData(pane, code, cache);
  if (pane.pairEvents?.length) applyPairMarkers(pane);
}

function setOverlayScale(frame, mode) {
  if (!frame) return;
  if (mode !== "price" && mode !== "ratio" && mode !== "shared") return;
  if (frame.overlayScale === mode) return;
  frame.overlayScale = mode;
  repaintFrameOverlays(frame);
}

function clearFrameOverlays(frame) {
  if (!frame) return;
  frame.pairOpp = "";
  frame.pairOppName = "";
  frame.pairFut = "";
  frame.pairFutName = "";
  const codes = takeFrameOverlays(frame);
  for (const pane of frame.panes) {
    for (const sh of codes) dropOverlaySeries(pane, sh);
  }
  syncMainBarColors(frame);
  syncPairLegs(frame);
  for (const pane of frame.panes) buildPaneTools(pane);
  for (const sh of codes) releaseSymbol(sh);
}

async function removeFrameOverlay(frame, shcode) {
  const i = frame.overlays.indexOf(shcode);
  if (i < 0) return;
  frame.overlays.splice(i, 1);
  frame.overlayTargets.delete(shcode);
  delete frame.overlayNames[shcode];
  delete frame.overlayColors[shcode];
  delete frame.overlayStyles[shcode];
  forgetPairCode(frame, shcode);
  syncPairLegs(frame);
  syncFrameData2(frame);
  for (const pane of frame.panes) {
    if (pane.overlayStyles) delete pane.overlayStyles[shcode];
    dropOverlaySeries(pane, shcode);
    buildPaneTools(pane);
  }
  repaintFrameOverlays(frame);
  syncMainBarColors(frame);
  syncFrameChips(frame);
  await releaseSymbol(shcode);
}

async function addFrameOverlay(frame, shcode, name) {
  shcode = String(shcode ?? "").trim().toUpperCase();
  if (!frame || !shcode || !allFrames().includes(frame)) {
    finishSeedIfIdle(shcode);
    return;
  }
  const main = frameSymbolPane(frame)?.symbol || "";
  if (shcode === main || frame.overlays.includes(shcode) || frame.overlayTargets.has(shcode)) {
    syncFrameChips(frame);
    finishSeedIfIdle(shcode);
    return;
  }
  frame.overlayTargets.add(shcode);
  let fail = "";
  await withChartLoad(async () => {
    const epoch = loadEpoch;
    const w = await watchSymbol(shcode);
    if (!loadStill(epoch)) {
      finishSeedIfIdle(shcode);
      return;
    }
    if (!allFrames().includes(frame) || !frame.overlayTargets.has(shcode)) {
      frame.overlayTargets.delete(shcode);
      if (!symbolHeld(shcode)) releaseSymbol(shcode);
      finishSeedIfIdle(shcode);
      return;
    }
    if (!w.ok) {
      frame.overlayTargets.delete(shcode);
      if (!symbolHeld(shcode)) releaseSymbol(shcode);
      fail = `종목 관측 실패 (${shcode}): ${w.error}`;
      finishSeedIfIdle(shcode);
      return;
    }
    if (w.name) feed.forSymbol(shcode).name = w.name;
    frame.overlayNames[shcode] = w.name || name || "";
    frame.overlays.push(shcode);
    if (!frame.mainColor) frame.mainColor = takeOverlayColor(frame);
    frame.overlayColors[shcode] = takeOverlayColor(frame);
    frame.overlayTargets.delete(shcode);
    if (!engineWatches.includes(shcode)) engineWatches.push(shcode);
    for (const pane of frame.panes) {
      if (!pane.overlayStyles) pane.overlayStyles = {};
      // 새로 올리는 종목은 캔들바. 칸이나 화면틀에 이미 있는 모양은 그대로 둔다.
      if (!pane.overlayStyles[shcode]) pane.overlayStyles[shcode] = frame.overlayStyles?.[shcode] || "candle";
      ensureOverlaySeries(pane, shcode);
      buildPaneTools(pane);
    }
    syncMainBarColors(frame);
    syncPairLegs(frame);
    syncFrameData2(frame);
    await seedSymbol(shcode);
  });
  if (fail) alert(fail);
}

function refreshData2(shcode) {
  const cache = feed.get(shcode);
  if (!cache) return;
  for (const pane of panes) {
    if (pane.data2 !== shcode) continue;
    const entry = pane.active.get("fx_data2");
    if (!entry) continue;
    try { entry.handle.setSource(cache); } catch (err) {
      console.error(`[${shcode}] Data2 반영 실패`, err);
    }
  }
}

function refreshKsData2(pane) {
  const entry = pane?.active?.get("ks_data2");
  if (!entry) return;
  const fut = WeeklyLegs.futForData2(pane.linkLegs);
  const cache = fut ? feed.get(fut) : null;
  try { entry.handle.setSource(cache || null); } catch (err) {
    console.error(`[${fut || ""}] 국내선물 Data2 반영 실패`, err);
  }
}

function refreshKsData2ForSymbol(shcode) {
  for (const pane of panes) {
    if (WeeklyLegs.futForData2(pane.linkLegs) === shcode) refreshKsData2(pane);
  }
}

// 과거 봉 시딩: PUB/SUB는 과거 메시지를 보존하지 않으므로 스냅샷을 가져온다.
// 같은 종목의 동시 시딩은 하나로 합친다 (칸 여러 개가 같은 종목을 고를 수 있다).
const seedInflight = new Map(); // shcode → 진행 중 Promise

// 로딩 퍼센트는 종목별 봉 페이지다. 상한/16이 한 종목의 페이지 수.
// 화면틀은 받기 전에 종목을 모두 잡아 두어, 나중에 시작하는 참조·겹침이 분모를 늘리지 않게 한다.
const seedProgress = new Map(); // shcode → { done, total }

function seedPageLimit() {
  return Math.max(1, Math.ceil(barCap / 16));
}

function paintLoadPct() {
  const node = document.getElementById("load-pct");
  if (!node) return;
  let done = 0;
  let total = 0;
  for (const slot of seedProgress.values()) {
    done += slot.done;
    total += slot.total;
  }
  const pct = total > 0 ? Math.min(100, Math.round((done * 100) / total)) : 0;
  node.textContent = `${pct}%`;
}

function noteSeed(shcode, done, total) {
  seedProgress.set(shcode, { done, total });
  paintLoadPct();
}

function finishSeed(shcode) {
  if (!shcode) return;
  const total = seedProgress.get(shcode)?.total ?? seedPageLimit();
  seedProgress.set(shcode, { done: total, total });
  paintLoadPct();
}

// 예약만 되고 시딩이 시작되지 않은 종목(관측 실패 등)은 남은 페이지를 끝난 것으로 친다.
function finishSeedIfIdle(shcode) {
  const code = String(shcode ?? "").trim().toUpperCase();
  if (!code || seedInflight.has(code) || !seedProgress.has(code)) return;
  finishSeed(code);
}

function reserveSeeds(codes) {
  const total = seedPageLimit();
  for (const code of codes) {
    const sh = String(code ?? "").trim().toUpperCase();
    if (!sh || seedProgress.has(sh)) continue;
    seedProgress.set(sh, { done: 0, total });
  }
  paintLoadPct();
}

function seedSymbol(shcode) {
  const epoch = loadEpoch;
  if (!loadStill(epoch)) return Promise.resolve();
  const running = seedInflight.get(shcode);
  if (running) return running;
  const p = withChartLoad(() => {
    if (!loadStill(epoch)) return;
    return seedSymbolNow(shcode);
  }).finally(() => {
    if (seedInflight.get(shcode) === p) seedInflight.delete(shcode);
  });
  seedInflight.set(shcode, p);
  return p;
}

// 링 전체(상한 2560봉)를 페이지로 나눠 가져와 종목 캐시에 합친다.
// 가져오는 동안 리셋(세대 교체·엔진 재시작)이 끼어들면(seedToken 변경) 그 응답은
// 리셋 이전 기준이므로 폐기하고 새 기준으로 다시 가져온다 — 시딩을 기다리는 쪽이
// 무효한 결과를 받아 빈 차트로 남지 않게 한다.
async function seedSymbolNow(shcode) {
  const cache = feed.forSymbol(shcode);
  const pageLimit = seedPageLimit();
  const epoch = loadEpoch;
  // 어떤 경로로 빠져도 이 종목의 남은 페이지는 끝난 것으로 친다. 재시도(continue)는 루프 안이라 여기 안 든다.
  try {
  for (let attempt = 0; attempt < 3; attempt++) {
    const seedTok = cache.seedToken;
    noteSeed(shcode, 0, pageLimit); // 리셋으로 다시 받으면 이 종목만 0으로 되돌린다
    try {
      const all = [];
      const memEvents = [];
      const pstEvents = [];
      const gapsSec = []; // 구멍 구간 누적 (µs → 초) — 페이지 경계 구멍은 엔진이 경계 쌍까지 검사해 준다
      let back = 0;
      for (let pages = 0; pages < pageLimit; pages++) {
        if (!loadStill(epoch)) return;
        const res = await fetch(`/api/chart?shcode=${encodeURIComponent(shcode)}&back_index=${back}`, {
          signal: loadAbort.signal,
        });
        if (!loadStill(epoch) || !res.ok) return;
        const data = await res.json();
        const p = data.payload;
        if (p == null || !Array.isArray(p.bars)) return;
        noteSeed(shcode, pages + 1, pageLimit);
        feed.noteGeneration(cache, p.generation);
        // 지표 매니페스트: 첫 페이지에서 한 번 받아 칸 지표 패널을 구성한다
        if (pages === 0 && Array.isArray(p.indicators)) setIndicatorManifest(p.indicators);
        const rows = p.bars ?? [];
        const inds = p.ind ?? [];
        for (let ri = 0; ri < rows.length; ri++) {
          const [t, o, h, l, c] = rows[ri];
          const fx3rows = p.fx3 ?? [];
          const pgrows = p.pgap ?? [];
          const rgrows = p.rgap ?? [];
          const mgrows = p.mgap ?? [];
          const ymrows = p.ymae ?? [];
          const snrows = p.sniper ?? [];
          const osrows = p.os ?? [];
          const curows = p.cu ?? [];
          const sncorows = p.snco ?? [];
          const pvcrows = p.pvc ?? [];
          all.push({
            time: Number(t) / 1e6, open: o, high: h, low: l, close: c,
            ind: inds[ri], fx3: fx3rows[ri], pgap: pgrows[ri], rgap: rgrows[ri],
            mgap: mgrows[ri], ymae: ymrows[ri], sniper: snrows[ri], pvc: pvcrows[ri],
            os: osrows[ri], cu: curows[ri], snco: sncorows[ri],
          });
        }
        // ⑥⑦ 갱신·저장 이벤트 (희소) — 페이지 경계에서 중복되지 않게 시각으로 모은다
        for (const e of p.mem ?? []) memEvents.push(e);
        for (const e of p.pst ?? []) pstEvents.push(e);
        for (const g of p.gaps ?? []) gapsSec.push([Number(g[0]) / 1e6, Number(g[1]) / 1e6]);
        if (all.length >= barCap || !p.next_back_index) break;
        back = p.next_back_index;
      }
      if (cache.seedToken !== seedTok) continue; // 시딩 중 리셋 — 새 기준으로 다시 가져온다
      feed.reset(cache); // 시딩 중 라이브로 쌓인 봉과 혼합하지 않는다 (스냅샷 기준으로 다시 쌓음)
      cache.gaps = gapsSec; // 수신·보관만 한다 — 표시용 채움은 균일 분 그리드(엔진 gaps의
                            // 상위 집합)가 담당하므로 표시 경로는 읽지 않는다 (gaps.js 헤더 참조).
                            // reset 이후에 넣어야 지워지지 않는다
      all.sort((a, b) => a.time - b.time);
      let dedup = all.filter((b, i) => i === 0 || b.time !== all[i - 1].time);
      if (dedup.length > barCap) dedup = dedup.slice(dedup.length - barCap);
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
      // ⑥⑦ 이벤트 → 봉별 아이템으로 변환해 캐시에 심는다 (렌더러가 applySeed에서 복원)
      for (const item of MiraeLayers.buildMemItems(dedup, memEvents)) {
        const ind = cache.barInd.get(item.time);
        if (ind) ind.memItem = item;
      }
      for (const item of buildPstItems(dedup, pstEvents)) {
        const ind = cache.barInd.get(item.time);
        if (ind) ind.pstItem = item;
      }

      if (!loadStill(epoch)) return;
      renderSymbolPanes(shcode);
      return;
    } catch { /* 시딩 실패는 라이브 스트림으로 진행 */ }
    return;
  }
  } finally {
    finishSeed(shcode);
  }
}

// 지표 매니페스트를 저장하고 모든 칸의 지표 패널 트리를 다시 만든다.
// 내용이 같으면 건너뛴다. 종목 입력은 화면틀에 있어 이 재구성과 무관하다.
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
    // 모드 배지 — index.html의 정적 문자열("replay")은 초기값일 뿐, 실제 모드는 여기서 덮는다
    if (typeof p.mode === "string" && p.mode !== "") el.mode.textContent = p.mode;
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
  const targets = [...new Set(panes.flatMap((p) => [p.symbol, ...(p.frame?.overlays || [])]).filter(Boolean))];
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

// 같은 종목·같은 봉의 틱은 마지막 것만 남긴다. 봉이 바뀐 메시지는 순서를 지킨다.
// 그리기는 프레임당 한 번이라, 틱이 화면 갱신보다 빨라도 시각이 밀리지 않는다.
const liveQueue = [];
let liveFlush = false;
function enqueueLive(msg) {
  const p = msg?.payload ?? {};
  const sh = typeof p.shcode === "string" && p.shcode !== "" ? p.shcode : "";
  const bar = p.bar_open_time;
  const prev = liveQueue.find((q) => q.sh === sh && q.bar === bar);
  if (prev) prev.msg = msg;
  else liveQueue.push({ sh, bar, msg });
  if (!liveFlush) {
    liveFlush = true;
    requestAnimationFrame(flushLive);
  }
}
function flushLive() {
  liveFlush = false;
  const batch = liveQueue.splice(0, liveQueue.length);
  for (const item of batch) {
    try {
      applyStatus(item.msg);
    } catch (err) {
      console.error("status 적용 실패", err);
    }
  }
}

let wsConnected = false;
let chartLoads = 0;
// 로딩 취소는 세대를 올리고 진행 중 요청을 끊는다. 늦게 끝난 작업은 세대를 보고 화면을 건드리지 않는다.
let loadEpoch = 0;
let loadAbort = new AbortController();
let loadVeilOff = false;

function loadStill(epoch) {
  return epoch === loadEpoch;
}

function cancelLoad() {
  loadEpoch += 1;
  loadAbort.abort();
  loadAbort = new AbortController();
  for (const pane of panes) pane.selSeq += 1;
  loadVeilOff = true;
  paintWsState();
  void newScreen({ force: true });
}

function lockChartInput(chart, locked) {
  chart.applyOptions({ handleScroll: !locked, handleScale: !locked });
}

function paintWsState() {
  if (chartLoads === 0) loadVeilOff = false;
  const loading = chartLoads > 0 && !loadVeilOff;
  document.body.classList.toggle("charts-locked", loading);
  for (const pane of panes) {
    if (pane.chart) lockChartInput(pane.chart, loading);
  }
  if (!loading) seedProgress.clear();
  paintLoadPct();
  if (loading) {
    el.wsState.textContent = "로딩중";
    el.wsState.className = "badge load";
    return;
  }
  if (wsConnected) {
    el.wsState.textContent = "연결됨";
    el.wsState.className = "badge ok";
    return;
  }
  el.wsState.textContent = "연결 끊김 — 재시도";
  el.wsState.className = "badge err";
}

function withChartLoad(work) {
  chartLoads += 1;
  loadVeilOff = false;
  paintWsState();
  return Promise.resolve()
    .then(work)
    .finally(() => {
      chartLoads -= 1;
      paintWsState();
    });
}

function connect() {
  const ws = new WebSocket(WS_URL);
  ws.onopen = () => {
    wsConnected = true;
    paintWsState();
  };
  ws.onclose = () => {
    wsConnected = false;
    paintWsState();
    setTimeout(connect, 2000);
  };
  ws.onmessage = (ev) => {
    let data;
    try { data = JSON.parse(ev.data); } catch { return; }
    if (data.kind !== "status") return;
    if (data.stream_event === "restart" || data.stream_event === "gap") {
      onStreamReset();
    }
    enqueueLive(data.message ?? {});
  };
}

// ---- 화면틀 v2 ----
// 화면틀에는 레이아웃·칸별 지표·고른 원본 시그널·상대/선물·시스템 기준과 변수값을 저장한다.
// 실계좌 주문 자동 시작은 넣지 않는다.
// 직렬화/검증은 workspace.js의 순수 함수가 담당한다 (node:test 대상).

function collectWorkspace() {
  const frames = allFrames().map((frame) => ({
    height: frame.row.heightFrac,
    overlays: frame.overlays.slice(),
    overlayStyles: { ...frame.overlayStyles },
    overlayScale: overlayScaleMode(frame),
    panels: frame.panes.map((pane) => ({
      height: pane.heightFrac,
      symbol: pane.symbol,
      panelOpen: pane.panelOpen,
      data2: pane.data2 || "",
      candles: pane.barStyle !== "none",
      barStyle: pane.barStyle || "candle",
      overlayStyles: { ...(pane.overlayStyles || {}) },
      indicators: [...pane.active.entries()].map(([id, entry]) => ({ id, layers: { ...entry.layers } })),
      systems: [...(pane.systems || [])],
      systemVars: pane.systemVars,
      systemBasis: pane.systemBasis,
      linkLegs: pane.linkLegs,
    })),
  }));
  return Workspace.serialize(el.wsName.value.trim(), frames, {
    cols: colWeights.length,
    colWeights,
  });
}

// 시딩이 덮어쓸 창. 같은 화면틀만 본다. 넉넉한 창이 있으면 그것을 쓰고,
// 없으면 1봉에 가까운 창이라도 그 틀 안에서만 유지한다. 다른 틀은 읽지 않는다.
function captureFrameSeedView(frame) {
  const aligned = captureAlignView(frame);
  if (aligned) return aligned;
  for (const pane of frame.panes) {
    if (!pane.chart) continue;
    const times = feed.get(pane.symbol)?.seriesTimes;
    const width = pane.chartEl?.clientWidth ?? 0;
    if (!times?.length || !(width > 0)) continue;
    const lr = pane.chart.timeScale().getVisibleLogicalRange();
    if (!lr || !(lr.to > lr.from)) continue;
    const spacingPx = width / (lr.to - lr.from + 1);
    if (!(spacingPx > 0) || !Number.isFinite(spacingPx)) continue;
    return { spacingPx, rightTime: PaneSync.timeAt(times, lr.to) };
  }
  return null;
}

// 기준 창: 그 화면틀에서 봉이 두 개 이상 보이는 첫 칸. 1봉으로 찌그러진 창은 건너뛴다.
function captureAlignView(frame) {
  for (const pane of frame.panes) {
    if (!pane.chart) continue;
    const times = feed.get(pane.symbol)?.seriesTimes;
    const width = pane.chartEl?.clientWidth ?? 0;
    if (!times?.length || !(width > 0)) continue;
    const lr = pane.chart.timeScale().getVisibleLogicalRange();
    if (!lr || !(lr.to > lr.from + 1)) continue;
    return {
      spacingPx: width / (lr.to - lr.from + 1),
      rightTime: PaneSync.timeAt(times, lr.to),
    };
  }
  return null;
}

// 그 화면틀의 칸만 오른쪽 끝 시각과 px 봉 간격에 맞춘다.
// settle이면 라이브러리가 범위를 고쳐 보내는 늦은 에코가 다시 퍼지지 않게 잠시 막는다.
function applyAlignView(frame, view, settle) {
  if (!view || !(view.spacingPx > 0)) return;
  for (const pane of frame.panes) {
    const times = feed.get(pane.symbol)?.seriesTimes;
    const width = pane.chartEl?.clientWidth ?? 0;
    if (!times?.length || !(width > 0)) continue;
    const toM = PaneSync.logicalAt(times, view.rightTime);
    const fromM = toM - (width / view.spacingPx - 1);
    // 범위가 숫자가 아니거나 뒤집히면 라이브러리가 예외를 던지고 불러오기가 멈춘다.
    if (!Number.isFinite(fromM) || !Number.isFinite(toM) || fromM > toM) continue;
    if (settle) mutePaneRangeForSeeding(pane);
    else mutePaneRange(pane);
    pane.chart.timeScale().setVisibleLogicalRange({ from: fromM, to: toM });
  }
}

function alignFramePanes(frame, settle) {
  applyAlignView(frame, captureAlignView(frame), settle);
}

function alignAllPanes(settle) {
  for (const frame of allFrames()) alignFramePanes(frame, settle);
}

// 종목 해지는 하지 않는다. 새 격자가 watch를 건 뒤에 applyWorkspace가 releaseSymbol로 정리한다.
function destroyGrid() {
  for (const frame of allFrames()) releaseFrameSearch(frame);
  for (const pane of [...panes]) destroyPane(pane, { release: false });
  cancelAnimationFrame(alignRaf);
  pendingAlignFrames.clear();
  holdAlignFrames.clear();
  panesEl.querySelectorAll(".resizebar").forEach((e) => e.remove());
  for (const row of gridRows) row.el.remove();
  gridRows = [];
  currentPane = null;
  currentFrame = null;
  colWeights = [1];
}

// 화면틀 v2 적용: 격자를 다시 만들고 칸별 종목은 watch→시딩으로 복원한다 (비동기 진행).
// symbol 없는 칸(구 화면틀)은 parse 단에서 current_symbol로 폴백되어 들어온다.
// 예전 종목·Data2의 unwatch는 새 칸 시딩이 끝난 뒤에 한다 (엔진의 마지막-watch 해지 거부 회피).
async function applyWorkspace(parsed) {
  const epoch = loadEpoch;
  return withChartLoad(async () => {
    const before = new Set();
    for (const pane of panes) {
      if (pane.symbol) before.add(pane.symbol);
      if (pane.data2) before.add(pane.data2);
    }
    for (const frame of allFrames()) {
      for (const sh of frame.overlays || []) before.add(sh);
    }
    destroyGrid();
    const specs = Array.isArray(parsed.frames) ? parsed.frames : [];
    let cols = Number.isInteger(parsed.cols) && parsed.cols >= 1 ? parsed.cols : 1;
    if (!specs.length || specs.length % cols !== 0) cols = 1;
    if (specs.length) {
      colWeights = Array.isArray(parsed.colWeights) && parsed.colWeights.length === cols
        ? parsed.colWeights.slice()
        : Array.from({ length: cols }, () => 1 / cols);
    }
    const seedCodes = [];
    for (const spec of specs) {
      for (const panel of spec.panels ?? []) {
        if (panel.symbol) seedCodes.push(panel.symbol);
      }
      for (const code of spec.overlays || []) seedCodes.push(code);
    }
    reserveSeeds(seedCodes);
    const jobs = [];
    specs.forEach((spec, index) => {
      let row = gridRows[gridRows.length - 1];
      if (index % cols === 0) row = makeRow(spec.height);
      const frame = createFrame(row);
      frame.overlayScale = spec.overlayScale === "price" || spec.overlayScale === "ratio" || spec.overlayScale === "shared"
        ? spec.overlayScale : "shared";
      frame.pendingOverlays = Array.isArray(spec.overlays) ? spec.overlays.slice() : [];
      frame.pendingOverlayStyles = spec.overlayStyles && typeof spec.overlayStyles === "object"
        ? { ...spec.overlayStyles } : {};
      const panels = spec.panels ?? [];
      // 한 화면틀은 종목 하나다. 칸마다 종목이 달랐던 문서는 첫 종목으로 맞춘다.
      const frameSymbol = panels.find((p) => p.symbol)?.symbol || "";
      for (const panel of panels) {
        const pane = createPane(frame, panel.height);
        pane.overlayStyles = { ...(panel.overlayStyles || {}) };
        setPanelOpen(pane, panel.panelOpen); // 구 화면틀은 parse가 기본값(열림)으로 정규화한다
        setBarStyle(pane, panel.barStyle || (panel.candles === false ? "none" : "candle"));
        for (const ind of panel.indicators ?? []) activateIndicator(pane, ind.id, ind.layers);
        pane.systems = new Set(panel.systems || []);
        pane.systemVars = panel.systemVars ? JSON.parse(JSON.stringify(panel.systemVars)) : {};
        pane.systemBasis = { ...(panel.systemBasis || {}) };
        if (panel.linkLegs) pane.linkLegs = WeeklyLegs.copy(panel.linkLegs);
        buildPaneTools(pane);
        jobs.push((async () => {
          try {
            if (frameSymbol) await selectPaneSymbol(pane, frameSymbol);
          } catch (err) {
            // 한 칸이 실패해도 다른 칸 시딩과 마지막 다시 그리기는 계속한다.
            console.error(`화면틀 칸 적용 실패 (${panel.symbol || "?"})`, err);
          }
        })());
      }
    });
    const built = allFrames();
    currentFrame = built[built.length - 1] || null;
    layoutGrid();
    updateBadgeVisibility();
    await Promise.all(jobs);
    if (!loadStill(epoch)) return;
    for (const frame of allFrames()) {
      const codes = frame.pendingOverlays || [];
      const styles = frame.pendingOverlayStyles || {};
      frame.pendingOverlays = null;
      frame.pendingOverlayStyles = null;
      const main = frameSymbolPane(frame)?.symbol || "";
      for (const code of codes) {
        if (!code || code === main) continue;
        if (styles[code]) frame.overlayStyles[code] = styles[code];
        try {
          await addFrameOverlay(frame, code);
        } catch (err) {
          console.error(`화면틀 겹침 적용 실패 (${code})`, err);
        }
      }
      for (const pane of frame.panes || []) refreshWeeklyPlots(pane);
    }
    // 관측에 실패해 페이지를 안 받은 종목도 여기서 마친다. 이후 맞추기는 짧은 꼬리다.
    for (const sh of seedProgress.keys()) finishSeed(sh);
    for (const pane of panes) {
      const w = pane.chartEl.clientWidth;
      const h = pane.chartEl.clientHeight;
      if (w > 0 && h > 0) pane.chart.resize(w, h);
    }
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    // 칸 폭이 0이던 첫 시딩은 지표가 비어 보일 수 있다. 레이아웃 뒤에 한 번 더 깐다.
    for (const sh of new Set(panes.map((p) => p.symbol).filter(Boolean))) {
      try { renderSymbolPanes(sh); } catch (err) { console.error(`[${sh}] 다시 그리기 실패`, err); }
    }
    for (const sh of new Set(panes.map((p) => p.data2).filter(Boolean))) {
      try { refreshData2(sh); } catch (err) { console.error(`[${sh}] 참조 다시 그리기 실패`, err); }
    }
    for (const frame of allFrames()) {
      try { alignFramePanes(frame, true); } catch (err) { console.error("화면틀 시간축 맞추기 실패", err); }
    }
    // 라이브러리가 칸마다 범위를 한 번 고친 뒤에, 같은 화면틀 기준으로 다시 덮는다.
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    for (const frame of allFrames()) {
      try { alignFramePanes(frame, true); } catch (err) { console.error("화면틀 시간축 맞추기 실패", err); }
    }
    // releaseSymbol은 아직 종목·Data2·진행 중 선택으로 쓰는 코드는 바로 반환한다.
    for (const sh of before) await releaseSymbol(sh);
  });
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

async function saveWorkspace(opts) {
  const token = await apiToken();
  if (!token) return alert("토큰이 필요합니다.");
  const name = el.wsName.value.trim();
  if (!name) return alert("화면틀 이름이 필요합니다.");
  // 저장 버튼만 확인한다. 변수 확인 뒤의 자동 저장(quiet)은 방금 연 이름을 갱신한다.
  if (!opts?.quiet) {
    let exists = false;
    try {
      const listed = await fetch("/api/workspaces");
      if (listed.ok) {
        const data = await listed.json();
        exists = Array.isArray(data.workspaces) && data.workspaces.includes(name);
      }
    } catch { /* 확인이 안 되면 저장 요청이 같은 오류를 보여 준다 */ }
    if (exists && !confirm(`화면틀 '${name}' 파일이 있습니다. 덮어쓰겠습니까?`)) return;
  }
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
  if (!opts?.quiet) alert(`화면틀 '${name}' 저장됨`);
}

async function loadWorkspace(opts) {
  const name = el.wsName.value.trim();
  const res = await fetch(`/api/workspaces/${encodeURIComponent(name)}`);
  if (!res.ok) {
    if (!opts?.quiet) alert("화면틀 없음");
    return false;
  }
  const data = await res.json();
  const parsed = Workspace.parse(data, (id) => id in RENDERERS);
  if (!parsed) {
    alert("구 버전 화면틀은 적용할 수 없습니다 — 기본 상태를 유지합니다");
    return false;
  }
  // 화면 복원으로 전략을 자동 시작하거나 주문을 재실행하지 않는다.
  // 칸·지표 시딩이 끝난 뒤에 알린다. 먼저 알리면 아직 빈 차트를 완료로 본다.
  // 시작 시 자동 열기(quiet)는 완료 알림을 띄우지 않는다. 취소한 로드도 알리지 않는다.
  const epoch = loadEpoch;
  try {
    await applyWorkspace(parsed);
  } catch (err) {
    if (!loadStill(epoch) || err?.name === "AbortError") return false;
    console.error("화면틀 적용 실패", err);
    alert(`화면틀 '${name}' 적용 중 오류: ${err && err.message ? err.message : err}`);
    return false;
  }
  if (!loadStill(epoch)) return false;
  if (!opts?.quiet) alert(`화면틀 '${name}' 적용`);
  return true;
}

async function pushBarCap(n) {
  const token = await apiToken();
  if (!token) return { ok: false };
  const res = await fetch("/api/chart/cap", {
    method: "POST",
    headers: { "content-type": "application/json", "x-trader-token": token },
    body: JSON.stringify({ bars: n }),
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) return { ok: false, data };
  const applied = Number(data.payload && data.payload.bars);
  return { ok: true, bars: Number.isInteger(applied) ? applied : n };
}

// 상한 입력을 확정한다. 엔진이 관측 종목을 다시 받고, 열려 있는 차트는 그 개수만 다시 그린다.
async function commitBarCap(raw) {
  const input = document.getElementById("bar-cap");
  const clamped = clampBarCap(Number(raw));
  if (clamped == null) {
    input.value = String(barCap);
    return;
  }
  input.value = String(clamped);
  if (clamped === barCap) return;
  barCap = clamped;
  localStorage.setItem(BAR_CAP_KEY, String(barCap));
  await withChartLoad(async () => {
    const pushed = await pushBarCap(barCap);
    if (pushed.ok && pushed.bars !== barCap) {
      barCap = pushed.bars;
      input.value = String(barCap);
      localStorage.setItem(BAR_CAP_KEY, String(barCap));
    }
    const syms = new Set();
    for (const pane of panes) {
      if (pane.symbol) syms.add(pane.symbol);
      if (pane.data2) syms.add(pane.data2);
    }
    for (const sh of syms) await seedSymbol(sh);
  });
}

document.getElementById("screen-new").onclick = () => newScreen();
document.getElementById("load-cancel").onclick = cancelLoad;
document.getElementById("row-add").onclick = addFrameRow;
document.getElementById("col-add").onclick = addFrameCol;
document.getElementById("bar-cap").addEventListener("change", (ev) => {
  commitBarCap(ev.target.value);
});
document.getElementById("ws-save").onclick = saveWorkspace;
document.getElementById("ws-load").onclick = loadWorkspace;

// 창 크기가 바뀌면 각 칸의 차트 크기를 다시 맞춘다 (기준은 차트 호스트 — syncPaneSize와 같다)
addEventListener("resize", () => {
  for (const pane of panes) syncPaneSize(pane);
});

// 드롭다운 바깥 클릭은 열린 검색 결과를 닫는다
document.addEventListener("click", (ev) => {
  for (const frame of allFrames()) {
    if (!frame.symResults.hidden && !frame.pickerEl.contains(ev.target)) hideFrameResults(frame);
  }
});

// 시작은 화면틀 default. 파일이 없을 때만 빈 차트 하나다.
// 그 빈 차트이고 엔진이 관측 중인 종목이 하나뿐이면 그 종목을 칸 1에 둔다.
async function bootstrap() {
  const row = makeRow(1);
  const frame = createFrame(row);
  createPane(frame, 1);
  currentFrame = frame;
  layoutGrid();
  updateBadgeVisibility();
  const capInput = document.getElementById("bar-cap");
  capInput.value = String(barCap);
  if (barCap !== BAR_CAP_DEFAULT) await pushBarCap(barCap);
  // 상태 조회는 엔진이 없으면 오래 걸린다. default 열기를 그 뒤에 두지 않는다.
  const watchesReady = refreshEngineWatches();
  connect();
  loadYlCatalog();
  el.wsName.value = "default";
  const epoch = loadEpoch;
  const opened = await loadWorkspace({ quiet: true });
  if (!opened && loadStill(epoch)) {
    await watchesReady;
    if (engineWatches.length === 1 && panes[0] && !panes[0].symbol) {
      await selectPaneSymbol(panes[0], engineWatches[0]);
    }
  }
}
bootstrap();
