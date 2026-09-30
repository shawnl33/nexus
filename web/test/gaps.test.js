// 시간축 구멍 whitespace 펼침(gaps.js) 단위 테스트.
// 스냅샷 gaps 구간(초)이 캔들 사이에 {time}만 가진 whitespace 항목으로 섞이는지 검증한다.
// 시리즈 길이 = 봉 수 + whitespace 수 라는 계약(pane-sync getLength, app.js 주석)도 핀한다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/gaps.js");
const Gaps = globalThis.Gaps;

const bar = (t) => ({ time: t, open: 1, high: 2, low: 0, close: 1 });
const times = (rows) => rows.map((r) => r.time);

test("withWhitespace: 단일 구멍이 분 단위 포인트로 펼쳐져 봉 사이에 들어간다", () => {
  const bars = [0, 60, 300, 360].map(bar);
  const mixed = Gaps.withWhitespace(bars, [[120, 240]]);
  assert.deepEqual(times(mixed), [0, 60, 120, 180, 240, 300, 360]);
  // whitespace 항목은 time만 가진다 (가격 필드가 있으면 캔들로 그려진다)
  for (const t of [120, 180, 240]) {
    assert.deepEqual(Object.keys(mixed.find((r) => r.time === t)), ["time"]);
  }
  // 봉 항목은 OHLC를 그대로 보존한다
  assert.equal(mixed.find((r) => r.time === 300).close, 1);
  // 시리즈 길이 계약: 봉 4 + whitespace 3
  assert.equal(mixed.length - bars.length, 3);
});

test("withWhitespace: 여러 구멍이 각각 펼쳐지고 전체 오름차순이 유지된다", () => {
  const bars = [0, 60, 180, 600].map(bar);
  const mixed = Gaps.withWhitespace(bars, [[120, 120], [240, 540]]);
  assert.deepEqual(times(mixed),
    [0, 60, 120, 180, 240, 300, 360, 420, 480, 540, 600]);
  assert.equal(mixed.length - bars.length, 7); // 1 + 6
});

test("withWhitespace: 구멍이 없으면 봉만 담긴 새 배열이다 (wsCount 0 계약)", () => {
  const bars = [0, 60, 120].map(bar);
  for (const gaps of [undefined, null, []]) {
    const mixed = Gaps.withWhitespace(bars, gaps);
    assert.deepEqual(times(mixed), [0, 60, 120]);
    assert.equal(mixed.length - bars.length, 0);
    assert.notEqual(mixed, bars); // 복사본 — 호출자 배열을 건드리지 않는다
  }
});

test("withWhitespace: 봉에 맞붙은 1분짜리 구멍(경계 구멍)도 포인트 1개로 나온다", () => {
  const bars = [0, 120].map(bar);
  const mixed = Gaps.withWhitespace(bars, [[60, 60]]);
  assert.deepEqual(times(mixed), [0, 60, 120]);
  assert.deepEqual(Object.keys(mixed[1]), ["time"]);
});

test("withWhitespace: 봉과 겹치는 포인트는 만들지 않는다 (중복 time 방어)", () => {
  const bars = [0, 60, 180].map(bar);
  // 잘못된 구간이 봉 시각(60, 180)을 덮어도 그 자리는 건너뛴다
  const mixed = Gaps.withWhitespace(bars, [[60, 180]]);
  assert.deepEqual(times(mixed), [0, 60, 120, 180]);
});

test("withWhitespace: 빈 봉 목록·깨진 구간은 건너뛴다", () => {
  assert.deepEqual(Gaps.withWhitespace([], [[60, 120]]), [{ time: 60 }, { time: 120 }]);
  const bars = [0, 60].map(bar);
  assert.deepEqual(times(Gaps.withWhitespace(bars, [["x", 120], null, [180, "y"]])),
    [0, 60]);
});
