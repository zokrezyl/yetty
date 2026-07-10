// Bring up the REAL zsh.wasm (the 2 MB, 169-import binary) on the
// native wasm engine and drive `zsh -f -c 'echo ...'` as far as it goes.
// Pure/early libc is implemented for real; everything else is an
// instrumented stub that logs the first time zsh reaches it — that log
// IS the prioritized worklist toward an interactive prompt.
//
// Run: node zsh_boot.mjs [path-to-zsh.wasm]
//
// PROTOTYPE (issue #21, milestone 1): superseded by zsh_main.mjs /
// zsh_host.mjs. Kept as a probe; missing imports fail loudly via
// strictImportEnv rather than the old silent return-0 auto-stub.
import { readFile } from "node:fs/promises";
import { strictImportEnv } from "./import_manifest.mjs";

const wasmPath = process.argv[2] || new URL("../../../../tmp/zsh.wasm", import.meta.url);
const bytes = await readFile(wasmPath);
const mod = await WebAssembly.compile(bytes);

const ARGV = ["zsh", "-f", "-c", "echo hello from real zsh"];
const ENVP = ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/"];

// --- state set right after instantiation ---
const state = { mem: null, view: null, u8: null, brk: 0 };
const enc = new TextEncoder();
const dec = new TextDecoder();

const grow = (need) => {
  while (state.brk + need > state.u8.length) {
    state.mem.grow(64); // +4 MiB
    refresh();
  }
};
const refresh = () => {
  state.view = new DataView(state.mem.buffer);
  state.u8 = new Uint8Array(state.mem.buffer);
};
const alloc = (n, align = 8) => {
  state.brk = (state.brk + (align - 1)) & ~(align - 1);
  grow(n);
  const p = state.brk;
  state.brk += n;
  return p;
};
const putStr = (s) => {
  const b = enc.encode(s + "\0");
  const p = alloc(b.length, 1);
  state.u8.set(b, p);
  return p;
};
const cstr = (ptr) => {
  if (!ptr) return "(null)";
  let end = ptr;
  while (state.u8[end]) end++;
  return dec.decode(state.u8.subarray(ptr, end));
};

// --- output capture ---
let out = "";
const writeBytes = (fd, ptr, len) => {
  const text = dec.decode(state.u8.subarray(ptr, ptr + len));
  out += text;
  process.stdout.write(text);
  return len;
};

// --- errno cell ---
let errnoPtr = 0;

// --- printf family: read fmt + packed va buffer from guest memory ---
// wasm32 / FreeBSD-i386: int/long/size_t/pointer = 4 bytes, aligned to
// 4; double/long long = 8 bytes, aligned to 8 in the va buffer.
function formatFromGuest(fmtPtr, vaPtr) {
  const fmt = cstr(fmtPtr);
  let va = vaPtr;
  const next4 = () => { va = (va + 3) & ~3; const v = state.view.getInt32(va, true); va += 4; return v; };
  const nextU4 = () => { va = (va + 3) & ~3; const v = state.view.getUint32(va, true); va += 4; return v; };
  const next8 = () => { va = (va + 7) & ~7; const v = state.view.getFloat64(va, true); va += 8; return v; };
  let result = "";
  for (let i = 0; i < fmt.length; i++) {
    if (fmt[i] !== "%") { result += fmt[i]; continue; }
    let spec = "%";
    i++;
    // flags, width, precision (consumed but only partially honored)
    while (i < fmt.length && "-+ #0".includes(fmt[i])) { spec += fmt[i++]; }
    let width = "";
    while (i < fmt.length && /[0-9]/.test(fmt[i])) { width += fmt[i]; spec += fmt[i++]; }
    let prec = "";
    if (fmt[i] === ".") { spec += fmt[i++]; while (i < fmt.length && /[0-9]/.test(fmt[i])) { prec += fmt[i]; spec += fmt[i++]; } }
    let lengthMod = "";
    while (i < fmt.length && "hljztLq".includes(fmt[i])) { lengthMod += fmt[i]; spec += fmt[i++]; }
    const conv = fmt[i];
    const padW = width ? parseInt(width, 10) : 0;
    const pad = (s) => (padW && s.length < padW ? (spec.includes("-") ? s.padEnd(padW) : s.padStart(padW)) : s);
    switch (conv) {
      case "d": case "i": result += pad(String(next4())); break;
      case "u": result += pad(String(nextU4())); break;
      case "x": result += pad(nextU4().toString(16)); break;
      case "X": result += pad(nextU4().toString(16).toUpperCase()); break;
      case "o": result += pad(nextU4().toString(8)); break;
      case "p": result += "0x" + nextU4().toString(16); break;
      case "c": result += String.fromCharCode(next4() & 0xff); break;
      case "s": { const sp = nextU4(); let s = cstr(sp); if (prec) s = s.slice(0, parseInt(prec, 10)); result += pad(s); break; }
      case "f": case "F": case "g": case "G": case "e": result += pad(String(next8())); break;
      case "%": result += "%"; break;
      default: result += spec + (conv || ""); break;
    }
  }
  return result;
}
const emit = (fd, text) => { const p = putStr(text); writeBytes(fd, p, enc.encode(text).length); };
// sysroot FILE* handles: __stdinp=1, __stdoutp=2, __stderrp=3 → fd 0/1/2.
const fdOfFile = (fp) => (fp >= 1 && fp <= 3 ? fp - 1 : 1);
let nextFd = 10; // fake fds for dup/pipe/fcntl(F_DUPFD)

