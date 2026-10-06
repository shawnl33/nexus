// 칸 지표 패널 트리(indicator-tree.js) 구성 단위 테스트.
// DOM 없이 순수 함수만 검증한다: 카테고리 분류, 미분류 지표의 기타 그룹,
// 모르는 지표 필터, 빈 카테고리 제거, 카테고리 안의 매니페스트 순서 유지.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/indicator-tree.js");
const T = globalThis.IndicatorTree;

// 엔진 매니페스트와 같은 형태 (src/app/main.c SNAP_IND_MANIFEST)
const MANIFEST = [
  { id: "mirae_v16", name: "미래곡선 V16", layers: [{ id: "score", name: "① 통합 점수", defaultOn: true }] },
  { id: "sma", name: "이평선 5/20/60", layers: [{ id: "sma5", name: "SMA 5", defaultOn: true }] },
];

test("buildTree: 매니페스트 지표를 카테고리별로 묶는다", () => {
  const tree = T.buildTree(MANIFEST);
  assert.deepEqual(tree.map((c) => c.id), ["mirae", "ma"]);
  assert.equal(tree[0].name, "미래곡선");
  assert.equal(tree[0].indicators[0].id, "mirae_v16");
  assert.equal(tree[1].name, "이동평균");
  assert.equal(tree[1].indicators[0].id, "sma");
  // 지표 메타는 매니페스트 객체 그대로다 (이름·레이어를 UI가 그대로 읽는다)
  assert.equal(tree[0].indicators[0], MANIFEST[0]);
});

test("buildTree: 매핑에 없는 지표는 마지막 기타 카테고리에 모인다", () => {
  const tree = T.buildTree([...MANIFEST, { id: "rsi", name: "RSI" }, { id: "macd", name: "MACD" }]);
  assert.deepEqual(tree.map((c) => c.id), ["mirae", "ma", "misc"]);
  assert.deepEqual(tree[2].indicators.map((m) => m.id), ["rsi", "macd"]);
});

test("buildTree: isKnown이 모르는 지표는 걸러낸다 (렌더러 없음)", () => {
  const known = (id) => id === "sma"; // mirae_v16 렌더러가 없는 프론트라고 가정
  const tree = T.buildTree(MANIFEST, known);
  assert.deepEqual(tree.map((c) => c.id), ["ma"]);
});

test("buildTree: 국내선물 Data2는 위클리 지표와 같은 묶음이다", () => {
  const tree = T.buildTree([
    { id: "w_link_long", name: "위클리 프라이스링크 양매수" },
    { id: "ks_data2", name: "스나이퍼스코프 국내선물 Data2" },
  ]);
  const weekly = tree.find((c) => c.id === "weekly");
  assert.ok(weekly);
  assert.deepEqual(weekly.indicators.map((m) => m.id), ["w_link_long", "ks_data2"]);
});

test("buildTree: 지표가 없는 카테고리는 빼고, 카테고리 안에서는 매니페스트 순서다", () => {
  const tree = T.buildTree([
    { id: "z2" }, { id: "sma" }, { id: "z1" }, // 기타 2개 + 이평균, 미래곡선 없음
  ]);
  assert.deepEqual(tree.map((c) => c.id), ["ma", "misc"]); // 빈 mirae 카테고리는 없다
  assert.deepEqual(tree[1].indicators.map((m) => m.id), ["z2", "z1"]); // 매니페스트 순서 유지
});

test("buildTree: 잘못된 입력은 빈 트리/항목 무시로 흡수한다", () => {
  assert.deepEqual(T.buildTree(undefined), []);
  assert.deepEqual(T.buildTree(null), []);
  assert.deepEqual(T.buildTree("x"), []);
  assert.deepEqual(T.buildTree([null, 42, {}, { id: 7 }]), []); // id 없는 항목 무시
});
