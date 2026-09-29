// 미래곡선 렌더링 레이어(mirae-layers.js) 단위 테스트.
// 스냅샷 ind 배열 파싱(특히 [21..25] 확장 구간)과 barInd 확장, 원본 V16 표시 규칙을 검증한다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/mirae-layers.js");
const M = globalThis.MiraeLayers;

// ind 레이아웃 (docs/display_payload.md): [0]closed [1]reg_valid [2]reg_line [3]reg_r2
// [4..6]pred [7]score [8]ob_valid [9]ob_score [10]resid [11]pvol [12..14]pred_dir
// [15..20]mkt [21]final_valid [22]final_dir [23]final_state [24]final_strength [25]reg_flat
function makeInd(over = {}) {
  const ind = [1, 1, 34550, 0.75, 34600, 34620, 34640, 3, 1, 1.5, 12.5, 30, 1, 1, -1,
               1, 34500, 34540, 34460, 34580, 34420, 1, 1, 2, 80, 34550];
  for (const [k, v] of Object.entries(over)) ind[Number(k)] = v;
  return ind;
}

test("parseInd: ind 배열 [21..25] final/reg_flat 파싱", () => {
  const d = M.parseInd(makeInd());
  assert.equal(d.finalValid, true);
  assert.equal(d.finalDir, 1);
  assert.equal(d.finalState, 2);
  assert.equal(d.finalStrength, 80);
  assert.equal(d.regFlat, 34550);
  assert.equal(d.regValid, true);
  assert.equal(d.regLine, 34550);
  assert.equal(d.r2, 0.75);
  assert.deepEqual(d.pred, [34600, 34620, 34640]);
  assert.deepEqual(d.predDir, [1, 1, -1]);
  assert.equal(d.score, 3);
  assert.equal(d.mktValid, true);
  assert.deepEqual(d.mkt, [34500, 34540, 34460, 34580, 34420]);
});

test("parseInd: 무효 플래그와 비수치 값", () => {
  const d = M.parseInd(makeInd({ 1: 0, 21: 0, 23: -1, 25: undefined }));
  assert.equal(d.regValid, false);
  assert.equal(d.finalValid, false);
  assert.equal(d.finalState, -1);
  assert.ok(Number.isNaN(d.regFlat));
  assert.equal(M.parseInd(undefined), null);
  assert.equal(M.parseInd("x"), null);
});

test("barIndFromInd: r2·reg_flat·final_state 확장 필드 유지", () => {
  const e = M.barIndFromInd(M.parseInd(makeInd({ 3: 0.55, 23: -2, 25: 34551 })));
  assert.deepEqual(e.predDir, [1, 1, -1]);
  assert.equal(e.regValid, true);
  assert.equal(e.r2, 0.55);
  assert.equal(e.regFlat, 34551);
  assert.equal(e.finalValid, true);
  assert.equal(e.finalState, -2);
});

test("barIndFromPayload: 라이브 status 페이로드에서 같은 형태로 변환", () => {
  const e = M.barIndFromPayload({
    reg_valid: 1, reg_r2: 0.42, reg_flat: 34550,
    pred_dir: [0, -1, 1], final: [1, -1, -2, 70],
  });
  assert.deepEqual(e.predDir, [0, -1, 1]);
  assert.equal(e.regValid, true);
  assert.equal(e.r2, 0.42);
  assert.equal(e.regFlat, 34550);
  assert.equal(e.finalValid, true);
  assert.equal(e.finalState, -2);
  // final 키가 없으면 무효로 둔다
  const bare = M.barIndFromPayload({ reg_valid: 0 });
  assert.equal(bare.finalValid, false);
  assert.equal(bare.finalState, 0);
});

test("scoreColor: 7단계 점수색 (원본 단계화_1분_색상)", () => {
  assert.equal(M.scoreColor(5), "rgb(220,0,0)");   // >4
  assert.equal(M.scoreColor(3), "rgb(255,100,70)"); // >2
  assert.equal(M.scoreColor(1), "rgb(255,185,185)"); // >0
  assert.equal(M.scoreColor(-5), "rgb(0,0,180)");   // <-4
  assert.equal(M.scoreColor(-3), "rgb(60,130,255)"); // <-2
  assert.equal(M.scoreColor(-1), "rgb(180,210,255)"); // <0
  assert.equal(M.scoreColor(0), "rgb(150,150,150)");
  // 경계값: 4는 >4가 아니라 >2 구간, -4는 <-4가 아니라 <-2 구간
  assert.equal(M.scoreColor(4), "rgb(255,100,70)");
  assert.equal(M.scoreColor(-4), "rgb(60,130,255)");
});

