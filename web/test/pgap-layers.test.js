import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/pgap-layers.js");
const P = globalThis.PgapLayers;

test("parsePgap는 삼선·오선 두 칸을 읽는다", () => {
  const [a, b] = P.parsePgap([[1, 4, 4, 0, 100, 0, 1, 0x808080, 1, 0x808080, 0, 0], null]);
  assert.equal(a.ready, true);
  assert.equal(a.gap, 4);
  assert.equal(a.plot3, false);
  assert.equal(b, null);
});
