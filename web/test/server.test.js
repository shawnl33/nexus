// 대시보드 서버 테스트: HTTP API, 화면틀 CRUD + 인증, WS 브리지.
// 스텁 엔진은 inproc ZeroMQ Router/Publisher로 제공한다.

import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
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
      const type = req.payload?.type;
      const reqData = req.payload?.data ?? {};
      let status = "applied";
      let errorCode = "none";
      let payload;
      if (type === "market.instruments") {
        payload = {
          total: 2,
          returned: 1,
          items: [{ shcode: "005930", name: "삼성전자", fut: 0 }],
          echo_q: reqData.q ?? "",
        };
      } else if (type === "chart.snapshot") {
        const back = reqData.back_index ?? 0;
        const shcode = reqData.shcode ?? "005930";
        payload = back === 0
          ? {
              shcode,
              generation: 3,
              timeframe_sec: 60,
              total: 3,
              next_back_index: 2,
              bars: [[1704153660000000, 105, 106, 101, 102, 40], [1704153720000000, 102, 108, 100, 107, 41]],
            }
          : {
              shcode,
              generation: 3,
              timeframe_sec: 60,
              total: 3,
              next_back_index: 0,
              bars: [[1704153600000000, 100, 110, 90, 105, 42]],
            };
      } else if (type === "market.watch") {
        if (reqData.shcode === "999999") {
          status = "rejected";
          errorCode = "watch_limit";
          payload = { shcode: reqData.shcode };
        } else {
          if (reqData.shcode === "SLOW01") {
            // 백필이 붙은 실제 watch처럼 4초 뒤에 응답한다 (주식 백필 ≈ 3~5초 실측)
            await new Promise((r) => setTimeout(r, 4000));
          }
          payload = { shcode: reqData.shcode, name: "테스트종목", generation: 1, backfilled: 1 };
        }
      } else if (type === "market.unwatch") {
        payload = { shcode: reqData.shcode, watches: 0 };
      } else {
        payload = { mode: "replay", state: "ok" };
      }
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
        status,
        error_code: errorCode,
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

test("GET /api/chart forwards shcode to engine and validates format", async () => {
  // shcode 지정 시 엔진에 그대로 전달된다 (스텁이 되돌려준다)
  const res = await fetch(`${base}/api/chart?shcode=A016C000&back_index=0`);
  assert.equal(res.status, 200);
  const data = await res.json();
  assert.equal(data.payload.shcode, "A016C000");

  // 형식이 잘못된 shcode는 엔진 호출 없이 400
  const bad = await fetch(`${base}/api/chart?shcode=ab`);
  assert.equal(bad.status, 400);
  assert.equal((await bad.json()).error, "invalid_symbol");
});

test("POST /api/symbols/watch proxies market.watch with token", async () => {
  // 토큰 없으면 403
  let res = await fetch(`${base}/api/symbols/watch`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ shcode: "000660" }),
  });
  assert.equal(res.status, 403);

  // shcode 형식 검증: 짧음/김/비문자
  for (const shcode of ["abc", "a".repeat(13), 12345]) {
    res = await fetch(`${base}/api/symbols/watch`, {
      method: "POST",
      headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
      body: JSON.stringify({ shcode }),
    });
    assert.equal(res.status, 400, `shcode=${shcode}`);
    assert.equal((await res.json()).error, "invalid_symbol");
  }

  // JSON 파싱 실패
  res = await fetch(`${base}/api/symbols/watch`, {
    method: "POST",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: "{",
  });
  assert.equal(res.status, 400);
  assert.equal((await res.json()).error, "invalid_json");

  // 정상: 엔진 응답을 그대로 전달한다
  res = await fetch(`${base}/api/symbols/watch`, {
    method: "POST",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: JSON.stringify({ shcode: "000660" }),
  });
  assert.equal(res.status, 200);
  const data = await res.json();
  assert.equal(data.status, "applied");
  assert.deepEqual(data.payload, { shcode: "000660", name: "테스트종목", generation: 1, backfilled: 1 });

  // 엔진 거부(watch_limit 등)는 400으로 매핑한다
  res = await fetch(`${base}/api/symbols/watch`, {
    method: "POST",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: JSON.stringify({ shcode: "999999" }),
  });
  assert.equal(res.status, 400);
  const rejected = await res.json();
  assert.equal(rejected.status, "rejected");
  assert.equal(rejected.error_code, "watch_limit");
});

