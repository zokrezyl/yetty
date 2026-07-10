import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
const here = new URL(".", import.meta.url);
const types = { ".html":"text/html", ".mjs":"text/javascript" };
const server = createServer(async (req,res)=>{ const p=new URL("."+(req.url==="/"?"/share_probe.html":req.url.split("?")[0]),here);
  try{const b=await readFile(p);res.setHeader("Content-Type",types[extname(p.pathname)]||"application/octet-stream");res.setHeader("Cross-Origin-Opener-Policy","same-origin");res.setHeader("Cross-Origin-Embedder-Policy","require-corp");res.end(b);}catch{res.statusCode=404;res.end("nf");}});
await new Promise(r=>server.listen(8155,"127.0.0.1",r));
const prof=mkdtempSync(join(tmpdir(),"c-"));
const chrome=spawn(process.env.YOS_CHROME||"google-chrome-stable",["--headless=new","--no-sandbox","--disable-gpu","--remote-debugging-port=9361",`--user-data-dir=${prof}`,"about:blank"],{stdio:["ignore","ignore","pipe"]});
const clean=()=>{try{chrome.kill();}catch{}server.close();};process.on("exit",clean);
async function url(){for(let i=0;i<100;i++){try{const r=await fetch("http://127.0.0.1:9361/json/version");if(r.ok)return (await r.json()).webSocketDebuggerUrl;}catch{}await new Promise(r=>setTimeout(r,100));}throw 0;}
const wsu=await url();const t=await fetch("http://127.0.0.1:9361/json/new?about:blank",{method:"PUT"}).then(r=>r.json());
const ws=new WebSocket(t.webSocketDebuggerUrl);let id=0;const pend=new Map();
ws.onmessage=ev=>{const m=JSON.parse(ev.data);if(m.id&&pend.has(m.id)){pend.get(m.id)(m.result);pend.delete(m.id);}};
await new Promise(r=>ws.onopen=r);
const send=(method,params={})=>new Promise(r=>{const i=++id;pend.set(i,r);ws.send(JSON.stringify({id:i,method,params}));});
await send("Runtime.enable");await send("Page.navigate",{url:"http://127.0.0.1:8155/share_probe.html"});
let res=null;for(let i=0;i<60;i++){const r=await send("Runtime.evaluate",{expression:"window.__probe||null",returnByValue:true});res=r.result.value;if(res)break;await new Promise(r=>setTimeout(r,100));}
console.error("PROBE:", JSON.stringify(res));clean();process.exit(0);
