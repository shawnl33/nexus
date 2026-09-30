// 종목별 피드(feed.js) 단위 테스트.
// DOM 없이 순수 로직만 검증한다: 캐시 격리(종목별 독립), 봉 기록 순서,
// 리셋 시 참조 유지와 seedToken, 종목별 세대 추적.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/feed.js");
const Feed = globalThis.Feed;

test("forSymbol: 같은 종목은 같은 캐시, 종목끼리는 완전히 격리된다", () => {
  const feed = Feed.create();
  const a = feed.forSymbol("005930");
  const b = feed.forSymbol("A016C000");
  assert.equal(feed.forSymbol("005930"), a); // 재조회는 같은 객체
  assert.notEqual(a, b);
  assert.equal(a.shcode, "005930");
  assert.equal(a.tickRaw, 5); // 기본 틱: 선물 raw 5

  feed.noteBar(a, 1000, { time: 1000, open: 1, high: 2, low: 0, close: 1 });
  a.barInd.set(1000, { score: 3 });
  a.tickRaw = 100;
  assert.equal(b.bars.size, 0); // 다른 종목 캐시는 오염되지 않는다
  assert.equal(b.barInd.size, 0);
  assert.equal(b.tickRaw, 5);

  assert.deepEqual(feed.symbols().sort(), ["005930", "A016C000"]);
  assert.equal(feed.get("999999"), undefined); // 생성 없이 조회
});

test("noteBar: 뒤에 붙는 순서와 역행(이진 삽입) 모두 시각 오름차순을 유지한다", () => {
  const feed = Feed.create();
  const c = feed.forSymbol("005930");
  feed.noteBar(c, 1000, { time: 1000 });
  feed.noteBar(c, 1060, { time: 1060 });
  feed.noteBar(c, 1120, { time: 1120 });
  feed.noteBar(c, 1030, { time: 1030 }); // 늦은 정정 역행
  feed.noteBar(c, 1060, { time: 1060, close: 9 }); // 같은 시각 갱신 — 위치 유지
  assert.deepEqual(c.barSeq, [1000, 1030, 1060, 1120]);
  assert.equal(c.barPos.get(1030), 1);
  assert.equal(c.barPos.get(1060), 2);
  assert.equal(c.bars.get(1060).close, 9); // 값은 덮어쓴다
});

test("noteBar: 갱신/꼬리 추가/중간 삽입을 코드로 돌려준다 (0 갱신, 1 꼬리, 2 중간)", () => {
  const feed = Feed.create();
  const c = feed.forSymbol("005930");
  assert.equal(feed.noteBar(c, 1000, { time: 1000 }), 1);  // 첫 봉 (꼬리 추가)
  assert.equal(feed.noteBar(c, 1060, { time: 1060 }), 1);  // 꼬리 추가
  assert.equal(feed.noteBar(c, 1060, { time: 1060, close: 9 }), 0); // 같은 봉 갱신
  assert.equal(feed.noteBar(c, 1030, { time: 1030 }), 2);  // 늦은 정정 역행 삽입
  assert.equal(feed.noteBar(c, 1030, { time: 1030 }), 0);  // 역행 봉의 재갱신은 갱신
});

test("noteBar: 구멍(whitespace) 자리 채움은 중간 삽입 코드 2를 돌려준다", () => {
  const feed = Feed.create();
  const c = feed.forSymbol("005930");
  for (const t of [1000, 1060, 1120]) feed.noteBar(c, t, { time: t });
  // 시딩이 만든 whitespace 흉내: 1030, 1090 두 분이 봉 없이 시각 목록에만 있다
  c.seriesTimes.splice(1, 0, 1030);
  c.seriesTimes.splice(3, 0, 1090);
  c.wsCount = 2;
  c.gaps = [[1030, 1090]];
  assert.equal(feed.noteBar(c, 1030, { time: 1030, close: 7 }), 2); // 구멍 채움
  assert.equal(c.wsCount, 1); // whitespace 하나가 캔들로 교체됐다
  assert.deepEqual(c.seriesTimes, [1000, 1030, 1060, 1090, 1120]); // 시각 목록 길이 불변
  assert.equal(c.seriesTimes.length, c.barSeq.length + c.wsCount); // 시리즈 길이 정합
});

test("fillGapMinute: 구간 앞/가운데/끝의 분을 빼고, 구간 밖이면 false", () => {
  const feed = Feed.create();
  const c = feed.forSymbol("s");

  c.gaps = [[1200, 1320]]; // 3분 구간 (1200, 1260, 1320)
  assert.equal(feed.fillGapMinute(c, 1190), false); // 구간 밖 — 손대지 않는다
  assert.deepEqual(c.gaps, [[1200, 1320]]);

  assert.equal(feed.fillGapMinute(c, 1200), true);  // 앞 끝 채움 → 앞을 줄인다
  assert.deepEqual(c.gaps, [[1260, 1320]]);

  assert.equal(feed.fillGapMinute(c, 1320), true);  // 뒤 끝 채움 → 뒤를 줄인다
  assert.deepEqual(c.gaps, [[1260, 1260]]);

  assert.equal(feed.fillGapMinute(c, 1260), true);  // 1분짜리 구간은 소멸한다
  assert.deepEqual(c.gaps, []);

  c.gaps = [[1200, 1380]];
  assert.equal(feed.fillGapMinute(c, 1260), true);  // 가운데 채움 → 둘로 쪼갠다
  assert.deepEqual(c.gaps, [[1200, 1200], [1320, 1380]]);
});

