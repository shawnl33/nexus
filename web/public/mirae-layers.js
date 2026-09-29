// 미래곡선 보조지표 렌더링 레이어 — 원본 YesLanguage V16의 Plot 시맨틱을
// lightweight-charts 커스텀 시리즈/프리미티브로 재현한다.
// 계산은 엔진(src/)이 하고, 이 파일은 페이로드 값의 시각화와 원본 표시 규칙
// (7단계 점수색·R² 굵기·결과 띠 색·범위선 숨김·마켓 단계색)만 담당한다.
// 브라우저에서는 전역 MiraeLayers, node:test에서는 globalThis.MiraeLayers로 쓴다.
"use strict";

const MiraeLayers = (() => {
  const SPANS = [5, 10, 15]; // 예측봉수1~3
  const MIN_R2 = 0.4;  // 원본 최소신뢰도
  const HIGH_R2 = 0.7; // 회귀선 굵기 상단 기준
  // ④ 결과 띠 오프셋: tick×4 (선물 tick 0.05pt → raw ×100 기준 20)
  const BAND_OFFSET = 20;
  // ⑧ 마켓거리기준 하한: 선물 1틱(0.05pt) = raw 5
  const MKT_TICK = 5;

  const rgb = (r, g, b) => `rgb(${r},${g},${b})`;
  const num = (v) => (typeof v === "number" && Number.isFinite(v) ? v : NaN);
  const int = (v) => (Number.isFinite(v) ? Math.trunc(v) : 0);

  // ---- 원본 표시 규칙 (순수 함수, 단위 테스트 대상) ----

  // ①② 공통 7단계 점수색 (v16:970-983 단계화_1분_색상)
  function scoreColor(score) {
    if (score > 4) return rgb(220, 0, 0);
    if (score > 2) return rgb(255, 100, 70);
    if (score > 0) return rgb(255, 185, 185);
    if (score < -4) return rgb(0, 0, 180);
    if (score < -2) return rgb(60, 130, 255);
    if (score < 0) return rgb(180, 210, 255);
    return rgb(150, 150, 150);
  }
  // ② 회귀선 굵기 (v16:993-995)
  const regWidth = (r2) => (r2 >= HIGH_R2 ? 6 : r2 >= MIN_R2 ? 4 : 2);
  // ⑤ 매매 상태 덧선 (v16:537-551)
  function tradeStyle(state) {
    if (state >= 2) return { color: rgb(255, 0, 0), width: 5 };
    if (state === 1) return { color: rgb(255, 128, 0), width: 2 };
    if (state <= -2) return { color: rgb(0, 0, 255), width: 5 };
    if (state === -1) return { color: rgb(0, 160, 200), width: 2 };
    return null; // state==0/무효 → NoPlot
  }
  // ④ 결과 띠 색 (v16:237-242, 254-256) — src는 10봉 전 봉의 지표 엔트리
  function bandColor(src) {
    if (!src || !src.regValid) return rgb(205, 205, 205);
    const d = src.predDir ? src.predDir[1] : 0;
    const strong = src.r2 >= MIN_R2;
    if (d > 0) return strong ? rgb(255, 0, 0) : rgb(255, 145, 145);
    if (d < 0) return strong ? rgb(0, 0, 255) : rgb(145, 170, 255);
    return rgb(150, 150, 150);
  }
  // ⑥⑦ 범위선 숨김: 최근 5봉(현재 봉 포함, 오름차순)이 목표3를 완전히 벗어났는지
  // (v16:679-701, 771-793 — H<목표3 5봉 연속이면 상단 숨김, L>목표3이면 하단 숨김)
  function rangeFlags(recent, target3) {
    if (!Number.isFinite(target3) || recent.length < 5) return { showU: true, showL: true };
    let below = true, above = true;
    for (const b of recent) {
      if (!(b.high < target3)) below = false;
      if (!(b.low > target3)) above = false;
    }
    return { showU: !below, showL: !above };
  }
  // ⑧ 마켓중심단계 (v16:878-893)
  function mktStage(prevCenter, center, close, regFlat, u1) {
    const basis = Math.max(MKT_TICK, Math.abs(u1 - center));
    const strength = Math.max(-100, Math.min(100, ((close - center) / basis) * 100));
    const slope = center - prevCenter;
    if (slope > 0 && close > center && close > regFlat) return strength >= 66 ? 3 : strength >= 33 ? 2 : 1;
    if (slope < 0 && close < center && close < regFlat) return strength <= -66 ? -3 : strength <= -33 ? -2 : -1;
    return 0;
  }
  function mktStageColor(stage) {
    if (stage >= 3) return rgb(220, 0, 0);
    if (stage === 2) return rgb(255, 100, 70);
    if (stage === 1) return rgb(255, 185, 185);
    if (stage <= -3) return rgb(0, 0, 180);
    if (stage === -2) return rgb(60, 130, 255);
    if (stage === -1) return rgb(180, 210, 255);
    return rgb(120, 120, 120);
  }

  // ---- 페이로드 파싱 (docs/display_payload.md 레이아웃) ----

  // 스냅샷 ind 배열 [0..25] → 정규화 객체
  function parseInd(ind) {
    if (!Array.isArray(ind)) return null;
    return {
      closed: ind[0] === 1,
      regValid: ind[1] === 1,
      regLine: num(ind[2]),
      r2: num(ind[3]),
      pred: [num(ind[4]), num(ind[5]), num(ind[6])],
      score: num(ind[7]),
      obValid: ind[8] === 1,
      obScore: num(ind[9]),
      resid: num(ind[10]),
      pvol: num(ind[11]),
      predDir: [int(ind[12]), int(ind[13]), int(ind[14])],
      mktValid: ind[15] === 1,
      mkt: [num(ind[16]), num(ind[17]), num(ind[18]), num(ind[19]), num(ind[20])], // center,u1,l1,u2,l2
      finalValid: ind[21] === 1,
      finalDir: int(ind[22]),
      finalState: int(ind[23]),
      finalStrength: num(ind[24]),
      regFlat: num(ind[25]),
    };
  }
  // 봉별 지표 캐시(barInd) 엔트리: 시딩(parseInd 결과)과 라이브(status 페이로드) 공통 형태
  function barIndFromInd(d) {
    return {
      predDir: d.predDir, regValid: d.regValid, r2: d.r2, regFlat: d.regFlat,
      finalValid: d.finalValid, finalState: d.finalState,
    };
  }
  function barIndFromPayload(p) {
    const fin = Array.isArray(p.final) ? p.final : [];
    return {
      predDir: Array.isArray(p.pred_dir) ? p.pred_dir.map(int) : [0, 0, 0],
      regValid: p.reg_valid === 1,
      r2: num(p.reg_r2),
      regFlat: num(p.reg_flat),
      finalValid: fin[0] === 1,
      finalState: int(fin[2]),
    };
  }

  // ---- 커스텀 시리즈 공통부 ----

  // 유효 구간만 잇는다: 무효 봉(whitespace)은 originalData가 없고,
  // 끊긴 봉은 논리 인덱스(b.time)가 1씩 증가하지 않으므로 선분을 그리지 않는다.
  function forEachSegment(bars, range, fn) {
    const from = Math.max(0, Math.floor(range ? range.from : 0));
    const to = Math.min(bars.length, Math.ceil(range ? range.to : bars.length));
    for (let i = from; i + 1 < to; i++) {
      const a = bars[i], b = bars[i + 1];
      if (!a || !b || !a.originalData || !b.originalData) continue;
      if (b.time !== a.time + 1) continue;
      if (!Number.isFinite(a.x) || !Number.isFinite(b.x)) continue;
      fn(a, b);
    }
  }
  function strokeSeg(ctx, x0, y0, x1, y1, color, width) {
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.beginPath();
    ctx.moveTo(x0, y0);
    ctx.lineTo(x1, y1);
    ctx.stroke();
  }
  function strokeH(ctx, priceConverter, x0, x1, value, color, width) {
    if (!Number.isFinite(value)) return;
    const y = priceConverter(value);
    if (y === null) return;
    strokeSeg(ctx, x0, y, x1, y, color, width);
  }

  function makeSegmentPaneView(segmentFn) {
    let bars = [];
    let range = null;
    const renderer = {
      draw(target, priceConverter) {
        if (bars.length < 2) return;
        target.useMediaCoordinateSpace(({ context: ctx }) => {
          forEachSegment(bars, range, (a, b) => segmentFn(ctx, priceConverter, a, b));
        });
      },
    };
    return {
      renderer: () => renderer,
      update(data) { bars = data.bars; range = data.visibleRange; },
      priceValueBuilder: (d) => [d.value],
      isWhitespace: (d) => !Number.isFinite(d.value),
      defaultOptions: () => ({
        color: "#9598a1",
        priceLineVisible: false,
        lastValueVisible: false,
        crosshairMarkerVisible: false,
      }),
    };
  }

  // ②+⑤ 회귀선·매매 상태 (Plot7 + Plot30/31, 2패스 덧선)
  function createRegLinePaneView() {
    return makeSegmentPaneView((ctx, priceConverter, a, b) => {
      const da = a.originalData, db = b.originalData;
      const ya = priceConverter(da.value), yb = priceConverter(db.value);
      if (ya === null || yb === null) return;
      strokeSeg(ctx, a.x, ya, b.x, yb, scoreColor(da.score), regWidth(da.r2));
      const style = tradeStyle(da.finalState);
      if (style) strokeSeg(ctx, a.x, ya, b.x, yb, style.color, style.width);
    });
  }

  // ④ 과거 채점 결과 띠 (Plot21 기본모드) — 회귀선 아래 가로선, 봉별 색
  // (아이템 필드명 c: color는 라이브러리 예약 필드라 originalData에서 빠진다)
  function createResultBandPaneView() {
    return makeSegmentPaneView((ctx, priceConverter, a, b) => {
      const da = a.originalData, db = b.originalData;
      const ya = priceConverter(da.value), yb = priceConverter(db.value);
      if (ya === null || yb === null) return;
      strokeSeg(ctx, a.x, ya, b.x, yb, da.c, 2);
    });
  }

  // ⑧ 마켓 중심선 (Plot51) — 봉별 단계색, 굵기 3
  function createMarketCenterPaneView() {
    return makeSegmentPaneView((ctx, priceConverter, a, b) => {
      const da = a.originalData, db = b.originalData;
      const ya = priceConverter(da.value), yb = priceConverter(db.value);
      if (ya === null || yb === null) return;
      strokeSeg(ctx, a.x, ya, b.x, yb, mktStageColor(da.stage), 3);
    });
  }

  // ① 통합 점수 막대 (Plot1) — 0 기준 세로 막대, 굵기 2+|점수|
  function createScoreBarPaneView() {
    let bars = [];
    let range = null;
    const renderer = {
      draw(target, priceConverter) {
        target.useMediaCoordinateSpace(({ context: ctx }) => {
          const y0 = priceConverter(0);
          if (y0 === null) return;
          const from = Math.max(0, Math.floor(range ? range.from : 0));
          const to = Math.min(bars.length, Math.ceil(range ? range.to : bars.length));
          for (let i = from; i < to; i++) {
            const bar = bars[i];
            const d = bar && bar.originalData;
            if (!d || !Number.isFinite(d.value) || !Number.isFinite(bar.x)) continue;
            const y1 = priceConverter(d.value);
            if (y1 === null) continue;
            const w = 2 + Math.abs(d.value); // 원본 굵기 = 2+|점수|
            ctx.fillStyle = scoreColor(d.value);
            ctx.fillRect(bar.x - w / 2, Math.min(y0, y1), w, Math.max(1, Math.abs(y1 - y0)));
          }
        });
      },
    };
    return {
      renderer: () => renderer,
      update(data) { bars = data.bars; range = data.visibleRange; },
      priceValueBuilder: (d) => [0, d.value],
      isWhitespace: (d) => !Number.isFinite(d.value),
      defaultOptions: () => ({
        color: "#9598a1",
        priceLineVisible: false,
        lastValueVisible: false,
        crosshairMarkerVisible: false,
      }),
    };
  }

  // ⑥⑦ 방향 기억/지속 사진 (Plot32~41, 42~50) — 확정/저장 시점 값을 다음 갱신까지
  // 유지하는 수평 계단선. 봉별 데이터 아이템이 그 봉 구간의 수평 세그먼트를 그린다.
  const STEP_STYLE = {
    mem: {
      baseColor: "#e5e5e5", baseWidth: 2, // Plot32 기준가격 (원본은 RGB(0,0,0), 다크 테마 대비를 위해 밝게)
      targetWidths: [3, 5, 3],
      up: [rgb(255, 170, 170), rgb(255, 0, 0), rgb(180, 0, 0)],
      dn: [rgb(140, 170, 255), rgb(0, 0, 255), rgb(0, 0, 150)],
      rangeColors: [rgb(190, 210, 190), rgb(165, 165, 165), rgb(205, 195, 205)],
    },
    pst: {
      baseColor: null, // ⑦에는 기준가격선 없음
      targetWidths: [1, 2, 1],
      up: [rgb(255, 190, 190), rgb(255, 100, 100), rgb(220, 100, 100)],
      dn: [rgb(170, 190, 255), rgb(100, 130, 255), rgb(100, 120, 220)],
      rangeColors: [rgb(210, 225, 210), rgb(190, 190, 190), rgb(220, 210, 220)],
    },
  };
  function createStepLinesPaneView(kind) {
    const st = STEP_STYLE[kind];
    const view = makeSegmentPaneView((ctx, priceConverter, a, b) => {
      const d = a.originalData;
      const x0 = a.x, x1 = b.x;
      if (st.baseColor) strokeH(ctx, priceConverter, x0, x1, d.price, st.baseColor, st.baseWidth);
      const colors = d.dir > 0 ? st.up : d.dir < 0 ? st.dn : null;
      if (colors) {
        for (let k = 0; k < 3; k++) {
          strokeH(ctx, priceConverter, x0, x1, d.t[k], colors[k], st.targetWidths[k]);
        }
      }
      // ⑥은 갱신 봉에 범위선 숨김 (원본 회귀기억갱신==1 → NoPlot36~41)
      if (d.upd) return;
      if (d.showU) {
        for (let k = 0; k < 3; k++) strokeH(ctx, priceConverter, x0, x1, d.u[k], st.rangeColors[k], 1);
      }
      if (d.showL) {
        for (let k = 0; k < 3; k++) strokeH(ctx, priceConverter, x0, x1, d.l[k], st.rangeColors[k], 1);
      }
    });
    return view;
  }

  // ③ 미래 목표 광선 (v16:357-424 TL 작도) — 마지막 봉 기준 직전 봉에서 시작해
  // 우측으로 무한 연장하는 수평선 9개 (목표 3 + 범위 6). 캔들 시리즈 프리미티브.
  function createFutureRaysPrimitive() {
    const TARGET = [
      { w: 1, up: rgb(255, 145, 145), dn: rgb(145, 170, 255), flat: rgb(160, 160, 160) },
      { w: 3, up: rgb(255, 0, 0), dn: rgb(0, 0, 255), flat: rgb(130, 130, 130) },
      { w: 1, up: rgb(205, 55, 55), dn: rgb(55, 85, 205), flat: rgb(160, 160, 160) },
    ];
    const RANGE_OK = [rgb(190, 210, 190), rgb(165, 165, 165), rgb(205, 195, 205)];
    const RANGE_WEAK = rgb(180, 180, 180); // R²<최소신뢰도면 범위 6선 전부 회색
    let state = null;
    let chart = null;
    let series = null;
    let requestUpdate = () => {};
    const renderer = {
      draw(target) {
        const s = state;
        if (!s || !chart || !series) return;
        target.useMediaCoordinateSpace(({ context: ctx, mediaSize }) => {
          let x0 = chart.timeScale().timeToCoordinate(s.prevTime);
          if (x0 === null || x0 === undefined) x0 = chart.timeScale().timeToCoordinate(s.time);
          if (x0 === null || x0 === undefined) x0 = 0;
          const x1 = mediaSize.width;
          const lines = [];
          for (let k = 0; k < 3; k++) {
            const st = TARGET[k];
            const d = s.predDir[k] || 0;
            lines.push({ v: s.pred[k], c: d > 0 ? st.up : d < 0 ? st.dn : st.flat, w: st.w });
          }
          // 범위k = max(잔차, 변동성×0.25) × √지평k (원본 MTF기준오차·MTF범위)
          const base = Math.max(Math.abs(s.resid), Math.abs(s.pvol) * 0.25);
          const ok = s.r2 >= MIN_R2;
          for (let k = 0; k < 3; k++) {
            const band = base * Math.sqrt(SPANS[k]);
            const c = ok ? RANGE_OK[k] : RANGE_WEAK;
            lines.push({ v: s.pred[k] + band, c, w: 1 });
            lines.push({ v: s.pred[k] - band, c, w: 1 });
          }
          for (const ln of lines) {
            if (!Number.isFinite(ln.v)) continue;
            const y = series.priceToCoordinate(ln.v);
            if (y === null || y === undefined) continue;
            strokeSeg(ctx, x0, y, x1, y, ln.c, ln.w);
          }
        });
      },
    };
    const paneView = { renderer: () => (state ? renderer : null) };
    const paneViews = [paneView];
    return {
      attached(param) {
        chart = param.chart;
        series = param.series;
        requestUpdate = param.requestUpdate;
      },
      detached() {
        chart = null;
        series = null;
        requestUpdate = () => {};
      },
      paneViews: () => paneViews,
      autoscaleInfo: () => null,
      set(next) {
        state = next;
        requestUpdate();
      },
      clear() {
        if (state !== null) {
          state = null;
          requestUpdate();
        }
      },
    };
  }

  return {
    SPANS, MIN_R2, HIGH_R2, BAND_OFFSET, MKT_TICK,
    scoreColor, regWidth, tradeStyle, bandColor, rangeFlags, mktStage, mktStageColor,
    parseInd, barIndFromInd, barIndFromPayload,
    createRegLinePaneView, createScoreBarPaneView, createResultBandPaneView,
    createStepLinesPaneView, createMarketCenterPaneView, createFutureRaysPrimitive,
  };
})();

if (typeof globalThis !== "undefined") {
  globalThis.MiraeLayers = MiraeLayers;
}
