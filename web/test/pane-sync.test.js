// 칸 동기화(pane-sync.js) 단위 테스트.
// DOM 없이 lightweight-charts 인터페이스 목으로 검증한다: 시간축/크로스헤어 전파,
// 프로그램적 변경의 뮤트, 재진입(무한 루프) 가드, 구독 해제. 목은 적용 호출이 리스너를
// 동기적으로 다시 발생시키는 worst case를 흉내 낸다. (실측: lightweight-charts 4.2.3은
// setVisibleLogicalRange/scrollToRealTime을 rAF에서 비동기 발생시키고, candleSeries.
// update는 동기 + 다음 프레임 비동기를 함께 낸다 — 어느 쪽이든 뮤트·가드 계약은 같다.)

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/pane-sync.js");
const PaneSync = globalThis.PaneSync;

// lightweight-charts 차트 목. 호출 기록(calls)과 리스너 목록(listeners)을 노출한다.
// autofire=true면 적용 호출이 리스너를 동기적으로 다시 발생시키는 worst case를 흉내 낸다.
// false면 적용은 기록만 한다 — 실제 라이브러리처럼 에코를 나중에 수동으로 흘리는 용도.
function mockChart(autofire = true) {
  const calls = { setRange: [], setPos: [], clear: 0 };
  const listeners = { range: new Set(), cross: new Set() };
  const ts = {
    subscribeVisibleLogicalRangeChange: (cb) => listeners.range.add(cb),
    unsubscribeVisibleLogicalRangeChange: (cb) => listeners.range.delete(cb),
    setVisibleLogicalRange: (range) => {
      calls.setRange.push(range);
      if (autofire) for (const cb of [...listeners.range]) cb(range); // 적용이 다시 이벤트를 일으킨다
    },
  };
  const chart = {
    timeScale: () => ts,
    subscribeCrosshairMove: (cb) => listeners.cross.add(cb),
    unsubscribeCrosshairMove: (cb) => listeners.cross.delete(cb),
    setCrosshairPosition: (price, time, series) => {
      calls.setPos.push({ price, time, series });
      for (const cb of [...listeners.cross]) cb({ time }); // 재진입 시나리오
    },
    clearCrosshairPosition: () => { calls.clear++; },
  };
  return { chart, calls, listeners };
}

function fireRange(m, range) {
  for (const cb of [...m.listeners.range]) cb(range);
}
function fireCross(m, param) {
  for (const cb of [...m.listeners.cross]) cb(param);
}

const BARS = new Map([
  [1000, { time: 1000, open: 10, high: 12, low: 9, close: 11 }],
  [1060, { time: 1060, open: 11, high: 13, low: 10, close: 12 }],
]);
const getPrice = (t) => BARS.get(t)?.close;

function setup(n) {
  const sync = PaneSync.create(getPrice);
  const ms = Array.from({ length: n }, mockChart);
  const series = ms.map((_, i) => ({ id: `s${i}` }));
  const handles = ms.map((m, i) => sync.add(m.chart, series[i]));
  return { sync, ms, series, handles };
}

test("시간축: 한 칸의 범위 변경이 나머지 칸에 같은 범위로 적용된다", () => {
  const { ms } = setup(3);
  const range = { from: 5, to: 40 };
  fireRange(ms[0], range);
  assert.deepEqual(ms[1].calls.setRange, [range]);
  assert.deepEqual(ms[2].calls.setRange, [range]);
  assert.deepEqual(ms[0].calls.setRange, []); // 발생 칸에는 적용하지 않는다
});

test("시간축: 적용이 다시 이벤트를 일으켜도 무한 루프 없이 1회만 적용된다 (에코 억제)", () => {
  const { ms } = setup(2);
  fireRange(ms[0], { from: 0, to: 10 });
  // 목은 setVisibleLogicalRange 안에서 리스너를 동기 재발생시킨다.
  // 적용 값과 같은 에코는 expectEcho가 삼킨다 — 억제가 없으면 A↔B를 오가며 누적된다.
  assert.equal(ms[1].calls.setRange.length, 1);
  assert.equal(ms[0].calls.setRange.length, 0);
});

test("시간축: 칸이 1개면 아무 일도 하지 않는다", () => {
  const { ms } = setup(1);
  fireRange(ms[0], { from: 0, to: 10 });
  fireCross(ms[0], { time: 1000 });
  assert.equal(ms[0].calls.setRange.length, 0);
  assert.equal(ms[0].calls.setPos.length, 0);
});