test("POST /api/symbols/watch waits for a slow engine (backfill)", async (t) => {
  // 엔진은 market.watch 처리 안에서 백필을 동기로 끝내고 나서야 응답한다
  // (주식 ≈3~5초, 선물 ~10초 실측). 3초 타임아웃이면 502로 실패한다.
  const started = Date.now();
  const res = await fetch(`${base}/api/symbols/watch`, {
    method: "POST",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: JSON.stringify({ shcode: "SLOW01" }),
    signal: AbortSignal.timeout(20000),
  });
  const elapsed = Date.now() - started;
  assert.equal(res.status, 200);
  const data = await res.json();
  assert.equal(data.status, "applied");
  assert.equal(data.payload.shcode, "SLOW01");
  assert.ok(elapsed >= 3900, `stub delay should elapse (got ${elapsed}ms)`);
});

test("POST /api/symbols/unwatch proxies market.unwatch with token", async () => {
  // 토큰 없으면 403
  let res = await fetch(`${base}/api/symbols/unwatch`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ shcode: "000660" }),
  });
  assert.equal(res.status, 403);

  // 잘못된 shcode
  res = await fetch(`${base}/api/symbols/unwatch`, {
    method: "POST",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: JSON.stringify({ shcode: "x" }),
  });
  assert.equal(res.status, 400);

  // 정상
  res = await fetch(`${base}/api/symbols/unwatch`, {
    method: "POST",
    headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
    body: JSON.stringify({ shcode: "000660" }),
  });
  assert.equal(res.status, 200);
  const data = await res.json();
  assert.equal(data.status, "applied");
  assert.deepEqual(data.payload, { shcode: "000660", watches: 0 });
});

test("engine down: watch/unwatch map connection_error to 502", async (t) => {
  // 엔진 없이 서버만 띄운다: 명령 채널이 죽은 포트를 가리키게 한다
  const port = 18931;
  const child = spawn(process.execPath, [new URL("../server.js", import.meta.url).pathname], {
    env: {
      ...process.env,
      ENGINE_CMD_ENDPOINT: "tcp://127.0.0.1:59876",
      ENGINE_PUB_ENDPOINT: "tcp://127.0.0.1:59877",
      ENGINE_CMD_TIMEOUT_MS: "1000", // 죽은 엔진 시나리오는 짧은 타임아웃으로 빠르게 끝낸다
      DASHBOARD_PORT: String(port),
    },
    stdio: "ignore",
  });
  t.after(() => child.kill());
  try {
    // 기동 대기
    const deadline = Date.now() + 5000;
    for (;;) {
      try {
        const res = await fetch(`http://127.0.0.1:${port}/api/token-info`);
        if (res.ok) break;
      } catch { /* not yet */ }
      if (Date.now() > deadline) throw new Error("child server did not start");
      await new Promise((r) => setTimeout(r, 100));
    }
    // 자식 프로세스도 같은 .runtime/token 파일을 읽으므로 토큰이 같다
    const res = await fetch(`http://127.0.0.1:${port}/api/symbols/watch`, {
      method: "POST",
      headers: { "content-type": "application/json", "x-trader-token": AUTH_TOKEN },
      body: JSON.stringify({ shcode: "005930" }),
    });
    assert.equal(res.status, 502);
    assert.equal((await res.json()).error_code, "connection_error");
  } finally {
    child.kill();
  }
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

