// C Trading Engine 관리 대시보드 서버 (계획서 §3, §16, §18)
//
// - Node는 표시·설정만 담당한다. 핵심 지표·매매 신호를 재계산하지 않는다.
// - 브라우저는 이 서버의 HTTP/WebSocket만 사용하고 ZeroMQ에 직접 연결하지 않는다.
// - loopback 전용. 변경 요청에는 인증 토큰 + Origin 검사를 적용한다.
// - 화면틀은 별도 JSON 파일로 소유하고, 저장은 원자적 파일 교체를 사용한다.

import { createServer } from "node:http";
import { readFile, writeFile, rename, mkdir, readdir, unlink } from "node:fs/promises";
import { existsSync, readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { join, extname, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { randomBytes } from "node:crypto";
import { WebSocketServer } from "ws";
import * as zmq from "zeromq";

const __dirname = dirname(fileURLToPath(import.meta.url));
const PUBLIC_DIR = join(__dirname, "public");
const WORKSPACE_DIR = join(__dirname, ".runtime", "workspaces");
const TOKEN_PATH = join(__dirname, ".runtime", "token");

const CMD_ENDPOINT = process.env.ENGINE_CMD_ENDPOINT ?? "tcp://127.0.0.1:5555";
const PUB_ENDPOINT = process.env.ENGINE_PUB_ENDPOINT ?? "tcp://127.0.0.1:5556";
const PORT = Number(process.env.DASHBOARD_PORT ?? 8080);
const HOST = "127.0.0.1";

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
};

// ---- 인증 토큰: 첫 실행 시 생성, 파일 권한 소유자만 (계획서 §18) ----
function loadToken() {
  if (existsSync(TOKEN_PATH)) {
    return readFileSync(TOKEN_PATH, "utf8").trim();
  }
  mkdirSync(dirname(TOKEN_PATH), { recursive: true });
  const token = randomBytes(24).toString("hex");
  writeFileSync(TOKEN_PATH, token, { mode: 0o600 });
  return token;
}
const AUTH_TOKEN = loadToken();

// ---- 엔진 명령 채널 (ROUTER/DEALER) ----
let reqSeq = 1;
async function engineCommand(commandId, type, payloadJson) {
  const dealer = new zmq.Dealer({ sendTimeout: 1000, receiveTimeout: 3000 });
  try {
    dealer.connect(CMD_ENDPOINT);
    const payload =
      payloadJson != null
        ? { type, data: JSON.parse(payloadJson) }
        : { type };
    const msg = {
      protocol_version: 1,
      message_type: "command",
      engine_instance_id: "0",
      request_id: `dash-${reqSeq++}`,
      command_id: commandId,
      stream_id: "",
      sequence: String(reqSeq),
      event_time: "0",
      emitted_at: "0",
      status: "",
      error_code: "",
      payload,
    };
    await dealer.send(["", JSON.stringify(msg)]);
    const [, body] = await dealer.receive();
    return JSON.parse(body.toString());
  } catch (e) {
    return { status: "error", error_code: "connection_error", message: String(e) };
  } finally {
    dealer.close();
  }
}

// ---- 화면틀 (원자적 저장) ----
async function workspacePath(name) {
  if (!/^[A-Za-z0-9_가-힣-]{1,64}$/.test(name)) return null;
  return join(WORKSPACE_DIR, `${name}.json`);
}

async function saveWorkspace(name, data) {
  const path = await workspacePath(name);
  if (path == null) return false;
  const withMeta = { ...data, schema_version: 1, name };
  const tmp = `${path}.tmp-${process.pid}`;
  await mkdir(WORKSPACE_DIR, { recursive: true });
  await writeFile(tmp, JSON.stringify(withMeta, null, 2), "utf8");
  await rename(tmp, path); // 원자적 교체: 저장 중 실패핮 기존 파일 보존
  return true;
}

// ---- HTTP ----
function json(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, { "content-type": "application/json; charset=utf-8" });
  res.end(body);
}

function mutationAllowed(req) {
  const token = req.headers["x-trader-token"];
  if (token !== AUTH_TOKEN) return false;
  const origin = req.headers.origin;
  if (origin != null && !origin.startsWith(`http://${HOST}`) && !origin.startsWith(`http://localhost`)) {
    return false;
  }
  return true;
}

async function readBody(req) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > 1024 * 1024) throw new Error("body too large");
    chunks.push(chunk);
  }
  return Buffer.concat(chunks).toString("utf8");
}

