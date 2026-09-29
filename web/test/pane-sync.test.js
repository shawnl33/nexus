// 칸 동기화(pane-sync.js) 단위 테스트.
// DOM 없이 lightweight-charts 인터페이스 목으로 검증한다: 시간축/크로스헤어 전파,
// 재진입(무한 루프) 가드, 구독 해제. 목은 적용 호출이 리스너를 동기적으로 다시
// 발생시키는 worst case를 흉내 낸다 (실제 라이브러리도 setVisibleLogicalRange가
// subscribeVisibleLogicalRangeChange를 동기 발생시킨다).

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/pane-sync.js");
const PaneSync = globalThis.PaneSync;

// lightweight-charts 차트 목. 호출 기록(calls)과 리스너 목록(listeners)을 노출한다.
function mockChart() {
  const calls = { setRange: [], setPos: [], clear: 0 };
  const listeners = { range: new Set(), cross: new Set() };
  const ts = {
    subscribeVisibleLogicalRangeChange: (cb) => listeners.range.add(cb),
    unsubscribeVisibleLogicalRangeChange: (cb) => listeners.range.delete(cb),
    setVisibleLogicalRange: (range) => {
      calls.setRange.push(range);
      for (const cb of [...listeners.range]) cb(range); // 적용이 다시 이벤트를 일으킨다
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

test("시간축: 적용이 다시 이벤트를 일으켜도 무한 루프 없이 1회만 적용된다 (가드)", () => {
  const { ms } = setup(2);
  fireRange(ms[0], { from: 0, to: 10 });
  // 목은 setVisibleLogicalRange 안에서 리스너를 동기 재발생시킨다.
  // 가드가 없으면 A↔B를 오가며 호출이 누적된다.
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
  sync.add(b.chart, { id: "sb" }, (t) => bBars.get(t)?.close);

  fireCross(a, { time: 1000 });
  // B 칸에는 공유 캐시(11)가 아니라 B 종목 캐시의 555로 찍힌다
  assert.deepEqual(b.calls.setPos, [{ price: 555, time: 1000, series: { id: "sb" } }]);

  fireCross(b, { time: 1000 });
  // A 칸은 칸별 getPrice가 없으므로 create의 공유 값을 쓴다
  assert.deepEqual(a.calls.setPos, [{ price: 11, time: 1000, series: { id: "sa" } }]);
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