test("시간축: null 범위(데이터 없는 차트)는 전파하지 않는다", () => {
  const { ms } = setup(2);
  fireRange(ms[0], null);
  assert.equal(ms[1].calls.setRange.length, 0);
});

test("크로스헤어: 같은 시각에 상대 칸 시리즈·그 시각 봉 종가로 위치를 찍는다", () => {
  const { ms, series } = setup(2);
  fireCross(ms[0], { time: 1000 });
  assert.deepEqual(ms[1].calls.setPos, [{ price: 11, time: 1000, series: series[1] }]);

  // 반대 방향도 동일하게 동작한다
  fireCross(ms[1], { time: 1060 });
  assert.deepEqual(ms[0].calls.setPos, [{ price: 12, time: 1060, series: series[0] }]);
});

test("크로스헤어: 마우스 이탈(time 없음)이면 나머지 칸도 지운다", () => {
  const { ms } = setup(3);
  fireCross(ms[1], {});
  assert.equal(ms[0].calls.clear, 1);
  assert.equal(ms[2].calls.clear, 1);
  assert.equal(ms[1].calls.clear, 0); // 발생 칸은 라이브러리가 스스로 지운다
});

test("크로스헤어: 캐시에 없는 시각은 위치를 찍지 않는다", () => {
  const { ms } = setup(2);
  fireCross(ms[0], { time: 9999 }); // BARS에 없음
  assert.equal(ms[1].calls.setPos.length, 0);
  assert.equal(ms[1].calls.clear, 0);
});

test("크로스헤어: 칸별 getPrice가 있으면 공유 값보다 우선한다 (종목별 가격)", () => {
  const sync = PaneSync.create(getPrice);
  const a = mockChart(), b = mockChart();
  const bBars = new Map([[1000, { time: 1000, close: 555 }]]); // B 칸 종목의 캐시
  sync.add(a.chart, { id: "sa" });
  sync.add(b.chart, { id: "sb" }, { getPrice: (t) => bBars.get(t)?.close });

  fireCross(a, { time: 1000 });
  // B 칸에는 공유 캐시(11)가 아니라 B 종목 캐시의 555로 찍힌다
  assert.deepEqual(b.calls.setPos, [{ price: 555, time: 1000, series: { id: "sb" } }]);

  fireCross(b, { time: 1000 });
  // A 칸은 칸별 getPrice가 없으므로 create의 공유 값을 쓴다
  assert.deepEqual(a.calls.setPos, [{ price: 11, time: 1000, series: { id: "sa" } }]);
});

// 길이를 아는 칸끼리의 시간축 전파 — 종목별 데이터 길이가 다른 다중 종목 대응.
// 선물(2400봉)과 주식(499봉)처럼 길이가 달라도 같은 인덱스를 억지로 맞추지 않는다.
function setupWithLengths(lens, autofire = true) {
  const sync = PaneSync.create();
  const ms = lens.map(() => mockChart(autofire));
  const handles = ms.map((m, i) => sync.add(m.chart, { id: `s${i}` }, { getLength: () => lens[i] }));
  return { sync, ms, handles };
}