test("regWidth: R² 0.70/0.40 경계", () => {
  assert.equal(M.regWidth(0.7), 6);
  assert.equal(M.regWidth(0.85), 6);
  assert.equal(M.regWidth(0.4), 4);
  assert.equal(M.regWidth(0.69), 4);
  assert.equal(M.regWidth(0.39), 2);
});

test("tradeStyle: 매매 상태 덧선 (±2 굵기5, ±1 굵기2, 0은 gap)", () => {
  assert.deepEqual(M.tradeStyle(2), { color: "rgb(255,0,0)", width: 5 });
  assert.deepEqual(M.tradeStyle(1), { color: "rgb(255,128,0)", width: 2 });
  assert.deepEqual(M.tradeStyle(-2), { color: "rgb(0,0,255)", width: 5 });
  assert.deepEqual(M.tradeStyle(-1), { color: "rgb(0,160,200)", width: 2 });
  assert.equal(M.tradeStyle(0), null);
});

test("bandColor: 10봉 전 방향2·신뢰도 기준 (원본 MTF검증색상2)", () => {
  // 10봉 전 무효/없음 → 회색
  assert.equal(M.bandColor(undefined), "rgb(205,205,205)");
  assert.equal(M.bandColor({ regValid: false, predDir: [0, 1, 0], r2: 0.9 }), "rgb(205,205,205)");
  // 방향>0: R²≥0.40 진한 빨강, 미만 연한 빨강
  assert.equal(M.bandColor({ regValid: true, predDir: [0, 1, 0], r2: 0.4 }), "rgb(255,0,0)");
  assert.equal(M.bandColor({ regValid: true, predDir: [0, 1, 0], r2: 0.39 }), "rgb(255,145,145)");
  // 방향<0: 파랑 계열
  assert.equal(M.bandColor({ regValid: true, predDir: [0, -1, 0], r2: 0.9 }), "rgb(0,0,255)");
  assert.equal(M.bandColor({ regValid: true, predDir: [0, -1, 0], r2: 0.1 }), "rgb(145,170,255)");
  // 방향 0 → 회색
  assert.equal(M.bandColor({ regValid: true, predDir: [1, 0, 1], r2: 0.9 }), "rgb(150,150,150)");
});

test("bandColor: 세션 가드 — 10봉 전이 다른 세션이면 무효(회색) (원본 v16:224-226)", () => {
  const up = { regValid: true, predDir: [0, 1, 0], r2: 0.9 };
  assert.equal(M.bandColor(up, false), "rgb(205,205,205)"); // 세션 경계 넘음
  assert.equal(M.bandColor(up, true), "rgb(255,0,0)");      // 같은 세션
});

test("bandOffset: tick×4 (원본 PriceScale×4)", () => {
  assert.equal(M.bandOffset(5), 20);   // 선물 0.05pt×100
  assert.equal(M.bandOffset(100), 400); // 주식 1원×100
});

test("rangeFlags: 최근 5봉 H/L이 목표3를 완전히 벗어나야 숨김", () => {
  const mk = (high, low) => ({ high, low });
  const below5 = [mk(90, 80), mk(91, 81), mk(92, 82), mk(93, 83), mk(94, 84)];
  assert.deepEqual(M.rangeFlags(below5, 100), { showU: false, showL: true });
  const above5 = [mk(110, 101), mk(111, 102), mk(112, 103), mk(113, 104), mk(114, 105)];
  assert.deepEqual(M.rangeFlags(above5, 100), { showU: true, showL: false });
  // 한 봉이라도 걸치면 둘 다 표시
  const mixed = [...below5.slice(1), mk(120, 70)];
  assert.deepEqual(M.rangeFlags(mixed, 100), { showU: true, showL: true });
  // 경계값: H == 목표3는 "완전 아래"가 아니다
  const touch = [...below5.slice(1), mk(100, 90)];
  assert.deepEqual(M.rangeFlags(touch, 100), { showU: true, showL: true });
  // 5봉 미만이면 표시 유지
  assert.deepEqual(M.rangeFlags(below5.slice(0, 4), 100), { showU: true, showL: true });
  assert.deepEqual(M.rangeFlags([], NaN), { showU: true, showL: true });
});

