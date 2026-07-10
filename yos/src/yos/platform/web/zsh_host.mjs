// Browser-safe host for the REAL zsh.wasm. No node/DOM APIs — give it a
// compiled WebAssembly.Module and an output callback. Each runZsh() makes
// a fresh instance + fresh heap, so it works as a per-command REPL:
// every typed line runs through real zsh (`zsh -f -c '<line>'`).
//
// Implemented for real: argv/env, exit, malloc, mem/string, errno,
// write/writev, the printf family. Stateful libc (real FS, tty, fork) is
// stubbed to non-interactive defaults — enough for builtins like echo,
// print, [[ ]], arithmetic, parameter expansion. onUnimpl reports the
// first hit of anything not yet implemented (the worklist).
//
// PROTOTYPE (issue #21, milestone 1): hand-written JS libc, not the
// production yos surface. Imports it does not implement fail loudly via
// strictImportEnv, never via a catch-all return-0 fallback.

import { strictImportEnv } from "./import_manifest.mjs";

export function runZsh(mod, argv, onOutput, onUnimpl, opts = {}) {
  const tools = opts.tools || new Map(); // name -> compiled WebAssembly.Module
  const basename = (p) => { const i = p.lastIndexOf("/"); return i < 0 ? p : p.slice(i + 1); };
  const enc = new TextEncoder();
  const dec = new TextDecoder();
  const env_vars = ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/"];

  const state = { mem: null, view: null, u8: null, brk: 0, main: null };
  const refresh = () => { state.view = new DataView(state.mem.buffer); state.u8 = new Uint8Array(state.mem.buffer); };
  const grow = (need) => { while (state.brk + need > state.u8.length) { state.mem.grow(64); refresh(); } };
  const alloc = (n, align = 8) => { state.brk = (state.brk + (align - 1)) & ~(align - 1); grow(n); const p = state.brk; state.brk += n; return p; };
  const putStr = (s) => { const b = enc.encode(s + "\0"); const p = alloc(b.length, 1); state.u8.set(b, p); return p; };
  const cstr = (ptr) => { if (!ptr) return ""; let e = ptr; while (state.u8[e]) e++; return dec.decode(state.u8.subarray(ptr, e)); };

  let errnoPtr = 0;
  let nextFd = 10;
  const writeBytes = (fd, ptr, len) => { onOutput(fd, dec.decode(state.u8.subarray(ptr, ptr + len))); return len; };
  const fdOfFile = (fp) => (fp >= 1 && fp <= 3 ? fp - 1 : 1);
  const emit = (fd, text) => onOutput(fd, text);

  function formatFromGuest(fmtPtr, vaPtr) {
    const fmt = cstr(fmtPtr);
    let va = vaPtr;
    const n4 = () => { va = (va + 3) & ~3; const v = state.view.getInt32(va, true); va += 4; return v; };
    const u4 = () => { va = (va + 3) & ~3; const v = state.view.getUint32(va, true); va += 4; return v; };
    const f8 = () => { va = (va + 7) & ~7; const v = state.view.getFloat64(va, true); va += 8; return v; };
    let out = "";
    for (let i = 0; i < fmt.length; i++) {
      if (fmt[i] !== "%") { out += fmt[i]; continue; }
      let spec = "%"; i++;
      while (i < fmt.length && "-+ #0".includes(fmt[i])) spec += fmt[i++];
      let width = ""; while (i < fmt.length && /[0-9]/.test(fmt[i])) { width += fmt[i]; spec += fmt[i++]; }
      let prec = ""; if (fmt[i] === ".") { spec += fmt[i++]; while (i < fmt.length && /[0-9]/.test(fmt[i])) { prec += fmt[i]; spec += fmt[i++]; } }
      while (i < fmt.length && "hljztLq".includes(fmt[i])) spec += fmt[i++];
      const conv = fmt[i];
      const w = width ? parseInt(width, 10) : 0;
      const pad = (s) => (w && s.length < w ? (spec.includes("-") ? s.padEnd(w) : s.padStart(w)) : s);
      switch (conv) {
        case "d": case "i": out += pad(String(n4())); break;
        case "u": out += pad(String(u4())); break;
        case "x": out += pad(u4().toString(16)); break;
        case "X": out += pad(u4().toString(16).toUpperCase()); break;
        case "o": out += pad(u4().toString(8)); break;
        case "p": out += "0x" + u4().toString(16); break;
        case "c": out += String.fromCharCode(n4() & 0xff); break;
        case "s": { const sp = u4(); let s = cstr(sp); if (prec) s = s.slice(0, parseInt(prec, 10)); out += pad(s); break; }
        case "f": case "F": case "g": case "G": case "e": out += pad(String(f8())); break;
        case "%": out += "%"; break;
        default: out += spec + (conv || ""); break;
      }
    }
    return out;
  }

  // Only the functions the prototype actually implements. Missing imports
  // are hardened by strictImportEnv() at instantiation time (fail loudly),
  // not pre-filled with silent return-0 stubs (issue #21).
  const env = {};

  Object.assign(env, {
    __main_argc_argv: (argc, argv2) => state.main(argc, argv2),
    __yos_argc: () => argv.length,
    __yos_argv_setup: (p) => { for (let i = 0; i < argv.length; i++) state.view.setUint32(p + i * 4, putStr(argv[i]), true); },
    __yos_envc: () => env_vars.length,
    __yos_envp_setup: (p) => { for (let i = 0; i < env_vars.length; i++) state.view.setUint32(p + i * 4, putStr(env_vars[i]), true); },
    exit: (c) => { const e = new Error("exit"); e.isExit = true; e.code = c; throw e; },
    _exit: (c) => { const e = new Error("exit"); e.isExit = true; e.code = c; throw e; },
    abort: () => { const e = new Error("abort"); e.isExit = true; e.code = 134; throw e; },

    malloc: (n) => alloc(n),
    calloc: (a, b) => { const p = alloc(a * b); state.u8.fill(0, p, p + a * b); return p; },
    realloc: (p, n) => { const q = alloc(n); if (p) state.u8.copyWithin(q, p, p + n); return q; },
    free: () => {},
    memset: (d, c, n) => { state.u8.fill(c & 0xff, d, d + n); return d; },
    bzero: (d, n) => { state.u8.fill(0, d, d + n); return 0; },
    memcpy: (d, s, n) => { state.u8.copyWithin(d, s, s + n); return d; },
    memmove: (d, s, n) => { state.u8.copyWithin(d, s, s + n); return d; },

    __error: () => errnoPtr,
    getenv: (n) => { const k = cstr(n); const h = env_vars.find((e) => e.startsWith(k + "=")); return h ? putStr(h.slice(k.length + 1)) : 0; },
    setenv: () => 0, unsetenv: () => 0, putenv: () => 0,
    setlocale: () => putStr("C"), nl_langinfo: () => putStr(""), ___mb_cur_max: () => 1,
    sysconf: () => -1,
    getuid: () => 0, geteuid: () => 0, getgid: () => 0, getegid: () => 0,
    getpid: () => 1, getppid: () => 0, getpgrp: () => 1, getgroups: () => 0,
    isatty: () => 0,

    write: (fd, p, l) => writeBytes(fd, p, l),
    writev: (fd, iov, c) => { let t = 0; for (let i = 0; i < c; i++) { const b = state.view.getUint32(iov + i * 8, true); const l = state.view.getUint32(iov + i * 8 + 4, true); t += writeBytes(fd, b, l); } return t; },
    printf: (f, v) => { const s = formatFromGuest(f, v); emit(1, s); return s.length; },
    vprintf: (f, v) => { const s = formatFromGuest(f, v); emit(1, s); return s.length; },
    fprintf: (fp, f, v) => { const s = formatFromGuest(f, v); emit(fdOfFile(fp), s); return s.length; },
    vfprintf: (fp, f, v) => { const s = formatFromGuest(f, v); emit(fdOfFile(fp), s); return s.length; },
    snprintf: (d, n, f, v) => { const s = formatFromGuest(f, v); const b = enc.encode(s).subarray(0, Math.max(0, n - 1)); state.u8.set(b, d); state.u8[d + b.length] = 0; return s.length; },
    vsnprintf: (d, n, f, v) => env.snprintf(d, n, f, v),
    sprintf: (d, f, v) => { const s = formatFromGuest(f, v); const b = enc.encode(s); state.u8.set(b, d); state.u8[d + b.length] = 0; return s.length; },
    vsprintf: (d, f, v) => env.sprintf(d, f, v),
    fputc: (c, fp) => { emit(fdOfFile(fp), String.fromCharCode(c & 0xff)); return c & 0xff; },
    putc: (c, fp) => env.fputc(c, fp),
    putchar: (c) => { emit(1, String.fromCharCode(c & 0xff)); return c & 0xff; },
    fputs: (s, fp) => { emit(fdOfFile(fp), cstr(s)); return 1; },
    puts: (s) => { emit(1, cstr(s) + "\n"); return 1; },
    fwrite: (p, sz, nm, fp) => { writeBytes(fdOfFile(fp), p, sz * nm); return nm; },
    fflush: () => 0, fileno: (fp) => fp, setvbuf: () => 0, setbuf: () => 0, __swbuf: (c) => c & 0xff,
    clearerr: () => 0, ferror: () => 0, feof: () => 0,

    strlen: (p) => { let n = 0; while (state.u8[p + n]) n++; return n; },
    strcmp: (a, b) => { let i = 0; for (;;) { const x = state.u8[a + i], y = state.u8[b + i]; if (x !== y) return x - y; if (!x) return 0; i++; } },
    strncmp: (a, b, n) => { for (let i = 0; i < n; i++) { const x = state.u8[a + i], y = state.u8[b + i]; if (x !== y) return x - y; if (!x) return 0; } return 0; },
    strcasecmp: (a, b) => { const lc = (c) => (c >= 65 && c <= 90 ? c + 32 : c); let i = 0; for (;;) { const x = lc(state.u8[a + i]), y = lc(state.u8[b + i]); if (x !== y) return x - y; if (!x) return 0; i++; } },
    strncasecmp: (a, b, n) => { const lc = (c) => (c >= 65 && c <= 90 ? c + 32 : c); for (let i = 0; i < n; i++) { const x = lc(state.u8[a + i]), y = lc(state.u8[b + i]); if (x !== y) return x - y; if (!x) return 0; } return 0; },
    strcoll: (a, b) => env.strcmp(a, b),
    strchr: (s, c) => { c &= 0xff; for (let p = s; ; p++) { if (state.u8[p] === c) return p; if (!state.u8[p]) return c === 0 ? p : 0; } },
    strrchr: (s, c) => { c &= 0xff; let hit = 0; for (let p = s; ; p++) { if (state.u8[p] === c) hit = p; if (!state.u8[p]) return c === 0 ? p : hit; } },
    strstr: (h, n) => { const needle = cstr(n); if (!needle) return h; const hay = cstr(h); const idx = hay.indexOf(needle); return idx < 0 ? 0 : h + enc.encode(hay.slice(0, idx)).length; },
    strcpy: (d, s) => { let i = 0; do { state.u8[d + i] = state.u8[s + i]; } while (state.u8[s + i++]); return d; },
    strncpy: (d, s, n) => { let i = 0; for (; i < n && state.u8[s + i]; i++) state.u8[d + i] = state.u8[s + i]; for (; i < n; i++) state.u8[d + i] = 0; return d; },
    strlcpy: (d, s, n) => { const len = env.strlen(s); if (n) { const c = Math.min(len, n - 1); state.u8.copyWithin(d, s, s + c); state.u8[d + c] = 0; } return len; },
    strcat: (d, s) => { env.strcpy(d + env.strlen(d), s); return d; },
    strncat: (d, s, n) => { let dl = env.strlen(d), i = 0; for (; i < n && state.u8[s + i]; i++) state.u8[d + dl + i] = state.u8[s + i]; state.u8[d + dl + i] = 0; return d; },
    strdup: (s) => putStr(cstr(s)),
    strspn: (s, set) => { const ss = cstr(set); let n = 0; for (;;) { const c = state.u8[s + n]; if (!c || !ss.includes(String.fromCharCode(c))) return n; n++; } },
    strcspn: (s, set) => { const ss = cstr(set); let n = 0; for (;;) { const c = state.u8[s + n]; if (!c || ss.includes(String.fromCharCode(c))) return n; n++; } },
    memchr: (s, c, n) => { c &= 0xff; for (let i = 0; i < n; i++) if (state.u8[s + i] === c) return s + i; return 0; },
    memcmp: (a, b, n) => { for (let i = 0; i < n; i++) { const x = state.u8[a + i], y = state.u8[b + i]; if (x !== y) return x - y; } return 0; },
    strtoul: (s, endp, base) => { const str = cstr(s); const m = str.match(/^\s*[+-]?(0x[0-9a-fA-F]+|[0-9]+)/); const v = m ? parseInt(m[0], base || (m[0].includes("0x") ? 16 : 10)) >>> 0 : 0; if (endp) state.view.setUint32(endp, s + (m ? m[0].length : 0), true); return v; },
    strtol: (s, e, b) => env.strtoul(s, e, b) | 0,
    strtod: (s, e) => { const v = parseFloat(cstr(s)); return isNaN(v) ? 0 : v; },
    strtonum: (s) => { const v = parseInt(cstr(s), 10); return isNaN(v) ? 0 : v; },
    atoi: (s) => parseInt(cstr(s), 10) || 0,
    strerror: (n) => putStr("Error " + n),
    qsort: () => 0,

    // external commands: a known tool name resolves like a real
    // executable on PATH, and execve loads + runs its wasm.
    access: (pathPtr) => (tools.has(basename(cstr(pathPtr))) ? 0 : -1),
    execve: (pathPtr, argvPtr) => {
      const name = basename(cstr(pathPtr));
      if (!tools.has(name)) { if (errnoPtr) state.view.setUint32(errnoPtr, 2, true); return -1; /* ENOENT */ }
      const args = [];
      for (let p = argvPtr; ; p += 4) { const sp = state.view.getUint32(p, true); if (!sp) break; args.push(cstr(sp)); }
      const res = runZsh(tools.get(name), args.length ? args : [name], onOutput, onUnimpl, opts);
      const e = new Error("exec"); e.isExit = true; e.code = typeof res.exitCode === "number" ? res.exitCode : 0; throw e; // execve does not return
    },

    // minimal fd / tty surface: no FS, no tty (non-interactive), EOF reads.
    open: () => { if (errnoPtr) state.view.setUint32(errnoPtr, 2, true); return -1; },
    openat: () => -1, close: () => 0, read: () => 0,
    fcntl: (fd, cmd) => (cmd === 0 ? nextFd++ : 0),
    dup: () => nextFd++, dup2: (a, b) => b,
    pipe: (p) => { state.view.setUint32(p, nextFd++, true); state.view.setUint32(p + 4, nextFd++, true); return 0; },
    ttyname: () => 0, tcgetattr: () => -1, tcsetattr: () => 0, tcgetpgrp: () => -1, tcsetpgrp: () => 0,
    ioctl: () => -1, poll: () => 0, select: () => 0,
    fstat: () => -1, lstat: () => -1, stat: () => -1, lseek: () => 0,
    umask: () => 0o22, getcwd: (buf) => { state.u8.set(enc.encode("/\0"), buf); return buf; },
    signal: () => 0, sigaction: () => 0, sigemptyset: () => 0, sigfillset: () => 0,
    sigaddset: () => 0, sigprocmask: () => 0, sigsuspend: () => 0,
    alarm: () => 0, kill: () => 0, killpg: () => 0, setpgid: () => 0, getlogin: () => 0,
    getrlimit: () => 0, setrlimit: () => 0, getrusage: () => 0,
    gethostname: (buf, len) => { const b = enc.encode("yos-web".slice(0, len - 1) + "\0"); state.u8.set(b, buf); return 0; },
    // real clock (wasm32/i386: time_t, suseconds_t, long tv_nsec all 4 bytes).
    time: (tptr) => { const s = Math.floor(Date.now() / 1000); if (tptr) state.view.setUint32(tptr, s, true); return s; },
    gettimeofday: (tv) => { const ms = Date.now(); if (tv) { state.view.setUint32(tv, Math.floor(ms / 1000), true); state.view.setUint32(tv + 4, (ms % 1000) * 1000, true); } return 0; },
    clock_gettime: (id, ts) => { const ms = Date.now(); if (ts) { state.view.setUint32(ts, Math.floor(ms / 1000), true); state.view.setUint32(ts + 4, (ms % 1000) * 1e6, true); } return 0; },
    localtime: () => 0, mktime: () => 0, strftime: (dst, n, fmt) => { const s = new Date().toString().slice(0, n - 1); state.u8.set(enc.encode(s + "\0"), dst); return s.length; },
    nanosleep: () => 0, sleep: () => 0,
    srand: () => 0, rand: () => 42,
  });

  let inst;
  try { strictImportEnv(env, mod, { label: "zsh", onCall: onUnimpl }); inst = new WebAssembly.Instance(mod, { env }); }
  catch (e) { return { exitCode: "instantiate-failed", error: e.message }; }
  state.mem = inst.exports.memory;
  state.main = inst.exports.main;
  refresh();
  const hb = inst.exports.__heap_base;
  state.brk = (typeof hb === "object" ? hb.value : hb) ?? (1 << 20);
  errnoPtr = alloc(4);

  try { inst.exports._start(); return { exitCode: 0 }; }
  catch (e) { return e.isExit ? { exitCode: e.code } : { exitCode: "trap", error: e.message }; }
}
