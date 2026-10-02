// 대시보드 프론트엔드 (계획서 §18).
// C가 계산한 값을 표시만 한다. 지표·점수를 재계산하지 않는다.
// 칸(pane)마다 차트 1개 + 종목 1개를 두고, 종목별 데이터 캐시는 feed.js가 격리한다.
// 지표는 렌더러(RENDERERS)를 칸에 활성화해 표시하고, 칸 왼쪽의 접이식 지표 패널
// (카테고리 ▸ 지표 체크박스 ▸ 레이어 체크박스 트리)은 엔진 스냅샷의 indicators
// 매니페스트에서 만든다 (트리 분류는 indicator-tree.js).
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
// Pane: { id, el, toolsEl, pickerEl, panelEl, chartEl, chart, candleSeries, heightFrac, syncHandle,
//         symbol, symName, selSeq, selTarget, searchSeq, searchTimer,
//         symInput, symNameEl, symResults, panelOpen, panelToggleEl, treeFold,
//         active: Map<indId, { renderer, handle, layers: {layerId: bool} }> }
// symbol이 ""이면 미선택: 빈 차트에 종목 입력만 보인다 (지표 패널은 안내 문구만).
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
  fx_pvc: PvcLayers.PvcRenderer,
  fx_data2: Data2Layers.Data2Renderer,
};

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

// 프로그램적 시간축 변경이 다른 칸으로 번지지 않게 그 칸의 범위 이벤트를 뮤트한다
// (전파 계약은 pane-sync.js 헤더 참조). 범위 이벤트는 동기 호출 안과 뒤따르는 rAF
// 프레임에 걸쳐 나오므로(실측), 뮤트는 프레임이 지난 뒤에 푼다 — 라이브 1봉 적용용.
function mutePaneRange(pane) {
  paneSync.mute(pane.syncHandle);
  requestAnimationFrame(() => requestAnimationFrame(() => paneSync.unmute(pane.syncHandle)));
}

