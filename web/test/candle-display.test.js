// HTS식 몸통 표시 변환(candle-display.js) 단위 테스트.
// 옵션이 켜졌을 때 캔들 시리즈에 넘기는 값만 표시용으로 바꾸는 규칙을 핀다:
// 시가=직전 실제 봉 종가(첫 봉은 자기 시가 유지), 직전 종가가 범위 밖이면 high/low를
// 늘려 몸통이 꼬리 밖으로 나가지 않게 하고, close·time·부가 키는 원래대로 둔다.
// whitespace 통과(체인은 실제 봉끼리), 입력 불변(캐시 원본 보호), 라이브 1봉 변환을 검증한다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/candle-display.js");
const CandleDisplay = globalThis.CandleDisplay;

const bar = (t, o, h, l, c) => ({ time: t, open: o, high: h, low: l, close: c });

test("htsChain: 시가는 직전 봉 종가로 교체, 첫 봉은 자기 시가 유지", () => {
  const bars = [bar(0, 100, 110, 90, 105), bar(60, 106, 112, 101, 108), bar(120, 107, 109, 100, 102)];
  const out = CandleDisplay.htsChain(bars);
  assert.equal(out[0].open, 100); // 첫 봉: 자기 시가 유지
  assert.equal(out[1].open, 105); // 직전 봉 종가
  assert.equal(out[2].open, 108);
  // close·time은 원래대로
  assert.deepEqual(out.map((b) => b.close), [105, 108, 102]);
  assert.deepEqual(out.map((b) => b.time), [0, 60, 120]);
});

test("htsChain: 직전 종가가 범위 밖이면 high/low를 확장한다 (몸통이 꼬리 밖으로 안 나감)", () => {
  // 위로: 직전 종가(110)가 다음 봉 고가(108)보다 높다
  const up = CandleDisplay.htsChain([bar(0, 100, 110, 95, 110), bar(60, 103, 108, 101, 104)]);
  assert.equal(up[1].open, 110);
  assert.equal(up[1].high, 110); // max(108, 110) — 확장
  assert.equal(up[1].low, 101);  // min(101, 110) — 그대로
  // 아래로: 직전 종가(95)가 다음 봉 저가(100)보다 낮다
  const dn = CandleDisplay.htsChain([bar(0, 100, 105, 90, 95), bar(60, 102, 106, 100, 104)]);
  assert.equal(dn[1].open, 95);
  assert.equal(dn[1].high, 106); // 그대로
  assert.equal(dn[1].low, 95);   // min(100, 95) — 확장
  // 범위 안: high/low는 원래대로
  const inRange = CandleDisplay.htsChain([bar(0, 100, 110, 90, 105), bar(60, 103, 108, 101, 104)]);
  assert.equal(inRange[1].high, 108);
  assert.equal(inRange[1].low, 101);
});

test("htsChain: whitespace는 그대로 통과하고 체인은 실제 봉끼리 잇는다", () => {
  const ws = { time: 60 }; // gaps.js 균일 분 그리드가 만드는 {time}만의 항목
  const bars = [bar(0, 100, 110, 90, 105), ws, bar(120, 104, 107, 99, 103)];
  const out = CandleDisplay.htsChain(bars);
  assert.equal(out.length, 3); // 길이·시각 목록이 입력과 같다 (wsCount 계약)
  assert.equal(out[1], ws); // whitespace 항목은 같은 참조 그대로 통과
  assert.equal(out[2].open, 105); // whitespace를 건드리지 않고 직전 실제 봉의 종가로 잇는다
  assert.equal(out[2].close, 103);
});

test("htsChain: 입력을 변이하지 않고 새 배열을 돌려준다 (캐시 원본 보호)", () => {
  const bars = [bar(0, 100, 110, 90, 105), bar(60, 103, 108, 101, 104)];
  const snapshot = JSON.stringify(bars);
  const out = CandleDisplay.htsChain(bars);
  assert.notEqual(out, bars); // 새 배열
  assert.equal(JSON.stringify(bars), snapshot); // 원본 배열·봉 객체 그대로
  assert.notEqual(out[1], bars[1]); // 변환된 봉은 새 객체
});

test("htsChain: close·volume 외 부가 키(시딩 봉의 ind 등)도 보존한다", () => {
  const seeded = [
    { time: 0, open: 100, high: 110, low: 90, close: 105, ind: 7 },
    { time: 60, open: 103, high: 108, low: 101, close: 104, ind: 8, volume: 12 },
  ];
  const out = CandleDisplay.htsChain(seeded);
  assert.equal(out[0].ind, 7);
  assert.equal(out[1].ind, 8);
  assert.equal(out[1].volume, 12);
});

test("htsChain: 비배열·빈 입력은 빈 배열", () => {
  assert.deepEqual(CandleDisplay.htsChain(undefined), []);
  assert.deepEqual(CandleDisplay.htsChain([]), []);
});

test("htsChainBar: 라이브 1봉 변환 — 시가 교체 + high/low 확장, 원본 불변", () => {
  const live = bar(60, 103, 108, 101, 104);
  const out = CandleDisplay.htsChainBar(110, live); // 직전 종가가 고가 위
  assert.deepEqual(out, { time: 60, open: 110, high: 110, low: 101, close: 104 });
  assert.equal(live.open, 103); // 캐시 원본 불변
  const inRange = CandleDisplay.htsChainBar(105, live); // 범위 안이면 high/low 그대로
  assert.deepEqual(inRange, { time: 60, open: 105, high: 108, low: 101, close: 104 });
  assert.notEqual(inRange, live); // 변환 시에는 항상 새 객체
});

test("htsChainBar: 직전 종가가 없으면(첫 봉) 원래 봉을 그대로 돌려준다 — 자기 시가 유지", () => {
  const first = bar(0, 100, 110, 90, 105);
  assert.equal(CandleDisplay.htsChainBar(undefined, first), first);
  assert.equal(CandleDisplay.htsChainBar(NaN, first), first);
});
