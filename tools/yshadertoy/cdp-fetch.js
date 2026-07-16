// Fetch a Shadertoy shader without an API key by driving a real browser through
// Cloudflare: navigate to /view/<id>, let Chrome clear the challenge, and
// capture the JSON the page's own XHR to /shadertoy returns.
//   node cdp-fetch.js <shader-id> <out.json> [port]
//
// A PERSISTENT profile dir caches Cloudflare's cf_clearance cookie, so once the
// challenge is cleared subsequent fetches skip it. Navigation is retried a few
// times because the headless challenge is flaky.
const { spawn } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const id = process.argv[2];
const outJson = process.argv[3];
const port = parseInt(process.argv[4] || "9377", 10);
const viewUrl = `https://www.shadertoy.com/view/${id}`;

const profileDir = path.join(os.homedir(), ".cache", "yshadertoy", "cf-profile");
fs.mkdirSync(profileDir, { recursive: true });
const flags = [
  "--headless=new", "--no-sandbox", "--disable-dev-shm-usage", "--window-size=1280,900",
  "--user-agent=Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36",
  `--remote-debugging-port=${port}`, `--user-data-dir=${profileDir}`, "about:blank",
];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function pageWs() {
  for (let i = 0; i < 60; i++) {
    try {
      const t = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      const p = t.find((x) => x.type === "page" && x.webSocketDebuggerUrl);
      if (p) return p.webSocketDebuggerUrl;
    } catch {}
    await sleep(250);
  }
  throw new Error("no devtools page");
}

function connect(url) {
  const ws = new WebSocket(url);
  let nextId = 1;
  const pending = new Map();
  const listeners = [];
  ws.addEventListener("message", (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    else if (m.method) listeners.forEach((fn) => fn(m));
  });
  const ready = new Promise((r) => ws.addEventListener("open", r));
  const send = (method, params = {}) =>
    new Promise((res) => { const cid = nextId++; pending.set(cid, res); ws.send(JSON.stringify({ id: cid, method, params })); });
  return { ready, send, on: (fn) => listeners.push(fn), close: () => ws.close() };
}

(async () => {
  const child = spawn("google-chrome", flags, { stdio: "ignore", detached: true });
  const cleanup = () => { try { process.kill(-child.pid, "SIGKILL"); } catch {} };
  try {
    const cdp = connect(await pageWs());
    await cdp.ready;
    await cdp.send("Network.enable");
    await cdp.send("Page.enable");
    let shaderReqId = null;
    cdp.on((m) => {
      if (m.method === "Network.responseReceived" &&
          m.params.response.url.endsWith("/shadertoy") && m.params.response.status === 200) {
        shaderReqId = m.params.requestId;
      }
    });

    let body = null;
    for (let attempt = 0; attempt < 3 && !body; attempt++) {
      shaderReqId = null;
      await cdp.send("Page.navigate", { url: viewUrl });
      for (let i = 0; i < 60; i++) {          // up to 30s per attempt
        if (shaderReqId) {
          const r = await cdp.send("Network.getResponseBody", { requestId: shaderReqId });
          if (r.result && r.result.body) {
            body = r.result.base64Encoded ? Buffer.from(r.result.body, "base64").toString("utf8") : r.result.body;
            break;
          }
        }
        await sleep(500);
      }
    }
    if (!body) throw new Error("no /shadertoy response (Cloudflare not cleared, or shader private)");
    JSON.parse(body);
    fs.writeFileSync(outJson, body);
    console.log("OK " + outJson + " (" + fs.statSync(outJson).size + " bytes)");
    cdp.close();
  } catch (e) {
    console.error("FAIL: " + e.message);
    process.exitCode = 1;
  } finally {
    cleanup();
  }
})();
