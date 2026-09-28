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

  // 스텁 엔진: status 명령에 applied로 응답
  (async () => {
    for await (const [id, , body] of router) {
      const req = JSON.parse(body.toString());
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
        payload: { mode: "replay", state: "ok" },
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

