// 균일 분 그리드 채움(gaps.js) 단위 테스트.
// 연속 봉 사이의 1분 초과 공백이 전부 분당 1칸 {time}만 가진 whitespace 항목으로 섞이는지,
// 스팬(첫~마지막 봉) 밖은 채우지 않는지, 그리고 이 변경의 목적인 균일 그리드 성질
// (구멍 패턴이 다른 두 시리즈를 채우면 같은 시각이 같은 인덱스에 온다)을 검증한다.
// 시리즈 길이 = 봉 수 + whitespace 수 라는 계약(pane-sync getLength, app.js 주석)도 핀다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/gaps.js");
const Gaps = globalThis.Gaps;

const bar = (t) => ({ time: t, open: 1, high: 2, low: 0, close: 1 });
const times = (rows) => rows.map((r) => r.time);
// from(포함)~to(미포함) 매분 목록
const minutes = (from, to) => {
  const out = [];
  for (let t = from; t < to; t += 60) out.push(t);
  return out;
};
// 1칸=1분 불변식: 인접 항목 시각 차가 전부 60초
const assertUniformGrid = (rows) => {
  for (let i = 1; i < rows.length; i++) {
    assert.equal(rows[i].time - rows[i - 1].time, 60, `인덱스 ${i}의 간격`);
  }
};

test("whitespaceRuns: 가격 없는 연속 시각만 구간으로 묶는다", () => {
  const rows = [
    { time: 0, open: 1 },
    { time: 60 },
    { time: 120 },
    { time: 180, open: 1 },
    { time: 240, value: 2 },
    { time: 300 },
    { time: 360, close: 1 },
  ];
  assert.deepEqual(Gaps.whitespaceRuns(rows), [
    { start: 60, end: 120 },
    { start: 300, end: 300 },
  ]);
});

test("whitespaceRuns: 봉만 있거나 비어 있으면 구간이 없다", () => {
  assert.deepEqual(Gaps.whitespaceRuns([]), []);
  assert.deepEqual(Gaps.whitespaceRuns(undefined), []);
  assert.deepEqual(Gaps.whitespaceRuns([{ time: 0, open: 1 }, { time: 60, close: 2 }]), []);
});

test("whitespaceRuns: 시리즈 앞뒤의 공백도 구간이다", () => {
  assert.deepEqual(Gaps.whitespaceRuns([
    { time: 0 },
    { time: 60, open: 0 },
    { time: 120 },
  ]), [
    { start: 0, end: 0 },
    { start: 120, end: 120 },
  ]);
});

test("withWhitespace: 봉 사이 공백이 분 단위 포인트로 펼쳐져 들어간다 (1칸=1분)", () => {
  const bars = [0, 60, 300, 360].map(bar);
  const mixed = Gaps.withWhitespace(bars);
  assert.deepEqual(times(mixed), [0, 60, 120, 180, 240, 300, 360]);
  assertUniformGrid(mixed);
  // whitespace 항목은 time만 가진다 (가격 필드가 있으면 캔들로 그려진다)
  for (const t of [120, 180, 240]) {
    assert.deepEqual(Object.keys(mixed.find((r) => r.time === t)), ["time"]);
  }
  // 봉 항목은 OHLC를 그대로 보존한다
  assert.equal(mixed.find((r) => r.time === 300).close, 1);
  // 시리즈 길이 계약: 봉 4 + whitespace 3
  assert.equal(mixed.length - bars.length, 3);
});

test("withWhitespace: 여러 공백이 각각 펼쳐지고 전체 오름차순이 유지된다", () => {
  const bars = [0, 60, 180, 600].map(bar);
  const mixed = Gaps.withWhitespace(bars);
  assert.deepEqual(times(mixed),
    [0, 60, 120, 180, 240, 300, 360, 420, 480, 540, 600]);
  assert.equal(mixed.length - bars.length, 7); // 1 + 6
  assertUniformGrid(mixed);
});

test("withWhitespace: 공백이 없으면 봉만 담긴 새 배열이다 (wsCount 0 계약)", () => {
  const bars = [0, 60, 120].map(bar);
  const mixed = Gaps.withWhitespace(bars);
  assert.deepEqual(times(mixed), [0, 60, 120]);
  assert.equal(mixed.length - bars.length, 0);
  assert.notEqual(mixed, bars); // 복사본 — 호출자 배열을 건드리지 않는다
});

test("withWhitespace: 스팬 밖은 채우지 않는다 — 첫 봉 이전·마지막 봉 이후에 whitespace 없음", () => {
  // 봉 1개: 채울 공백 자체가 없다
  const one = Gaps.withWhitespace([bar(6000)]);
  assert.deepEqual(times(one), [6000]);
  // 봉 2개가 3분 간격: 사이 2분만 채우고 양끝 밖은 그대로다
  const mixed = Gaps.withWhitespace([bar(6000), bar(6180)]);
  assert.deepEqual(times(mixed), [6000, 6060, 6120, 6180]);
  assert.equal(mixed[0].time, 6000); // 첫 봉 이전으로 번지지 않는다
  assert.equal(mixed[mixed.length - 1].time, 6180); // 마지막 봉 이후도 마찬가지
  // 빈 입력·비배열 입력은 빈 배열 (구 엔진 gaps만으로 whitespace를 만들던 동작은 폐기)
  assert.deepEqual(Gaps.withWhitespace([]), []);
  assert.deepEqual(Gaps.withWhitespace(undefined), []);
});