test("noteBar: seriesTimes도 봉과 함께 시각 오름차순을 유지한다 (시간축 동기화의 시각 기준)", () => {
  const feed = Feed.create();
  const c = feed.forSymbol("005930");
  feed.noteBar(c, 1000, { time: 1000 }); // 꼬리 추가
  feed.noteBar(c, 1060, { time: 1060 }); // 꼬리 추가
  feed.noteBar(c, 1030, { time: 1030 }); // 역행 이진 삽입
  assert.deepEqual(c.seriesTimes, [1000, 1030, 1060]);

  // 시딩이 만든 whitespace(구멍) 포인트가 섞인 상태 흉내 — 봉 인덱스와 어긋난다
  c.seriesTimes.push(1090, 1120);
  c.wsCount = 2;
  feed.noteBar(c, 1080, { time: 1080 }); // 구멍 사이로 들어가는 늦은 봉 — 오름차순 위치를 찾는다
  assert.deepEqual(c.seriesTimes, [1000, 1030, 1060, 1080, 1090, 1120]);
  assert.equal(c.wsCount, 2); // 새 시각 추가는 wsCount를 건드리지 않는다

  // 구멍 자리를 늦은 봉이 채우면: 차트도 그 자리를 캔들로 교체할 뿐 길이가 그대로이므로
  // 시각을 중복 삽입하지 않고 whitespace 카운트만 내린다 (getLength 정합)
  feed.noteBar(c, 1090, { time: 1090 });
  assert.deepEqual(c.seriesTimes, [1000, 1030, 1060, 1080, 1090, 1120]);
  assert.deepEqual(c.barSeq, [1000, 1030, 1060, 1080, 1090]);
  assert.equal(c.wsCount, 1);
  assert.equal(c.barSeq.length + c.wsCount, c.seriesTimes.length); // 시리즈 길이 정합
});

test("recentBars: pos까지 최근 n개를 오름차순으로 돌려준다", () => {
  const feed = Feed.create();
  const c = feed.forSymbol("s");
  for (const t of [1000, 1060, 1120, 1180]) feed.noteBar(c, t, { time: t });
  assert.deepEqual(feed.recentBars(c, 3, 5).map((b) => b.time), [1000, 1060, 1120, 1180]);
  assert.deepEqual(feed.recentBars(c, 1, 5).map((b) => b.time), [1000, 1060]); // 앞이 짧으면 있는 만큼
  assert.deepEqual(feed.recentBars(c, undefined, 5), []); // pos 없음
});

test("reset: 맵 참조를 유지한 채 비우고 seedToken을 올린다", () => {
  const feed = Feed.create();
  const c = feed.forSymbol("s");
  feed.noteBar(c, 1000, { time: 1000 });
  c.barInd.set(1000, { score: 1 });
  c.tickRaw = 100;
  c.gaps = [[1060, 1120]]; // 시딩 구멍 구간
  c.wsCount = 2;
  const barsRef = c.bars, seqRef = c.barSeq, stRef = c.seriesTimes; // 렌더러 ctx가 잡는 참조
  const tok = c.seedToken;

  feed.reset(c);
  assert.equal(c.bars.size, 0);
  assert.equal(c.barSeq.length, 0);
  assert.equal(c.barPos.size, 0);
  assert.equal(c.tickRaw, 5);
  assert.deepEqual(c.gaps, []); // 구멍·whitespace 카운트도 리셋된다
  assert.equal(c.wsCount, 0);
  assert.equal(c.seriesTimes.length, 0); // 시각 목록도 리셋 — 시딩 끝 renderSymbolPanes가 다시 세운다
  assert.equal(c.bars, barsRef); // 참조 유지 — 기존 ctx가 끊기지 않는다
  assert.equal(c.barSeq, seqRef);
  assert.equal(c.seriesTimes, stRef);
  assert.equal(c.seedToken, tok + 1); // 늦은 시딩 응답 폐기 트리거
});

test("drop: 캐시를 제거하고, 같은 종목 재조회는 새 캐시를 만든다", () => {
  const feed = Feed.create();
  const a = feed.forSymbol("005930");
  feed.noteBar(a, 1000, { time: 1000 });
  a.name = "삼성전자";
  assert.equal(feed.drop("005930"), true);
  assert.equal(feed.get("005930"), undefined);
  assert.deepEqual(feed.symbols(), []);
  const fresh = feed.forSymbol("005930"); // unwatch 후 다시 watch하면 빈 캐시부터
  assert.notEqual(fresh, a);
  assert.equal(fresh.bars.size, 0);
  assert.equal(fresh.name, "");
  assert.equal(feed.drop("999999"), false); // 없는 종목 제거는 false
});

test("noteGeneration: 종목별로 새 세대만 true, 낮은 세대는 폐기 대상", () => {
  const feed = Feed.create();
  const a = feed.forSymbol("A");
  const b = feed.forSymbol("B");
  assert.equal(feed.noteGeneration(a, 3), true);
  assert.equal(a.generation, 3);
  assert.equal(feed.noteGeneration(a, 3), false); // 같은 세대
  assert.equal(feed.noteGeneration(a, 2), false); // 낮은 세대 (늦은 응답)
  assert.equal(feed.noteGeneration(a, "4"), false); // 비수치
  assert.equal(b.generation, 0); // 종목별 독립
  assert.equal(feed.noteGeneration(b, 1), true);
});