test("mktStage: 기울기·종가 위치·회귀선 조합의 7단계", () => {
  // 기울기>0, C>중심, C>회귀선: 위치강도 33/66 기준 1/2/3 (조건 충족 시 최소 1)
  // basis = max(5, |u1-center|) = max(5, 40) = 40
  assert.equal(M.mktStage(99, 100, 108, 90, 140), 1);  // 강도 20 <33
  assert.equal(M.mktStage(99, 100, 116, 90, 140), 2);  // 강도 40
  assert.equal(M.mktStage(99, 100, 136, 90, 140), 3);  // 강도 90
  // 기울기<0, C<중심, C<회귀선: -1/-2/-3
  assert.equal(M.mktStage(101, 100, 92, 200, 140), -1);
  assert.equal(M.mktStage(101, 100, 84, 200, 140), -2);
  assert.equal(M.mktStage(101, 100, 64, 200, 140), -3);
  // 조건 불충족 → 0: 기울기 0 / 종가가 회귀선 아래 / 종가가 중심 아래
  assert.equal(M.mktStage(100, 100, 150, 90, 140), 0);
  assert.equal(M.mktStage(99, 100, 150, 160, 140), 0);
  assert.equal(M.mktStage(99, 100, 95, 90, 140), 0);
  // 밴드 폭이 0이면 틱(raw 5)이 하한
  assert.equal(M.mktStage(99, 100, 104, 90, 100), 3); // 강도 4/5*100=80
  // tick 인자: 주식(100 raw)이면 하한이 100
  assert.equal(M.mktStage(99, 100, 104, 90, 100, 100), 1); // 강도 4/100*100=4
  assert.equal(M.mktStage(99, 100, 104, 90, 100, 5), 3);   // 선물과 동일
});

test("mktStageColor: 단계별 색", () => {
  assert.equal(M.mktStageColor(3), "rgb(220,0,0)");
  assert.equal(M.mktStageColor(2), "rgb(255,100,70)");
  assert.equal(M.mktStageColor(1), "rgb(255,185,185)");
  assert.equal(M.mktStageColor(-3), "rgb(0,0,180)");
  assert.equal(M.mktStageColor(-2), "rgb(60,130,255)");
  assert.equal(M.mktStageColor(-1), "rgb(180,210,255)");
  assert.equal(M.mktStageColor(0), "rgb(120,120,120)");
});

test("커스텀 시리즈 pane view 기본 계약", () => {
  for (const view of [M.createRegLinePaneView(), M.createResultBandPaneView(),
                      M.createMarketCenterPaneView(), M.createStepLinesPaneView("mem"),
                      M.createStepLinesPaneView("pst")]) {
    assert.equal(view.isWhitespace({ time: 1 }), true);            // value 없음 → whitespace(갭)
    assert.equal(view.isWhitespace({ time: 1, value: 100 }), false);
    assert.deepEqual(view.priceValueBuilder({ value: 100 }), [100]);
    assert.equal(typeof view.renderer().draw, "function");
  }
  const score = M.createScoreBarPaneView();
  assert.deepEqual(score.priceValueBuilder({ value: -3 }), [0, -3]); // 0 기준선이 스케일에 포함
  const rays = M.createFutureRaysPrimitive();
  assert.equal(rays.autoscaleInfo(), null); // 광선은 자동 스케일에 영향 없음
});

// ---- MiraeRenderer 패널 렌더러 계약 (가짜 차트/시리즈로 검증) ----

function fakeSeries() {
  return {
    data: null, updates: [],
    setData(d) { this.data = d; },
    update(d) { this.updates.push(d); },
    applyOptions() {},
  };
}
function fakeChart() {
  const made = [];
  const removed = [];
  return {
    made, removed,
    addCustomSeries() { const s = fakeSeries(); made.push(s); return s; },
    addLineSeries() { const s = fakeSeries(); made.push(s); return s; },
    removeSeries(s) { removed.push(s); const i = made.indexOf(s); if (i >= 0) made.splice(i, 1); },
    priceScale() { return { applyOptions() {} }; },
  };
}
function fakeCandleSeries() {
  return {
    primitives: [],
    attachPrimitive(p) { this.primitives.push(p); },
    detachPrimitive(p) { const i = this.primitives.indexOf(p); if (i >= 0) this.primitives.splice(i, 1); },
  };
}
function makeCtx(rows) {
  // rows: [t, indEntry] — barInd/barSeq/barPos를 같은 형태로 구성
  const barInd = new Map(rows);
  const barSeq = rows.map(([t]) => t);
  const barPos = new Map(rows.map(([t], i) => [t, i]));
  return {
    bars: new Map(rows.map(([t]) => [t, { time: t, open: 1, high: 2, low: 0, close: 1 }])),
    barInd, barSeq, barPos,
    tickRaw: () => 5,
    sameSession: () => true,
    recentBars: () => [],
  };
}