// 시딩 적용용 뮤트: scrollToRealTime은 400ms 스크롤 애니메이션이라 프레임마다 범위
// 이벤트를 흘리므로(실측), 애니메이션이 끝날 때까지 뮤트를 유지한다.
function mutePaneRangeForSeeding(pane) {
  paneSync.mute(pane.syncHandle);
  setTimeout(() => paneSync.unmute(pane.syncHandle), 450); // 400ms 애니메이션 + 여유
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

// 칸 높이는 #panes 대비 % (pane.heightFrac 0..1). 인접 칸 사이의 리사이즈바를
// 드래그해 조절한다. 높이를 바꾼 뒤에는 차트 크기를 다시 맞춘다.
// 차트 크기의 기준은 차트 호스트(.chart-host)다 — 지표 패널을 접으면 칸 폭은
// 그대로여도 호스트 폭이 늘어나므로, 패널 접기/펼치기에서도 이 함수를 부른다.
function syncPaneSize(pane) {
  pane.el.style.height = `${pane.heightFrac * 100}%`;
  pane.chart.resize(pane.chartEl.clientWidth, pane.chartEl.clientHeight);
}

// 칸 폭이 나중에 잡히거나 패널을 접으면 봉 간격이 달라진다. 그때 시간축을 다시 맞춘다.
let alignFrame = 0;
const paneResize = new ResizeObserver(() => {
  cancelAnimationFrame(alignFrame);
  alignFrame = requestAnimationFrame(() => alignAllPanes(false));
});

function createPane(heightFrac = 1) {
  const div = document.createElement("div");
  div.className = "pane";
  div.style.height = `${heightFrac * 100}%`;
  // 칸 구조: 도구줄(종목 입력 + ☰ + ×) + 본문([지표 패널 | 차트] 수평 분할)
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
  panesEl.append(div);
  const pane = {
    id: nextPaneId++, el: div, toolsEl: tools, panelEl: panel, chartEl: chartHost, heightFrac,
    symbol: "", symName: "", selSeq: 0, selTarget: "", searchSeq: 0, searchTimer: null,
    active: new Map(), chart: null, candleSeries: null, syncHandle: null,
    pickerEl: null, symInput: null, symNameEl: null, symResults: null,
    panelOpen: true, panelToggleEl: null, treeFold: {}, data2: "",
    barStyle: "candle", barDraw: "candle",
  };
  pane.chart = LightweightCharts.createChart(chartHost, chartOptions(pane));
  paneResize.observe(chartHost);
  pane.candleSeries = makePriceSeries(pane.chart, "candle", false);
  pane.syncHandle = paneSync.add(pane.chart, pane.candleSeries, {
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
  buildPanePicker(pane); // 종목 입력은 칸 도구줄 맨 앞에 1회 만든다 (트리 재구성과 무관)
  buildPaneToolButtons(pane); // ☰(지표 패널 접기)·×(칸 삭제)도 1회 만든다 (상태만 동기화)
  buildPaneTools(pane); // 지표 패널의 트리를 채운다
  // 차트는 도구줄이 비어 있는 시점의 호스트 크기로 생성된다 — 도구줄이 자라며 호스트가
  // 줄어들었으므로 생성 시점부터 정확한 크기로 맞춘다 (안 맞추면 첫 리사이즈 트리거
  // 전까지 차트 하단의 시간축이 잘린다 — bootstrap의 첫 칸·addPane의 새 칸 모두)
  syncPaneSize(pane);
  panes.push(pane);
  pane.el.addEventListener("pointerdown", () => { currentPane = pane; });
  return pane;
}

let currentPane = null;

function viewedPane() {
  if (currentPane && panes.includes(currentPane) && currentPane.symbol) return currentPane;
  return panes.find((p) => p.symbol) || null;
}

// 도구줄 버튼: ☰(지표 패널 접기/펼치기) + ×(칸 삭제). 트리 재구성 때 다시 만들지 않는다
function buildPaneToolButtons(pane) {
  const toggle = document.createElement("button");
  toggle.className = "chip panel-toggle on"; // 기본 열림(panelOpen: true)
  toggle.textContent = "☰";
  toggle.title = "지표 패널 접기/펼치기";
  toggle.onclick = () => setPanelOpen(pane, !pane.panelOpen);
  pane.toolsEl.append(toggle);
  pane.panelToggleEl = toggle;

  const del = document.createElement("button");
  del.className = "chip del";
  del.textContent = "×";
  del.title = "이 차트 삭제";
  del.onclick = () => removePane(pane);
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
                  ctx.strokeStyle = bar.close >= bar.open ? CANDLE_UP : CANDLE_DN;
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

function makePriceSeries(chart, draw, hollow) {
  if (draw === "bar") {
    return chart.addBarSeries({
      upColor: CANDLE_UP, downColor: CANDLE_DN,
      thinBars: false,
    });
  }
  if (draw === "line") {
    return chart.addLineSeries({
      color: "#d1d4dc", lineWidth: 2,
      priceLineVisible: true, lastValueVisible: true,
    });
  }
  return chart.addCandlestickSeries(candleColors(hollow ? "outline" : "fill"));
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
    const rows = bars.length ? Gaps.withWhitespace(bars).map((r) => pricePoint(r, draw)) : [];
    mutePaneRange(pane);
    if (pane.candleSeries) pane.chart.removeSeries(pane.candleSeries);
    pane.candleBorder = null;
    pane.candleSeries = makePriceSeries(pane.chart, draw, hollow);
    pane.barDraw = draw;
    if (pane.syncHandle) pane.syncHandle.candleSeries = pane.candleSeries;
    if (rows.length) pane.candleSeries.setData(rows);
  } else if (draw === "candle" && pane.candleSeries) {
    pane.candleSeries.applyOptions(candleColors(hollow ? "outline" : "fill"));
  }
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
}

function setPanelOpen(pane, open) {
  pane.panelOpen = open;
  pane.panelEl.hidden = !open;
  pane.panelToggleEl.classList.toggle("on", open);
  syncPaneSize(pane);
}

function removePane(pane, { release = true } = {}) {
  const i = panes.indexOf(pane);
  if (i < 0) return;
  panes.splice(i, 1);
  paneResize.unobserve(pane.chartEl);
  if (currentPane === pane) currentPane = null;
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
// 해지 요청은 종목별 큐에 넣어 이 종목의 watch와 순서를 맞춘다: 독립 fetch의 엔진 도착
// 순서는 보장되지 않아, 먼저 낸 unwatch가 뒤에 낸 watch보다 늦게 도착하면 막 채택한
// 종목을 끊는다 (칸 삭제·stale 폐기 경로 모두 같은 클래스). 큐에서 기다리는 동안 이
// 종목을 채택하는 선택이 붙었으면(symbol/selTarget) 해지를 건너뛴다.
async function releaseSymbol(shcode) {
  if (!shcode || panes.some((p) => p.symbol === shcode)) return;
  seedInflight.delete(shcode); // 진행 중 시딩은 고아 캐시를 채우고 렌더 없이 끝난다
  feed.drop(shcode);
  await symbolOpQueue.enqueue(shcode, async () => {
    if (panes.some((p) => p.symbol === shcode || p.selTarget === shcode)) return;
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

  input.addEventListener("input", () => {
    const upper = input.value.toUpperCase();
    if (upper !== input.value) {
      const pos = input.selectionStart;
      input.value = upper;
      if (pos != null) input.setSelectionRange(pos, pos);
    }
    onPaneSymbolInput(pane);
  });
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
    // fut: 0=주식, 1=국내선물, 2=해외선물 (엔진 market.instruments)
    mkt.textContent = it.fut === 2 ? "해외" : it.fut ? "선물" : "";
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
      const data = await res.json().catch(() => ({}));
      if (!res.ok) {
        const why = data.error_code === "registry_unavailable"
          ? "종목 목록을 아직 받지 못했습니다"
          : "종목 검색 실패";
        showPaneResults(pane, [], seq);
        if (seq === pane.searchSeq && pane.symResults.firstChild) {
          pane.symResults.firstChild.textContent = why;
        }
        return;
      }
      showPaneResults(pane, data.payload?.items ?? [], seq);
    } catch {
      /* 검색 실패는 드롭다운만 닫는다 */
    }
  }, 200);
}

// watch 요청 공통부 (선택·화면틀 복원·스트림 리셋 재구독이 함께 쓴다).
// 종목별 큐(WatchGuard.createOpQueue)로 직렬화한다: 같은 종목의 unwatch가
// 뒤에 시작된 watch보다 엔진에 늦게 도착하는 경주를 막는다 (releaseSymbol 참조).
const symbolOpQueue = WatchGuard.createOpQueue();
function watchSymbol(shcode) {
  return symbolOpQueue.enqueue(shcode, async () => {
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
  });
}

// 칸에 종목을 설정한다: watch → 캐시에 반영 → 시딩(완료 시 그 종목의 모든 칸을 다시 그림).
// 실패하면 칸은 기존 종목과 화면을 그대로 유지한다 (입력창만 현재 종목으로 되돌린다).
// selSeq는 빠른 연속 선택 시 늦은 완료를 폐기한다. 폐기되는 선택은 엔진이 이미 그 종목을
// watch했을 수 있으므로, 아무도 이어받지 않은 watch이면 해지한다 (누수 방지). 선택 실패
// 시에도 같은 판정으로 정리한다 — 다른 선택의 stale 완료가 이 선택의 selTarget을 보고
// 해지를 보류했던 watch가 실패와 함께 주인을 잃는 경우가 있다.
async function selectPaneSymbol(pane, shcode, name) {
  shcode = String(shcode ?? "").trim();
  hidePaneResults(pane);
  if (!shcode || shcode === pane.symbol) {
    syncPaneSymbolUi(pane);
    return;
  }
  const seq = ++pane.selSeq;
  pane.selTarget = shcode; // 진행 중 선택 목표 — stale 폐기 시 watch 해지 판정에 쓴다
  const w = await watchSymbol(shcode);
  if (seq !== pane.selSeq) {
    // 그 사이 다른 선택이 시작됐다. 늦게 붙은 watch는 어느 칸도 안 보고 다른 진행 중
    // 선택도 노리지 않으면 그대로 새어 나간다 — 해지한다 (인계된 watch는 건드리지 않는다).
    // w.ok가 false여도 엔진엔 적용되고 응답만 유실됐을 수 있으므로 같은 판정으로 정리한다
    // (관측 중이 아닌 종목의 unwatch는 엔진이 거절해 무해하다)
    if (WatchGuard.staleWatchLeaks(shcode, panes)) releaseSymbol(shcode);
    return;
  }
  if (!w.ok) {
    pane.selTarget = "";
    syncPaneSymbolUi(pane);
    // 채택 실패로 이 종목에 주인이 남지 않았으면 남은 watch를 해지한다
    // (stale 폐기 경로와 같은 판정 — 인계받은 watch가 있으면 건드리지 않는다)
    if (WatchGuard.staleWatchLeaks(shcode, panes)) releaseSymbol(shcode);
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
  buildPaneTools(pane); // 지표 트리가 보이기 시작한다
  updateBadgeVisibility();
  // 이전 종목은 새 watch가 붙은 뒤에 해제한다 (엔진의 마지막-watch 해지 거부를 피한다)
  if (prev) releaseSymbol(prev);
  await seedSymbol(shcode);
  if (pane.active.has("fx_data2")) ensureDefaultData2(pane);
}

// 참조가 비어 있으면 ES↔NQ 같은 월물을 넣어 바로 관측한다.
function ensureDefaultData2(pane) {
  if (pane.data2 || !pane.symbol) return;
  const code = Data2Layers.defaultCode(pane.symbol);
  if (!code || code === pane.symbol) return;
  setPaneData2(pane, code).catch(() => {});
}

// 스나이퍼 Data2: 참조 종목의 1분 비율을 이 칸 차트에 겹친다.
async function setPaneData2(pane, code) {
  const next = String(code ?? "").trim().toUpperCase();
  const prev = pane.data2 || "";
  pane.data2 = next;
  if (!pane.active.has("fx_data2")) activateIndicator(pane, "fx_data2");
  const entry = pane.active.get("fx_data2");
  if (!next) {
    if (entry) entry.handle.clear();
    if (prev && WatchGuard.staleWatchLeaks(prev, panes)) releaseSymbol(prev);
    buildPaneTools(pane);
    return;
  }
  const w = await watchSymbol(next);
  if (pane.data2 !== next) return;
  if (!w.ok) {
    pane.data2 = prev;
    alert(`참조 종목 관측 실패 (${next}): ${w.error}`);
    buildPaneTools(pane);
    return;
  }
  if (w.name) feed.forSymbol(next).name = w.name;
  await seedSymbol(next);
  if (entry && pane.data2 === next) entry.handle.setSource(feed.get(next));
  if (prev && prev !== next && WatchGuard.staleWatchLeaks(prev, panes)) releaseSymbol(prev);
  buildPaneTools(pane);
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

// 칸 지표 패널 트리: 카테고리 ▸ 지표 체크박스 ▸ 그 지표의 레이어 체크박스들.
// 종목 미선택 칸은 안내 문구만 보인다 (빈 차트 원칙 — 지표는 종목이 있어야 동작한다).
// 트리 구조(카테고리 분류)는 indicator-tree.js가 매니페스트에서 만들고,
// 체크 상태는 항상 이 칸의 pane.active·layers를 그대로 반영한다 — 상태를 바꾸는 모든 경로
// (토글·종목 변경·화면틀 복원·매니페스트 갱신)에서 이 함수를 다시 불러 트리를 재구성한다
// (칩 시절 buildPaneTools의 호출 지점과 같은 계약).
function buildPaneTools(pane) {
  const panel = pane.panelEl;
  panel.replaceChildren();

  panel.append(buildChartControls(pane));
  if (!pane.symbol) {
    const hint = document.createElement("div");
    hint.className = "ind-hint";
    hint.textContent = "종목을 선택하면 지표를 고를 수 있습니다";
    panel.append(hint);
    return;
  }

  for (const cat of IndicatorTree.buildTree(indicatorManifest, (id) => id in RENDERERS)) {
    panel.append(buildCatNode(pane, cat));
  }
}

// 왼쪽 패널 위: 이 칸의 분봉 모양을 고른다. 칸을 더 여는 것은 위쪽 + 차트다.
function buildChartControls(pane) {
  const box = document.createElement("div");
  box.className = "ind-chart-tools";
  const label = document.createElement("label");
  label.className = "bar-style";
  label.append(document.createTextNode("분봉"));
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
  box.append(label);
  return box;
}

// 트리 카테고리 노드: 머리(접기/펼치기 + 이름) + 지표 목록.
// 접힘 상태는 pane.treeFold에 칸 UI 로컬로 둔다 (직렬화하지 않는다).
function buildCatNode(pane, cat) {
  const node = document.createElement("div");
  node.className = "ind-cat";
  const folded = !!pane.treeFold[cat.id];
  const head = document.createElement("div");
  head.className = "ind-cat-head";
  head.textContent = `${folded ? "▸" : "▾"} ${cat.name}`;
  head.title = folded ? "펼치기" : "접기";
  head.onclick = () => {
    pane.treeFold[cat.id] = !folded;
    buildPaneTools(pane);
  };
  node.append(head);
  if (!folded) {
    const body = document.createElement("div");
    body.className = "ind-cat-body";
    for (const meta of cat.indicators) body.append(buildIndNode(pane, meta));
    node.append(body);
  }
  return node;
}

// 트리 지표 노드: 지표 체크박스 + (켜져 있으면) 그 지표의 레이어 체크박스들.
// 지표 체크는 activateIndicator/deactivateIndicator를 그대로 부르고 트리를 재구성한다
// (칩 시절과 같은 경로). 레이어 체크는 handle.setLayers만 반영한다 — 체크박스 자체가
// 상태 표시라 트리 재구성은 필요 없다 (칩 시절 classList.toggle과 같은 계약).
function buildIndNode(pane, meta) {
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
    if (box.checked && meta.id === "fx_data2" && !pane.data2) ensureDefaultData2(pane);
    buildPaneTools(pane);
    updateBadgeVisibility();
  };
  row.append(box, document.createTextNode(meta.name ?? meta.id));
  node.append(row);
  if (meta.id === "fx_data2") {
    const ref = document.createElement("input");
    ref.className = "data2-ref";
    ref.placeholder = "참조 코드";
    ref.size = 8;
    ref.value = pane.data2 || "";
    ref.title = "이 칸에 겹칠 참조 종목. Enter로 적용";
    ref.addEventListener("input", () => {
      const upper = ref.value.toUpperCase();
      if (upper !== ref.value) {
        const pos = ref.selectionStart;
        ref.value = upper;
        if (pos != null) ref.setSelectionRange(pos, pos);
      }
    });
    ref.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") setPaneData2(pane, ref.value);
    });
    node.append(ref);
  }

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
    if (pane.data2) handle.setSource(feed.get(pane.data2));
  } else {
    const cache = pane.symbol ? feed.get(pane.symbol) : undefined;
    if (cache) handle.applySeed(ctxFor(cache));
  }
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
    // 세대 상승은 엔진 쪽 재구성 신호다 (종목 전환·RT 캐치업 병합). 비운 뒤 스냅샷을
    // 다시 가져와야 병합된 과거 봉(캐치업)까지 화면에 반영된다 — 라이브만으로는 구멍이 남는다
    if (panes.some((pane) => pane.symbol === sh || pane.data2 === sh)) seedSymbol(sh);
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
        if (isTail) {
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
          pane.candleSeries.update(pricePoint(cache.bars.get(t), pane.barDraw));
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
  }
  const ind = MiraeLayers.barIndFromPayload(p);
  ind.fx3 = Fx3Layers.parseFx3(p.fx3);
  ind.pgap = PgapLayers.parsePgap(p.pgap);
  ind.rgap = PgapLayers.parseSide(p.rgap);
  ind.mgap = PgapLayers.parseSide(p.mgap);
  ind.ymae = YmaeLayers.parseYmae(p.ymae);
  ind.sniper = SniperLayers.parseSniper(p.sniper);
  ind.pvc = PvcLayers.parsePvc(p.pvc);
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

  // 지표 표시는 이 종목을 보는 칸의 활성 렌더러만 담당한다 (시딩 중인 칸은 건너뛴다 —
  // 비운 차트에 라이브 지표를 그리는 것도 같은 클래스의 오염이다)
  const ctx = ctxFor(cache);
  const seedingNow = seedInflight.has(sh);
  for (const pane of panes) {
    if (pane.symbol !== sh || seedingNow) continue;
    for (const [indId, { handle }] of pane.active) {
      if (indId === "fx_data2" && pane.data2 && pane.data2 !== sh) continue;
      // 지표 시리즈도 칸별로 격리한다 — 한 지표의 실패가 다른 지표·칸으로 번지지 않게.
      try {
        // 정정(과거 시각) 틱에는 지표 시리즈의 update()도 같은 throw가 나므로
        // (mirae-layers reg/score/band/mktband, sma-layers — 2026-09-30 실측),
        // 캐시에서 전체를 다시 그리는 시딩 경로로 처리한다. 꼬리는 기존처럼 라이브 반영.
        if (isTail) handle.applyLive(p, ctx);
        else handle.applySeed(ctx);
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
      if (isTail) entry.handle.applyLive(p, ctx);
      else entry.handle.applySeed(ctx);
    } catch (err) {
      console.error(`[${sh}] Data2 반영 실패 t=${t}`, err);
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
  pane.candleSeries.setData(rows.map((r) => pricePoint(r, pane.barDraw)));
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
  // pane-sync getLength의 시리즈 길이(봉 + whitespace)와 같은 기준 — 반드시 여기서 갱신한다
  cache.wsCount = rows.length - bars.length;
  // pane-sync getTimes의 시각 기준 — 시리즈(봉 + whitespace)의 인덱스와 1:1로 맞닿아야 하므로
  // setData에 넘길 같은 rows에서 만든다. 이후 라이브 꼬리는 noteBar가 함께 민다.
  cache.seriesTimes = rows.map((r) => r.time);
  const ctx = ctxFor(cache);
  const times = cache.seriesTimes;
  // setVisibleLogicalRange는 다음 프레임에야 차트에 반영된다. 같은 루프에서
  // getVisibleLogicalRange를 읽으면 방금 넣은 창이 아니라 이전 창이 나온다.
  // 그래서 적용한 오른쪽 끝 시각과 px 봉 간격을 변수로 넘긴다.
  let heldSpacing = 0;
  let heldRight = 0;
  for (const pane of panes) {
    if (pane.symbol !== shcode) continue;
    // 시딩 적용(setData + 초기 범위 지정)은 프로그램적 변경 — 그 칸이 자기 최신 범위로
    // 돌아가며 내는 범위 이벤트가 다른 칸의 탐색 위치를 빼앗지 않게 뮤트한다
    mutePaneRangeForSeeding(pane);
    pane.candleSeries.setData(rows.map((r) => pricePoint(r, pane.barDraw)));
    const width = pane.chartEl.clientWidth;
    if (!(heldSpacing > 0)) {
      for (const q of panes) {
        if (q === pane || !q.symbol) continue;
        const lr = q.chart.timeScale().getVisibleLogicalRange();
        const qTimes = feed.get(q.symbol)?.seriesTimes;
        const qw = q.chartEl.clientWidth;
        if (lr && lr.to > lr.from && qw > 0
            && Array.isArray(qTimes) && qTimes.length > 0) {
          heldSpacing = qw / (lr.to - lr.from + 1);
          heldRight = PaneSync.timeAt(qTimes, lr.to);
          break;
        }
      }
    }
    if (heldSpacing > 0 && times.length && width > 0) {
      const toM = PaneSync.logicalAt(times, heldRight);
      const fromM = toM - (width / heldSpacing - 1);
      pane.chart.timeScale().setVisibleLogicalRange({ from: fromM, to: toM });
    } else if (rows.length) {
      const from = Math.max(0, rows.length - INITIAL_VISIBLE_BARS);
      const to = rows.length - 1;
      pane.chart.timeScale().setVisibleLogicalRange({ from, to });
      if (width > 0 && times.length) {
        heldSpacing = width / (to - from + 1);
        heldRight = PaneSync.timeAt(times, to);
      }
    }
    for (const { handle } of pane.active.values()) handle.applySeed(ctx);
  }
  refreshData2(shcode);
  restoreHeaderBadges();
}

// 참조 종목 시딩이 끝나면, 그 종목을 Data2로 보는 칸의 비율선을 다시 그린다.
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
      const gapsSec = []; // 구멍 구간 누적 (µs → 초) — 페이지 경계 구멍은 엔진이 경계 쌍까지 검사해 준다
      let back = 0;
      for (let pages = 0; pages < 80; pages++) {
        const res = await fetch(`/api/chart?shcode=${encodeURIComponent(shcode)}&back_index=${back}`);
        if (!res.ok) return;
        const data = await res.json();
        const p = data.payload;
        if (p == null || !Array.isArray(p.bars)) return;
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
          const pvcrows = p.pvc ?? [];
          all.push({
            time: Number(t) / 1e6, open: o, high: h, low: l, close: c,
            ind: inds[ri], fx3: fx3rows[ri], pgap: pgrows[ri], rgap: rgrows[ri],
            mgap: mgrows[ri], ymae: ymrows[ri], sniper: snrows[ri], pvc: pvcrows[ri],
          });
        }
        // ⑥⑦ 갱신·저장 이벤트 (희소) — 페이지 경계에서 중복되지 않게 시각으로 모은다
        for (const e of p.mem ?? []) memEvents.push(e);
        for (const e of p.pst ?? []) pstEvents.push(e);
        for (const g of p.gaps ?? []) gapsSec.push([Number(g[0]) / 1e6, Number(g[1]) / 1e6]);
        if (!p.next_back_index) break;
        back = p.next_back_index;
      }
      if (cache.seedToken !== seedTok) continue; // 시딩 중 리셋 — 새 기준으로 다시 가져온다
      feed.reset(cache); // 시딩 중 라이브로 쌓인 봉과 혼합하지 않는다 (스냅샷 기준으로 다시 쌓음)
      cache.gaps = gapsSec; // 수신·보관만 한다 — 표시용 채움은 균일 분 그리드(엔진 gaps의
                            // 상위 집합)가 담당하므로 표시 경로는 읽지 않는다 (gaps.js 헤더 참조).
                            // reset 이후에 넣어야 지워지지 않는다
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
        ind.fx3 = Fx3Layers.parseFx3(b.fx3);
        ind.pgap = PgapLayers.parsePgap(b.pgap);
        ind.rgap = PgapLayers.parseSide(b.rgap);
        ind.mgap = PgapLayers.parseSide(b.mgap);
        ind.ymae = YmaeLayers.parseYmae(b.ymae);
        ind.sniper = SniperLayers.parseSniper(b.sniper);
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

      renderSymbolPanes(shcode);
      return;
    } catch { /* 시딩 실패는 라이브 스트림으로 진행 */ }
    return;
  }
}