test("시간축(에코): 적용한 범위가 그대로 돌아오면 다시 전파하지 않는다 (비동기 에코 억제)", () => {
  const { ms } = setupWithLengths([2400, 499], false); // 실제 라이브러리처럼 에코는 수동으로 흘린다
  // 사용자가 선물 칸의 깊은 과거로 간다 — 주식 칸 데이터 밖이라 꼬리 창으로 클램프된다
  fireRange(ms[0], { from: 1000, to: 1100 });
  assert.deepEqual(ms[1].calls.setRange, [{ from: 398, to: 498 }]);
  // 실제 라이브러리는 적용 값과 같은 범위 이벤트를 rAF에서 돌려준다 (에코) —
  // 이 에코가 다시 전파되면 꼬리 에코가 발생 칸을 꼬리로 끌어간다
  fireRange(ms[1], { from: 398, to: 498 }); // 적용 값과 정확히 일치 → 에코
  assert.deepEqual(ms[0].calls.setRange, []); // 발생 칸은 자리를 지킨다
  // 에코는 1회성이다: 같은 값이 다시 오면(사용자가 그 범위로 움직임) 전파한다
  fireRange(ms[1], { from: 398, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, [{ from: 2299, to: 2399 }]); // 꼬리 정렬 규칙
});

test("시간축(에코): 에코와 다른 값이 먼저 오면 실제 변경으로 전파한다", () => {
  const { ms } = setupWithLengths([499, 499], false);
  fireRange(ms[0], { from: 100, to: 200 });
  assert.deepEqual(ms[1].calls.setRange, [{ from: 100, to: 200 }]);
  // 에코({100,200})가 오기 전에 사용자가 그 칸을 움직였다 — 그대로 전파한다
  fireRange(ms[1], { from: 150, to: 250 });
  assert.deepEqual(ms[0].calls.setRange, [{ from: 150, to: 250 }]);
});

test("시간축(가드): 적용이 조정된 값으로 동기 재발생해도 무한 루프 없이 정착한다", () => {
  // 적용 호출이 조정된 범위(클램프 등)로 리스너를 동기 재발생시키는 목 —
  // 에코 값이 달라 억제가 못 삼키는 경우 syncing 가드가 루프를 막는다.
  const sync = PaneSync.create();
  const adjusting = [];
  const make = () => {
    const calls = [];
    const listeners = new Set();
    const ts = {
      subscribeVisibleLogicalRangeChange: (cb) => listeners.add(cb),
      unsubscribeVisibleLogicalRangeChange: (cb) => listeners.delete(cb),
      setVisibleLogicalRange: (range) => {
        calls.push(range);
        const adjusted = { from: range.from + 0.5, to: range.to + 0.5 }; // 조정된 에코
        for (const cb of [...listeners]) cb(adjusted);
      },
    };
    const chart = {
      timeScale: () => ts,
      subscribeCrosshairMove: () => {},
      unsubscribeCrosshairMove: () => {},
      setCrosshairPosition: () => {},
      clearCrosshairPosition: () => {},
    };
    const m = { chart, calls, fire: (r) => { for (const cb of [...listeners]) cb(r); } };
    adjusting.push(m);
    return m;
  };
  const a = make(), b = make();
  sync.add(a.chart, { id: "sa" });
  sync.add(b.chart, { id: "sb" });
  a.fire({ from: 0, to: 10 });
  assert.equal(b.calls.length, 1); // 1회 적용 후 정착 (조정 에코는 가드가 삼킨다)
  assert.equal(a.calls.length, 0);
});

test("시간축(꼬리): 발생 칸이 최신에 붙어 있으면 대상 칸은 길이 무관하게 자기 최신 창으로 간다", () => {
  const { ms } = setupWithLengths([2400, 499]);
  fireRange(ms[1], { from: 449, to: 498 }); // 주식 칸이 자기 꼬리 (사용자 제스처)
  // 선물 칸은 449~498(자기 데이터 중간)이 아니라 자기 최신 창으로 복귀한다
  assert.deepEqual(ms[0].calls.setRange, [{ from: 2399 - 49, to: 2399 }]);
});

test("시간축(꼬리): 사용자의 꼬리 정렬은 과거 탐색 중인 칸도 끌어온다 (예외 없음)", () => {
  const { ms } = setupWithLengths([2400, 499]);
  // 사용자가 두 칸을 함께 과거로 이동했다 (중간 창은 같은 범위로 전파된다)
  fireRange(ms[0], { from: 100, to: 200 });
  assert.deepEqual(ms[1].calls.setRange, [{ from: 100, to: 200 }]);
  // 사용자가 주식 칸을 꼬리로 돌린다 — 과거 탐색 중이던 선물 칸도 예외 없이 끌려온다
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, [{ from: 2399 - 49, to: 2399 }]);
});

test("시간축(꼬리): 최신을 따라가는 칸은 꼬리 전파를 계속 받는다", () => {
  const { ms } = setupWithLengths([2400, 499]);
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, [{ from: 2350, to: 2399 }]);
  // 꼬리 정렬은 반복돼도 매번 그대로 적용된다
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, [{ from: 2350, to: 2399 }, { from: 2350, to: 2399 }]);
});

test("시간축(꼬리): 최대 축소의 꼬리 정렬은 모든 칸이 전체를 보게 한다", () => {
  const { ms } = setupWithLengths([2400, 499]);
  fireRange(ms[0], { from: -100, to: 2399 }); // 선물 칸을 전체가 보이게 최대 축소 (꼬리 붙음)
  // 주식 칸은 같은 폭의 자기 최신 창 — 폭(2499)이 데이터(499)보다 커 전체가 보인다
  assert.deepEqual(ms[1].calls.setRange, [{ from: 498 - 2499, to: 498 }]);
});

