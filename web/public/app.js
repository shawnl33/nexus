// 대시보드 프론트엔드 (계획서 §18).
// C가 계산한 값을 표시만 한다. 지표·점수를 재계산하지 않는다.
// 보조지표 렌더링은 원본 YesLanguage V16의 Plot 시맨틱을 따른다 (mirae-layers.js).

"use strict";

const WS_URL = `ws://${location.host}/ws`;
const bars = new Map(); // time(sec) → candle
let generation = 0;     // 종목 전환 시 올려 늦은 응답을 폐기 (계획서 §18)
let tickRaw = 5;        // raw 단위 틱 크기 (엔진 tick 키가 갱신; 선물 5, 주식 100)

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

const chart = LightweightCharts.createChart(document.getElementById("chart"), {
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
});
const candleSeries = chart.addCandlestickSeries({
  upColor: "#ef5350", downColor: "#2962ff",
  borderUpColor: "#ef5350", borderDownColor: "#2962ff",
  wickUpColor: "#ef5350", wickDownColor: "#2962ff",
});

// ---- 보조지표 레이어 (원본 V16 Plot 시맨틱 — mirae-layers.js) ----
// ② 회귀선 + ⑤ 매매 상태 덧선: 한 커스텀 시리즈가 2패스로 그린다
const regLineSeries = chart.addCustomSeries(MiraeLayers.createRegLinePaneView(), {});
// ① 통합 점수 막대: 아래 보조 칸의 0 기준 세로 색 막대
const scoreSeries = chart.addCustomSeries(MiraeLayers.createScoreBarPaneView(), { priceScaleId: "score" });
chart.priceScale("score").applyOptions({ scaleMargins: { top: 0.84, bottom: 0.02 } });
// ④ 과거 채점 결과 띠: 회귀선 바로 아래 가로선
const bandSeries = chart.addCustomSeries(MiraeLayers.createResultBandPaneView(), {});
// ⑥⑦ 방향 기억·지속 사진: 수평 계단선. 오래된 선은 현재 가격과 멀 수 있어 자동 스케일에서 제외
const memSeries = chart.addCustomSeries(MiraeLayers.createStepLinesPaneView("mem"), { autoscaleInfoProvider: () => null });
const pstSeries = chart.addCustomSeries(MiraeLayers.createStepLinesPaneView("pst"), { autoscaleInfoProvider: () => null });
// ⑧ 마켓 밴드: 중심선(단계색)은 커스텀, 밴드 4선은 고정색 라인 (원본 Plot52~55)
const mktCenterSeries = chart.addCustomSeries(MiraeLayers.createMarketCenterPaneView(), {});
const mktU1 = chart.addLineSeries({ color: "rgb(255,120,120)", lineWidth: 1, priceLineVisible: false, lastValueVisible: false });
const mktL1 = chart.addLineSeries({ color: "rgb(120,150,255)", lineWidth: 1, priceLineVisible: false, lastValueVisible: false });
const mktU2 = chart.addLineSeries({ color: "rgb(255,0,0)", lineWidth: 1, priceLineVisible: false, lastValueVisible: false });
const mktL2 = chart.addLineSeries({ color: "rgb(0,0,255)", lineWidth: 1, priceLineVisible: false, lastValueVisible: false });
// ③ 미래 목표 광선: 마지막 봉 기준 수평 광선 9개 (목표 3 + 범위 6)
const futureRays = MiraeLayers.createFutureRaysPrimitive();
candleSeries.attachPrimitive(futureRays);