// 지표 매니페스트를 저장하고 모든 칸의 지표 패널 트리를 다시 만든다.
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
    enqueueLive(data.message ?? {});
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
      panelOpen: pane.panelOpen, // 칸별 지표 패널 접기/펼치기
      data2: pane.data2 || "",
      candles: pane.barStyle !== "none",
      barStyle: pane.barStyle || "candle",
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

// 기준 창: 봉이 두 개 이상 보이는 첫 칸. 1봉으로 찌그러진 창은 건너뛴다.
function captureAlignView() {
  for (const pane of panes) {
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

// 모든 칸을 그 오른쪽 끝 시각과 px 봉 간격에 맞춘다.
// settle이면 라이브러리가 범위를 고쳐 보내는 늦은 에코가 다시 퍼지지 않게 잠시 막는다.
function applyAlignView(view, settle) {
  if (!view || !(view.spacingPx > 0)) return;
  for (const pane of panes) {
    const times = feed.get(pane.symbol)?.seriesTimes;
    const width = pane.chartEl?.clientWidth ?? 0;
    if (!times?.length || !(width > 0)) continue;
    const toM = PaneSync.logicalAt(times, view.rightTime);
    const fromM = toM - (width / view.spacingPx - 1);
    if (settle) mutePaneRangeForSeeding(pane);
    else mutePaneRange(pane);
    pane.chart.timeScale().setVisibleLogicalRange({ from: fromM, to: toM });
  }
}

function alignAllPanes(settle) {
  applyAlignView(captureAlignView(), settle);
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
    setPanelOpen(pane, spec.panelOpen); // 구 화면틀은 parse가 기본값(열림)으로 정규화한다
    setBarStyle(pane, spec.barStyle || (spec.candles === false ? "none" : "candle"));
    for (const ind of spec.indicators) activateIndicator(pane, ind.id, ind.layers);
    if (spec.data2) jobs.push(setPaneData2(pane, spec.data2).catch(() => {}));
    buildPaneTools(pane);
    if (spec.symbol) jobs.push(selectPaneSymbol(pane, spec.symbol).catch(() => {}));
  }
  normalizeHeights();
  rebuildResizeBars();
  updateBadgeVisibility();
  await Promise.all(jobs);
  await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
  const view = captureAlignView();
  applyAlignView(view, true);
  // 라이브러리가 칸마다 범위를 한 번 고친 뒤에, 같은 기준으로 다시 덮는다.
  await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
  applyAlignView(view, true);
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

// 칸 추가. 지금 보고 있는 칸의 종목·지표·캔들·참조를 그대로 복사한다.
async function addPane() {
  const src = viewedPane();
  const newFrac = 1 / (panes.length + 1);
  const scale = 1 - newFrac; // 기존 칸 heightFrac 합은 ≈1
  for (const p of panes) {
    p.heightFrac *= scale;
    syncPaneSize(p);
  }
  const pane = createPane(newFrac);
  rebuildResizeBars();
  if (!src?.symbol) return;
  await selectPaneSymbol(pane, src.symbol, src.symName);
  if (pane.symbol !== src.symbol) return;
  for (const [id, entry] of src.active) activateIndicator(pane, id, entry.layers);
  if (src.data2) await setPaneData2(pane, src.data2);
  setBarStyle(pane, src.barStyle || "candle");
  buildPaneTools(pane);
  currentPane = pane;
}

document.getElementById("pane-add").onclick = addPane;
document.getElementById("ws-save").onclick = saveWorkspace;
document.getElementById("ws-load").onclick = loadWorkspace;

// 창 크기가 바뀌면 각 칸의 차트 크기를 다시 맞춘다 (기준은 차트 호스트 — syncPaneSize와 같다)
addEventListener("resize", () => {
  for (const pane of panes) syncPaneSize(pane);
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
