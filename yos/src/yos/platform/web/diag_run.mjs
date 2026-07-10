import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
const here=new URL(".",import.meta.url);
const types={".html":"text/html",".mjs":"text/javascript",".wasm":"application/wasm"};
const server=createServer(async(req,res)=>{const p=new URL("."+(req.url==="/"?"/threads.html":req.url.split("?")[0]),here);try{const b=await readFile(p);res.setHeader("Content-Type",types[extname(p.pathname)]||"application/octet-stream");res.setHeader("Cross-Origin-Opener-Policy","same-origin");res.setHeader("Cross-Origin-Embedder-Policy","require-corp");res.end(b);}catch{res.statusCode=404;res.end("nf");}});
await new Promise(r=>server.listen(8160,"127.0.0.1",r));
const prof=mkdtempSync(join(tmpdir(),"c-"));
const chrome=spawn(process.env.YOS_CHROME,["--headless=new","--no-sandbox","--disable-gpu","--remote-debugging-port=9371",`--user-data-dir=${prof}`,"about:blank"],{stdio:["ignore","ignore","pipe"]});
const clean=()=>{try{chrome.kill();}catch{}server.close();};process.on("exit",clean);
async function url(){for(let i=0;i<100;i++){try{const r=await fetch("http://127.0.0.1:9371/json/version");if(r.ok)return(await r.json()).webSocketDebuggerUrl;}catch{}await new Promise(r=>setTimeout(r,100));}}
const t=await fetch("http://127.0.0.1:9371/json/new?about:blank",{method:"PUT"}).then(r=>r.json());
const ws=new WebSocket(t.webSocketDebuggerUrl);let id=0;const pend=new Map();const errs=[];
ws.onmessage=ev=>{const m=JSON.parse(ev.data);if(m.id&&pend.has(m.id)){pend.get(m.id)(m.result);pend.delete(m.id);}else if(m.method==="Log.entryAdded"&&m.params.entry.level==="error")errs.push(m.params.entry.text);};
await new Promise(r=>ws.onopen=r);await url();
const send=(method,params={})=>new Promise(r=>{const i=++id;pend.set(i,r);ws.send(JSON.stringify({id:i,method,params}));});
await send("Runtime.enable");await send("Log.enable");await send("Page.enable");
await send("Page.navigate",{url:"http://127.0.0.1:8160/threads.html?diag=1"});
let res=null;for(let i=0;i<70;i++){const r=await send("Runtime.evaluate",{expression:"window.__threads||null",returnByValue:true});res=r.result.value;if(res)break;await new Promise(r=>setTimeout(r,100));}
console.error("DIAG RESULT:",JSON.stringify(res));
console.error("PAGE ERRORS:",errs.join(" | ")||"none");
clean();process.exit(0);