// 봉별 지표 캐시: ④ 결과 띠의 10봉 전 조회 등에 쓴다 (시딩·라이브 공통)
const barInd = new Map(); // time → { predDir[3], regValid, r2, regFlat, finalValid, finalState }
const barSeq = [];        // 시각 오름차순 목록
const barPos = new Map(); // time → barSeq 인덱스
let lastMktCenter = NaN;  // ⑧ 마켓중심기울기 = 중심 − 이전중심
let mktBandOn = false;    // ⑧ 마켓 밴드 표시 (원본 입력 마켓밴드표시; 기본 숨김, 헤더 토글)
let bandOn = true;        // ④ 결과 띠 표시 (원본 입력 과거예측표시=1 기본 켜짐, 헤더 토글)

// ④ 결과 띠 다시 그리기 — 토글 시 barInd 캐시에서 전체 복원/제거
function rebuildBand() {
  if (!bandOn) {
    bandSeries.setData([]);
    return;
  }
  const data = [];
  for (let i = 0; i < barSeq.length; i++) {
    const ind = barInd.get(barSeq[i]);
    if (!ind || !Number.isFinite(ind.regFlat)) continue;
    const src = i >= 10 ? barInd.get(barSeq[i - 10]) : undefined;
    data.push({ time: barSeq[i], value: ind.regFlat - MiraeLayers.bandOffset(tickRaw),
                c: MiraeLayers.bandColor(src, sameSession(src, ind)) });
  }
  bandSeries.setData(data);
}