test("MiraeRenderer: createHandle이 계약 메서드를 가진 handle을 만든다", () => {
  const chart = fakeChart();
  const candle = fakeCandleSeries();
  const h = M.MiraeRenderer.createHandle(chart, candle);
  for (const m of ["setLayers", "applyLive", "applySeed", "clear"]) {
    assert.equal(typeof h[m], "function", m);
  }
  assert.equal(candle.primitives.length, 1); // ③ 미래 목표 광선 프리미티브
  assert.equal(chart.made.length, 10); // 커스텀 6(reg/score/band/mem/pst/mktCenter) + 마켓 밴드 라인 4
});

test("MiraeRenderer: applySeed가 ctx 캐시에서 복원하고 setLayers가 즉시 토글한다", () => {
  const chart = fakeChart();
  const candle = fakeCandleSeries();
  const h = M.MiraeRenderer.createHandle(chart, candle);
  // 커스텀 시리즈 생성 순서: [0]regLine [1]score [2]band [3]mem [4]pst [5]mktCenter
  const bandSeries = chart.made[2];
  const regLineSeries = chart.made[0];

  const mkInd = (regFlat) => ({
    ...M.barIndFromPayload({ reg_valid: 1, reg_r2: 0.7, reg_flat: regFlat, final: [1, 1, 2] }),
    score: 3, pred: [101, 102, 103], resid: 1, pvol: 1,
  });
  const rows = [];
  for (let i = 0; i < 12; i++) rows.push([100 + i * 60, mkInd(1000 + i)]);
  const ctx = makeCtx(rows);

  h.applySeed(ctx);
  assert.equal(regLineSeries.data.length, 12); // ② 회귀선 전 구간
  assert.equal(bandSeries.data.length, 12);    // ④ 기본 켜짐
  const rays = candle.primitives[0];
  assert.notEqual(rays.paneViews()[0].renderer(), null); // ③ 광선 복원됨

  // 레이어 토글: band 끄면 즉시 비워지고, 다시 켜면 캐시에서 복원
  h.setLayers({ band: false });
  assert.deepEqual(bandSeries.data, []);
  h.setLayers({ band: true });
  assert.equal(bandSeries.data.length, 12);

  h.clear();
  assert.deepEqual(regLineSeries.data, []);
  assert.deepEqual(bandSeries.data, []);
  assert.equal(rays.paneViews()[0].renderer(), null);
});

test("MiraeRenderer: applyLive가 봉별 갱신과 캐시된 ⑥⑦ 아이템을 표시한다", () => {
  const chart = fakeChart();
  const candle = fakeCandleSeries();
  const h = M.MiraeRenderer.createHandle(chart, candle);
  const regLineSeries = chart.made[0];
  const memSeries = chart.made[3];

  const ctx = makeCtx([]);
  const t = 100000;
  ctx.barSeq.push(t);
  ctx.barPos.set(t, 0);
  ctx.bars.set(t, { time: t, open: 1, high: 2, low: 0, close: 1 });
  ctx.barInd.set(t, M.barIndFromPayload({ reg_valid: 1, reg_r2: 0.5, reg_flat: 9000, final: [1, 1, 1] }));

  // ⑥⑦ 아이템 캐싱은 app.js applyStatus가 담당한다 — 여기서는 같은 함수로 미리 심는다
  const mem = [1, 1, 1, 5000, 5100, 5200, 5300, 5400, 5500, 5600, 4900, 4800, 4700];
  ctx.barInd.get(t).memItem = M.memItemFromPayload(t, mem, ctx.recentBars(0, 5));

  h.applyLive({ bar_open_time: t * 1e6, score: -2, reg_valid: 1, mem }, ctx);
  assert.equal(regLineSeries.updates.length, 1);
  assert.equal(regLineSeries.updates[0].value, 9000);
  assert.equal(memSeries.updates.length, 1);
  assert.equal(memSeries.updates[0].upd, true); // 갱신 봉은 범위선 숨김
  // ⑥ 아이템이 barInd 캐시에 있으면 새 렌더러도 applySeed로 복원할 수 있다
  const chart2 = fakeChart();
  const h2 = M.MiraeRenderer.createHandle(chart2, fakeCandleSeries());
  h2.applySeed(ctx);
  assert.equal(ctx.barInd.get(t).memItem.value, 5200); // value = mem[5] (목표2)
  assert.equal(chart2.made[3].data.length, 1);         // 새 렌더러의 ⑥ 시리즈에 복원됨
  assert.equal(chart2.made[3].data[0].value, 5200);

  // regValid 없는 봉은 갭(whitespace)으로 갱신
  const t2 = t + 60;
  ctx.barSeq.push(t2);
  ctx.barPos.set(t2, 1);
  ctx.barInd.set(t2, M.barIndFromPayload({ reg_valid: 0 }));
  h.applyLive({ bar_open_time: t2 * 1e6 }, ctx);
  assert.equal(regLineSeries.updates.length, 2);
  assert.equal(regLineSeries.updates[1].value, undefined);
});