test("시간축(뮤트): 뮤트된 칸의 범위 이벤트는 전파되지 않는다 (시딩·라이브 꼬리 이동)", () => {
  const { sync, ms, handles } = setupWithLengths([2400, 499]);
  sync.mute(handles[1]);
  // 프로그램적 변경(시딩 scrollToRealTime·라이브 update) — 꼬리 창이어도 전파되지 않는다
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, []);
  // 중간 창도 마찬가지
  fireRange(ms[1], { from: 100, to: 200 });
  assert.deepEqual(ms[0].calls.setRange, []);
  // 뮤트된 칸도 다른 칸의 사용자 변경은 그대로 받는다 (대상 규칙은 변함없다)
  fireRange(ms[0], { from: 300, to: 400 });
  assert.deepEqual(ms[1].calls.setRange, [{ from: 300, to: 400 }]);
});

test("시간축(뮤트): unmute하면 다시 전파된다", () => {
  const { sync, ms, handles } = setupWithLengths([2400, 499]);
  sync.mute(handles[1]);
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, []);
  sync.unmute(handles[1]);
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, [{ from: 2350, to: 2399 }]);
});

test("시간축(뮤트): 뮤트는 중첩을 센다 — 겹친 뮤트 창이 서로를 풀지 않는다", () => {
  const { sync, ms, handles } = setupWithLengths([2400, 499]);
  sync.mute(handles[1]);
  sync.mute(handles[1]);
  sync.unmute(handles[1]); // 한 겹만 풀림 — 아직 뮤트 상태
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, []);
  sync.unmute(handles[1]);
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, [{ from: 2350, to: 2399 }]);
  // 바닥 아래로는 내려가지 않는다
  sync.unmute(handles[1]);
  fireRange(ms[1], { from: 449, to: 498 });
  assert.deepEqual(ms[0].calls.setRange, [{ from: 2350, to: 2399 }, { from: 2350, to: 2399 }]);
});

test("시간축(클램프): 중간 창이 대상 칸 데이터 밖이면 가장 가까운 유효 창으로 이동한다", () => {
  const { ms } = setupWithLengths([2400, 499]);
  fireRange(ms[0], { from: 1000, to: 1100 }); // 선물 칸의 중간 구간 탐색 (꼬리 아님)
  // 주식 칸(499봉)에는 1000~1100이 데이터 밖 → 최신 창으로 클램프
  assert.deepEqual(ms[1].calls.setRange, [{ from: 498 - 100, to: 498 }]);
});

test("시간축(클램프): 첫 봉보다 왼쪽 창은 대상 칸의 첫 창으로 이동한다", () => {
  const { ms } = setupWithLengths([499, 2400]);
  fireRange(ms[0], { from: -30, to: -10 }); // 왼쪽 여백 너머 (꼬리 아님)
  assert.deepEqual(ms[1].calls.setRange, [{ from: 0, to: 20 }]);
});

test("시간축: 데이터 없는 칸(길이 0)에는 범위를 적용하지 않는다", () => {
  const { ms } = setupWithLengths([2400, 0]);
  fireRange(ms[0], { from: 2350, to: 2399 }); // 꼬리 창이어도
  fireRange(ms[0], { from: 100, to: 200 });   // 중간 창이어도
  assert.equal(ms[1].calls.setRange.length, 0);
});

test("시간축: 데이터와 겹치는 중간 창은 같은 논리 범위를 그대로 적용한다", () => {
  const { ms } = setupWithLengths([2400, 499]);
  fireRange(ms[0], { from: 300, to: 400 }); // 2400봉 칸의 중간 창 — 주식 칸 데이터와 겹침
  assert.deepEqual(ms[1].calls.setRange, [{ from: 300, to: 400 }]);
});

// 시각 도메인 동기화: 칸마다 getTimes(시리즈 항목별 시각, 초 오름차순)를 제공한다.
// 봉 수·구멍(whitespace) 수가 종목마다 달라도 같은 '시계 창'으로 맞추는지 검증한다.
function setupWithTimes(specs, autofire = true) {
  const sync = PaneSync.create();
  const ms = specs.map(() => mockChart(autofire));
  const handles = ms.map((m, i) => sync.add(m.chart, { id: `s${i}` }, {
    getLength: () => specs[i].times.length,
    getTimes: () => specs[i].times,
  }));
  return { sync, ms, handles };
}

