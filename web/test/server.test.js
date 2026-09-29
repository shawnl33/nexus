// 대시보드 서버 테스트: HTTP API, 화면틀 CRUD + 인증, WS 브리지.
// 스텁 엔진은 inproc ZeroMQ Router/Publisher로 제공한다.

import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import * as zmq from "zeromq";
import WebSocket from "ws";

process.env.ENGINE_CMD_ENDPOINT = "inproc://dash-test-cmd";
process.env.ENGINE_PUB_ENDPOINT = "inproc://dash-test-pub";
process.env.DASHBOARD_PORT = "0";

const { server, startBridge, stopBridge, AUTH_TOKEN } = await import("../server.js");

let router;
let publisher;
let base;


before(async () => {
  router = new zmq.Router();
  await router.bind(process.env.ENGINE_CMD_ENDPOINT);
  publisher = new zmq.Publisher();
  await publisher.bind(process.env.ENGINE_PUB_ENDPOINT);

  // 스텁 엔진: status 명령에 applied로 응답, market.instruments는 검색어를 되돌려준다
  (async () => {
    for await (const [id, , body] of router) {
      const req = JSON.parse(body.toString());
      const payload = req.payload?.type === "market.instruments"
        ? {
            total: 2,
            returned: 1,
            items: [{ shcode: "005930", name: "삼성전자", fut: 0 }],
            echo_q: req.payload.data?.q ?? "",
          }
        : req.payload?.type === "chart.snapshot"
          ? (() => {
              const back = req.payload.data?.back_index ?? 0;
              if (back === 0) {
                return {
                  shcode: "005930",
                  generation: 3,
                  timeframe_sec: 60,
                  total: 3,
                  next_back_index: 2,
                  bars: [[1704153660000000, 105, 106, 101, 102, 40], [1704153720000000, 102, 108, 100, 107, 41]],
                };
              }
              return {
                shcode: "005930",
                generation: 3,
                timeframe_sec: 60,
                total: 3,
                next_back_index: 0,
                bars: [[1704153600000000, 100, 110, 90, 105, 42]],
              };
            })()
          : { mode: "replay", state: "ok" };
      const reply = {
        protocol_version: 1,
        message_type: "command_result",
        engine_instance_id: "99",
        request_id: req.request_id,
        command_id: req.command_id,
        stream_id: "",
        sequence: "1",
        event_time: "0",
        emitted_at: "0",
        status: "applied",
        error_code: "none",
        payload,
      };
      await router.send([id, "", JSON.stringify(reply)]);
    }
  })();

  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  base = `http://127.0.0.1:${server.address().port}`;
  startBridge();
});

after(async () => {
  server.close();
  stopBridge();
  router.close();
  publisher.close();
});

test("GET /api/status forwards engine reply", async () => {
  const res = await fetch(`${base}/api/status`);
  assert.equal(res.status, 200);
  const data = await res.json();
  assert.equal(data.status, "applied");
  assert.equal(data.payload.mode, "replay");
});

test("GET /api/token issues token to same-origin/local clients", async () => {
  // 헤더 없음(curl 등 로컬 도구) → 발급
  let res = await fetch(`${base}/api/token`);
  assert.equal(res.status, 200);
  assert.equal((await res.json()).token, AUTH_TOKEN);

  // 같은 출처 브라우저 → 발급
  res = await fetch(`${base}/api/token`, { headers: { "sec-fetch-site": "same-origin" } });
  assert.equal(res.status, 200);

  // 교차 출처 브라우저 → 거부
  res = await fetch(`${base}/api/token`, { headers: { "sec-fetch-site": "cross-site" } });
  assert.equal(res.status, 403);
  res = await fetch(`${base}/api/token`, { headers: { origin: "http://evil.example" } });
  assert.equal(res.status, 403);
});

test("GET /api/market proxies registry search with query", async () => {
  const res = await fetch(`${base}/api/market?q=${encodeURIComponent("삼성")}&limit=20`);
  assert.equal(res.status, 200);
  const data = await res.json();
  assert.equal(data.status, "applied");
  assert.equal(data.payload.echo_q, "삼성");
  assert.equal(data.payload.items[0].shcode, "005930");
});

test("GET /api/market rejects overlong query", async () => {
  const res = await fetch(`${base}/api/market?q=${"a".repeat(65)}`);
  assert.equal(res.status, 400);
});