test("MiraeRenderer: 매니페스트 8개 레이어 칩을 setLayers로 개별 토글한다", () => {
  const chart = fakeChart();
  const candle = fakeCandleSeries();
  const h = M.MiraeRenderer.createHandle(chart, candle);
  const [regLineSeries, scoreSeries, bandSeries, memSeries] = chart.made;
  const rays = candle.primitives[0];

  const mkInd = (regFlat) => ({
    ...M.barIndFromPayload({ reg_valid: 1, reg_r2: 0.7, reg_flat: regFlat, final: [1, 1, 2] }),
    score: 3, pred: [101, 102, 103], resid: 1, pvol: 1,
  });
  const rows = [];
  for (let i = 0; i < 12; i++) rows.push([100 + i * 60, mkInd(1000 + i)]);
  rows[5][1].memItem = M.memItemFromPayload(rows[5][0],
    [1, 0, 1, 5000, 5100, 5200, 5300, 5400, 5500, 5600, 4900, 4800, 4700], []);
  const ctx = makeCtx(rows);
  h.applySeed(ctx);
  assert.equal(regLineSeries.data.length, 12);
  assert.equal(scoreSeries.data.length, 12);
  assert.equal(memSeries.data.length, 1);
  assert.notEqual(rays.paneViews()[0].renderer(), null);

  // ① 점수 끄기/켜기
  h.setLayers({ score: false });
  assert.deepEqual(scoreSeries.data, []);
  h.setLayers({ score: true });
  assert.equal(scoreSeries.data.length, 12);

  // ② 회귀선만 꺼도 ⑤ 상태 덧선용 데이터는 유지된다 (그리기는 pane view가 layers를 본다)
  h.setLayers({ reg: false });                 // ⑤만 켜짐
  assert.equal(regLineSeries.data.length, 12);
  h.setLayers({ reg: true, state: false });    // ②만 켜짐
  assert.equal(regLineSeries.data.length, 12);
  h.setLayers({ reg: false, state: false });   // 둘 다 끄면 시리즈가 빈다
  assert.deepEqual(regLineSeries.data, []);
  h.setLayers({ reg: true, state: true });
  assert.equal(regLineSeries.data.length, 12);

  // ③ 광선 끄기/켜기
  h.setLayers({ rays: false });
  assert.equal(rays.paneViews()[0].renderer(), null);
  h.setLayers({ rays: true });
  assert.notEqual(rays.paneViews()[0].renderer(), null);

  // ⑥ 방향 기억 끄기/켜기 — 캐시에서 복원
  h.setLayers({ memory: false });
  assert.deepEqual(memSeries.data, []);
  h.setLayers({ memory: true });
  assert.equal(memSeries.data.length, 1);

  // ④⑧은 기존 계약대로
  h.setLayers({ band: false });
  assert.deepEqual(bandSeries.data, []);
  h.setLayers({ mktband: true });
  assert.equal(chart.made[5].data.length, 0); // mktValid 없는 캐시 → 빈 밴드

  // 알 수 없는 키는 무시한다
  h.setLayers({ unknown_layer: false });
  assert.equal(regLineSeries.data.length, 12);
});

test("MiraeRenderer: destroy가 시리즈와 프리미티브를 차트에서 분리한다", () => {
  const chart = fakeChart();
  const candle = fakeCandleSeries();
  const h = M.MiraeRenderer.createHandle(chart, candle);
  assert.equal(chart.made.length, 10);
  h.destroy();
  assert.equal(chart.made.length, 0);
  assert.equal(chart.removed.length, 10);
  assert.equal(candle.primitives.length, 0);
});