// 60초 균등 시리즈 시각 목록
function uniformTimes(start, n) {
  return Array.from({ length: n }, (_, i) => start + i * 60);
}

test("시간축(시각): 시작 시각·길이가 다른 칸끼리도 같은 '시계 창'으로 맞춘다", () => {
  // A: 1000초부터 500봉, B: 120분 늦게 시작하는 380봉 — 논리 인덱스 기준이면 어긋나는 조합
  const aT = uniformTimes(1000, 500);
  const bT = uniformTimes(1000 + 120 * 60, 380);
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: 200, to: 299 }); // A의 시각 창 [13000, 18940]
  // B는 같은 인덱스 200~299(자기 시각으로는 엉뚱한 창)가 아니라 같은 시각 창의 80~179로 간다
  assert.deepEqual(ms[1].calls.setRange, [{ from: 80, to: 179 }]);
  // 적용된 논리 범위가 가리키는 시각이 발생 칸과 정확히 같다
  const applied = ms[1].calls.setRange[0];
  assert.equal(bT[applied.from], aT[200]);
  assert.equal(bT[applied.to], aT[299]);

  // 실제 라이브러리라면 적용 값이 에코로 돌아와 삼켜진다 — 그 뒤 사용자가 되돌리면 전파한다
  fireRange(ms[1], { from: 80, to: 179 }); // 적용 값과 정확히 일치 → 에코, 삼킨다
  assert.deepEqual(ms[0].calls.setRange, []);
  fireRange(ms[1], { from: 80, to: 179 }); // 같은 값의 사용자 제스처 — 반대 방향도 시각 왕복
  assert.deepEqual(ms[0].calls.setRange, [{ from: 200, to: 299 }]);
});

test("시간축(시각): 소수 인덱스는 양옆 항목 시각으로 보간해 환산한다", () => {
  const aT = uniformTimes(1000, 500);
  const bT = uniformTimes(1000, 500);
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: 100.5, to: 200.5 }); // 시각 창 [7030, 13030]
  // 첫 >= 7030은 7060(인덱스 101), 마지막 <= 13030은 13000(인덱스 200)
  assert.deepEqual(ms[1].calls.setRange, [{ from: 101, to: 200 }]);
});

test("시간축(시각): 데이터 밖 인덱스는 양끝 간격으로 외삽해 시각으로 환산한다", () => {
  const aT = uniformTimes(1000, 500);
  const bT = uniformTimes(1000, 300);
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: -5, to: 100 }); // 왼쪽 초과 — tFrom = 1000 - 5*60 = 700으로 외삽
  // B는 700 이상의 첫 항목(인덱스 0)부터 7000(인덱스 100)까지
  assert.deepEqual(ms[1].calls.setRange, [{ from: 0, to: 100 }]);
});

test("시간축(시각): 19분 구멍(비균등 간격)을 건너는 창도 같은 시각 폭으로 맞춘다", () => {
  // A: 인덱스 99와 100 사이에 19분 구멍 — whitespace 없이 시각만 건너뜀 (라이브 꼬리 형태)
  const aT = [...uniformTimes(1000, 100), ...uniformTimes(1000 + 119 * 60, 100)];
  const bT = uniformTimes(1000, 300); // B는 구멍 없이 매분 있다
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: 95, to: 105 }); // A 시각 창 [6700, 8440] — 11봉이 29분을 덮는다
  // B는 같은 시각 창을 덮는 95~124(30봉) — 봉 수는 달라도 시계 창은 같다
  assert.deepEqual(ms[1].calls.setRange, [{ from: 95, to: 124 }]);
});

test("시간축(시각): 창이 대상 칸의 봉 사이(구멍)에 들어가면 가장 가까운 봉 하나를 보여준다", () => {
  const aT = uniformTimes(1000, 500);
  const bT = [1000, 1060, 2080, 2140]; // 1060과 2080 사이 큰 구멍
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: 2, to: 15 }); // A 시각 창 [1120, 1900] — B의 구멍 안에 떨어진다
  // 구멍 양옆 봉(1060, 2080) 중 창 중심(1510)에 가까운 1060(인덱스 1)으로 모은다
  assert.deepEqual(ms[1].calls.setRange, [{ from: 1, to: 1 }]);
});

