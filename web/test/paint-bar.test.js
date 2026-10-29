// 봉 색은 켠 페인트바만 고른다. 미래곡선이 켜져 있어도 통합판정색은 봉에 올리지 않는다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/os-layers.js");
const Os = globalThis.OsLayers;

function on(ids) {
  const active = new Map();
  for (const id of ids) active.set(id, { handle: { wantsBar: () => true } });
  return active;
}

const ind = {
  vb: 0xff00aa,
  os: { osPaint: 0x112233, paintRgb: 0x445566, judgeRgb: 0xff0000 },
};

test("페인트바가 없으면 봉 색이 없다", () => {
  assert.equal(Os.barPaintRgb(on(["fx_curve_os"]), ind), 0);
  assert.equal(Os.barPaintRgb(on([]), ind), 0);
});

test("켠 페인트바 색만 쓰고 VolBurst, 1분봉용, 강조 순이다", () => {
  assert.equal(Os.barPaintRgb(on(["fx_paint_v12"]), ind), 0x445566);
  assert.equal(Os.barPaintRgb(on(["fx_paint_os"]), ind), 0x112233);
  assert.equal(Os.barPaintRgb(on(["vb_paint"]), ind), 0xff00aa);
  assert.equal(Os.barPaintRgb(on(["vb_paint", "fx_paint_os", "fx_paint_v12"]), ind), 0xff00aa);
  assert.equal(Os.barPaintRgb(on(["fx_paint_os", "fx_paint_v12"]), ind), 0x112233);
});

test("봉색 레이어를 끄면 그 페인트바는 건너뛴다", () => {
  const active = new Map();
  active.set("vb_paint", { handle: { wantsBar: () => false } });
  active.set("fx_paint_v12", { handle: { wantsBar: () => true } });
  assert.equal(Os.barPaintRgb(active, ind), 0x445566);
});