// ⑧ 마켓 밴드 다시 그리기 — 토글 시 barInd 캐시에서 전체 복원/제거
function rebuildMktBand() {
  if (!mktBandOn) {
    for (const s of [mktCenterSeries, mktU1, mktL1, mktU2, mktL2]) s.setData([]);
    return;
  }
  const cData = [], u1 = [], l1 = [], u2 = [], l2 = [];
  let prev = NaN;
  for (const t of barSeq) {
    const ind = barInd.get(t);
    const bar = bars.get(t);
    if (!ind || !ind.mktValid || !bar) continue;
    const [center, u1v, l1v, u2v, l2v] = ind.mkt;
    cData.push({ time: t, value: center,
                 stage: MiraeLayers.mktStage(prev, center, bar.close, ind.regFlat, u1v, tickRaw) });
    prev = center;
    u1.push({ time: t, value: u1v });
    l1.push({ time: t, value: l1v });
    u2.push({ time: t, value: u2v });
    l2.push({ time: t, value: l2v });
  }
  lastMktCenter = prev;
  mktCenterSeries.setData(cData);
  mktU1.setData(u1);
  mktL1.setData(l1);
  mktU2.setData(u2);
  mktL2.setData(l2);
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

function resetIndicators() {
  bars.clear();
  barInd.clear();
  barSeq.length = 0;
  barPos.clear();
  lastMktCenter = NaN;
  tickRaw = 5;
  candleSeries.setData([]);
  for (const s of [regLineSeries, scoreSeries, bandSeries, memSeries, pstSeries,
                   mktCenterSeries, mktU1, mktL1, mktU2, mktL2]) s.setData([]);
  futureRays.clear();
}

const el = {
  wsState: document.getElementById("ws-state"),
  score: document.getElementById("score"),
  reg: document.getElementById("reg"),
  pred: document.getElementById("pred"),
  ob: document.getElementById("ob"),
  final: document.getElementById("final"),
  wsName: document.getElementById("ws-name"),
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
    candleSeries.update(bars.get(t));
  }
  const pos = barPos.get(t);
  const ind = MiraeLayers.barIndFromPayload(p);
  barInd.set(t, ind);
  if (typeof p.tick === "number" && Number.isFinite(p.tick) && p.tick > 0) tickRaw = p.tick;

  // ② 회귀선 (reg_flat) + ⑤ 매매 상태 덧선. reg_valid==0이면 갭
  if (ind.regValid && Number.isFinite(ind.regFlat)) {
    regLineSeries.update({
      time: t, value: ind.regFlat,
      score: Number.isFinite(p.score) ? p.score : 0, r2: ind.r2,
      finalState: ind.finalValid ? ind.finalState : 0,
    });
    el.reg.textContent = `회귀선 ${fmtPrice(p.reg_line)} (R² ${ind.r2.toFixed(2)})`;
    el.reg.className = "badge ok";
  } else {
    regLineSeries.update({ time: t });
    el.reg.textContent = "회귀: 워밍업";
    el.reg.className = "badge";
  }

  // ③ 미래 목표 광선: 매 봉 값이 바뀌면 광선이 새 값으로 이동한다 (이력 없음)
  const preds = p.pred ?? [];
  if (ind.regValid && preds.length === 3 && preds.every(Number.isFinite)) {
    futureRays.set({
      time: t, prevTime: pos > 0 ? barSeq[pos - 1] : t - 60,
      pred: preds, predDir: ind.predDir, r2: ind.r2,
      resid: p.resid ?? 0, pvol: p.pvol ?? 0,
    });
    el.pred.textContent = `예측 ${preds.map((v) => fmtPrice(v)).join(" / ")}`;
  } else {
    futureRays.clear();
  }

  // ① 통합 점수 막대
  if (typeof p.score === "number") {
    el.score.textContent = String(p.score);
    el.score.style.color = scoreTextColor(p.score);
    scoreSeries.update({ time: t, value: p.score });
  }

  // ④ 과거 채점 결과 띠: reg_flat − tick×4, 색은 10봉 전 예측방향2·신뢰도 기준
  if (bandOn && Number.isFinite(ind.regFlat)) {
    const src = pos !== undefined && pos >= 10 ? barInd.get(barSeq[pos - 10]) : undefined;
    bandSeries.update({ time: t, value: ind.regFlat - MiraeLayers.bandOffset(tickRaw),
                        c: MiraeLayers.bandColor(src, sameSession(src, ind)) });
  }

  // ⑧ 마켓 밴드: [valid, center, u1, l1, u2, l2] — 기본 숨김(헤더 토글로 표시)
  if (Array.isArray(p.mkt) && p.mkt[0] === 1) {
    const [, center, u1, l1, u2, l2] = p.mkt;
    if (mktBandOn) {
      mktCenterSeries.update({ time: t, value: center, stage: MiraeLayers.mktStage(lastMktCenter, center, c, ind.regFlat, u1, tickRaw) });
      mktU1.update({ time: t, value: u1 });
      mktL1.update({ time: t, value: l1 });
      mktU2.update({ time: t, value: u2 });
      mktL2.update({ time: t, value: l2 });
    }
    lastMktCenter = center;
  } else if (mktBandOn) {
    mktCenterSeries.update({ time: t });
    mktU1.update({ time: t });
    mktL1.update({ time: t });
    mktU2.update({ time: t });
    mktL2.update({ time: t });
  }

  // ⑥ 방향 기억: [valid, updated, dir, price, t1..3, u1..3, l1..3]
  // 세트 값은 확정 봉 사이 유지된다. 범위선은 갱신 봉에 숨기고, 이후엔 5봉 이탈 규칙을 적용한다.
  if (Array.isArray(p.mem)) {
    if (p.mem[0] === 1) {
      const updated = p.mem[1] === 1;
      const flags = updated ? { showU: false, showL: false }
        : MiraeLayers.rangeFlags(recentBars(pos, 5), p.mem[6]);
      memSeries.update({
        time: t, value: p.mem[5], dir: p.mem[2], price: p.mem[3],
        t: p.mem.slice(4, 7), u: p.mem.slice(7, 10), l: p.mem.slice(10, 13),
        showU: flags.showU, showL: flags.showL, upd: updated,
      });
    } else {
      memSeries.update({ time: t });
    }
  }

  // ⑦ 지속 사진: [saved, valid, dir, t1..3, u1..3, l1..3] — 저장 세트가 다음 저장까지 유지
  if (Array.isArray(p.pst)) {
    if (p.pst[1] === 1) {
      const flags = MiraeLayers.rangeFlags(recentBars(pos, 5), p.pst[5]);
      pstSeries.update({
        time: t, value: p.pst[4], dir: p.pst[2],
        t: p.pst.slice(3, 6), u: p.pst.slice(6, 9), l: p.pst.slice(9, 12),
        showU: flags.showU, showL: flags.showL,
      });
    } else {
      pstSeries.update({ time: t });
    }
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
    candleSeries.setData(dedup);

    // 봉별 지표 복원: 스냅샷의 ind 배열로 과거 구간의 보조지표를 다시 그린다
    const regData = [];
    const scoreData = [];
    for (let i = 0; i < dedup.length; i++) {
      const b = dedup[i];
      const d = MiraeLayers.parseInd(b.ind);
      if (!d) continue;
      if (Number.isFinite(d.tick) && d.tick > 0) tickRaw = d.tick;
      barInd.set(b.time, MiraeLayers.barIndFromInd(d));
      if (Number.isFinite(d.score)) {
        scoreData.push({ time: b.time, value: d.score });
      }
      if (d.regValid && Number.isFinite(d.regFlat)) {
        regData.push({ time: b.time, value: d.regFlat,
                       score: Number.isFinite(d.score) ? d.score : 0, r2: d.r2,
                       finalState: d.finalValid ? d.finalState : 0 });
      }
    }
    regLineSeries.setData(regData);
    scoreSeries.setData(scoreData);
    rebuildBand();    // ④ 결과 띠 (기본 켜짐; 꺼져 있으면 제거)
    rebuildMktBand(); // ⑧ 마켓 밴드 (기본 숨김; 켜져 있으면 barInd 캐시에서 복원)

    // ⑥⑦ 이벤트 복원: 세트가 다음 갱신/저장까지 유지되는 수평 계단선
    memSeries.setData(buildMemItems(dedup, memEvents));
    pstSeries.setData(buildPstItems(dedup, pstEvents));

    // 마지막 봉의 값으로 배지·미래 목표 광선을 복원한다
    const lastBar = dedup[dedup.length - 1];
    const last = lastBar ? MiraeLayers.parseInd(lastBar.ind) : null;
    if (last) {
      el.score.textContent = String(last.score);
      el.score.style.color = scoreTextColor(last.score);
      updateFinalBadge(last.finalValid, last.finalState);
      if (last.regValid) {
        el.reg.textContent = `회귀선 ${fmtPrice(last.regLine)} (R² ${last.r2.toFixed(2)})`;
        el.reg.className = "badge ok";
        if (last.pred.every(Number.isFinite)) {
          el.pred.textContent = `예측 ${last.pred.map((v) => fmtPrice(v)).join(" / ")}`;
          futureRays.set({
            time: lastBar.time,
            prevTime: dedup.length > 1 ? dedup[dedup.length - 2].time : lastBar.time - 60,
            pred: last.pred, predDir: last.predDir, r2: last.r2,
            resid: last.resid, pvol: last.pvol,
          });
        }
      }
      if (last.obValid) {
        el.ob.textContent = `호가 ${last.obScore.toFixed(1)}`;
        el.ob.className = last.obScore > 0 ? "badge ok" : "badge err";
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

// ⑧ 마켓 밴드 토글 (기본 숨김 — 원본 입력 마켓밴드표시=0에 해당)
const mktbandBtn = document.getElementById("mktband-toggle");
mktbandBtn.onclick = () => {
  mktBandOn = !mktBandOn;
  mktbandBtn.classList.toggle("on", mktBandOn);
  rebuildMktBand();
};

// ④ 결과 띠 토글 (기본 켜짐 — 원본 입력 과거예측표시=1에 해당)
const bandBtn = document.getElementById("band-toggle");
bandBtn.onclick = () => {
  bandOn = !bandOn;
  bandBtn.classList.toggle("on", bandOn);
  rebuildBand();
};

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

seedChart();
connect();