const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, `http://${HOST}`);
    const path = url.pathname;

    if (req.method === "GET" && path === "/api/token-info") {
      // 토큰 자체는 낸지 않고 설정 유묻만 알린다
      return json(res, 200, { auth_required: true });
    }
    if (req.method === "GET" && path === "/api/status") {
      const reply = await engineCommand(`dash-status-${reqSeq}`, "status", null);
      return json(res, reply.error_code === "connection_error" ? 502 : 200, reply);
    }

    if (req.method === "POST" && path === "/api/symbols/select") {
      // 화면의 선택 종목 변경은 화면 상태 변경이며, 전략 거래 대상 변경이 아니다 (계획서 §18)
      if (!mutationAllowed(req)) return json(res, 403, { error: "forbidden" });
      const body = await readBody(req);
      let shcode;
      try {
        shcode = JSON.parse(body).shcode;
      } catch {
        return json(res, 400, { error: "invalid_json" });
      }
      if (typeof shcode !== "string" || shcode.length < 4 || shcode.length > 12) {
        return json(res, 400, { error: "invalid_symbol" });
      }
      const reply = await engineCommand(`dash-select-${reqSeq}`, "market.select", JSON.stringify({ shcode }));
      const code = reply.error_code === "connection_error" ? 502 : reply.status === "rejected" ? 400 : 200;
      return json(res, code, reply);
    }

    if (path.startsWith("/api/workspaces")) {
      const name = decodeURIComponent(path.split("/")[3] ?? "");
      if (req.method === "GET" && !name) {
        await mkdir(WORKSPACE_DIR, { recursive: true });
        const files = (await readdir(WORKSPACE_DIR)).filter((f) => f.endsWith(".json"));
        return json(res, 200, { workspaces: files.map((f) => f.slice(0, -5)) });
      }
      const wpath = await workspacePath(name);
      if (wpath == null) return json(res, 400, { error: "invalid_name" });
      if (req.method === "GET") {
        try {
          const data = await readFile(wpath, "utf8");
          res.writeHead(200, { "content-type": "application/json; charset=utf-8" });
          return res.end(data);
        } catch {
          return json(res, 404, { error: "not_found" });
        }
      }
      if (req.method === "PUT" || req.method === "DELETE") {
        if (!mutationAllowed(req)) return json(res, 403, { error: "forbidden" });
        if (req.method === "PUT") {
          const body = await readBody(req);
          let data;
          try {
            data = JSON.parse(body);
          } catch {
            return json(res, 400, { error: "invalid_json" });
          }
          // 화면틀에는 비밀값·엔진 변경 가능 상태를 넣지 않는다 (계획서 §18)
          delete data.secrets;
          delete data.strategy_autostart;
          const ok = await saveWorkspace(name, data);
          return json(res, ok ? 200 : 400, { saved: ok });
        }
        await unlink(wpath).catch(() => {});
        return json(res, 200, { deleted: true });
      }
    }

    if (req.method === "GET" && (path === "/" || extname(path) !== "")) {
      const rel = path === "/" ? "index.html" : path.slice(1);
      if (rel.includes("..")) return json(res, 400, { error: "invalid_path" });
      const candidates = [join(PUBLIC_DIR, rel), join(__dirname, "node_modules", rel)];
      for (const file of candidates) {
        try {
          const data = await readFile(file);
          res.writeHead(200, { "content-type": MIME[extname(file)] ?? "application/octet-stream" });
          return res.end(data);
        } catch { /* try next */ }
      }
      return json(res, 404, { error: "not_found" });
    }

    json(res, 404, { error: "not_found" });
  } catch (e) {
    json(res, 500, { error: "internal", message: String(e) });
  }
});

// ---- WebSocket 브리지: 엔진 상태 스트림 → 브라우저 ----
const wss = new WebSocketServer({ server, path: "/ws" });
const pubSub = new zmq.Subscriber();
const clients = new Set();
const tracker = { engine_instance_id: null, last_sequence: null };

async function startBridge() {
  pubSub.connect(PUB_ENDPOINT);
  pubSub.subscribe("display");
  for await (const [topic, body] of pubSub) {
    let msg;
    try {
      msg = JSON.parse(body.toString());
    } catch {
      continue; // 잘못된 형식은 버린다 (조용히 정상 표시하지 않음)
    }
    const seq = Number(msg.sequence);
    const engineId = msg.engine_instance_id;
    let stream_event = "ok";
    if (tracker.engine_instance_id !== engineId) {
      stream_event = tracker.engine_instance_id == null ? "ok" : "restart";
      tracker.engine_instance_id = engineId;
      tracker.last_sequence = seq;
    } else if (seq !== tracker.last_sequence + 1) {
      stream_event = seq > tracker.last_sequence ? "gap" : "ok";
      if (seq > tracker.last_sequence) tracker.last_sequence = seq;
    } else {
      tracker.last_sequence = seq;
    }
    const out = JSON.stringify({
      kind: "status",
      stream: topic.toString(),
      stream_event,
      message: msg,
    });
    for (const ws of clients) {
      if (ws.readyState === ws.OPEN) ws.send(out);
    }
  }
}

wss.on("connection", (ws) => {
  clients.add(ws);
  ws.send(JSON.stringify({ kind: "hello", engine_cmd: CMD_ENDPOINT !== "tcp://127.0.0.1:5555" ? "custom" : "default" }));
  ws.on("close", () => clients.delete(ws));
});

function stopBridge() {
  try { pubSub.close(); } catch { /* ignore */ }
  wss.close();
}

const isMain = process.argv[1] != null && fileURLToPath(import.meta.url) === process.argv[1];
if (isMain) {
  server.listen(PORT, HOST, () => {
    console.log(`dashboard: http://${HOST}:${PORT} (token: ${TOKEN_PATH})`);
    startBridge().catch((e) => {
      console.error("status bridge stopped:", e);
      process.exitCode = 1;
    });
  });
}

export { server, startBridge, stopBridge, AUTH_TOKEN, CMD_ENDPOINT, PUB_ENDPOINT };
