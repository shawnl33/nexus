// 사이드바 원본 트리. 폴더 이름과 파일 이름(확장자 제외)만 확인한다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/yl-tree.js");
const T = globalThis.YlTree;

const CATALOG = {
  dirs: [
    { name: "experiments", files: ["표시실험1_모양과눈금.txt"] },
    { name: "functions", files: ["WSF_KSValues1mV1.txt"] },
    { name: "strategies", files: [".gitkeep", "note.md"] },
    {
      name: "signals",
      files: [
        "#우드스탁_위클리_페어시스템_양매도.txt",
        "#우드스탁_위클리_페어시스템_양매수.txt",
      ],
    },
    {
      name: "indicators",
      files: [
        "#우드스탁_위클리_프라이스링크_양매도.txt",
        "#WSF_해외선물미래곡선V2.txt",
        "아직없음.txt",
      ],
    },
  ],
};

test("build: 실험·함수·빈 폴더는 빼고 폴더 이름으로 묶는다", () => {
  const tree = T.build(CATALOG);
  assert.deepEqual(tree.map((d) => d.name), ["indicators", "signals"]);
});

test("build: 보이는 이름은 확장자가 없고 시그널은 시스템으로 연결된다", () => {
  const tree = T.build(CATALOG);
  const signals = tree.find((d) => d.name === "signals");
  const short = signals.items.find((i) => i.file.startsWith("#우드스탁_위클리_페어시스템_양매도"));
  assert.equal(short.label, "#우드스탁_위클리_페어시스템_양매도");
  assert.equal(short.label.includes("."), false);
  assert.equal(short.port.kind, "system");
  assert.equal(short.port.id, "pair-short");
  assert.equal(short.port.side, -1);
  assert.equal(short.port.leg, "w_link_short");
  assert.deepEqual(T.inputsFor("pair-short").find((row) => row[0] === "총투자금"), ["총투자금", 10000000]);
});

test("build: 연결되지 않은 원본은 이름만 남고 미래곡선 V2는 V1과 같은 항목이다", () => {
  const indicators = T.build(CATALOG).find((d) => d.name === "indicators");
  const plain = indicators.items.find((i) => i.file === "아직없음.txt");
  assert.equal(plain.label, "아직없음");
  assert.equal(plain.port, null);
  const v2 = indicators.items.find((i) => i.file === "#WSF_해외선물미래곡선V2.txt");
  assert.equal(v2.port.id, "fx_mirae_v1");
  const link = indicators.items.find((i) => i.label === "#우드스탁_위클리_프라이스링크_양매도");
  assert.equal(link.port.id, "w_link_short");
});

test("build: 잘못된 입력은 빈 트리다", () => {
  assert.deepEqual(T.build(undefined), []);
  assert.deepEqual(T.build({ dirs: null }), []);
});