test("withWhitespace: 하루 이상의 큰 구멍도 분당 1칸으로 전부 채운다", () => {
  const t0 = 60 * 60 * 24 * 7; // 임의 기준 (분 정렬)
  const t1 = t0 + 60 * 60 * 25; // 25시간 뒤
  const mixed = Gaps.withWhitespace([bar(t0), bar(t1)]);
  assert.equal(mixed.length, 2 + (25 * 60 - 1)); // 봉 2 + whitespace 1499
  assert.deepEqual(mixed[1], { time: t0 + 60 });
  assert.deepEqual(mixed[mixed.length - 2], { time: t1 - 60 });
  assertUniformGrid(mixed);
});

test("withWhitespace: 주말 구멍(금 18:00 → 월 09:00 KST)도 분당 1칸으로 채운다", () => {
  const fri = Date.UTC(2026, 9, 2, 9, 0) / 1000;  // 2026-10-02(금) 18:00 KST
  const mon = Date.UTC(2026, 9, 5, 0, 0) / 1000;  // 2026-10-05(월) 09:00 KST
  const mixed = Gaps.withWhitespace([bar(fri - 60), bar(fri), bar(mon), bar(mon + 60)]);
  // 금 18:00 봉과 월 09:00 봉 사이 63시간이 전부 빈 칸으로 남는다
  assert.equal(mixed.length, 4 + (3780 - 1));
  assertUniformGrid(mixed);
  assert.equal(mixed[0].time, fri - 60); // 스팬 시작은 첫 봉
  assert.equal(mixed[mixed.length - 1].time, mon + 60); // 스팬 끝은 마지막 봉
});

test("withWhitespace: 세션 경계 봉(야간 20:00) 전후의 공백도 같은 규칙으로 채운다", () => {
  const kst = (d, h, m) => Date.UTC(2026, 9, d, h - 9, m) / 1000; // KST = UTC+9
  // 20:00 봉 → 익일 08:55 봉 (선물 프리마켓 직전까지 공백)
  const mixed = Gaps.withWhitespace([bar(kst(1, 20, 0)), bar(kst(2, 8, 55))]);
  assert.equal(mixed.length, 2 + (12 * 60 + 55 - 1));
  assertUniformGrid(mixed);
  assert.equal(mixed[1].time, kst(1, 20, 1)); // 경계 직후 1분부터
  assert.equal(mixed[mixed.length - 2].time, kst(2, 8, 54)); // 다음 봉 직전까지
});

test("withWhitespace: 봉과 겹치는 포인트는 구조적으로 만들어지지 않는다 (중복 time 없음)", () => {
  const bars = [0, 60, 180].map(bar);
  const mixed = Gaps.withWhitespace(bars);
  assert.deepEqual(times(mixed), [0, 60, 120, 180]);
  assert.equal(new Set(times(mixed)).size, mixed.length); // 중복 시각 없음
  assert.deepEqual(Object.keys(mixed[2]), ["time"]); // 120은 whitespace
});

test("withWhitespace: 균일 그리드 성질 — 구멍 패턴이 다른 두 시리즈도 채우면 같은 시각이 같은 인덱스다", () => {
  // 선물식 패턴(쉬는 시간만 구멍)과 주식식 패턴(밤 전체가 구멍)의 축소 모형:
  // 스팬 [0, 720] 안에서 서로 다른 위치에 봉을 가진다
  const a = [0, 60, 120, 480, 540, 720].map(bar); // 구멍 180~420, 600~660
  const b = [0, 300, 360, 660, 720].map(bar);     // 구멍 60~240, 420~600
  const fa = Gaps.withWhitespace(a);
  const fb = Gaps.withWhitespace(b);
  // 채운 뒤에는 둘 다 스팬의 매분 그리드 — 시각 배열이 완전히 같다
  assert.deepEqual(times(fa), minutes(0, 780));
  assert.deepEqual(times(fb), minutes(0, 780));
  assertUniformGrid(fa);
  assertUniformGrid(fb);
  // 그래서 같은 시계 창(창 가장자리 시각이 같음)이면 모든 공유 시각의 논리 인덱스가
  // 일치한다 — pane-sync의 logicalAt이 칸마다 같은 값을 돌려준다는 데이터 측 보증
  for (const t of [0, 60, 300, 480, 720]) {
    assert.equal(fa.findIndex((r) => r.time === t), fb.findIndex((r) => r.time === t),
      `시각 ${t}의 인덱스`);
  }
});