// --- build import object: only what we implement; missing imports fail
// loudly via strictImportEnv at instantiation (no silent return-0). ---
const seenUnimpl = new Set();
const env = {};

Object.assign(env, {
  // clang command model: zsh's _start calls env.__main_argc_argv, which
  // the host resolves to the module's exported `main`.
  __main_argc_argv: (argc, argv) => state.main(argc, argv),
  __yos_argc: () => ARGV.length,
  __yos_argv_setup: (argvPtr) => { for (let i = 0; i < ARGV.length; i++) state.view.setUint32(argvPtr + i * 4, putStr(ARGV[i]), true); },
  __yos_envc: () => ENVP.length,
  __yos_envp_setup: (envpPtr) => { for (let i = 0; i < ENVP.length; i++) state.view.setUint32(envpPtr + i * 4, putStr(ENVP[i]), true); },
  exit: (code) => { const e = new Error("exit"); e.code = code; e.isExit = true; throw e; },
  _exit: (code) => { const e = new Error("exit"); e.code = code; e.isExit = true; throw e; },
  abort: () => { const e = new Error("abort"); e.isExit = true; e.code = 134; throw e; },

  malloc: (n) => alloc(n),
  calloc: (a, b) => { const p = alloc(a * b); state.u8.fill(0, p, p + a * b); return p; },
  realloc: (p, n) => { const q = alloc(n); if (p) state.u8.copyWithin(q, p, p + Math.min(n, state.brk - p)); return q; },
  free: () => {},

  memset: (d, c, n) => { state.u8.fill(c & 0xff, d, d + n); return d; },
  bzero: (d, n) => { state.u8.fill(0, d, d + n); return 0; },
  memcpy: (d, s, n) => { state.u8.copyWithin(d, s, s + n); return d; },
  memmove: (d, s, n) => { state.u8.copyWithin(d, s, s + n); return d; },

  __error: () => errnoPtr,
  getenv: (namePtr) => { const k = cstr(namePtr); const hit = ENVP.find((e) => e.startsWith(k + "=")); return hit ? putStr(hit.slice(k.length + 1)) : 0; },
  setenv: () => 0, unsetenv: () => 0,
  setlocale: () => putStr("C"),
  sysconf: () => -1,
  getuid: () => 0, geteuid: () => 0, getgid: () => 0, getegid: () => 0,
  getpid: () => 1, getppid: () => 0, getpgrp: () => 1,
  isatty: () => 0,

  write: (fd, ptr, len) => writeBytes(fd, ptr, len),
  writev: (fd, iov, cnt) => { let total = 0; for (let i = 0; i < cnt; i++) { const base = state.view.getUint32(iov + i * 8, true); const l = state.view.getUint32(iov + i * 8 + 4, true); total += writeBytes(fd, base, l); } return total; },
  printf: (fmt, va) => { const s = formatFromGuest(fmt, va); emit(1, s); return s.length; },
  vprintf: (fmt, va) => { const s = formatFromGuest(fmt, va); emit(1, s); return s.length; },
  fprintf: (fp, fmt, va) => { const s = formatFromGuest(fmt, va); emit(fdOfFile(fp), s); return s.length; },
  vfprintf: (fp, fmt, va) => { const s = formatFromGuest(fmt, va); emit(fdOfFile(fp), s); return s.length; },
  snprintf: (dst, n, fmt, va) => { const s = formatFromGuest(fmt, va); const b = enc.encode(s).subarray(0, Math.max(0, n - 1)); state.u8.set(b, dst); state.u8[dst + b.length] = 0; return s.length; },
  vsnprintf: (dst, n, fmt, va) => env.snprintf(dst, n, fmt, va),
  sprintf: (dst, fmt, va) => { const s = formatFromGuest(fmt, va); const b = enc.encode(s); state.u8.set(b, dst); state.u8[dst + b.length] = 0; return s.length; },
  vsprintf: (dst, fmt, va) => env.sprintf(dst, fmt, va),
  fputc: (c, fp) => { emit(fdOfFile(fp), String.fromCharCode(c & 0xff)); return c & 0xff; },
  putc: (c, fp) => env.fputc(c, fp),
  fputs: (sp, fp) => { emit(fdOfFile(fp), cstr(sp)); return 1; },
  puts: (sp) => { emit(1, cstr(sp) + "\n"); return 1; },
  fwrite: (ptr, sz, nm, fp) => { writeBytes(fdOfFile(fp), ptr, sz * nm); return nm; },
  fflush: () => 0, fileno: (fp) => fp, setvbuf: () => 0, __swbuf: (c) => c & 0xff,

  // --- pure string / mem (real implementations) ---
  strlen: (p) => { let n = 0; while (state.u8[p + n]) n++; return n; },
  strcmp: (a, b) => { let i = 0; for (;;) { const x = state.u8[a + i], y = state.u8[b + i]; if (x !== y) return x - y; if (!x) return 0; i++; } },
  strncmp: (a, b, n) => { for (let i = 0; i < n; i++) { const x = state.u8[a + i], y = state.u8[b + i]; if (x !== y) return x - y; if (!x) return 0; } return 0; },
  strcasecmp: (a, b) => { const lc = (c) => (c >= 65 && c <= 90 ? c + 32 : c); let i = 0; for (;;) { const x = lc(state.u8[a + i]), y = lc(state.u8[b + i]); if (x !== y) return x - y; if (!x) return 0; i++; } },
  strncasecmp: (a, b, n) => { const lc = (c) => (c >= 65 && c <= 90 ? c + 32 : c); for (let i = 0; i < n; i++) { const x = lc(state.u8[a + i]), y = lc(state.u8[b + i]); if (x !== y) return x - y; if (!x) return 0; } return 0; },
  strcoll: (a, b) => env.strcmp(a, b),
  strchr: (s, c) => { c &= 0xff; for (let p = s; ; p++) { if (state.u8[p] === c) return p; if (!state.u8[p]) return c === 0 ? p : 0; } },
  strrchr: (s, c) => { c &= 0xff; let hit = 0; for (let p = s; ; p++) { if (state.u8[p] === c) hit = p; if (!state.u8[p]) { if (c === 0) return p; return hit; } } },
  strstr: (h, n) => { const needle = cstr(n); if (!needle) return h; const hay = cstr(h); const idx = hay.indexOf(needle); return idx < 0 ? 0 : h + enc.encode(hay.slice(0, idx)).length; },
  strcpy: (d, s) => { let i = 0; do { state.u8[d + i] = state.u8[s + i]; } while (state.u8[s + i++]); return d; },
  strncpy: (d, s, n) => { let i = 0; for (; i < n && state.u8[s + i]; i++) state.u8[d + i] = state.u8[s + i]; for (; i < n; i++) state.u8[d + i] = 0; return d; },
  strlcpy: (d, s, n) => { const len = env.strlen(s); if (n) { const c = Math.min(len, n - 1); state.u8.copyWithin(d, s, s + c); state.u8[d + c] = 0; } return len; },
  strcat: (d, s) => { env.strcpy(d + env.strlen(d), s); return d; },
  strncat: (d, s, n) => { let dl = env.strlen(d), i = 0; for (; i < n && state.u8[s + i]; i++) state.u8[d + dl + i] = state.u8[s + i]; state.u8[d + dl + i] = 0; return d; },
  strdup: (s) => putStr(cstr(s)),
  strspn: (s, set) => { const setStr = cstr(set); let n = 0; for (;;) { const c = state.u8[s + n]; if (!c || !setStr.includes(String.fromCharCode(c))) return n; n++; } },
  strcspn: (s, set) => { const setStr = cstr(set); let n = 0; for (;;) { const c = state.u8[s + n]; if (!c || setStr.includes(String.fromCharCode(c))) return n; n++; } },
  memchr: (s, c, n) => { c &= 0xff; for (let i = 0; i < n; i++) if (state.u8[s + i] === c) return s + i; return 0; },
  memcmp: (a, b, n) => { for (let i = 0; i < n; i++) { const x = state.u8[a + i], y = state.u8[b + i]; if (x !== y) return x - y; } return 0; },
  strtoul: (s, endp, base) => { const str = cstr(s); const m = str.match(/^\s*[+-]?(0x[0-9a-fA-F]+|[0-9]+)/); const v = m ? parseInt(m[0], base || (m[0].includes("0x") ? 16 : 10)) >>> 0 : 0; if (endp) state.view.setUint32(endp, s + (m ? m[0].length : 0), true); return v; },
  strtol: (s, endp, base) => env.strtoul(s, endp, base) | 0,
  strtonum: (s) => { const v = parseInt(cstr(s), 10); return isNaN(v) ? 0 : v; },
  atoi: (s) => parseInt(cstr(s), 10) || 0,
  strerror: (n) => putStr("Error " + n),

  // --- minimal fd / tty surface: no real FS, no tty (non-interactive),
  // EOF on reads. Enough for `zsh -c` to run a builtin like echo. ---
  open: (pathPtr, flags) => { const path = cstr(pathPtr); if (errnoPtr) state.view.setUint32(errnoPtr, 2, true); return -1; /* ENOENT — no FS yet */ },
  openat: () => -1,
  close: () => 0,
  read: () => 0, // EOF
  fcntl: (fd, cmd) => (cmd === 0 ? nextFd++ : 0), // F_DUPFD=0
  dup: () => nextFd++,
  dup2: (a, b) => b,
  pipe: (ptr) => { state.view.setUint32(ptr, nextFd++, true); state.view.setUint32(ptr + 4, nextFd++, true); return 0; },
  ttyname: () => 0,
  ttyname_r: () => 0,
  tcgetattr: () => -1,
  tcsetattr: () => 0,
  tcgetpgrp: () => -1,
  tcsetpgrp: () => 0,
  ioctl: () => -1,
  poll: () => 0,
  select: () => 0,
  fstat: () => -1,
  lstat: () => -1,
  stat: () => -1,
  access: () => -1,
  lseek: () => 0,
  umask: () => 0o22,
  getcwd: (buf, n) => { const s = enc.encode("/\0"); state.u8.set(s, buf); return buf; },
  // signals — accept and ignore for a non-interactive run.
  signal: () => 0, sigaction: () => 0, sigemptyset: () => 0, sigfillset: () => 0,
  sigaddset: () => 0, sigprocmask: () => 0, sigsuspend: () => 0,
  alarm: () => 0, kill: () => 0, killpg: () => 0,
  setpgid: () => 0, getlogin: () => 0,
  getrlimit: () => 0, setrlimit: () => 0, getrusage: () => 0,
});

