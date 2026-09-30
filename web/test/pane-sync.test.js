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