test("GET /api/chart proxies engine bar snapshot with pagination", async () => {
  let res = await fetch(`${base}/api/chart?back_index=0`);
  assert.equal(res.status, 200);
  let data = await res.json();
  assert.equal(data.payload.shcode, "005930");
  assert.equal(data.payload.bars.length, 2);
  assert.equal(data.payload.next_back_index, 2);

  res = await fetch(`${base}/api/chart?back_index=2`);
  assert.equal(res.status, 200);
  data = await res.json();
  assert.equal(data.payload.bars.length, 1);
  assert.equal(data.payload.bars[0][4], 105);
  assert.equal(data.payload.next_back_index, 0);
});

test("workspace save/load/list/delete with token", async () => {
  const wsData = { panels: [{ id: "chart", type: "chart" }], current_symbol: "1" };

  // 토큰 없으면 403
  let res = await fetch(`${base}/api/workspaces/t1`, {
    method: "PUT",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(wsData),
  });
  assert.equal(res.status, 403);

  // 토큰으로 저장
  res = await fetch(`${base}/api/workspaces/t1`, {
    method: "PUT",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: JSON.stringify(wsData),
  });
  assert.equal(res.status, 200);
  assert.deepEqual((await res.json()), { saved: true });

  // 불러오기: schema_version이 부여된다
  res = await fetch(`${base}/api/workspaces/t1`);
  assert.equal(res.status, 200);
  const loaded = await res.json();
  assert.equal(loaded.schema_version, 1);
  assert.equal(loaded.current_symbol, "1");

  // 목록
  res = await fetch(`${base}/api/workspaces`);
  const list = await res.json();
  assert.ok(list.workspaces.includes("t1"));

  // 삭제 (토큰 필요)
  res = await fetch(`${base}/api/workspaces/t1`, { method: "DELETE", headers: { "x-trader-token": AUTH_TOKEN } });
  assert.equal(res.status, 200);
  res = await fetch(`${base}/api/workspaces/t1`);
  assert.equal(res.status, 404);
});

test("workspace v2는 schema_version 2를 그대로 보존한다", async () => {
  const v2 = {
    schema_version: 2,
    current_symbol: "A016C000",
    panels: [
      { height: 0.6, indicators: [{ id: "mirae_v16", layers: { band: true, mktband: false } }] },
      { height: 0.4, indicators: [{ id: "sma", layers: { sma5: true, sma20: true, sma60: false } }] },
    ],
  };
  let res = await fetch(`${base}/api/workspaces/v2test`, {
    method: "PUT",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: JSON.stringify(v2),
  });
  assert.equal(res.status, 200);

  res = await fetch(`${base}/api/workspaces/v2test`);
  assert.equal(res.status, 200);
  const loaded = await res.json();
  assert.equal(loaded.schema_version, 2); // v1로 덮어쓰지 않는다
  assert.equal(loaded.name, "v2test");
  assert.equal(loaded.current_symbol, "A016C000");
  assert.deepEqual(loaded.panels, v2.panels);

  await fetch(`${base}/api/workspaces/v2test`, { method: "DELETE", headers: { "x-trader-token": AUTH_TOKEN } });
});

test("invalid workspace name rejected", async () => {
  const res = await fetch(`${base}/api/workspaces/..%2Fevil`, {
    method: "PUT",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: "{}",
  });
  assert.equal(res.status, 400);
});

test("static index.html served", async () => {
  const res = await fetch(`${base}/`);
  assert.equal(res.status, 200);
  const html = await res.text();
  assert.ok(html.includes("미래곡선"));
});

test("websocket forwards engine status stream", async () => {
  const ws = new WebSocket(`ws://127.0.0.1:${server.address().port}/ws`);
  await new Promise((resolve, reject) => {
    ws.on("open", resolve);
    ws.on("error", reject);
  });

  const received = new Promise((resolve) => {
    ws.on("message", (raw) => {
      const data = JSON.parse(raw.toString());
      if (data.kind === "status") resolve(data);
    });
  });

  // slow joiner 방지 후 발행
  await new Promise((r) => setTimeout(r, 100));
  await publisher.send(["display", JSON.stringify({
    protocol_version: 1,
    message_type: "status",
    engine_instance_id: "42",
    sequence: "1",
    event_time: "1000",
    emitted_at: "1000",
    stream_id: "display",
    payload: { bar_open_time: "1704153660000000", closed: 0, score: 3 },
  })]);

  const data = await received;
  assert.equal(data.stream, "display");
  assert.equal(data.message.payload.score, 3);
  ws.close();
});