test("parseInd: ind[28..31] SMA 확장 구간 파싱", () => {
  const ind = makeInd();
  ind[26] = 5; ind[27] = 20260929; ind[28] = 1; ind[29] = 34510.5; ind[30] = 34500.25; ind[31] = 34480;
  const d = M.parseInd(ind);
  assert.equal(d.tick, 5);
  assert.equal(d.day, 20260929);
  assert.equal(d.smaValid, true);
  assert.deepEqual(d.sma, [34510.5, 34500.25, 34480]);

  // 확장이 없는 구형 스냅샷(길이 26~28)은 sma 무효로 읽는다
  const short = M.parseInd(makeInd());
  assert.equal(short.smaValid, false);
  assert.ok(short.sma.every((v) => Number.isNaN(v)));
});

test("barInd 확장: sma/ob/regLine 필드가 시딩·라이브 공통 형태로 유지된다", () => {
  const ind = makeInd();
  ind[28] = 1; ind[29] = 101; ind[30] = 102; ind[31] = 103;
  const e = M.barIndFromInd(M.parseInd(ind));
  assert.equal(e.smaValid, true);
  assert.deepEqual(e.sma, [101, 102, 103]);
  assert.equal(e.obValid, true);
  assert.equal(e.obScore, 1.5);
  assert.equal(e.regLine, 34550); // 배지 복원이 라이브와 같은 출처(regLine)를 쓴다

  const live = M.barIndFromPayload({ sma: [1, 201, 202, 203], ob_valid: 1, ob_score: -0.5, reg_line: 34551 });
  assert.equal(live.smaValid, true);
  assert.deepEqual(live.sma, [201, 202, 203]);
  assert.equal(live.obValid, true);
  assert.equal(live.obScore, -0.5);
  assert.equal(live.regLine, 34551);

  // sma 키가 없는 구형 엔진 페이로드는 무효로 둔다
  const bare = M.barIndFromPayload({ reg_valid: 0 });
  assert.equal(bare.smaValid, false);
  assert.ok(bare.sma.every((v) => Number.isNaN(v)));
  assert.equal(bare.obValid, false);
  assert.ok(Number.isNaN(bare.regLine));
});

test("memItemFromPayload/pstItemFromPayload: 라이브 mem/pst → 봉별 아이템 (캐시 계약)", () => {
  const mem = [1, 1, 1, 5000, 5100, 5200, 5300, 5400, 5500, 5600, 4900, 4800, 4700];
  const item = M.memItemFromPayload(100, mem, []);
  assert.equal(item.time, 100);
  assert.equal(item.value, 5200);      // 목표2
  assert.equal(item.price, 5000);      // 기준가격
  assert.equal(item.dir, 1);
  assert.deepEqual(item.t, [5100, 5200, 5300]);
  assert.deepEqual(item.u, [5400, 5500, 5600]);
  assert.deepEqual(item.l, [4900, 4800, 4700]);
  assert.equal(item.upd, true);        // 갱신 봉
  assert.equal(item.showU, false);     // 갱신 봉은 범위선 숨김
  assert.equal(item.showL, false);

  // 갱신 봉이 아니면 5봉 이탈 규칙을 적용한다 (recent가 목표3=5300 아래 5봉 → 상단 숨김)
  const below5 = [90, 91, 92, 93, 94].map((v) => ({ high: v, low: v - 5 }));
  const held = M.memItemFromPayload(160, [1, 0, 1, 5000, 5100, 5200, 5300, 5400, 5500, 5600, 4900, 4800, 4700], below5);
  assert.equal(held.upd, false);
  assert.equal(held.showU, false);
  assert.equal(held.showL, true);

  // 무효/키 없음 구분: 무효는 null(캐시 비움), 키 없음은 undefined(캐시 유지)
  assert.equal(M.memItemFromPayload(100, [0], []), null);
  assert.equal(M.memItemFromPayload(100, undefined, []), undefined);

  const pst = [1, 1, -1, 7100, 7200, 7300, 7400, 7500, 7600, 6900, 6800, 6700];
  const snap = M.pstItemFromPayload(100, pst, []);
  assert.equal(snap.value, 7200); // 목표2
  assert.equal(snap.dir, -1);
  assert.deepEqual(snap.t, [7100, 7200, 7300]);
  assert.equal(snap.upd, undefined); // ⑦에는 갱신 봉 개념이 없다
  assert.equal(M.pstItemFromPayload(100, [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], []), null);
  assert.equal(M.pstItemFromPayload(100, undefined, []), undefined);
});