// --- instantiate + run ---
strictImportEnv(env, mod, {
  label: "zsh-boot",
  onCall: (name, args) => {
    if (!seenUnimpl.has(name)) {
      seenUnimpl.add(name);
      console.error(`  UNIMPL env.${name}(${args.join(",")})`);
    }
  },
});
let inst;
try {
  inst = await WebAssembly.instantiate(mod, { env });
} catch (e) {
  console.error("INSTANTIATE FAILED:", e.message);
  process.exit(1);
}
state.mem = inst.exports.memory;
state.main = inst.exports.main;
refresh();
const heapBase = inst.exports.__heap_base?.value ?? inst.exports.__heap_base ?? (1 << 20);
state.brk = (typeof heapBase === "number" ? heapBase : 1 << 20);
errnoPtr = alloc(4);

console.error(`zsh.wasm: ${bytes.length} bytes | heap_base=${heapBase} | running: ${ARGV.join(" ")}\n`);
let exitCode = "trap";
try {
  inst.exports._start();
  exitCode = 0;
} catch (e) {
  if (e.isExit) exitCode = e.code;
  else { console.error("\nTRAP:", e.message); exitCode = "trap:" + e.message; }
}
console.error(`\n--- exit: ${exitCode} ---`);
console.error(`captured output: ${JSON.stringify(out)}`);
console.error(`unimplemented hit (${seenUnimpl.size}): ${[...seenUnimpl].sort().join(" ")}`);