test("시간축(시각 꼬리): 꼬리 정렬은 대상 칸을 같은 '시각 폭'의 자기 최신 창으로 보낸다", () => {
  const aT = uniformTimes(1000, 500); // 끝 30940
  const bT = uniformTimes(8200, 380); // 끝 30940 (같은 말단, 120분 늦게 시작)
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: 450, to: 499 }); // A 꼬리 — 시각 폭 2940초(49분)
  // B는 인덱스 폭(49)이 아니라 시각 폭(49분)으로 자기 꼬리: [28000, 30940] → 330~379
  assert.deepEqual(ms[1].calls.setRange, [{ from: 330, to: 379 }]);
  const applied = ms[1].calls.setRange[0];
  assert.equal(bT[applied.from], aT[450]); // 시작 시각도 같다
  assert.equal(bT[applied.to], aT[499]);
});

test("시간축(시각 꼬리): 시각 폭이 대상 칸 데이터보다 길면 종전 인덱스 폭 창으로 되돌린다", () => {
  const aT = uniformTimes(1000, 500);
  const bT = uniformTimes(8200, 380); // B의 과거는 380분뿐
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: 0, to: 499 }); // A 전체(499분) 꼬리 정렬 — B의 과거를 넘는 시각 폭
  assert.deepEqual(ms[1].calls.setRange, [{ from: 379 - 499, to: 379 }]); // 종전 인덱스 폭 규칙
});

test("시간축(시각): 시각 창이 대상 칸 데이터보다 앞이면 그 칸의 첫 창으로 클램프한다", () => {
  const aT = uniformTimes(1000, 500);
  const bT = uniformTimes(100000, 300); // B는 A보다 훨씬 뒤 시간대 — 시각이 전혀 겹치지 않는다
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: 400, to: 490 }); // A 시각 창 [25000, 30400] — B 시작(100000) 이전
  // 무겹침 폴백: 종전 규칙대로 논리 범위를 B 길이에 클램프 (오른쪽 초과 → 같은 폭의 최신 창)
  assert.deepEqual(ms[1].calls.setRange, [{ from: 299 - 90, to: 299 }]);
});

test("시간축(시각): 시각 창이 대상 칸 데이터보다 뒤면 그 칸의 최신 창으로 클램프한다", () => {
  const aT = uniformTimes(1000, 500);
  const bT = uniformTimes(1000, 200); // B는 12940에서 끝난다
  const { ms } = setupWithTimes([{ times: aT }, { times: bT }], false);
  fireRange(ms[0], { from: 400, to: 490 }); // A 시각 창 [25000, 30400] — B 끝(12940) 이후 (꼬리 아님)
  assert.deepEqual(ms[1].calls.setRange, [{ from: 109, to: 199 }]); // 같은 폭의 최신 창
});

test("시간축(호환): getTimes 없는 칸은 종전 논리 규칙(인덱스 클램프·꼬리 폭)을 따른다", () => {
  const sync = PaneSync.create();
  const a = mockChart(false), b = mockChart(false);
  const aT = uniformTimes(1000, 500);
  sync.add(a.chart, { id: "sa" }, { getLength: () => 500, getTimes: () => aT });
  sync.add(b.chart, { id: "sb" }, { getLength: () => 499 }); // 시각 정보 없음
  fireRange(a, { from: 200, to: 299 }); // 중간 창 — 종전대로 같은 인덱스 클램프
  assert.deepEqual(b.calls.setRange, [{ from: 200, to: 299 }]);
  fireRange(a, { from: 450, to: 499 }); // 꼬리 — 종전대로 인덱스 폭의 최신 창
  assert.deepEqual(b.calls.setRange[1], { from: 498 - 49, to: 498 });
  // 발생 칸에 시각 정보가 없어도 대상 칸은 논리 규칙으로 받는다
  fireRange(b, { from: 100, to: 200 });
  assert.deepEqual(a.calls.setRange, [{ from: 100, to: 200 }]);
});

test("칸 삭제: remove 후에는 구독이 해제되어 더 이상 전파되지 않는다", () => {
  const { sync, ms, handles } = setup(2);
  sync.remove(handles[1]);
  assert.equal(sync.size, 1);
  // 리스너 누수 없이 해제되었는지
  assert.equal(ms[1].listeners.range.size, 0);
  assert.equal(ms[1].listeners.cross.size, 0);

  fireRange(ms[0], { from: 0, to: 10 });
  fireCross(ms[0], { time: 1000 });
  assert.equal(ms[1].calls.setRange.length, 0);
  assert.equal(ms[1].calls.setPos.length, 0);

  sync.remove(handles[1]); // 이중 해제는 무해
  assert.equal(sync.size, 1);
});
