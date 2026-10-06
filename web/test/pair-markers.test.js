// 프라이스링크가 켠 쪽의 페어 신호를 봉 위 글자로 만든다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/pair-markers.js");
const M = globalThis.PairMarkers;

test("양매도 진입은 연두 아래 화살표와 주문 이름이다", () => {
  const marks = M.fromEvents([
    { t: 2000, k: 1, side: -1, n: "양매도진입", q: 4, symbol: "PUT" },
    { t: 2060, k: 2, side: -1, n: "시간저수익부분청산", q: 3, symbol: "PUT" },
  ]);
  assert.equal(marks[0].text, "양매도진입");
  assert.equal(marks[0].qty, -4);
  assert.equal(marks[0].symbol, "PUT");
  assert.equal(marks[0].position, "aboveBar");
  assert.equal(marks[0].shape, "arrowDown");
  assert.equal(marks[0].color, "#d4ff4a");
  assert.equal(marks[0].qty, -4);
  assert.equal(marks[1].text, "시간저수익부분청산");
  assert.equal(marks[1].qty, -3);
  assert.equal(marks[1].position, "belowBar");
  assert.equal(marks[1].shape, "arrowUp");
  assert.equal(marks[1].color, "#ff8c00");
});

test("수량 없는 전량청산은 0이다", () => {
  const marks = M.fromEvents([
    { t: 2100, k: 3, side: -1, n: "잔량익절보호", q: 1 },
  ]);
  assert.equal(marks[0].qty, 0);
  assert.equal(marks[0].color, "#ff8c00");
});

test("같은 시각의 주문은 화살표를 따로 남긴다", () => {
  const marks = M.fromEvents([
    { t: 3000, k: 3, side: -1, n: "잔량익절보호", q: 0 },
    { t: 3000, k: 3, side: -1, n: "합산익절1차", q: -1 },
  ]);
  assert.equal(marks.length, 2);
  assert.deepEqual(marks.map((m) => m.text), ["잔량익절보호", "합산익절1차"]);
});

test("글자 옆에 수량을 붙인다", () => {
  const placed = M.placeLabel(
    { text: "양매도진입", qty: -4, position: "aboveBar", color: "#e03131" },
    { x: 100, highY: 200, lowY: 220 },
  );
  assert.deepEqual(placed.lines, ["양매도진입", "-4"]);
  assert.ok(placed.y < 200);
});

test("진입 글자와 청산 글자도 겹치지 않는다", () => {
  const entry = M.placeLabel(
    { text: "양매도진입", qty: -4, position: "aboveBar", color: "#d4ff4a" },
    { x: 100, highY: 100, lowY: 130 },
  );
  const partial = M.placeLabel(
    { text: "시간저수익부분청산", qty: -1, position: "belowBar", color: "#ff8c00" },
    { x: 150, highY: 70, lowY: 90 },
  );
  const first = M.placeLabel(
    { text: "합산익절1차", qty: -2, position: "belowBar", color: "#ff8c00" },
    { x: 210, highY: 80, lowY: 100 },
  );
  const last = M.placeLabel(
    { text: "잔량익절보호", qty: 0, position: "belowBar", color: "#ff8c00" },
    { x: 230, highY: 82, lowY: 102 },
  );
  const labels = [entry, partial, first, last];
  const widths = labels.map((label) => Math.max(...label.lines.map((line) => M.lineWidth(line, 0))));
  M.separateLabels(labels, widths);
  const boxes = labels.map((label, i) => M.labelBox(label, widths[i]));
  for (let i = 0; i < boxes.length; i++) {
    for (let j = i + 1; j < boxes.length; j++) {
      const a = boxes[i];
      const b = boxes[j];
      const overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
      assert.equal(overlap, false);
    }
  }
});

test("이웃한 청산 글자는 아래로 비켜 선다", () => {
  const first = M.placeLabel(
    { text: "합산익절1차", qty: -2, position: "belowBar", color: "#ff8c00" },
    { x: 100, highY: 40, lowY: 80 },
  );
  const second = M.placeLabel(
    { text: "잔량익절보호", qty: 0, position: "belowBar", color: "#ff8c00" },
    { x: 112, highY: 42, lowY: 82 },
  );
  const before = second.y;
  M.separateLabels([first, second], [96, 96]);
  assert.ok(second.y > before);
  const a = M.labelBox(first, 96);
  const b = M.labelBox(second, 96);
  const overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
  assert.equal(overlap, false);
});

test("라이브러리 마커에는 작은 글자를 싣지 않는다", () => {
  const marks = M.fromEvents([{ t: 1000, k: 1, side: -1, n: "양매도진입", q: -4 }]);
  const seriesMarks = M.seriesMarkers(marks);
  assert.equal(marks[0].text, "양매도진입");
  assert.equal(seriesMarks[0].text, undefined);
  assert.equal(seriesMarks[0].shape, "arrowDown");
  assert.equal(seriesMarks[0].color, "#d4ff4a");
});
