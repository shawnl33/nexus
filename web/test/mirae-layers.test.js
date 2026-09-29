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
