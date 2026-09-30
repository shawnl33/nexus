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
  // ④ 결과 띠 오프셋: tick×4 (원본 PriceScale×4). raw 기준 틱은 엔진 페이로드 tick 키가 준다
  const bandOffset = (tick) => tick * 4;

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
  // ④ 결과 띠 색 (v16:237-242, 254-256) — src는 10봉 전 봉의 지표 엔트리.
  // sameSession=false면 원본의 세션 가드(v16:224-226)로 무효 → 회색
  function bandColor(src, sameSession) {
    if (sameSession === false || !src || !src.regValid) return rgb(205, 205, 205);
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
  // ⑧ 마켓중심단계 (v16:878-893) — tick은 마켓거리기준 하한 (원본 Max(PriceScale, …))
  function mktStage(prevCenter, center, close, regFlat, u1, tick) {
    const basis = Math.max(tick > 0 ? tick : 5, Math.abs(u1 - center));
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

  // 스냅샷 ind 배열 [0..31] → 정규화 객체
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
      tick: num(ind[26]),
      day: num(ind[27]),
      smaValid: ind[28] === 1,
      sma: [num(ind[29]), num(ind[30]), num(ind[31])], // SMA 5/20/60
    };
  }
  // 봉별 지표 캐시(barInd) 엔트리: 시딩(parseInd 결과)과 라이브(status 페이로드) 공통 형태
  function barIndFromInd(d) {
    return {
      predDir: d.predDir, regValid: d.regValid, r2: d.r2, regFlat: d.regFlat,
      finalValid: d.finalValid, finalState: d.finalState, day: d.day,
      mktValid: d.mktValid, mkt: d.mkt,
      regLine: d.regLine,
      obValid: d.obValid, obScore: d.obScore,
      smaValid: d.smaValid, sma: d.sma,
    };
  }
  function barIndFromPayload(p) {
    const fin = Array.isArray(p.final) ? p.final : [];
    const mkt = Array.isArray(p.mkt) ? p.mkt : [];
    const sma = Array.isArray(p.sma) ? p.sma : [];
    return {
      predDir: Array.isArray(p.pred_dir) ? p.pred_dir.map(int) : [0, 0, 0],
      regValid: p.reg_valid === 1,
      r2: num(p.reg_r2),
      regFlat: num(p.reg_flat),
      finalValid: fin[0] === 1,
      finalState: int(fin[2]),
      day: num(p.day),
      mktValid: mkt[0] === 1,
      mkt: [num(mkt[1]), num(mkt[2]), num(mkt[3]), num(mkt[4]), num(mkt[5])],
      regLine: num(p.reg_line),
      obValid: p.ob_valid === 1,
      obScore: num(p.ob_score),
      smaValid: sma[0] === 1,
      sma: [num(sma[1]), num(sma[2]), num(sma[3])],
    };
  }

  // ⑥⑦ 봉별 아이템 생성 (라이브 status의 mem/pst 배열 → 수평 세그먼트 아이템).
  // 반환값: 아이템 객체 / null(무효 — 캐시 비움) / undefined(키 없음 — 캐시 유지).
  // 캐시 기록은 app.js applyStatus가 담당하고, 렌더러는 캐시된 아이템을 표시만 한다.
  function memItemFromPayload(t, mem, recent) {
    if (!Array.isArray(mem)) return undefined;
    if (mem[0] !== 1) return null;
    const updated = mem[1] === 1;
    const flags = updated ? { showU: false, showL: false } : rangeFlags(recent, mem[6]);
    return { time: t, value: mem[5], dir: mem[2], price: mem[3],
             t: mem.slice(4, 7), u: mem.slice(7, 10), l: mem.slice(10, 13),
             showU: flags.showU, showL: flags.showL, upd: updated };
  }
  function pstItemFromPayload(t, pst, recent) {
    if (!Array.isArray(pst)) return undefined;
    if (pst[1] !== 1) return null;
    const flags = rangeFlags(recent, pst[5]);
    return { time: t, value: pst[4], dir: pst[2],
             t: pst.slice(3, 6), u: pst.slice(6, 9), l: pst.slice(9, 12),
             showU: flags.showU, showL: flags.showL };
  }

  // ⑥ 스냅샷 mem 이벤트로 봉별 수평 세그먼트 아이템을 복원한다 (시딩 경로 —
  // app.js가 스냅샷 페이지에서 모은 이벤트와 dedup 봉 행을 넘긴다).
  // 이벤트 레이아웃: [time, valid, dir, price, t1..3, u1..3, l1..3, showT, showU, showL, reset]
  // (docs/display_payload.md §2). reset=1은 세션 경계 봉 표시: 엔진이 리셋 직후 상태를
  // 싣기 때문에 재저장이 없으면 valid=0으로 온다. 지배 이벤트가 무효인 봉부터는 세트를
  // 잇지 않으므로 진행 중 세트는 경계 시각에서 끊긴다. 리셋 봉에 새 세트가 함께 저장된
  // 경우(valid=1)는 이 이벤트가 새 세트의 시작이다 — 라이브의 mem[0]=0 처리와 같은 결과.
  // 구형 엔진의 16원소 이벤트는 reset 없음(0 간주, 기존 동작 유지).
  function buildMemItems(dedup, events) {
    const sorted = events
      .map((e) => ({ time: Number(e[0]) / 1e6, valid: e[1] === 1, dir: e[2], price: e[3],
                      t: e.slice(4, 7), u: e.slice(7, 10), l: e.slice(10, 13),
                      reset: e[16] === 1 }))
      .filter((e) => Number.isFinite(e.time))
      .sort((a, b) => a.time - b.time);
    const items = [];
    let ei = -1;
    for (let i = 0; i < dedup.length; i++) {
      const t = dedup[i].time;
      while (ei + 1 < sorted.length && sorted[ei + 1].time <= t) ei++;
      if (ei < 0) continue;
      const ev = sorted[ei];
      // 리셋(경계) 봉을 포함해 무효 이벤트가 지배하는 봉은 진행 중 세트를 잇지 않는다
      if (!ev.valid) continue;
      const upd = ev.time === t; // 갱신(또는 리셋 봉 재저장)에는 범위선 숨김
      const flags = upd ? { showU: false, showL: false }
        : rangeFlags(dedup.slice(Math.max(0, i - 4), i + 1), ev.t[2]);
      items.push({ time: t, value: ev.t[1], dir: ev.dir, price: ev.price,
                   t: ev.t, u: ev.u, l: ev.l, showU: flags.showU, showL: flags.showL, upd });
    }
    return items;
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
  // layers를 넘기면 reg/state 칩으로 각 패스를 끈다 (생략 시 둘 다 그린다)
  function createRegLinePaneView(layers) {
    return makeSegmentPaneView((ctx, priceConverter, a, b) => {
      const da = a.originalData, db = b.originalData;
      const ya = priceConverter(da.value), yb = priceConverter(db.value);
      if (ya === null || yb === null) return;
      if (!layers || layers.reg !== false) {
        strokeSeg(ctx, a.x, ya, b.x, yb, scoreColor(da.score), regWidth(da.r2));
      }
      if (!layers || layers.state !== false) {
        const style = tradeStyle(da.finalState);
        if (style) strokeSeg(ctx, a.x, ya, b.x, yb, style.color, style.width);
      }
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

  // ---- MiraeRenderer: 패널 단위 렌더러 계약 (패널 매니저 app.js가 소비) ----
  // createHandle(chart, candleSeries) → handle. handle은 한 차트에 붙는
  // 시리즈/프리미티브 묶음과 레이어 표시 상태를 소유한다.
  //   handle.setLayers(map)   — layer id → bool. 즉시 다시 그린다
  //     (score/reg/rays/band/state/memory/snap/mktband — 매니페스트 ①~⑧)
  //   handle.applyLive(p, ctx) — status 페이로드 1건 반영 (②③④⑤⑥⑦⑧)
  //   handle.applySeed(ctx)    — 시딩 완료 후 ctx 캐시에서 전체 복원
  //   handle.clear()           — 모든 레이어 제거
  //   handle.destroy()         — 시리즈/프리미티브를 차트에서 분리 (지표 해제 시)
  // 공유 ctx (app.js 제공): { bars, barInd, barSeq, barPos, tickRaw(), sameSession, recentBars }.
  // barInd 엔트리는 barIndFromInd/barIndFromPayload에 더해 app.js가 심는
  // score/pred/resid/pvol(③① 복원용)과 memItem/pstItem(⑥⑦ 봉별 아이템)을 쓴다.
  // ⑥⑦ 아이템은 app.js applyStatus가 렌더러 on/off와 무관하게 기록한다.
  const MiraeRenderer = {
    id: "mirae_v16",
    createHandle(chart, candleSeries) {
      // 레이어 표시 상태 — 매니페스트 defaultOn과 동일한 기본값 (⑧만 숨김)
      const layers = { score: true, reg: true, rays: true, band: true,
                       state: true, memory: true, snap: true, mktband: false };
      // ② 회귀선 + ⑤ 매매 상태 덧선: 한 커스텀 시리즈가 2패스로 그린다
      const regLineSeries = chart.addCustomSeries(createRegLinePaneView(layers), {});
      // ① 통합 점수 막대: 아래 보조 칸의 0 기준 세로 색 막대
      const scoreSeries = chart.addCustomSeries(createScoreBarPaneView(), { priceScaleId: "score" });
      chart.priceScale("score").applyOptions({ scaleMargins: { top: 0.84, bottom: 0.02 } });
      // ④ 과거 채점 결과 띠: 회귀선 바로 아래 가로선
      const bandSeries = chart.addCustomSeries(createResultBandPaneView(), {});
      // ⑥⑦ 방향 기억·지속 사진: 수평 계단선. 오래된 선은 현재 가격과 멀 수 있어 자동 스케일에서 제외
      const memSeries = chart.addCustomSeries(createStepLinesPaneView("mem"), { autoscaleInfoProvider: () => null });
      const pstSeries = chart.addCustomSeries(createStepLinesPaneView("pst"), { autoscaleInfoProvider: () => null });
      // ⑧ 마켓 밴드: 중심선(단계색)은 커스텀, 밴드 4선은 고정색 라인 (원본 Plot52~55)
      const mktCenterSeries = chart.addCustomSeries(createMarketCenterPaneView(), {});
      const mktU1 = chart.addLineSeries({ color: "rgb(255,120,120)", lineWidth: 1, priceLineVisible: false, lastValueVisible: false });
      const mktL1 = chart.addLineSeries({ color: "rgb(120,150,255)", lineWidth: 1, priceLineVisible: false, lastValueVisible: false });
      const mktU2 = chart.addLineSeries({ color: "rgb(255,0,0)", lineWidth: 1, priceLineVisible: false, lastValueVisible: false });
      const mktL2 = chart.addLineSeries({ color: "rgb(0,0,255)", lineWidth: 1, priceLineVisible: false, lastValueVisible: false });
      // ③ 미래 목표 광선: 마지막 봉 기준 수평 광선 9개 (목표 3 + 범위 6)
      const futureRays = createFutureRaysPrimitive();
      candleSeries.attachPrimitive(futureRays);

      let ctx = null;        // 마지막 applyLive/applySeed의 ctx — setLayers 재구축에 사용
      let lastMktCenter = NaN; // ⑧ 마켓중심기울기 = 중심 − 이전중심

      // ④ 결과 띠 다시 그리기 — 토글·시딩 시 barInd 캐시에서 전체 복원/제거
      function rebuildBand() {
        if (!layers.band || !ctx) {
          bandSeries.setData([]);
          return;
        }
        const data = [];
        const tick = ctx.tickRaw();
        for (let i = 0; i < ctx.barSeq.length; i++) {
          const ind = ctx.barInd.get(ctx.barSeq[i]);
          if (!ind || !Number.isFinite(ind.regFlat)) continue;
          const src = i >= 10 ? ctx.barInd.get(ctx.barSeq[i - 10]) : undefined;
          data.push({ time: ctx.barSeq[i], value: ind.regFlat - bandOffset(tick),
                      c: bandColor(src, ctx.sameSession(src, ind)) });
        }
        bandSeries.setData(data);
      }

      // ⑧ 마켓 밴드 다시 그리기 — 토글·시딩 시 barInd 캐시에서 전체 복원/제거
      function rebuildMktBand() {
        if (!layers.mktband || !ctx) {
          for (const s of [mktCenterSeries, mktU1, mktL1, mktU2, mktL2]) s.setData([]);
          return;
        }
        const cData = [], u1 = [], l1 = [], u2 = [], l2 = [];
        let prev = NaN;
        for (const t of ctx.barSeq) {
          const ind = ctx.barInd.get(t);
          const bar = ctx.bars.get(t);
          if (!ind || !ind.mktValid || !bar) continue;
          const [center, u1v, l1v, u2v, l2v] = ind.mkt;
          cData.push({ time: t, value: center,
                       stage: mktStage(prev, center, bar.close, ind.regFlat, u1v, ctx.tickRaw()) });
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

      // ③ 미래 목표 광선: 마지막 봉의 값으로 복원한다
      function rebuildRays() {
        if (!layers.rays || !ctx) {
          futureRays.clear();
          return;
        }
        const lastT = ctx.barSeq[ctx.barSeq.length - 1];
        const last = lastT === undefined ? undefined : ctx.barInd.get(lastT);
        if (last && last.regValid && Array.isArray(last.pred)
            && last.pred.length === 3 && last.pred.every(Number.isFinite)) {
          futureRays.set({
            time: lastT,
            prevTime: ctx.barSeq.length > 1 ? ctx.barSeq[ctx.barSeq.length - 2] : lastT - 60,
            pred: last.pred, predDir: last.predDir, r2: last.r2,
            resid: last.resid, pvol: last.pvol,
          });
        } else {
          futureRays.clear();
        }
      }

      // ①②⑤⑥⑦ 다시 그리기 — 레이어 칩 상태대로 캐시에서 전체 복원/제거
      function rebuildCore() {
        const regData = [];
        const scoreData = [];
        const memData = [];
        const pstData = [];
        if (ctx) {
          for (const t of ctx.barSeq) {
            const ind = ctx.barInd.get(t);
            if (!ind) continue;
            if (layers.score && Number.isFinite(ind.score)) {
              scoreData.push({ time: t, value: ind.score });
            }
            if ((layers.reg || layers.state) && ind.regValid && Number.isFinite(ind.regFlat)) {
              regData.push({ time: t, value: ind.regFlat,
                             score: Number.isFinite(ind.score) ? ind.score : 0, r2: ind.r2,
                             finalState: ind.finalValid ? ind.finalState : 0 });
            }
            if (layers.memory && ind.memItem) memData.push(ind.memItem);
            if (layers.snap && ind.pstItem) pstData.push(ind.pstItem);
          }
        }
        regLineSeries.setData(regData);
        scoreSeries.setData(scoreData);
        memSeries.setData(memData);
        pstSeries.setData(pstData);
      }

      function rebuildAll() {
        rebuildCore();
        rebuildBand();
        rebuildMktBand();
        rebuildRays();
      }

      return {
        setLayers(map) {
          let changed = false;
          for (const k of Object.keys(layers)) {
            if (k in map) {
              layers[k] = !!map[k];
              changed = true;
            }
          }
          if (changed) rebuildAll();
        },

        // 라이브 status 1건 — app.js가 barInd/barPos와 ⑥⑦ 아이템 캐시를 먼저 채운 뒤 부른다
        applyLive(p, c) {
          ctx = c;
          const t = Number(p.bar_open_time) / 1e6;
          if (!Number.isFinite(t) || t <= 0) return;
          const ind = c.barInd.get(t);
          if (!ind) return;
          const pos = c.barPos.get(t);
          const tick = c.tickRaw();

          // ② 회귀선 (reg_flat) + ⑤ 매매 상태 덧선. reg_valid==0이면 갭
          if (layers.reg || layers.state) {
            if (ind.regValid && Number.isFinite(ind.regFlat)) {
              regLineSeries.update({
                time: t, value: ind.regFlat,
                score: Number.isFinite(p.score) ? p.score : 0, r2: ind.r2,
                finalState: ind.finalValid ? ind.finalState : 0,
              });
            } else {
              regLineSeries.update({ time: t });
            }
          }

          // ③ 미래 목표 광선: 매 봉 값이 바뀌면 광선이 새 값으로 이동한다 (이력 없음)
          if (layers.rays) {
            const preds = p.pred ?? [];
            if (ind.regValid && preds.length === 3 && preds.every(Number.isFinite)) {
              futureRays.set({
                time: t, prevTime: pos > 0 ? c.barSeq[pos - 1] : t - 60,
                pred: preds, predDir: ind.predDir, r2: ind.r2,
                resid: p.resid ?? 0, pvol: p.pvol ?? 0,
              });
            } else {
              futureRays.clear();
            }
          }

          // ① 통합 점수 막대
          if (layers.score && typeof p.score === "number") {
            scoreSeries.update({ time: t, value: p.score });
          }

          // ④ 과거 채점 결과 띠: reg_flat − tick×4, 색은 10봉 전 예측방향2·신뢰도 기준
          if (layers.band && Number.isFinite(ind.regFlat)) {
            const src = pos !== undefined && pos >= 10 ? c.barInd.get(c.barSeq[pos - 10]) : undefined;
            bandSeries.update({ time: t, value: ind.regFlat - bandOffset(tick),
                                c: bandColor(src, c.sameSession(src, ind)) });
          }

          // ⑧ 마켓 밴드: [valid, center, u1, l1, u2, l2] — 레이어가 켜져 있을 때만 표시
          if (Array.isArray(p.mkt) && p.mkt[0] === 1) {
            const [, center, u1, l1, u2, l2] = p.mkt;
            if (layers.mktband) {
              const close = Array.isArray(p.ohlc) ? p.ohlc[3] : undefined;
              mktCenterSeries.update({ time: t, value: center, stage: mktStage(lastMktCenter, center, close, ind.regFlat, u1, tick) });
              mktU1.update({ time: t, value: u1 });
              mktL1.update({ time: t, value: l1 });
              mktU2.update({ time: t, value: u2 });
              mktL2.update({ time: t, value: l2 });
            }
            lastMktCenter = center;
          } else if (layers.mktband) {
            mktCenterSeries.update({ time: t });
            mktU1.update({ time: t });
            mktL1.update({ time: t });
            mktU2.update({ time: t });
            mktL2.update({ time: t });
          }

          // ⑥⑦ 방향 기억·지속 사진: 봉별 아이템은 app.js가 캐시에 심는다. 여기서는 표시만.
          if (layers.memory && Array.isArray(p.mem)) {
            memSeries.update(ind.memItem ?? { time: t });
          }
          if (layers.snap && Array.isArray(p.pst)) {
            pstSeries.update(ind.pstItem ?? { time: t });
          }
        },

        // 시딩 완료 후 전체 복원 — ctx 캐시(barInd/barSeq)만으로 다시 그린다
        applySeed(c) {
          ctx = c;
          rebuildAll();
        },

        clear() {
          for (const s of [regLineSeries, scoreSeries, bandSeries, memSeries, pstSeries,
                           mktCenterSeries, mktU1, mktL1, mktU2, mktL2]) s.setData([]);
          futureRays.clear();
          lastMktCenter = NaN;
        },

        destroy() {
          for (const s of [regLineSeries, scoreSeries, bandSeries, memSeries, pstSeries,
                           mktCenterSeries, mktU1, mktL1, mktU2, mktL2]) chart.removeSeries(s);
          candleSeries.detachPrimitive(futureRays);
        },
      };
    },
  };

  return {
    SPANS, MIN_R2, HIGH_R2, bandOffset,
    scoreColor, regWidth, tradeStyle, bandColor, rangeFlags, mktStage, mktStageColor,
    parseInd, barIndFromInd, barIndFromPayload, memItemFromPayload, pstItemFromPayload,
    buildMemItems,
    createRegLinePaneView, createScoreBarPaneView, createResultBandPaneView,
    createStepLinesPaneView, createMarketCenterPaneView, createFutureRaysPrimitive,
    MiraeRenderer,
  };
})();

if (typeof globalThis !== "undefined") {
  globalThis.MiraeLayers = MiraeLayers;
}
