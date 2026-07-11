// ─────────────────────────────────────────────────────────────────────────
// LEGACY / FROZEN — hand-written JavaScript libc host (epic #33, issue #39).
//
// This is the OLD browser runtime: a JavaScript reimplementation of the
// FreeBSD libc/syscall surface. The convergence effort (epic #33) replaces it
// with the yos C runtime compiled to wasm — see src/yos/platform/web/host/
// (yos-host.wasm) and docs/browser.md.
//
// FROZEN: no new *semantic* patches. Only test-harness compatibility changes
// are allowed here. Every libc behaviour added to this file is future
// divergence from the C host. New guest-visible semantics belong in the yos C
// bridge/impl/vfs, not here. This file stays the legacy runner (kept behind an
// explicit selector — see host-select.mjs) until the parity gates in
// docs/browser.md §5 pass; only then does the browser default switch to the C
// host and this is retired.
// ─────────────────────────────────────────────────────────────────────────
//
// Process-aware browser host for asyncified yos wasm guests.
//
// Real fork via asyncify, the same dance native yos runs: fork() calls
// asyncify_start_unwind, the wasm stack pops back to JS, we snapshot the
// linear memory, build a CHILD instance with a copy, and
// asyncify_start_rewind fast-forwards both back to the fork callsite —
// child returns 0, parent returns the child pid. A process table assigns
// pids; waitpid reaps. Cooperative (single-threaded): the child runs to
// completion before the parent resumes, which is correct for
// fork+wait and recursive fork trees (concurrent live children — pipes,
// pthreads — need Workers and come later).
//
// The libc surface is shared with the simple runner via buildLibc().
//
// PROTOTYPE (issue #21, milestone 1): hand-written JS libc/process model,
// not the production yos surface. Imports it does not implement fail loudly
// via strictImportEnv, never via a catch-all return-0 fallback.

import { loadLiblua, installLuaForwarders } from "./lua/lua_bridge.mjs";
import { strictImportEnv } from "./import_manifest.mjs";

const ASYNCIFY_NORMAL = 0, ASYNCIFY_UNWINDING = 1, ASYNCIFY_REWINDING = 2;
const ASYNCIFY_BUF_SIZE = 16384;

const enc = new TextEncoder();
const dec = new TextDecoder();
const DEFAULT_ENV = ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/"];

// Per-instance memory state + allocator over the guest's linear memory.
function makeState() {
  const state = { mem: null, view: null, u8: null, brk: 0, main: null, errnoPtr: 0, nextFd: 10, asyncifyPtr: 0 };
  state.refresh = () => { state.view = new DataView(state.mem.buffer); state.u8 = new Uint8Array(state.mem.buffer); };
  // The whole process tree runs in one synchronous JS turn (no GC
  // between forks), so every live instance's memory adds up. Modest
  // increments balance per-instance size against grow-call overhead; a
  // very deep recursive tree (100+) still pushes the browser's wasm
  // budget, so use a smaller fanout in-browser.
  state.grow = (need) => { while (state.brk + need > state.u8.length) { const before = state.u8.length; state.mem.grow(64); state.refresh(); if (state.u8.length === before) throw new Error("out of wasm memory"); } };
  // Address windows the bump allocator must never hand out — a shared-library
  // companion module (liblua.wasm) parks its static data + stack at a fixed
  // --global-base in the SAME linear memory (lua_bridge.mjs registers the
  // window). Without the skip, a guest heap that grows past the window
  // allocates straight through the library's data and corrupts it: from-zsh
  // nvim's server heap crossed 256 MiB and Lua state shredded into
  // "unreachable" traps. null until something registers; costs nothing then.
  state.reserved = null;
  state.reserve = (lo, hi) => { (state.reserved = state.reserved || []).push({ lo, hi }); };
  state.alloc = (n, align = 8) => {
    state.brk = (state.brk + (align - 1)) & ~(align - 1);
    if (state.reserved) for (const r of state.reserved) if (state.brk < r.hi && state.brk + n > r.lo) state.brk = (r.hi + (align - 1)) & ~(align - 1);
    state.grow(n); const p = state.brk; state.brk += n; return p;
  };
  state.putStr = (s) => { const b = enc.encode(s + "\0"); const p = state.alloc(b.length, 1); state.u8.set(b, p); return p; };
  state.cstr = (ptr) => { if (!ptr) return ""; let e = ptr; while (state.u8[e]) e++; return dec.decode(state.u8.subarray(ptr, e)); };
  return state;
}

function formatFromGuest(state, fmtPtr, vaPtr) {
  const fmt = state.cstr(fmtPtr);
  let va = vaPtr;
  // wasm32/i386: int/long/size_t/ptr = 4 bytes; long long/intmax = 8;
  // double = 8. The length modifier decides 4-vs-8 for integers.
  const n4 = () => { va = (va + 3) & ~3; const v = state.view.getInt32(va, true); va += 4; return v; };
  const u4 = () => { va = (va + 3) & ~3; const v = state.view.getUint32(va, true); va += 4; return v; };
  const n8 = () => { va = (va + 7) & ~7; const lo = state.view.getUint32(va, true), hi = state.view.getInt32(va + 4, true); va += 8; return hi * 4294967296 + lo; };
  const u8int = () => { va = (va + 7) & ~7; const lo = state.view.getUint32(va, true), hi = state.view.getUint32(va + 4, true); va += 8; return hi * 4294967296 + lo; };
  const f8 = () => { va = (va + 7) & ~7; const v = state.view.getFloat64(va, true); va += 8; return v; };
  let out = "";
  for (let i = 0; i < fmt.length; i++) {
    if (fmt[i] !== "%") { out += fmt[i]; continue; }
    let spec = "%"; i++;
    while (i < fmt.length && "-+ #0".includes(fmt[i])) spec += fmt[i++];
    // Width: digits, or `*` = take the width from the next int argument
    // (negative ⇒ left-justify). Consuming that arg here keeps the va_list
    // aligned — not handling `*` corrupts every following arg.
    let width = "", leftStar = false;
    if (fmt[i] === "*") { let wv = n4(); spec += fmt[i++]; if (wv < 0) { leftStar = true; wv = -wv; } width = String(wv); }
    else while (i < fmt.length && /[0-9]/.test(fmt[i])) { width += fmt[i]; spec += fmt[i++]; }
    let prec = "";
    if (fmt[i] === ".") { spec += fmt[i++]; if (fmt[i] === "*") { const pv = n4(); spec += fmt[i++]; prec = pv < 0 ? "" : String(pv); } else while (i < fmt.length && /[0-9]/.test(fmt[i])) { prec += fmt[i]; spec += fmt[i++]; } }
    let lengthMod = ""; while (i < fmt.length && "hljztLq".includes(fmt[i])) { lengthMod += fmt[i]; spec += fmt[i++]; }
    const wide = /ll|j|q/.test(lengthMod); // 8-byte integer
    const conv = fmt[i];
    const w = width ? parseInt(width, 10) : 0;
    const pad = (s) => (w && s.length < w ? (spec.includes("-") || leftStar ? s.padEnd(w) : s.padStart(w)) : s);
    switch (conv) {
      case "d": case "i": out += pad(String(wide ? n8() : n4())); break;
      case "u": out += pad(String(wide ? u8int() : u4())); break;
      case "x": out += pad((wide ? u8int() : u4()).toString(16)); break;
      case "X": out += pad((wide ? u8int() : u4()).toString(16).toUpperCase()); break;
      case "o": out += pad((wide ? u8int() : u4()).toString(8)); break;
      case "p": out += "0x" + u4().toString(16); break;
      case "c": out += String.fromCharCode(n4() & 0xff); break;
      case "s": { const sp = u4(); let s = state.cstr(sp); if (prec) s = s.slice(0, parseInt(prec, 10)); out += pad(s); break; }
      case "f": case "F": case "g": case "G": case "e": out += pad(String(f8())); break;
      case "%": out += "%"; break;
      default: out += spec + (conv || ""); break;
    }
  }
  return out;
}

// sscanf/vsscanf over a guest va_list. Mirrors formatFromGuest's ABI: the
// variadic args are 4-byte POINTERS (to int/char buffer/...) the parsed
// values are written through. Returns the number of fields assigned (the C
// contract). Supports the conversions tmux/zsh actually use: whitespace,
// literals, %d %i %u %x %o %c %s %f %[...] %n, '*' suppression, field width
// and h/l/ll length modifiers.
function scanfFromGuest(state, strPtr, fmtPtr, vaPtr) {
  const str = state.cstr(strPtr), fmt = state.cstr(fmtPtr);
  const view = state.view, u8 = state.u8;
  let si = 0, va = vaPtr, assigned = 0, matched = 0;
  const nextPtr = () => { va = (va + 3) & ~3; const p = view.getUint32(va, true); va += 4; return p; };
  const isWs = (ch) => ch === " " || ch === "\t" || ch === "\n" || ch === "\r" || ch === "\f" || ch === "\v";
  const skipWs = () => { while (si < str.length && isWs(str[si])) si++; };
  for (let fi = 0; fi < fmt.length; fi++) {
    const fc = fmt[fi];
    if (isWs(fc)) { skipWs(); continue; }
    if (fc !== "%") { if (str[si] !== fc) return assigned; si++; continue; }
    fi++;
    let suppress = false; if (fmt[fi] === "*") { suppress = true; fi++; }
    let width = ""; while (/[0-9]/.test(fmt[fi] || "")) width += fmt[fi++];
    width = width ? parseInt(width, 10) : 0;
    let lenMod = ""; while ("hljztLq".includes(fmt[fi] || "")) lenMod += fmt[fi++];
    const conv = fmt[fi];
    const wide = /ll|j|q/.test(lenMod), half = lenMod === "h", store = (p, v) => { if (wide) { const big = BigInt(Math.trunc(v)); view.setUint32(p, Number(big & 0xffffffffn) >>> 0, true); view.setInt32(p + 4, Number(big >> 32n), true); } else if (half) view.setInt16(p, v, true); else view.setInt32(p, v, true); };
    if (conv === "%") { skipWs(); if (str[si] === "%") si++; else return assigned; continue; }
    if (conv === "c") { const n = width || 1; if (si + n > str.length) return assigned; if (!suppress) { const p = nextPtr(); for (let k = 0; k < n; k++) u8[p + k] = str.charCodeAt(si + k); assigned++; } si += n; matched++; continue; }
    if (conv === "s") { skipWs(); const start = si; while (si < str.length && !isWs(str[si]) && (!width || si - start < width)) si++; if (si === start) return assigned; if (!suppress) { const p = nextPtr(); for (let k = 0; k < si - start; k++) u8[p + k] = str.charCodeAt(start + k); u8[p + (si - start)] = 0; assigned++; } matched++; continue; }
    if (conv === "[") {
      fi++; let neg = false; if (fmt[fi] === "^") { neg = true; fi++; } let set = ""; if (fmt[fi] === "]") { set += "]"; fi++; } while (fi < fmt.length && fmt[fi] !== "]") set += fmt[fi++];
      const inSet = (ch) => set.includes(ch) !== neg; const start = si; while (si < str.length && inSet(str[si]) && (!width || si - start < width)) si++;
      if (si === start) return assigned; if (!suppress) { const p = nextPtr(); for (let k = 0; k < si - start; k++) u8[p + k] = str.charCodeAt(start + k); u8[p + (si - start)] = 0; assigned++; } matched++; continue;
    }
    if (conv === "n") { if (!suppress) { const p = nextPtr(); view.setInt32(p, si, true); } continue; }
    if ("diuxXop".includes(conv)) {
      skipWs(); let rest = str.slice(si); if (width) rest = rest.slice(0, width);
      const re = conv === "x" || conv === "X" || conv === "p" ? /^[+-]?(0[xX])?[0-9a-fA-F]+/ : conv === "o" ? /^[+-]?[0-7]+/ : /^[+-]?[0-9]+/;
      const m = re.exec(rest); if (!m) return assigned; const tok = m[0]; si += tok.length;
      const base = conv === "x" || conv === "X" || conv === "p" ? 16 : conv === "o" ? 8 : 10;
      const val = conv === "u" ? parseInt(tok, base) >>> 0 : parseInt(tok, base);
      if (!suppress) { store(nextPtr(), val); assigned++; } matched++; continue;
    }
    if ("feEgGaA".includes(conv)) {
      skipWs(); let rest = str.slice(si); if (width) rest = rest.slice(0, width);
      const m = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/.exec(rest); if (!m) return assigned; si += m[0].length; const val = parseFloat(m[0]);
      if (!suppress) { const p = nextPtr(); if (lenMod.includes("l") || lenMod.includes("L")) view.setFloat64(p, val, true); else view.setFloat32(p, val, true); assigned++; } matched++; continue;
    }
    return assigned; // unknown conversion
  }
  return assigned;
}

// Fill one FreeBSD-i386 kinfo_proc record (768 bytes) at `off`, with the
// exact offsets the real ps reads.
function fillKinfoProc(state, off, p) {
  const v = state.view, u = state.u8;
  u.fill(0, off, off + 768);
  v.setInt32(off + 0, 768, true);       // ki_structsize
  v.setInt32(off + 40, p.pid, true);    // ki_pid
  v.setInt32(off + 44, p.ppid, true);   // ki_ppid
  v.setInt32(off + 48, p.pgid || p.pid, true); // ki_pgid
  v.setInt32(off + 56, p.sid || p.pid, true);  // ki_sid
  v.setInt32(off + 516, 1, true);       // ki_numthreads
  v.setInt32(off + 520, p.pid, true);   // ki_tid
  v.setUint32(off + 136, 0, true);      // ki_uid
  v.setUint32(off + 140, 0, true);      // ki_ruid
  u[off + 308] = p.exited ? 5 : 2;      // ki_stat: SZOMB(5) / SRUN(2)
  const comm = enc.encode((p.comm || "?").slice(0, 19));
  u.set(comm, off + 367);               // ki_comm[20]
  u.set(comm.subarray(0, 16), off + 314); // ki_tdname[17]
}

// FreeBSD-i386 struct stat: mode@24(2B), uid@28, gid@32, mtime@64,
// size@96(8B), blksize@112.
function fillStat(state, off, node) {
  const v = state.view, u = state.u8;
  u.fill(0, off, off + 128);
  const perm = typeof node.mode === "number" ? node.mode & 0o777 : (node.type === "dir" ? 0o755 : node.type === "char" ? 0o666 : 0o644);
  const ifmt = node.type === "dir" ? S_IFDIR
    : node.type === "char" ? S_IFCHR
    : node.type === "sock" ? S_IFSOCK
    : node.type === "pipe" ? S_IFIFO
    : node.type === "symlink" ? S_IFLNK
    : S_IFREG;
  const mode = ifmt | perm;
  v.setUint32(off + 8, node.ino || 1, true);                 // st_ino
  v.setUint32(off + 16, node.type === "dir" ? 2 : 1, true);  // st_nlink
  v.setUint16(off + 24, mode, true);                          // st_mode
  v.setUint32(off + 28, 1000, true);                          // st_uid (the single non-root user — matches getuid)
  v.setUint32(off + 32, 1000, true);                          // st_gid
  v.setUint32(off + 52, node.mtime || 0, true);              // st_atim.sec
  v.setUint32(off + 64, node.mtime || 0, true);              // st_mtim.sec
  v.setUint32(off + 76, node.mtime || 0, true);              // st_ctim.sec
  // A yfs-backed file whose body has not been fetched yet reports the
  // size the directory listing declared — stat() must never fetch.
  const size = node.type === "file" ? (node.data ? node.data.length : (node.size || 0))
    : node.type === "symlink" ? enc.encode(node.target || "").length
    : (node.entries ? node.entries.length * 64 : 0);
  v.setUint32(off + 96, size, true);                          // st_size
  v.setUint32(off + 112, 4096, true);                         // st_blksize
}

// FreeBSD dirent: d_fileno@0(8B), d_reclen@16(2B), d_type@18, d_namlen@20, d_name@24.
function fillDirent(state, off, name, isDir, ino) {
  const v = state.view, u = state.u8;
  u.fill(0, off, off + 280);
  v.setUint32(off + 0, ino || 1, true);          // d_fileno
  v.setUint16(off + 16, 280, true);              // d_reclen
  u[off + 18] = isDir ? 4 : 8;                   // d_type DT_DIR/DT_REG
  const nb = new TextEncoder().encode(name);
  v.setUint16(off + 20, nb.length, true);        // d_namlen
  u.set(nb, off + 24);                           // d_name
}

// FreeBSD-i386 struct tm: nine int fields tm_sec..tm_isdst, then
// tm_gmtoff(long) + tm_zone(ptr). Fill the nine ints at `off`.
function fillTmAt(state, off, t) {
  const d = new Date(t * 1000);
  const fields = [d.getUTCSeconds(), d.getUTCMinutes(), d.getUTCHours(), d.getUTCDate(), d.getUTCMonth(), d.getUTCFullYear() - 1900, d.getUTCDay(), 0, 0];
  state.u8.fill(0, off, off + 48);
  for (let i = 0; i < 9; i++) state.view.setInt32(off + i * 4, fields[i], true);
  return off;
}
// Returns a per-process struct-tm buffer (allocated once → pointer stable
// across calls, which the iso_*_buf_stable tests require).
function fillTm(state, proc, t) {
  if (!proc.pio.tmBuf) proc.pio.tmBuf = state.alloc(48, 4);
  return fillTmAt(state, proc.pio.tmBuf, t);
}

// Build the libc env for one process. `proc` and `mgr` give fork/exec/
// wait access to the process table; `io` is { onOutput, onUnimpl }.
function buildLibc(state, env_vars, io, mgr, proc) {
  const u8 = () => state.u8, view = () => state.view;
  const cstr = (p) => state.cstr(p), putStr = (s) => state.putStr(s);
  const basename = (p) => { const i = p.lastIndexOf("/"); return i < 0 ? p : p.slice(i + 1); };
  // ---- per-process fd table (proc.pio) ----
  // Each fd maps to an "open file description" (ofd) that is SHARED on
  // dup/dup2 and across fork (refcounted) — so a pipe written by a forked
  // child is read by the parent through the same buffer. cwd lives here
  // too, so a child's chdir cannot move the parent. See newPio/clonePio.
  const setErrno = (e) => { if (state.errnoPtr) view().setUint32(state.errnoPtr, e, true); };
  // Deliver SIGCHLD (FreeBSD = 20) to this process for any child that has
  // exited but not yet been reaped and not yet been signalled — invoking the
  // shell's installed handler so its wait loop makes progress.
  const deliverChildSignals = () => {
    if (!proc.sigHandlers) return;
    const handler = proc.sigHandlers[20];
    if (!handler || handler <= 1) return; // SIG_DFL / SIG_IGN
    const table = proc.inst.exports.__indirect_function_table;
    if (!table) return;
    for (const child of mgr.procs) {
      if (child.ppid === proc.pid && child.exited && !child.reaped && !child.sigchldDelivered) {
        child.sigchldDelivered = true;
        try { table.get(handler)(20); } catch (e) { if (e && (e.isExit || e.runaway)) throw e; }
      }
    }
  };
  // Deliver any queued asynchronous signals (e.g. SIGWINCH from a terminal
  // resize) by running their installed handlers. Called at a SAFE point —
  // when the guest is executing a syscall (running wasm), never while it is
  // asyncify-suspended. Returns true if a handler ran (so a blocking syscall
  // can report EINTR, the way a real signal interrupts poll()).
  const deliverPendingSignals = () => {
    if (!proc.pendingSignals || !proc.pendingSignals.length || !proc.sigHandlers) { if (proc.pendingSignals) proc.pendingSignals = []; return false; }
    const table = proc.inst.exports.__indirect_function_table;
    const sigs = proc.pendingSignals; proc.pendingSignals = [];
    if (!table) return false;
    let ran = false;
    for (const sig of sigs) { const handler = proc.sigHandlers[sig]; if (handler && handler > 1) { ran = true; try { table.get(handler)(sig); } catch (e) { if (e && (e.isExit || e.runaway)) throw e; } } }
    return ran;
  };
  // Deliver one signal to a target process: run its handler NOW if the target
  // is the current process (raise / self-kill); otherwise queue it for delivery
  // when that process next runs. Returns the action taken: "handled" if a
  // user handler ran/was queued, "ignored" for SIG_IGN, "default" otherwise.
  const runSignalHandler = (target, sig) => {
    const handler = target.sigHandlers && target.sigHandlers[sig];
    if (handler === 1) return "ignored"; // SIG_IGN
    if (!handler || handler <= 1) return "default";
    if (target === proc) { const table = proc.inst.exports.__indirect_function_table; if (table) { try { table.get(handler)(sig); } catch (e) { if (e && (e.isExit || e.runaway)) throw e; } } }
    else (target.pendingSignals = target.pendingSignals || []).push(sig);
    return "handled";
  };
  const fdEntry = (fd) => proc.pio.fds.get(fd);
  const allocFd = (min = 0) => { let f = min < 0 ? 0 : min; while (proc.pio.fds.has(f) || proc.pio.dirs.has(f)) f++; return f; };
  const installFd = (fd, ofd, cloexec = false) => { proc.pio.fds.set(fd, { ofd, cloexec }); return fd; };
  const derefOfd = (ofd) => {
    if (--ofd.refs > 0) return;
    if (ofd.kind === "pipe") { if (ofd.end === "r") ofd.pipe.readClosed = true; else ofd.pipe.writeClosed = true; }
    else if (ofd.kind === "sock") { ofd.rx.readClosed = true; ofd.tx.writeClosed = true; }
  };
  const closeFd = (fd) => { const e = proc.pio.fds.get(fd); if (e) { proc.pio.fds.delete(fd); derefOfd(e.ofd); return 0; } if (proc.pio.dirs.delete(fd)) return 0; setErrno(9); return -1; };
  const bufReadable = (b) => b.total > 0 || b.writeClosed;
  const ofdReadable = (ofd) => {
    if (ofd.kind === "char") return (ofd.dev === "in" && proc.interactive) ? (proc.tty || mgr.tty).inbuf.length > 0 : true;
    if (ofd.kind === "file") return true;
    if (ofd.kind === "pipe") return ofd.end === "r" && bufReadable(ofd.pipe);
    if (ofd.kind === "sock") return bufReadable(ofd.rx);
    // A listening unix socket is "readable" (for poll/select/kevent) when a
    // connection is pending in its accept queue — that is how an event loop
    // learns to call accept(). Without this the FIRST connection still works
    // (startup uses a blocking accept() with its own predicate) but the server
    // never wakes for a LATER connection, so `tmux attach` to a running server
    // hangs: the client connects but the server never accepts it.
    if (ofd.kind === "usock") return ofd.listening && ofd.acceptQueue.length > 0;
    return false;
  };
  const ofdWritable = (ofd) => ofd.kind === "pipe" ? (ofd.end === "w" && !ofd.pipe.readClosed) : ofd.kind === "sock" ? !ofd.tx.readClosed : true;
  const bufPush = (b, bytes) => { b.chunks.push(bytes.slice()); b.total += bytes.length; };
  const bufDrain = (b, dst, n) => { let got = 0; while (got < n && b.chunks.length) { const head = b.chunks[0]; const take = Math.min(head.length, n - got); dst.set(head.subarray(0, take), got); got += take; if (take === head.length) b.chunks.shift(); else b.chunks[0] = head.subarray(take); b.total -= take; } return got; };
  // Pseudo-terminal line discipline for the MASTER→SLAVE direction (bytes the
  // master, e.g. tmux, writes toward the shell). A real pty's tty layer does
  // this; ours must too, otherwise a shell with no line editor of its own —
  // like the `-sh` tmux opens in a pane — gets no echo and no line buffering:
  // you type into the pane and NOTHING appears until Enter, because the shell
  // only ever sees a whole line and never echoes. With cooked termios
  // (ICANON|ECHO, the pty default) each byte is echoed straight back to the
  // master (so tmux paints the character as you type) and accumulated; the line
  // is delivered to the slave's read only on Enter. Raw mode (a full-screen app
  // cleared ICANON/ECHO via tcsetattr) passes bytes straight through and the app
  // does its own echo — same split the outer-tty feed() uses.
  const ptyMasterWrite = (pty, bytes) => {
    const lflag = pty.termios.lflag | 0;
    const ECHO = lflag & 0x8, ICANON = lflag & 0x100;
    const echo = (s) => bufPush(pty.toMaster, enc.encode(s));
    const toSlave = (byte) => bufPush(pty.toSlave, Uint8Array.of(byte));
    for (let i = 0; i < bytes.length; i++) {
      let byte = bytes[i];
      if (byte === 13) byte = 10; // ICRNL: CR from Enter becomes NL
      if (!ICANON) { toSlave(byte); if (ECHO) echo(byte === 10 ? "\r\n" : String.fromCharCode(byte)); continue; }
      if (byte === 127 || byte === 8) { if (pty.canon.length) { pty.canon.pop(); if (ECHO) echo("\b \b"); } continue; } // backspace
      if (byte === 21) { while (pty.canon.length) { pty.canon.pop(); if (ECHO) echo("\b \b"); } continue; }        // ^U kill line
      if (byte === 10) { if (ECHO) echo("\r\n"); pty.canon.push(10); for (const b of pty.canon) toSlave(b); pty.canon = []; continue; }
      if (byte === 3 || byte === 4) { toSlave(byte); continue; } // ^C/^D pass straight through
      pty.canon.push(byte);
      if (ECHO) echo(byte < 32 ? "^" + String.fromCharCode(byte + 64) : String.fromCharCode(byte));
    }
    return bytes.length;
  };
  // Terminal OUTPUT line discipline (termios c_oflag), the counterpart to the
  // input discipline (feed / ptyMasterWrite). A cooked tty applies OPOST on the
  // way out — most importantly ONLCR, which maps a bare LF to CR-LF so a program
  // that prints "\n" (every Unix tool: ls, ps, …) starts the next line at column
  // 0 instead of staircasing. The program emits bare LF and the tty supplies the
  // CR, exactly as on a real terminal. Raw mode (tmux/vim clear OPOST) passes
  // bytes through untouched so absolute cursor positioning is not corrupted.
  const OPOST = 0x1, ONLCR = 0x2;
  const ttyOpost = (oflag, text) => ((oflag & OPOST) && (oflag & ONLCR)) ? text.replace(/\r?\n/g, "\r\n") : text;
  // ofd write: returns bytes written, or -errno (EPIPE = 32). This is the SINGLE
  // chokepoint for ALL guest output — write() lands here directly, and stdio
  // (printf/puts via emit, fprintf/fwrite via fpWrite) routes here through the
  // backing fd — so the tty discipline runs in exactly one place, and a write to
  // a redirected stdout reaches its pipe/file instead of the terminal.
  const ofdWrite = (ofd, bytes) => {
    // A terminal is one bidirectional fd: tmux's server writes the rendered
    // screen to the very tty fd it reads keystrokes from (received from the
    // client via SCM_RIGHTS as a dev:"in" char). So display writes to any
    // terminal char — "err" → stderr, everything else → stdout. Only
    // /dev/null swallows them. A cooked interactive tty post-processes via OPOST.
    if (ofd.kind === "char") { if (ofd.dev === "null") return bytes.length; const text = dec.decode(bytes); const tty = proc.tty || mgr.tty; const emit = (tty && tty.onOutput) || io.onOutput; emit(ofd.dev === "err" ? 2 : 1, (proc.interactive && tty) ? ttyOpost(tty.oflag | 0, text) : text); return bytes.length; }
    if (ofd.kind === "file") { const node = ofd.node; const at = ofd.append ? node.data.length : ofd.off; const merged = new Uint8Array(Math.max(node.data.length, at + bytes.length)); merged.set(node.data); merged.set(bytes, at); node.data = merged; ofd.off = at + bytes.length; return bytes.length; }
    if (ofd.kind === "pipe") return (ofd.end === "w" && !ofd.pipe.readClosed) ? (bufPush(ofd.pipe, bytes), bytes.length) : -32;
    if (ofd.kind === "sock") {
      if (mgr.tap) mgr.tap(proc.pid, "sock-write", ofd, bytes);
      if (ofd.pty && ofd.ptyMaster) return ptyMasterWrite(ofd.pty, bytes);
      // A pty SLAVE write is the pane program's output flowing toward the master
      // (tmux). Apply the pty's OWN output discipline (OPOST/ONLCR) so a bare LF
      // becomes CR-LF — exactly as the outer tty does for the top-level shell —
      // otherwise tmux receives bare LF and the pane staircases. A plain unix
      // socket (no ofd.pty: e.g. the tmux client/server imsg channel) is left
      // untouched so its binary protocol is not corrupted. The guest still wrote
      // bytes.length; the CR is the tty's, transparent to the program.
      const out = ofd.pty ? enc.encode(ttyOpost(ofd.pty.termios.oflag | 0, dec.decode(bytes))) : bytes;
      return ofd.tx.readClosed ? -32 : (bufPush(ofd.tx, out), bytes.length);
    }
    return bytes.length;
  };
  const ofdRead = (ofd, dst, n) => {
    if (ofd.kind === "char") return 0; // stdin / /dev/null → EOF
    if (ofd.kind === "file") { const data = ofd.node.data, end = Math.min(ofd.off + n, data.length); dst.set(data.subarray(ofd.off, end), 0); const got = end - ofd.off; ofd.off = end; return got; }
    if (ofd.kind === "pipe") return ofd.end === "r" ? bufDrain(ofd.pipe, dst, n) : 0;
    if (ofd.kind === "sock") { const got = bufDrain(ofd.rx, dst, n); if (mgr.tap && got > 0) mgr.tap(proc.pid, "sock-read", ofd, dst.slice(0, got)); return got; }
    return 0;
  };
  const newPipeBuf = () => ({ chunks: [], total: 0, readClosed: false, writeClosed: false, ancFds: [] });
  // A pseudo-terminal: two buffers + its own termios (NOT the global outer tty —
  // a pane shell must see a cooked pty even while tmux holds the outer tty raw)
  // and a canonical line buffer. toSlave carries master→slave input (through the
  // line discipline); toMaster carries slave→master output + echoes.
  const newPty = () => ({ toSlave: newPipeBuf(), toMaster: newPipeBuf(), termios: { lflag: 0x8 | 0x2 | 0x100 | 0x80 | 0x400, oflag: OPOST | ONLCR }, canon: [] });
  // poll/select readiness for one fd given the requested events.
  const pollOne = (fd, events) => { const e = fdEntry(fd); if (!e) return 0x20 /*POLLNVAL*/; let re = 0; if ((events & 0x1) && ofdReadable(e.ofd)) re |= 0x1; if ((events & 0x4) && ofdWritable(e.ofd)) re |= 0x4; return re; };
  const writeBytes = (fd, ptr, len) => { const e = fdEntry(fd); if (!e) { setErrno(9); return -1; } const r = ofdWrite(e.ofd, state.u8.subarray(ptr, ptr + len)); if (r < 0) { setErrno(-r); return -1; } return r; };
  // Create a directory at an already-resolved vfs path (shared by mkdir/mkdirat).
  const mkdirAt = (path, mode) => {
    if (mgr.vfs[path]) { setErrno(17); return -1; } // EEXIST
    const slash = path.lastIndexOf("/");
    const parent = mgr.vfs[slash === 0 ? "/" : path.slice(0, slash)];
    if (!parent || parent.type !== "dir") { setErrno(2); return -1; } // ENOENT
    mgr.vfs[path] = { type: "dir", entries: [], mode: (mode | 0) & 0o777, mtime: Math.floor(Date.now() / 1000), ino: mgr.nextIno++ };
    const name = path.slice(slash + 1);
    if (!parent.entries.includes(name)) parent.entries.push(name);
    return 0;
  };

  // ---- anonymous mmap allocator over the UPPER half of linear memory ----
  // The guest's brk heap bumps up from __heap_base in the LOW half; this arena
  // lives above it (anchored at ~memory_size/2), so a MAP_FIXED below the arena
  // base is, by construction, "over the heap" and refused. Per-process (copied
  // on fork via clonePio) so a child's mappings are isolated from the parent.
  // live[]/free[] are page-granular regions; free is kept sorted+coalesced.
  const MMAP_PAGE = 0x1000;
  const mmArena = () => {
    let mm = proc.pio.mm;
    if (!mm) {
      const base = (Math.max(state.u8.length >>> 1, (state.brk | 0) + 0x10000) + MMAP_PAGE - 1) & ~(MMAP_PAGE - 1);
      mm = proc.pio.mm = { base: base >>> 0, top: base >>> 0, live: [], free: [] };
    }
    return mm;
  };
  const mmGrow = (need) => { while ((need >>> 0) > state.u8.length) { const before = state.u8.length; state.mem.grow(16); state.refresh(); if (state.u8.length === before) return false; } return true; };
  const mmFreeAdd = (mm, addr, len) => {
    mm.free.push({ addr, len }); mm.free.sort((a, b) => a.addr - b.addr);
    for (let i = 0; i < mm.free.length - 1;) { const a = mm.free[i], b = mm.free[i + 1]; if (a.addr + a.len === b.addr) { a.len += b.len; mm.free.splice(i + 1, 1); } else i++; }
  };
  const mmFreeTake = (mm, len) => {
    for (let i = 0; i < mm.free.length; i++) { const r = mm.free[i]; if (r.len >= len) { const addr = r.addr; if (r.len === len) mm.free.splice(i, 1); else { r.addr += len; r.len -= len; } return addr; } }
    return -1;
  };
  // Read a guest C string but REFUSE one that runs to the end of linear memory
  // with no NUL terminator (a hostile unterminated path) — returns null so the
  // caller can report EFAULT instead of walking off the end of memory.
  const cstrBounded = (ptr) => { if (!ptr) return null; let e = ptr; const len = state.u8.length; while (e < len && state.u8[e]) e++; return e >= len ? null : dec.decode(state.u8.subarray(ptr, e)); };
  const mmFreeCarve = (mm, addr, len) => { // remove [addr,addr+len) from the free list (MAP_FIXED)
    const end = addr + len, out = [];
    for (const r of mm.free) { const rend = r.addr + r.len; if (rend <= addr || r.addr >= end) { out.push(r); continue; } if (r.addr < addr) out.push({ addr: r.addr, len: addr - r.addr }); if (rend > end) out.push({ addr: end, len: rend - end }); }
    mm.free = out;
  };

  // ---- interactive terminal: asyncify-suspended blocking read/poll ----
  // For an interactive process, stdin is a real tty: read()/poll() on it
  // BLOCK when no input is buffered. We suspend the whole guest with the
  // SAME asyncify unwind/rewind the universal binary already carries for
  // fork, and the page resumes it on each keystroke (Manager.pump). No
  // special build — the binary is fully asyncify-instrumented.
  const isTtyIn = (fd) => { const e = fdEntry(fd); return !!(proc.interactive && e && e.ofd.kind === "char" && e.ofd.dev === "in"); };
  const ttyServe = (buf, n) => { const tty = proc.tty || mgr.tty; let got = 0; while (got < n && tty.inbuf.length) u8()[buf + got++] = tty.inbuf.shift(); return got; };
  // Suspend the process via asyncify. `ready` is a predicate the scheduler
  // polls to know when to resume this process (data arrived, child exited,
  // key typed). `kind` is for diagnostics. Mirrors the fork unwind path.
  const beginBlock = (kind, ready, deadline) => {
    // Blocking needs the asyncify unwind/rewind exports. A guest built
    // without `wasm-opt --asyncify` cannot suspend — fail with a diagnosis
    // instead of the bare TypeError ("asyncify_start_unwind is not a
    // function") that top produced before its build was instrumented.
    if (typeof proc.inst.exports.asyncify_start_unwind !== "function") {
      throw new Error(`'${proc.comm}' blocked in ${kind}() but its wasm is not asyncify-instrumented — rebuild it with wasm-opt --asyncify (see nixpkgs/lib/build-yos-package.nix)`);
    }
    if (proc.asyncifyPtr === 0) proc.asyncifyPtr = state.alloc(ASYNCIFY_BUF_SIZE, 8);
    const b = proc.asyncifyPtr;
    view().setUint32(b, b + 8, true);
    view().setUint32(b + 4, b + ASYNCIFY_BUF_SIZE, true);
    proc.blocked = { kind, ready: ready || (() => true), deadline: deadline ?? Infinity };
    proc.inst.exports.asyncify_start_unwind(b);
  };
  const resuming = () => mgr.asyncifyState(proc) === ASYNCIFY_REWINDING;
  // Read sockaddr_un.sun_path (FreeBSD: sun_len@0, sun_family@1, path@2 NUL-term).
  const readSunPath = (addrPtr) => { let p = addrPtr + 2, s = ""; while (u8()[p]) { s += String.fromCharCode(u8()[p]); p++; } return s; };
  // FreeBSD sockaddr_in: sin_len@0=16, sin_family@1=AF_INET(2), sin_port@2
  // (network order), sin_addr@4 (network order, 4 bytes), sin_zero@8.
  const writeSockaddrIn = (ptr, port, addr) => { const m = u8(); m.fill(0, ptr, ptr + 16); m[ptr] = 16; m[ptr + 1] = 2; m[ptr + 2] = (port >> 8) & 0xff; m[ptr + 3] = port & 0xff; const a = addr || [127, 0, 0, 1]; m[ptr + 4] = a[0]; m[ptr + 5] = a[1]; m[ptr + 6] = a[2]; m[ptr + 7] = a[3]; };
  const readSinPort = (addrPtr) => (u8()[addrPtr + 2] << 8) | u8()[addrPtr + 3];
  const nextEphemeralPort = () => (mgr.nextPort = (mgr.nextPort || 49152) + 1);
  // waitpid/wait3/wait4 with WNOHANG + scheduler blocking. Blocks the caller
  // until a child becomes reapable (a real wait, not the old "child already
  // ran cooperatively" shortcut), so background jobs and job control work.
  const doWait = (pid, statusPtr, opts) => {
    const reapable = () => mgr.procs.some((p) => (pid > 0 ? (p.pid === pid && p.ppid === proc.pid && p.exited) : (p.ppid === proc.pid && p.exited && !p.reaped)));
    const hasKids = () => mgr.procs.some((p) => p.ppid === proc.pid && !p.reaped && !p.exited);
    if (proc.interactive && resuming()) proc.inst.exports.asyncify_stop_rewind();
    if (reapable()) return mgr.waitpid(proc, pid, statusPtr);
    if ((opts | 0) & 1) return 0; // WNOHANG
    if (proc.interactive && hasKids()) { beginBlock("wait", () => reapable() || !hasKids()); return 0; }
    return mgr.waitpid(proc, pid, statusPtr); // -1 ECHILD
  };

  // ---- FILE* layer over the fd table ----
  // A FILE* is a REAL guest `struct __sFILE` allocated in linear memory.
  // This matters because FreeBSD <stdio.h> inlines getc()/putc()/feof()/
  // ferror() as macros that dereference the FILE pointer directly:
  //   getc(fp)  => (--(fp)->_r < 0 ? __srget(fp) : *(fp)->_p++)
  //   feof(fp)  => ((fp)->_flags & __SEOF)
  // so an opaque handle (the old scheme) traps the moment a guest built
  // against the FreeBSD headers (tmux) does getc() on a fopen'd file. The
  // struct offsets (i386 / wasm32, 4-byte pointers):
  //   _p@0 _r@4 _w@8 _flags@12 _file@14 _bf._base@16 _bf._size@20
  // Std streams (stdin/out/err) reach us either as the guest libc's own
  // compiled-in structs (tmux) or as small integer sentinels 1/2/3 (the yos
  // sysroot zsh is built against); fileFd() resolves both.
  const FILE_STRUCT = 256, FILE_BUF = 1024;
  const SF_P = 0, SF_R = 4, SF_W = 8, SF_FLAGS = 12, SF_FILE = 14, SF_BFBASE = 16, SF_BFSIZE = 20;
  const SF_SRD = 0x4, SF_SWR = 0x8, SF_SEOF = 0x20, SF_SERR = 0x40;
  const fpFile = (fp) => proc.pio.files.get(fp);
  const setFlag = (fp, bit) => { if (fp > 3) view().setInt16(fp + SF_FLAGS, view().getInt16(fp + SF_FLAGS, true) | bit, true); };
  const clrFlag = (fp, bit) => { if (fp > 3) view().setInt16(fp + SF_FLAGS, view().getInt16(fp + SF_FLAGS, true) & ~bit, true); };
  // Mirror a std stream's read-EOF into its sentinel FILE _flags in guest
  // memory. stdin/stdout/stderr are the small-int handles 1/2/3, not real FILE
  // structs — but FreeBSD <stdio.h> expands feof(p)/ferror(p) to a MACRO that
  // reads (p)->_flags directly whenever the guest is single-threaded
  // (__isthreaded == 0, the common case). For a sentinel that dereferences the
  // reserved low memory below __global_base (stdin: address 1+SF_FLAGS = 13),
  // which is dead scratch. Keeping the __SEOF bit there in sync with the fd's
  // real EOF makes the inlined macro read the right answer — without it a
  // getline/getdelim loop that returns -1 at EOF looks like a read error
  // (e.g. sort err(2)s at file.c:684). No effect on real FILE structs.
  const markStdStreamEof = (fp, atEof) => { if (fp >= 1 && fp <= 3) { const cur = view().getInt16(fp + SF_FLAGS, true); view().setInt16(fp + SF_FLAGS, atEof ? (cur | SF_SEOF) : (cur & ~SF_SEOF), true); } };
  const newFile = (fd, flags) => {
    const sp = state.alloc(FILE_STRUCT, 8); u8().fill(0, sp, sp + FILE_STRUCT);
    const buf = state.alloc(FILE_BUF, 1);
    view().setUint32(sp + SF_P, buf, true);          // _p (empty)
    view().setInt32(sp + SF_R, 0, true);             // _r = 0 → first getc triggers __srget
    view().setInt16(sp + SF_FLAGS, flags, true);     // _flags
    view().setInt16(sp + SF_FILE, fd, true);         // _file
    view().setUint32(sp + SF_BFBASE, buf, true);     // _bf._base
    view().setInt32(sp + SF_BFSIZE, FILE_BUF, true); // _bf._size
    proc.pio.files.set(sp, { fd, mem: null });
    return sp;
  };
  // Resolve the backing fd: tracked fopen'd FILE, zsh sysroot sentinel
  // (1/2/3 → fd 0/1/2), or a compiled-in std struct (read its _file field).
  const fileFd = (fp) => { const f = fpFile(fp); if (f) return f.fd; if (fp >= 1 && fp <= 3) return fp - 1; const fd = view().getInt16(fp + SF_FILE, true); return fd >= 0 && fd <= 2 ? fd : 1; };
  const syncMem = (f) => { const bytes = f.mem.data; const buf = state.alloc(bytes.length + 1, 1); for (let i = 0; i < bytes.length; i++) u8()[buf + i] = bytes[i]; u8()[buf + bytes.length] = 0; view().setUint32(f.mem.ptrptr, buf, true); view().setUint32(f.mem.sizeptr, bytes.length, true); };
  const fpWrite = (fp, str) => { const bytes = typeof str === "string" ? enc.encode(str) : str; const f = fpFile(fp); if (f && f.mem) { for (let i = 0; i < bytes.length; i++) f.mem.data.push(bytes[i]); return bytes.length; } const fd = fileFd(fp); const e = fdEntry(fd); if (e) { const r = ofdWrite(e.ofd, bytes); if (r < 0) { setFlag(fp, SF_SERR); setErrno(-r); } } else io.onOutput(fd === 2 ? 2 : 1, dec.decode(bytes)); return bytes.length; };
  // Refill a fopen'd FILE's buffer from its fd, then consume one byte —
  // the FreeBSD __srget() contract (caller's macro already did --_r < 0).
  const fileRefill = (fp, f) => { const base = view().getUint32(fp + SF_BFBASE, true), size = view().getInt32(fp + SF_BFSIZE, true) || FILE_BUF; const e = fdEntry(f.fd); let n = 0; if (e) { const tmp = new Uint8Array(size); n = ofdRead(e.ofd, tmp, size); u8().set(tmp.subarray(0, n), base); } view().setUint32(fp + SF_P, base, true); view().setInt32(fp + SF_R, n, true); if (n === 0) { setFlag(fp, SF_SEOF); return -1; } return n; };
  const doSrget = (fp) => { const f = fpFile(fp); if (!f) { const e = fdEntry(fileFd(fp)); if (!e) return -1; if (e.ofd.unget && e.ofd.unget.length) { markStdStreamEof(fp, false); return e.ofd.unget.pop(); } const tmp = new Uint8Array(1); if (ofdRead(e.ofd, tmp, 1) === 0) { setFlag(fp, SF_SEOF); e.ofd.eof = true; markStdStreamEof(fp, true); return -1; } markStdStreamEof(fp, false); return tmp[0]; } if (fileRefill(fp, f) < 0) return -1; let p = view().getUint32(fp + SF_P, true), r = view().getInt32(fp + SF_R, true); const byte = u8()[p]; view().setUint32(fp + SF_P, p + 1, true); view().setInt32(fp + SF_R, r - 1, true); return byte; };
  // Function-side getc that shares the same _p/_r buffer the inlined macro
  // uses, so getc()/getline()/fread() on one FILE stay consistent.
  const fileGetc = (fp) => { const f = fpFile(fp); if (!f) { const e = fdEntry(fileFd(fp)); if (!e) return -1; if (e.ofd.unget && e.ofd.unget.length) { markStdStreamEof(fp, false); return e.ofd.unget.pop(); } const tmp = new Uint8Array(1); if (ofdRead(e.ofd, tmp, 1) === 0) { e.ofd.eof = true; markStdStreamEof(fp, true); return -1; } markStdStreamEof(fp, false); return tmp[0]; } let r = view().getInt32(fp + SF_R, true) - 1; if (r >= 0) { let p = view().getUint32(fp + SF_P, true); const byte = u8()[p]; view().setUint32(fp + SF_P, p + 1, true); view().setInt32(fp + SF_R, r, true); return byte; } view().setInt32(fp + SF_R, r, true); return doSrget(fp); };
  // stdio (printf/puts/putchar/…) writes to the real fd through ofdWrite, so it
  // shares the one tty output discipline and honours redirection — not a direct
  // io.onOutput bypass. Falls back to the raw sink only if the fd has no entry.
  const emit = (fd, text) => { const e = fdEntry(fd); if (e) ofdWrite(e.ofd, enc.encode(text)); else io.onOutput(fd, text); };
  const fmt = (f, v) => formatFromGuest(state, f, v);
  const exitWith = (code) => { const e = new Error("exit"); e.isExit = true; e.code = code | 0; throw e; };

  // Translate a POSIX BRE (default) / ERE (REG_EXTENDED) pattern to a JS RegExp
  // source. Handles the metacharacter-escaping difference — in a BRE, ( ) { } +
  // ? | are LITERAL unless backslashed, and \( \) \{ \} \+ \? \| are the
  // special forms; in an ERE (like JS) they are special bare — and expands
  // POSIX bracket classes [[:alpha:]] etc. Pure string work; no guest state.
  const posixRegexToJs = (pat, extended) => {
    const cls = { alpha: "A-Za-z", digit: "0-9", alnum: "A-Za-z0-9", space: "\\s", upper: "A-Z", lower: "a-z", blank: " \\t", punct: "!-/:-@\\[-`{-~", xdigit: "0-9A-Fa-f", cntrl: "\\x00-\\x1f\\x7f", print: "\\x20-\\x7e", graph: "\\x21-\\x7e" };
    pat = pat.replace(/\[:(\w+):\]/g, (whole, name) => cls[name] || whole);
    if (extended) return pat; // ERE ~= JS for the common subset
    let out = "", i = 0;
    while (i < pat.length) {
      const ch = pat[i];
      if (ch === "\\") {
        const next = pat[i + 1];
        if (next && "(){}+?|".includes(next)) { out += next; i += 2; continue; } // \( -> ( (special in BRE)
        out += ch + (next || ""); i += 2; continue;                              // keep \. \1 \< etc.
      }
      if ("(){}+?|".includes(ch)) { out += "\\" + ch; i++; continue; }           // bare -> literal in BRE
      out += ch; i++;
    }
    return out;
  };

  // Only the functions the prototype actually implements. Missing imports
  // are hardened by strictImportEnv() at instantiation time (fail loudly),
  // not pre-filled with silent return-0 stubs (issue #21).
  const env = {};

  Object.assign(env, {
    __main_argc_argv: (argc, argv2) => state.main(argc, argv2),
    __yos_argc: () => proc.argv.length,
    __yos_argv_setup: (p) => { for (let i = 0; i < proc.argv.length; i++) view().setUint32(p + i * 4, putStr(proc.argv[i]), true); },
    __yos_envc: () => proc.pio.env.length,
    __yos_envp_setup: (p) => { for (let i = 0; i < proc.pio.env.length; i++) view().setUint32(p + i * 4, putStr(proc.pio.env[i]), true); },
    exit: exitWith, _exit: exitWith,
    abort: () => exitWith(134),

    malloc: (n) => state.alloc(n),
    calloc: (a, b) => { const p = state.alloc(a * b); u8().fill(0, p, p + a * b); return p; },
    realloc: (p, n) => { const q = state.alloc(n); if (p) u8().copyWithin(q, p, p + n); return q; },
    free: () => {},
    memset: (d, c, n) => { u8().fill(c & 0xff, d, d + n); return d; },
    bzero: (d, n) => { u8().fill(0, d, d + n); return 0; },
    // explicit_bzero(3): like bzero but not optimised away — zeroing semantics
    // are all the guest can observe here, so it's the same fill.
    explicit_bzero: (d, n) => { u8().fill(0, d, d + n); return 0; },
    // mmap/munmap: anonymous mappings only (the sandbox has one flat memory).
    // Returns a wasm offset on success or -errno (a negative "pointer", which
    // is how the FreeBSD-shaped guest detects failure). `off` is i64 → BigInt.
    mmap: (addr, len, prot, flags, fd, off) => {
      len = ((len >>> 0) + 0xfff) & ~0xfff;
      if (len === 0) { setErrno(22); return -22; }
      const mm = mmArena();
      if (flags & 0x10) { // MAP_FIXED
        addr >>>= 0;
        if ((addr & 0xfff) || addr < mm.base) { setErrno(22); return -22; } // EINVAL — unaligned or over heap/low memory
        if (!mmGrow(addr + len)) { setErrno(12); return -12; }
        mmFreeCarve(mm, addr, len);
        mm.live = mm.live.filter((r) => r.addr + r.len <= addr || r.addr >= addr + len); // drop overlapping live (replacement)
        mm.live.push({ addr, len });
        if (addr + len > mm.top) mm.top = addr + len;
        u8().fill(0, addr, addr + len);
        return addr;
      }
      let region = mmFreeTake(mm, len);
      if (region < 0) {
        region = mm.top;
        // Skip reserved companion-library windows (see state.reserve) — the
        // mmap arena bumps upward just like brk and must not cross them.
        if (state.reserved) for (const r of state.reserved) if (region < r.hi && region + len > r.lo) region = (r.hi + MMAP_PAGE - 1) & ~(MMAP_PAGE - 1);
        if (!mmGrow(region + len)) { setErrno(12); return -12; }
        mm.top = region + len;
      }
      mm.live.push({ addr: region, len });
      u8().fill(0, region, region + len);
      return region;
    },
    munmap: (addr, len) => {
      addr >>>= 0; len = ((len >>> 0) + 0xfff) & ~0xfff;
      const mm = mmArena();
      const idx = mm.live.findIndex((r) => r.addr === addr);
      if (idx < 0) return 0; // not a live mapping — POSIX no-op (don't clobber static/brk bytes)
      const r = mm.live[idx]; mm.live.splice(idx, 1);
      u8().fill(0, r.addr, r.addr + r.len); // anonymous munmap zeroes
      mmFreeAdd(mm, r.addr, r.len);
      return 0;
    },
    mprotect: () => 0, madvise: () => 0, posix_madvise: () => 0, mlock: () => 0, munlock: () => 0, mlockall: () => 0, munlockall: () => 0, msync: () => 0, mincore: () => 0,
    // timingsafe_bcmp/timingsafe_memcmp: compare n bytes. Real ones run in
    // constant time; the guest only observes equal(0)/not-equal here.
    timingsafe_bcmp: (a, b, n) => { let diff = 0; const m = u8(); for (let i = 0; i < n; i++) diff |= m[a + i] ^ m[b + i]; return diff ? 1 : 0; },
    timingsafe_memcmp: (a, b, n) => { const m = u8(); for (let i = 0; i < n; i++) { const x = m[a + i], y = m[b + i]; if (x !== y) return x < y ? -1 : 1; } return 0; },
    memcpy: (d, s, n) => { u8().copyWithin(d, s, s + n); return d; },
    memmove: (d, s, n) => { u8().copyWithin(d, s, s + n); return d; },

    __error: () => state.errnoPtr,
    // Per-process environment (proc.pio.env), copied on fork so a child's
    // setenv/unsetenv cannot leak into the parent.
    getenv: (n) => { const k = cstr(n); const h = proc.pio.env.find((e) => e.startsWith(k + "=")); return h ? putStr(h.slice(k.length + 1)) : 0; },
    setenv: (namePtr, valPtr, overwrite) => { const k = cstr(namePtr), val = cstr(valPtr); const e = proc.pio.env; const i = e.findIndex((x) => x.startsWith(k + "=")); if (i >= 0) { if (overwrite) e[i] = k + "=" + val; } else e.push(k + "=" + val); return 0; },
    unsetenv: (namePtr) => { const k = cstr(namePtr); const e = proc.pio.env; const i = e.findIndex((x) => x.startsWith(k + "=")); if (i >= 0) e.splice(i, 1); return 0; },
    putenv: (strPtr) => { const s = cstr(strPtr); const k = s.split("=")[0]; const e = proc.pio.env; const i = e.findIndex((x) => x.startsWith(k + "=")); if (i >= 0) e[i] = s; else e.push(s); return 0; },
    // setlocale(cat, name): with a name, switch to it and echo it back; with
    // NULL, query the current locale. Stored per-process so a child's change
    // does not leak into the parent. ("" means "from environment" -> "C".)
    setlocale: (cat, namePtr) => { if (namePtr) { const name = cstr(namePtr); proc.pio.locale = name || "C"; } return putStr(proc.pio.locale || "C"); },
    nl_langinfo: () => putStr(""), ___mb_cur_max: () => 1,
    // localeconv(): a pointer to a "C"-locale struct lconv. Layout (FreeBSD
    // i386/wasm32): 10 char* pointers, then 14 signed-char fields. In the C
    // locale decimal_point is ".", every other string is "", and every numeric
    // field is CHAR_MAX (127 = "unspecified"). Cached per process so repeated
    // calls return the same static object, as the contract requires.
    localeconv: () => {
      if (proc.pio.lconv) return proc.pio.lconv;
      const dot = putStr("."), empty = putStr("");
      const lconv = state.alloc(10 * 4 + 14, 4);
      view().setUint32(lconv, dot, true);                       // decimal_point
      for (let i = 1; i < 10; i++) view().setUint32(lconv + i * 4, empty, true);
      for (let i = 0; i < 14; i++) u8()[lconv + 40 + i] = 127;  // CHAR_MAX
      proc.pio.lconv = lconv;
      return lconv;
    },
    // FreeBSD sysconf(_SC_*). Returning sane limits matters: getdtablesize()
    // (compiled into tmux) is sysconf(_SC_OPEN_MAX); if that is < 0 tmux's
    // imsg guard `getdtablecount()+overhead+1 >= getdtablesize()` is always
    // true, so it never recvmsg()s the client and spins on EAGAIN. _SC names
    // are the FreeBSD i386 values (unistd.h): OPEN_MAX=5, PAGESIZE=47, …
    sysconf: (name) => { switch (name) {
      case 1: return 262144;   // _SC_ARG_MAX
      case 2: return 256;      // _SC_CHILD_MAX
      case 3: return 128;      // _SC_CLK_TCK (FreeBSD)
      case 4: return 1023;     // _SC_NGROUPS_MAX
      case 5: return 1024;     // _SC_OPEN_MAX  (critical: bounds getdtablesize)
      case 47: return 65536;   // _SC_PAGESIZE / _SC_PAGE_SIZE
      case 56: return 1024;    // _SC_IOV_MAX
      case 57: case 58: return 1; // _SC_NPROCESSORS_CONF / _ONLN
      case 70: case 71: return 1024; // _SC_GETGR_R_SIZE_MAX / _SC_GETPW_R_SIZE_MAX
      case 72: return 255;     // _SC_HOST_NAME_MAX
      case 101: return 260;    // _SC_TTY_NAME_MAX
      case 120: return 32;     // _SC_SYMLOOP_MAX
      case 121: return 65536;  // _SC_PHYS_PAGES
      default: return -1;      // POSIX "no determinable limit"
    } },
    getpagesize: () => 65536,
    // Model an ordinary (non-root) user — native yos passes these through to
    // the host uid, which is non-zero; code that refuses to run as root (ssh)
    // checks getuid()!=0.
    getuid: () => 1000, geteuid: () => 1000, getgid: () => 1000, getegid: () => 1000,
    getpid: () => proc.pid, getppid: () => proc.ppid, getpgrp: () => proc.pgid, getpgid: (pid) => { const p = pid ? mgr.procs.find((q) => q.pid === pid) : proc; return p ? p.pgid : -1; }, getsid: () => proc.sid, getgroups: () => 0,
    // Interactive process: fds 0/1/2 are a terminal. This is what makes zsh
    // enter interactive mode (prompt + line editor) instead of batch mode.
    // A tty is any terminal char device (stdin/out/err — possibly received on
    // a higher fd via SCM_RIGHTS, as tmux's server gets the client's tty) or a
    // pty. /dev/null is not a tty. The interactive gate is what makes zsh pick
    // its line editor; tmux's forked server inherits interactive, so its
    // passed-through client fd reads as a terminal too.
    isatty: (fd) => { const e = fdEntry(fd); if (!e) return 0; const ofd = e.ofd; if (ofd.kind === "char" && ofd.dev !== "null") return proc.interactive ? 1 : 0; if (ofd.kind === "sock" && ofd.isPty) return 1; return 0; },

    write: (fd, p, l) => { if (p === 0 && l > 0) { setErrno(14); return -1; } return writeBytes(fd, p, l); }, // EFAULT on NULL+len
    writev: (fd, iov, c) => {
      if ((c | 0) < 0 || (c | 0) > 1024) { setErrno(22); return -1; } // EINVAL — bad iovcnt / > IOV_MAX
      const memLen = u8().length;
      for (let i = 0; i < c; i++) { const b = view().getUint32(iov + i * 8, true); const l = view().getUint32(iov + i * 8 + 4, true); if (l && (b + l > memLen || b + l < b)) { setErrno(14); return -1; } } // EFAULT — iovec out of bounds
      let t = 0;
      for (let i = 0; i < c; i++) { const b = view().getUint32(iov + i * 8, true); const l = view().getUint32(iov + i * 8 + 4, true); t += writeBytes(fd, b, l); }
      return t;
    },
    printf: (f, v) => { const s = fmt(f, v); emit(1, s); return s.length; },
    vprintf: (f, v) => { const s = fmt(f, v); emit(1, s); return s.length; },
    fprintf: (fp, f, v) => { const s = fmt(f, v); fpWrite(fp, s); return s.length; },
    vfprintf: (fp, f, v) => { const s = fmt(f, v); fpWrite(fp, s); return s.length; },
    snprintf: (d, n, f, v) => { const s = fmt(f, v); const b = enc.encode(s).subarray(0, Math.max(0, n - 1)); u8().set(b, d); u8()[d + b.length] = 0; return s.length; },
    vsnprintf: (d, n, f, v) => env.snprintf(d, n, f, v),
    sprintf: (d, f, v) => { const s = fmt(f, v); const b = enc.encode(s); u8().set(b, d); u8()[d + b.length] = 0; return s.length; },
    vsprintf: (d, f, v) => env.sprintf(d, f, v),
    fputc: (c, fp) => { fpWrite(fp, String.fromCharCode(c & 0xff)); return c & 0xff; },
    putc: (c, fp) => env.fputc(c, fp), putchar: (c) => { emit(1, String.fromCharCode(c & 0xff)); return c & 0xff; },
    fputs: (s, fp) => { fpWrite(fp, cstr(s)); return 1; },
    puts: (s) => { emit(1, cstr(s) + "\n"); return 1; },
    fwrite: (p, sz, nm, fp) => { const total = sz * nm; if (!total) return nm; fpWrite(fp, u8().subarray(p, p + total)); return nm; },
    open_memstream: (ptrptr, sizeptr) => { const sp = newFile(-1, SF_SWR); const f = fpFile(sp); f.mem = { data: [], ptrptr, sizeptr }; view().setUint32(ptrptr, 0, true); view().setUint32(sizeptr, 0, true); return sp; },
    // FILE* streams over the fd table (fopen/fdopen/fread/fgets/fseek/...).
    fopen: (pathPtr, modePtr) => {
      const mode = cstr(modePtr);
      let flags = 0, sf = 0;
      if (mode[0] === "r") { flags = mode.includes("+") ? 0x2 : 0x0; sf = mode.includes("+") ? SF_SRD | SF_SWR : SF_SRD; }
      else if (mode[0] === "w") { flags = (mode.includes("+") ? 0x2 : 0x1) | 0x200 | 0x400; sf = mode.includes("+") ? SF_SRD | SF_SWR : SF_SWR; }
      else if (mode[0] === "a") { flags = (mode.includes("+") ? 0x2 : 0x1) | 0x200 | 0x8; sf = mode.includes("+") ? SF_SRD | SF_SWR : SF_SWR; }
      const fd = env.open(pathPtr, flags);
      return fd < 0 ? 0 : newFile(fd, sf);
    },
    // fdopen: give the FILE its OWN descriptor (dup) so fclose() frees a
    // distinct fd slot. Otherwise fclose would free the caller's fd number and
    // a later open() could recycle it, so a defensive close() on the (now
    // stale) original fd would clobber the unrelated new file (issue #15).
    fdopen: (fd, modePtr) => { const mode = cstr(modePtr); const sf = mode[0] === "r" ? (mode.includes("+") ? SF_SRD | SF_SWR : SF_SRD) : (mode.includes("+") ? SF_SRD | SF_SWR : SF_SWR); const dupfd = env.dup(fd); if (dupfd < 0) return 0; return newFile(dupfd, sf); },
    fclose: (fp) => { const f = fpFile(fp); if (f) { if (f.mem) syncMem(f); else closeFd(f.fd); proc.pio.files.delete(fp); } return 0; },
    fread: (ptr, sz, nm, fp) => { const total = sz * nm; if (!total) return 0; const f = fpFile(fp); const out = new Uint8Array(total); let got = 0; if (f && !f.mem) { let p = view().getUint32(fp + SF_P, true), r = view().getInt32(fp + SF_R, true); while (r > 0 && got < total) { out[got++] = u8()[p++]; r--; } view().setUint32(fp + SF_P, p, true); view().setInt32(fp + SF_R, r, true); } if (got < total) { const e = fdEntry(fileFd(fp)); if (e) { const tmp = new Uint8Array(total - got); const n = ofdRead(e.ofd, tmp, total - got); out.set(tmp.subarray(0, n), got); got += n; } } u8().set(out.subarray(0, got), ptr); if (got < total) setFlag(fp, SF_SEOF); return sz ? Math.floor(got / sz) : 0; },
    fgetc: (fp) => fileGetc(fp), getc: (fp) => fileGetc(fp), getchar: () => { const e = fdEntry(0); if (!e) return -1; const t = new Uint8Array(1); return ofdRead(e.ofd, t, 1) === 0 ? -1 : t[0]; },
    __srget: (fp) => doSrget(fp),
    // ungetc: push the byte back into the buffer so the inlined getc macro
    // re-reads it from _p; the lexer's getc/ungetc pair always has _p>base.
    ungetc: (c, fp) => { const f = fpFile(fp); if (f && !f.mem) { let p = view().getUint32(fp + SF_P, true); const base = view().getUint32(fp + SF_BFBASE, true); if (p > base) { p--; u8()[p] = c & 0xff; view().setUint32(fp + SF_P, p, true); view().setInt32(fp + SF_R, view().getInt32(fp + SF_R, true) + 1, true); } else { u8()[fp + 64] = c & 0xff; view().setUint32(fp + SF_P, fp + 64, true); view().setInt32(fp + SF_R, 1, true); } clrFlag(fp, SF_SEOF); return c & 0xff; }
      // Sentinel std streams (1/2/3) have no FILE buffer to push into — keep a
      // per-fd pushback stack on the open description instead, consulted by
      // fileGetc/doSrget before the next real read. Without this a getc()+
      // ungetc()+getline() sequence on stdin silently drops the pushed byte
      // (sed does exactly this and loses the first char of every line).
      if (fp >= 1 && fp <= 3) { const e = fdEntry(fp - 1); if (e) { (e.ofd.unget || (e.ofd.unget = [])).push(c & 0xff); e.ofd.eof = false; markStdStreamEof(fp, false); } }
      return c & 0xff; },
    fgets: (buf, n, fp) => { let i = 0; while (i < n - 1) { const c = fileGetc(fp); if (c < 0) break; u8()[buf + i++] = c; if (c === 10) break; } if (i === 0) return 0; u8()[buf + i] = 0; return buf; },
    getline: (lineptrPtr, capPtr, fp) => { const bytes = []; for (;;) { const c = fileGetc(fp); if (c < 0) break; bytes.push(c); if (c === 10) break; } if (!bytes.length) return -1; const buf = state.alloc(bytes.length + 1, 1); for (let i = 0; i < bytes.length; i++) u8()[buf + i] = bytes[i]; u8()[buf + bytes.length] = 0; view().setUint32(lineptrPtr, buf, true); if (capPtr) view().setUint32(capPtr, bytes.length + 1, true); return bytes.length; },
    // getdelim(lineptr, cap, delim, fp): getline generalised to any delimiter
    // byte (getline is getdelim with '\n'). Grows a fresh guest buffer and
    // NUL-terminates; -1 at EOF with nothing read.
    getdelim: (lineptrPtr, capPtr, delim, fp) => { const stop = delim & 0xff; const bytes = []; for (;;) { const c = fileGetc(fp); if (c < 0) break; bytes.push(c); if (c === stop) break; } if (!bytes.length) return -1; const buf = state.alloc(bytes.length + 1, 1); for (let i = 0; i < bytes.length; i++) u8()[buf + i] = bytes[i]; u8()[buf + bytes.length] = 0; view().setUint32(lineptrPtr, buf, true); if (capPtr) view().setUint32(capPtr, bytes.length + 1, true); return bytes.length; },
    // fgetln(fp, lenPtr): BSD line reader. Returns a pointer to the next line
    // (including its trailing \n if present), NOT NUL-terminated, and writes the
    // byte count to *lenPtr. NULL at EOF. The buffer is owned by stdio and valid
    // until the next stream op, so a fresh guest allocation per call is fine
    // (mirrors getline/fgets above).
    fgetln: (fp, lenPtr) => { const bytes = []; for (;;) { const c = fileGetc(fp); if (c < 0) break; bytes.push(c); if (c === 10) break; } if (!bytes.length) { if (lenPtr) view().setUint32(lenPtr, 0, true); return 0; } const buf = state.alloc(bytes.length, 1); for (let i = 0; i < bytes.length; i++) u8()[buf + i] = bytes[i]; if (lenPtr) view().setUint32(lenPtr, bytes.length, true); return buf; },
    // Wide-char stdio: decode/encode one UTF-8 code point over the byte-level
    // FILE layer. WEOF is -1. (tr reads/writes its stream wide.)
    fgetwc: (fp) => {
      const b0 = fileGetc(fp);
      if (b0 < 0) return -1;
      if (b0 < 0x80) return b0;
      let extra, cp;
      if ((b0 & 0xe0) === 0xc0) { extra = 1; cp = b0 & 0x1f; }
      else if ((b0 & 0xf0) === 0xe0) { extra = 2; cp = b0 & 0x0f; }
      else if ((b0 & 0xf8) === 0xf0) { extra = 3; cp = b0 & 0x07; }
      else return 0xfffd;
      for (let i = 0; i < extra; i++) { const b = fileGetc(fp); if (b < 0) return -1; cp = (cp << 6) | (b & 0x3f); }
      return cp >>> 0;
    },
    getwc: (fp) => env.fgetwc(fp),
    getwchar: () => env.fgetwc(1),
    fputwc: (wc, fp) => { let s; try { s = String.fromCodePoint(wc >>> 0); } catch { s = "�"; } fpWrite(fp, enc.encode(s)); return wc >>> 0; },
    putwc: (wc, fp) => env.fputwc(wc, fp),
    putwchar: (wc) => env.fputwc(wc, 2),
    fseek: (fp, off, whence) => { const fd = fileFd(fp); if (fp > 3) { view().setInt32(fp + SF_R, 0, true); view().setUint32(fp + SF_P, view().getUint32(fp + SF_BFBASE, true), true); clrFlag(fp, SF_SEOF); } const e = fdEntry(fd); if (!e || e.ofd.kind !== "file") return -1; const len = e.ofd.node.data.length; e.ofd.off = whence === 2 ? len + off : whence === 1 ? e.ofd.off + off : off; return 0; },
    fseeko: (fp, off, whence) => env.fseek(fp, typeof off === "bigint" ? Number(off) : off, whence),
    ftell: (fp) => { const fd = fileFd(fp); const e = fdEntry(fd); if (!e || e.ofd.kind !== "file") return -1; const buffered = fp > 3 ? Math.max(0, view().getInt32(fp + SF_R, true)) : 0; return e.ofd.off - buffered; },
    ftello: (fp) => BigInt(env.ftell(fp)),
    rewind: (fp) => { env.fseek(fp, 0, 0); },
    fflush: (fp) => { const f = fpFile(fp); if (f && f.mem) syncMem(f); return 0; }, fileno: (fp) => fileFd(fp), setvbuf: () => 0, setbuf: () => 0, setbuffer: () => 0, setlinebuf: () => 0,
    getloadavg: (avgPtr, nelem) => { const n = Math.max(0, Math.min(nelem|0, 3)); for (let i=0;i<n;i++) view().setFloat64(avgPtr + i*8, 0, true); return n; },
    // stdio putc-overflow handler: the guest's streams are unbuffered, so
    // every putc/putchar char arrives here. Emit it (NOT discard).
    __swbuf: (c, fp) => { fpWrite(fp, String.fromCharCode(c & 0xff)); return c & 0xff; },
    clearerr: (fp) => { clrFlag(fp, SF_SEOF | SF_SERR); if (fp >= 1 && fp <= 3) { const e = fdEntry(fp - 1); if (e) e.ofd.eof = false; } return 0; },
    ferror: (fp) => (fp > 3 ? (view().getInt16(fp + SF_FLAGS, true) & SF_SERR ? 1 : 0) : 0),
    // feof: a real FILE carries the SF_SEOF flag; the sentinel std streams
    // (1/2/3 -> fd 0/1/2) have no struct, so consult the underlying fd's
    // read-EOF state instead — otherwise a getline/getdelim loop on stdin that
    // returns -1 at EOF looks like a read error to the guest (sort err(2)s).
    feof: (fp) => { if (fp > 3) return view().getInt16(fp + SF_FLAGS, true) & SF_SEOF ? 1 : 0; if (fp >= 1 && fp <= 3) { const e = fdEntry(fp - 1); return e && e.ofd.eof ? 1 : 0; } return 0; },

    strlen: (p) => { let n = 0; while (u8()[p + n]) n++; return n; },
    strcmp: (a, b) => { let i = 0; for (;;) { const x = u8()[a + i], y = u8()[b + i]; if (x !== y) return x - y; if (!x) return 0; i++; } },
    strncmp: (a, b, n) => { for (let i = 0; i < n; i++) { const x = u8()[a + i], y = u8()[b + i]; if (x !== y) return x - y; if (!x) return 0; } return 0; },
    strcasecmp: (a, b) => { const lc = (c) => (c >= 65 && c <= 90 ? c + 32 : c); let i = 0; for (;;) { const x = lc(u8()[a + i]), y = lc(u8()[b + i]); if (x !== y) return x - y; if (!x) return 0; i++; } },
    strncasecmp: (a, b, n) => { const lc = (c) => (c >= 65 && c <= 90 ? c + 32 : c); for (let i = 0; i < n; i++) { const x = lc(u8()[a + i]), y = lc(u8()[b + i]); if (x !== y) return x - y; if (!x) return 0; } return 0; },
    strcoll: (a, b) => env.strcmp(a, b),
    strchr: (s, c) => { c &= 0xff; for (let p = s; ; p++) { if (u8()[p] === c) return p; if (!u8()[p]) return c === 0 ? p : 0; } },
    strrchr: (s, c) => { c &= 0xff; let hit = 0; for (let p = s; ; p++) { if (u8()[p] === c) hit = p; if (!u8()[p]) return c === 0 ? p : hit; } },
    strstr: (h, n) => { const needle = cstr(n); if (!needle) return h; const hay = cstr(h); const idx = hay.indexOf(needle); return idx < 0 ? 0 : h + enc.encode(hay.slice(0, idx)).length; },
    strcpy: (d, s) => { let i = 0; do { u8()[d + i] = u8()[s + i]; } while (u8()[s + i++]); return d; },
    // stpcpy/stpncpy return a pointer to the copied NUL (unlike strcpy). top and
    // other BSD tools use that return as a write cursor; a missing import made
    // them scribble at address 0 and SIGSEGV.
    stpcpy: (d, s) => { const u = u8(); let i = 0; while (u[s + i]) { u[d + i] = u[s + i]; i++; } u[d + i] = 0; return d + i; },
    stpncpy: (d, s, n) => { const u = u8(); let i = 0; for (; i < n && u[s + i]; i++) u[d + i] = u[s + i]; const end = d + i; for (; i < n; i++) u[d + i] = 0; return end; },
    strncpy: (d, s, n) => { let i = 0; for (; i < n && u8()[s + i]; i++) u8()[d + i] = u8()[s + i]; for (; i < n; i++) u8()[d + i] = 0; return d; },
    strlcpy: (d, s, n) => { const len = env.strlen(s); if (n) { const c = Math.min(len, n - 1); u8().copyWithin(d, s, s + c); u8()[d + c] = 0; } return len; },
    strlcat: (d, s, n) => { let dl = 0; while (dl < n && u8()[d + dl]) dl++; const sl = env.strlen(s); if (dl === n) return n + sl; let i = 0; while (u8()[s + i] && dl + i < n - 1) { u8()[d + dl + i] = u8()[s + i]; i++; } u8()[d + dl + i] = 0; return dl + sl; },
    strcat: (d, s) => { env.strcpy(d + env.strlen(d), s); return d; },
    strncat: (d, s, n) => { let dl = env.strlen(d), i = 0; for (; i < n && u8()[s + i]; i++) u8()[d + dl + i] = u8()[s + i]; u8()[d + dl + i] = 0; return d; },
    strdup: (s) => putStr(cstr(s)),
    strspn: (s, set) => { const ss = cstr(set); let n = 0; for (;;) { const c = u8()[s + n]; if (!c || !ss.includes(String.fromCharCode(c))) return n; n++; } },
    strcspn: (s, set) => { const ss = cstr(set); let n = 0; for (;;) { const c = u8()[s + n]; if (!c || ss.includes(String.fromCharCode(c))) return n; n++; } },
    memchr: (s, c, n) => { c &= 0xff; for (let i = 0; i < n; i++) if (u8()[s + i] === c) return s + i; return 0; },
    memcmp: (a, b, n) => { for (let i = 0; i < n; i++) { const x = u8()[a + i], y = u8()[b + i]; if (x !== y) return x - y; } return 0; },
    strtoul: (s, endp, base) => { const str = cstr(s); const m = str.match(/^\s*[+-]?(0x[0-9a-fA-F]+|[0-9]+)/); const v = m ? parseInt(m[0], base || (m[0].includes("0x") ? 16 : 10)) >>> 0 : 0; if (endp) view().setUint32(endp, s + (m ? m[0].length : 0), true); return v; },
    strtol: (s, e, b) => env.strtoul(s, e, b) | 0,
    // MUST write endptr: callers (Lua's luaO_str2d) read *endptr right after,
    // so leaving it uninitialised makes them dereference a garbage pointer.
    strtod: (s, endp) => { const str = cstr(s); const m = str.match(/^\s*[+-]?(0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*([pP][+-]?\d+)?|\d+\.?\d*([eE][+-]?\d+)?|\.\d+([eE][+-]?\d+)?|inf(inity)?|nan)/i); const t = m ? m[0] : ""; const v = t ? parseFloat(t) : 0; if (endp) view().setUint32(endp, s + t.length, true); return isNaN(v) ? 0 : v; },
    strtonum: (s) => { const v = parseInt(cstr(s), 10); return isNaN(v) ? 0 : v; },
    atoi: (s) => parseInt(cstr(s), 10) || 0,
    strtoll: (s, e, b) => { const str = cstr(s); const m = str.match(/^\s*[+-]?(0[xX][0-9a-fA-F]+|[0-9]+)/); if (e) view().setUint32(e, s + (m ? m[0].length : 0), true); return m ? BigInt(m[0].trim()) : 0n; },
    strtoull: (s, e, b) => { const v = env.strtoll(s, e, b); return v < 0n ? v + (1n << 64n) : v; },
    strtoimax: (s, e, b) => env.strtoll(s, e, b), strtoumax: (s, e, b) => env.strtoull(s, e, b),
    atoll: (s) => env.strtoll(s, 0, 10), atol: (s) => env.strtoul(s, 0, 10) | 0,
    abs: (n) => Math.abs(n | 0), labs: (n) => Math.abs(n | 0), llabs: (n) => { const b = BigInt(n); return b < 0n ? -b : b; },
    fabs: (x) => Math.abs(x), fabsf: (x) => Math.abs(x),
    round: (x) => (x < 0 ? -Math.round(-x) : Math.round(x)), roundf: (x) => (x < 0 ? -Math.round(-x) : Math.round(x)),
    trunc: (x) => Math.trunc(x), truncf: (x) => Math.trunc(x),
    // sscanf(str, fmt, ...): the variadic tail is a packed va_list pointer.
    sscanf: (s, f, va) => scanfFromGuest(state, s, f, va),
    vsscanf: (s, f, va) => scanfFromGuest(state, s, f, va),
    // wctomb(s, wc): encode one code point as UTF-8; s==NULL → stateless (0).
    wctomb: (s, wc) => { if (!s) return 0; wc = wc >>> 0; let b; if (wc < 0x80) b = [wc]; else if (wc < 0x800) b = [0xc0 | (wc >> 6), 0x80 | (wc & 0x3f)]; else if (wc < 0x10000) b = [0xe0 | (wc >> 12), 0x80 | ((wc >> 6) & 0x3f), 0x80 | (wc & 0x3f)]; else b = [0xf0 | (wc >> 18), 0x80 | ((wc >> 12) & 0x3f), 0x80 | ((wc >> 6) & 0x3f), 0x80 | (wc & 0x3f)]; for (let i = 0; i < b.length; i++) u8()[s + i] = b[i]; return b.length; },
    wcrtomb: (s, wc) => (s ? env.wctomb(s, wc) : 1),
    strsignal: (sig) => putStr({ 1: "Hangup", 2: "Interrupt", 3: "Quit", 6: "Abort trap", 9: "Killed", 11: "Segmentation fault", 13: "Broken pipe", 14: "Alarm clock", 15: "Terminated", 17: "Stopped", 18: "Continued", 20: "Child exited", 28: "Window size changes" }[sig] || ("Signal " + sig)),
    // basename/dirname: write the result into a fresh buffer and return it
    // (we never mutate the caller's path, which is the safe POSIX variant).
    basename: (p) => { let s = cstr(p); s = s.replace(/\/+$/, ""); const i = s.lastIndexOf("/"); s = i < 0 ? s : s.slice(i + 1); return putStr(s === "" ? "/" : s); },
    dirname: (p) => { let s = cstr(p); s = s.replace(/\/+$/, ""); const i = s.lastIndexOf("/"); return putStr(i < 0 ? "." : i === 0 ? "/" : s.slice(0, i)); },
    // ctype: the guest imports these as functions (not the table-macro form),
    // so plain ASCII classification is correct. ___runetype returns the
    // FreeBSD _CTYPE_* bitmask for the rare macro-style caller.
    isalpha: (c) => ((c >= 65 && c <= 90) || (c >= 97 && c <= 122) ? 1 : 0),
    isdigit: (c) => (c >= 48 && c <= 57 ? 1 : 0),
    isalnum: (c) => ((c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) ? 1 : 0),
    isspace: (c) => (c === 32 || (c >= 9 && c <= 13) ? 1 : 0),
    isblank: (c) => (c === 32 || c === 9 ? 1 : 0),
    isupper: (c) => (c >= 65 && c <= 90 ? 1 : 0),
    islower: (c) => (c >= 97 && c <= 122 ? 1 : 0),
    isxdigit: (c) => ((c >= 48 && c <= 57) || (c >= 65 && c <= 70) || (c >= 97 && c <= 102) ? 1 : 0),
    iscntrl: (c) => (c < 32 || c === 127 ? 1 : 0),
    isprint: (c) => (c >= 32 && c < 127 ? 1 : 0),
    isgraph: (c) => (c > 32 && c < 127 ? 1 : 0),
    ispunct: (c) => (c > 32 && c < 127 && !((c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122)) ? 1 : 0),
    toupper: (c) => (c >= 97 && c <= 122 ? c - 32 : c), tolower: (c) => (c >= 65 && c <= 90 ? c + 32 : c),
    ___toupper: (c) => (c >= 97 && c <= 122 ? c - 32 : c), ___tolower: (c) => (c >= 65 && c <= 90 ? c + 32 : c),
    ___runetype: (c) => { let m = 0; if ((c >= 65 && c <= 90) || (c >= 97 && c <= 122)) m |= 0x100; if (c < 32 || c === 127) m |= 0x200; if (c >= 48 && c <= 57) m |= 0x400 | 0x400000; if (c > 32 && c < 127) m |= 0x800; if (c >= 97 && c <= 122) m |= 0x1000; if (c > 32 && c < 127 && !((c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122))) m |= 0x2000; if (c >= 32 && c < 127) m |= 0x40000; if (c === 32 || (c >= 9 && c <= 13)) m |= 0x4000; if (c >= 65 && c <= 90) m |= 0x8000; if ((c >= 48 && c <= 57) || (c >= 65 && c <= 70) || (c >= 97 && c <= 102)) m |= 0x10000; if (c === 32 || c === 9) m |= 0x20000; return m; },
    // libm — wasm32 has no built-in transcendentals; route to JS Math.
    sin: Math.sin, cos: Math.cos, tan: Math.tan, asin: Math.asin, acos: Math.acos, atan: Math.atan, atan2: (y, x) => Math.atan2(y, x),
    sinh: Math.sinh, cosh: Math.cosh, tanh: Math.tanh, exp: Math.exp, log: Math.log, log10: Math.log10, log2: Math.log2,
    sqrt: Math.sqrt, cbrt: Math.cbrt, floor: Math.floor, ceil: Math.ceil, fmod: (a, b) => a % b, hypot: (a, b) => Math.hypot(a, b),
    sinf: Math.sin, cosf: Math.cos, sqrtf: Math.sqrt, floorf: Math.floor, ceilf: Math.ceil, expf: Math.exp, logf: Math.log,
    strnlen: (s, max) => { let i = 0; while (i < max && u8()[s + i]) i++; return i; },
    // strtok / strtok_r tokenizer. strtok keeps its cursor on the proc so it
    // survives across calls without a file-scope global.
    strtok_r: (sPtr, delimPtr, savePtr) => { const delim = cstr(delimPtr); let start = sPtr ? sPtr : view().getUint32(savePtr, true); while (u8()[start] && delim.includes(String.fromCharCode(u8()[start]))) start++; if (!u8()[start]) { view().setUint32(savePtr, start, true); return 0; } let end = start; while (u8()[end] && !delim.includes(String.fromCharCode(u8()[end]))) end++; if (u8()[end]) { u8()[end] = 0; view().setUint32(savePtr, end + 1, true); } else view().setUint32(savePtr, end, true); return start; },
    strtok: (sPtr, delimPtr) => { const delim = cstr(delimPtr); let start = sPtr ? sPtr : proc.strtokSave || 0; while (u8()[start] && delim.includes(String.fromCharCode(u8()[start]))) start++; if (!u8()[start]) { proc.strtokSave = start; return 0; } let end = start; while (u8()[end] && !delim.includes(String.fromCharCode(u8()[end]))) end++; if (u8()[end]) { u8()[end] = 0; proc.strtokSave = end + 1; } else proc.strtokSave = end; return start; },
    atexit: () => 0, __cxa_atexit: () => 0, __cxa_finalize: () => 0,
    sched_yield: () => 0, sched_get_priority_max: () => 99, sched_get_priority_min: () => 0, sched_getcpu: () => 0,
    strerror: (n) => putStr("Error " + n),
    qsort: (base, nmemb, size, cmpIdx) => {
      const table = proc.inst.exports.__indirect_function_table;
      if (!table || !nmemb || !size) return 0;
      const cmp = table.get(cmpIdx);
      const elems = []; for (let i = 0; i < nmemb; i++) elems.push(u8().slice(base + i * size, base + (i + 1) * size));
      const scratchA = state.alloc(size), scratchB = state.alloc(size);
      elems.sort((x, y) => { u8().set(x, scratchA); u8().set(y, scratchB); return cmp(scratchA, scratchB) | 0; });
      for (let i = 0; i < nmemb; i++) u8().set(elems[i], base + i * size);
      return 0;
    },
    bsearch: (key, base, nmemb, size, cmpIdx) => { const table = proc.inst.exports.__indirect_function_table; if (!table) return 0; const cmp = table.get(cmpIdx); for (let i = 0; i < nmemb; i++) { const el = base + i * size; if ((cmp(key, el) | 0) === 0) return el; } return 0; },
    // BSD mergesort/heapsort: same (base, nmemb, size, cmp) contract as qsort
    // but return 0 on success / -1 on error. JS Array.sort is stable (so this
    // is a faithful mergesort), and qsort already does the element shuffling —
    // reuse it and report success.
    mergesort: (base, nmemb, size, cmpIdx) => { env.qsort(base, nmemb, size, cmpIdx); return 0; },
    heapsort: (base, nmemb, size, cmpIdx) => { env.qsort(base, nmemb, size, cmpIdx); return 0; },
    // ---- POSIX regex over JS RegExp (regcomp/regexec/regfree/regerror) ----
    // grep/sed compile a pattern into a regex_t then match lines. The compiled
    // JS RegExp lives in a per-process map keyed by the guest regex_t pointer.
    // regex_t (wasm32): re_magic@0, re_nsub@4 (size_t). regmatch_t: rm_so@0,
    // rm_eo@8, each a 64-bit regoff_t (== FreeBSD __off_t); offsets are BYTE
    // offsets into the UTF-8 subject. cflags: EXTENDED=1 ICASE=2 NOSUB=4
    // NEWLINE=8. The 'd' flag gives per-group match indices for submatches.
    regcomp: (preg, patternPtr, cflags) => {
      const src = posixRegexToJs(cstr(patternPtr), !!(cflags & 1));
      let flags = "d";
      if (cflags & 2) flags += "i";
      if (cflags & 8) flags += "m";
      let re;
      try { re = new RegExp(src, flags); } catch { return 13; } // REG_BADRPT
      let nsub = 0;
      try { nsub = new RegExp(src + "|").exec("").length - 1; } catch { nsub = 0; }
      (proc.pio.regex || (proc.pio.regex = new Map())).set(preg, { re, nosub: !!(cflags & 4) });
      view().setUint32(preg + 4, nsub, true); // re_nsub
      return 0;
    },
    regexec: (preg, strPtr, nmatch, pmatch, eflags) => {
      const entry = proc.pio.regex && proc.pio.regex.get(preg);
      if (!entry) return 1; // REG_NOMATCH
      // REG_STARTEND (4): the subject is bytes [strPtr+rm_so, strPtr+rm_eo) and
      // the returned offsets are relative to strPtr — sed drives its global
      // substitution by advancing rm_so, so honouring this is what stops an
      // infinite match-the-same-spot loop. Read the IN bounds BEFORE overwriting.
      const startend = !!(eflags & 4);
      let base = 0;
      let subBytes;
      if (startend && pmatch) {
        base = view().getInt32(pmatch, true);
        const end = view().getInt32(pmatch + 8, true);
        subBytes = u8().slice(strPtr + base, strPtr + end);
      } else {
        let e = strPtr; while (u8()[e]) e++; subBytes = u8().slice(strPtr, e);
      }
      const subject = dec.decode(subBytes);
      const m = entry.re.exec(subject);   // non-global: always the first match
      if (!m) return 1;
      if (nmatch > 0 && pmatch && !entry.nosub) {
        const byteOff = (jsIdx) => base + enc.encode(subject.slice(0, jsIdx)).length;
        const setOff = (ptr, val) => { view().setInt32(ptr, val, true); view().setInt32(ptr + 4, val < 0 ? -1 : 0, true); };
        for (let i = 0; i < nmatch; i++) {
          const cell = pmatch + i * 16;
          const span = m.indices && m.indices[i];
          if (span) { setOff(cell, byteOff(span[0])); setOff(cell + 8, byteOff(span[1])); }
          else { setOff(cell, -1); setOff(cell + 8, -1); }
        }
      }
      return 0;
    },
    regfree: (preg) => { if (proc.pio.regex) proc.pio.regex.delete(preg); },
    regerror: (errcode, preg, errbuf, errbufSize) => {
      const bytes = enc.encode("regex error");
      if (errbuf && errbufSize > 0) { const n = Math.min(bytes.length, (errbufSize | 0) - 1); for (let i = 0; i < n; i++) u8()[errbuf + i] = bytes[i]; u8()[errbuf + n] = 0; }
      return bytes.length + 1;
    },
    strcasestr: (h, n) => { const needle = cstr(n).toLowerCase(); if (!needle) return h; const hay = cstr(h); const idx = hay.toLowerCase().indexOf(needle); return idx < 0 ? 0 : h + enc.encode(hay.slice(0, idx)).length; },
    strsep: (stringpPtr, delimPtr) => {
      const start = view().getUint32(stringpPtr, true);
      if (!start) return 0;
      const delim = cstr(delimPtr);
      for (let p = start; u8()[p]; p++) {
        if (delim.includes(String.fromCharCode(u8()[p]))) {
          u8()[p] = 0;
          view().setUint32(stringpPtr, p + 1, true);
          return start;
        }
      }
      view().setUint32(stringpPtr, 0, true);
      return start;
    },
    vasprintf: (strpPtr, f, v) => { const s = fmt(f, v); const p = putStr(s); view().setUint32(strpPtr, p, true); return enc.encode(s).length; },
    asprintf: (strpPtr, f, v) => env.vasprintf(strpPtr, f, v),
    tzset: () => 0,
    strndup: (s, n) => { let len = 0; while (len < n && u8()[s + len]) len++; const p = state.alloc(len + 1); u8().copyWithin(p, s, s + len); u8()[p + len] = 0; return p; },
    strpbrk: (s, set) => { const ss = cstr(set); for (let p = s; u8()[p]; p++) if (ss.includes(String.fromCharCode(u8()[p]))) return p; return 0; },
    memmem: (h, hn, n, nn) => { if (!nn) return h; for (let i = 0; i + nn <= hn; i++) { let m = true; for (let j = 0; j < nn; j++) if (u8()[h + i + j] !== u8()[n + j]) { m = false; break; } if (m) return h + i; } return 0; },
    mempcpy: (d, s, n) => { u8().copyWithin(d, s, s + n); return d + n; },

    // --- process syscalls ---
    fork: () => mgr.fork(proc),
    vfork: () => mgr.fork(proc),
    waitpid: (pid, statusPtr, opts) => doWait(pid, statusPtr, opts),
    wait3: (statusPtr, opts) => doWait(-1, statusPtr, opts),
    wait4: (pid, statusPtr, opts) => doWait(pid, statusPtr, opts),
    access: (pathPtr) => { const raw = cstrBounded(pathPtr); if (raw === null) { setErrno(14); return -1; } if (raw === "") { setErrno(2); return -1; } const p = mgr.vfsPath(raw, proc.pio.cwd); return (mgr.tools.has(basename(raw)) || mgr.vfs[p]) ? 0 : (setErrno(2), -1); },
    execve: (pathPtr, argvPtr) => {
      const raw = cstr(pathPtr);
      const name = basename(raw);
      // Precompiled tools (the boot shell) resolve from the tool map;
      // everything else resolves through the VFS — a yfs-backed binary
      // is fetched + compiled on demand (docs/yfs.md).
      let mod = mgr.tools.get(name) || null;
      let execNode = null;
      if (!mod) {
        execNode = mgr.yfsExecNode(raw, proc.pio.cwd);
        if (!execNode) { if (state.errnoPtr) view().setUint32(state.errnoPtr, 2, true); return -1; }
        const hash = execNode.yfs && execNode.yfs.entry.h;
        if (hash && mgr.moduleCache.has(hash)) mod = mgr.moduleCache.get(hash);
      }
      const args = [];
      for (let p = argvPtr; ; p += 4) { const sp = view().getUint32(p, true); if (!sp) break; args.push(cstr(sp)); }
      const av = args.length ? args : [name];
      if (proc.interactive) {
        // true exec: replace this process's image in place (same pid, same fd
        // table / cwd / env), then unwind so the scheduler runs the new image.
        // A module past Chrome's main-thread sync-instantiation size limit
        // (nvim) boots asynchronously instead — the process parks in
        // "booting" and the scheduler picks it up when the instance resolves.
        // A cold yfs binary takes the same booting path: fetch, compile,
        // swap in.
        if (mod) {
          try {
            mgr.prepareExec(proc, mod, av);
          } catch (err) {
            if (!mgr.isSyncInstantiationLimit(err)) throw err;
            mgr.prepareExecAsync(proc, mod, av);
          }
        } else {
          mgr.prepareExecYfs(proc, execNode, av);
        }
        const e = new Error("exec"); e.isExec = true; throw e;
      }
      if (!mod) {
        mod = mgr.yfsModuleSync(execNode);
        if (!mod) { if (state.errnoPtr) view().setUint32(state.errnoPtr, 2, true); return -1; }
      }
      const res = mgr.runChildProgram(mod, av, proc.pid);
      exitWith(res.exitCode);
    },
    execv: (pathPtr, argvPtr) => env.execve(pathPtr, argvPtr), execvp: (pathPtr, argvPtr) => env.execve(pathPtr, argvPtr),

    // --- VFS + per-process fd table behind open/read/write/dup/pipe/poll ---
    // FreeBSD open flags: O_APPEND=0x8, O_CREAT=0x200, O_TRUNC=0x400,
    // O_CLOEXEC=0x100000.
    open: (pathPtr, flags, modePtr) => {
      const raw = cstrBounded(pathPtr); if (raw === null) { setErrno(14); return -1; } // EFAULT — unterminated path
      if (raw === "") { setErrno(2); return -1; } // POSIX: empty path is ENOENT, not the cwd
      const path = mgr.vfsPath(raw, proc.pio.cwd);
      const cloexec = !!(flags & 0x100000);
      // open() is variadic: with O_CREAT the mode arrives via clang's wasm32
      // shadow-stack pack (a POINTER, like fcntl). Deref it and apply the
      // process umask so a freshly created file gets mode & ~umask.
      const createMode = (flags & 0x200)
        ? (((modePtr ? view().getInt32(modePtr, true) : 0o666) & 0o777) & ~(proc.pio.umask | 0))
        : 0;
      // File-status flags fcntl(F_GETFL) must report back: access mode (0x3),
      // O_NONBLOCK (0x4), O_APPEND (0x8). Stash them on the fd entry at open().
      const statusFlags = flags & (0x3 | 0x4 | 0x8);
      const ret = (fd) => { if (fd >= 0) { const fe = proc.pio.fds.get(fd); if (fe) fe.flags = statusFlags; } return fd; };
      if (path === "/dev/null" || path === "/dev/zero") return ret(installFd(allocFd(), { kind: "char", dev: "null", refs: 1 }, cloexec));
      // /dev/tty — the process's controlling terminal: the shared session
      // tty, bidirectional (reads pull the key buffer, writes paint).
      // fzy opens it for its picker UI while stdin carries the piped
      // choices; dev:"in" is the readable-terminal char device.
      if (path === "/dev/tty") return ret(installFd(allocFd(), { kind: "char", dev: "in", refs: 1 }, cloexec));
      if (path.startsWith("/dev/pts/") && mgr.ptys) { const id = parseInt(path.slice(9), 10); const pty = mgr.ptys.get(id); if (pty) return ret(installFd(allocFd(), { kind: "sock", rx: pty.toSlave, tx: pty.toMaster, refs: 1, isPty: true, pty, ptyMaster: false }, cloexec)); }
      let node = mgr.vfs[path];
      if (!node) { if (flags & 0x200) node = mgr.vfsCreateFile(path, createMode); else { setErrno(2); return -1; } }
      if (node.type === "dir") return ret(installFd(allocFd(), { kind: "dir", node, path, refs: 1 }, cloexec));
      // yfs-backed node with a cold body: kick the fetch and suspend via
      // the same asyncify block a tty read uses; the scheduler resumes
      // this open when the bytes (or an error) arrive, and the rewound
      // call proceeds synchronously below. The `node.yfs` condition is
      // stable across unwind/rewind, so the resuming() branch pairs with
      // exactly this open. O_TRUNC skips the fetch — the old bytes are
      // dead anyway.
      if (node.type === "file" && node.yfs) {
        if (proc.interactive && resuming()) proc.inst.exports.asyncify_stop_rewind();
        if (!node.data && !(flags & 0x400)) {
          if (node.yfs.error) { setErrno(5); return -1; } // EIO
          mgr.yfsStart(node);
          if (!node.data) {
            if (node.yfs.error) { setErrno(5); return -1; }
            if (!proc.interactive) { setErrno(5); return -1; } // no scheduler to resume us
            beginBlock("open", () => !!node.data || !!(node.yfs && node.yfs.error));
            return -1;
          }
        }
      }
      if (flags & 0x400) node.data = new Uint8Array(0); // O_TRUNC
      return ret(installFd(allocFd(), { kind: "file", node, off: (flags & 0x8) ? node.data.length : 0, append: !!(flags & 0x8), refs: 1 }, cloexec));
    },
    openat: (dfd, pathPtr, flags) => env.open(pathPtr, flags),
    creat: (pathPtr, mode) => env.open(pathPtr, 0x200 | 0x400 | 0x1),
    close: (fd) => closeFd(fd),
    read: (fd, buf, n) => {
      if (buf === 0 && n > 0) { setErrno(14); return -1; } // EFAULT — NULL buffer
      const e = fdEntry(fd); if (!e) { setErrno(9); return -1; }
      const ofd = e.ofd;
      if (proc.interactive) {
        const isTty = ofd.kind === "char" && ofd.dev === "in";
        const blockable = isTty || (ofd.kind === "pipe" && ofd.end === "r") || ofd.kind === "sock";
        if (blockable) {
          const ready = isTty ? () => (proc.tty || mgr.tty).inbuf.length > 0
            : ofd.kind === "pipe" ? () => ofd.pipe.total > 0 || ofd.pipe.writeClosed
              : () => ofd.rx.total > 0 || ofd.rx.writeClosed;
          if (resuming()) {
            proc.inst.exports.asyncify_stop_rewind();
            // A signal (e.g. SIGWINCH from a resize) interrupting a blocking
            // read returns EINTR — NOT 0, which the guest reads as EOF and
            // exits on. And a spurious wake with nothing readable must RE-BLOCK
            // rather than return 0 (this is what killed an idle shell on resize).
            if (deliverPendingSignals()) { setErrno(4); return -1; }
            if (!ready()) { beginBlock("read", ready); return 0; }
          } else if (!ready()) { beginBlock("read", ready); return 0; } // suspend; scheduler resumes when ready
          if (isTty) return ttyServe(buf, n);
        }
      }
      const tmp = new Uint8Array(n); const got = ofdRead(ofd, tmp, n); u8().set(tmp.subarray(0, got), buf); return got;
    },
    readv: (fd, iov, cnt) => { if ((cnt | 0) < 0 || (cnt | 0) > 1024) { setErrno(22); return -1; } let total = 0; for (let i = 0; i < cnt; i++) { const b = view().getUint32(iov + i * 8, true); const l = view().getUint32(iov + i * 8 + 4, true); const got = env.read(fd, b, l); if (got <= 0) { return total || got; } total += got; if (got < l) break; } return total; },
    // off_t is 64-bit on the FreeBSD wasm32 ABI: args/return arrive as
    // BigInt. Mirror whatever width the import was declared with.
    lseek: (fd, off, whence) => { const big = typeof off === "bigint"; const e = fdEntry(fd); if (!e || e.ofd.kind !== "file") { setErrno(29); return big ? -1n : -1; } const o = big ? Number(off) : off; const len = e.ofd.node.data.length; const pos = whence === 2 ? len + o : whence === 1 ? e.ofd.off + o : o; e.ofd.off = pos; return big ? BigInt(pos) : pos; },
    // F_DUPFD=0, F_GETFD=1, F_SETFD=2, F_GETFL=3, F_SETFL=4, F_DUPFD_CLOEXEC=17.
    // fcntl's variadic 3rd argument arrives as a POINTER to clang's wasm32
    // shadow-stack vararg pack, not as the value — dereference it to the int
    // the guest actually passed (F_DUPFD minfd, F_SETFD/F_SETFL flags). The
    // native m3 bridge does the same; without it F_SETFL stored the pointer.
    // F_GETFL must report the fd's ACCESS MODE (O_ACCMODE bits), not just the
    // status flags open() stashed: libuv's uv_tty_init/uv__stream_open derives
    // its READABLE/WRITABLE stream flags from `fcntl(F_GETFL) & O_ACCMODE`. A
    // bare 0 reads as O_RDONLY, the stream never gets UV_HANDLE_WRITABLE, and
    // every uv_write() on it fails UV_EPIPE with no syscall — nvim's TUI died
    // exactly there ("flush_buf: uv_write failed: broken pipe"). Terminal char
    // devices and sockets are bidirectional; pipe ends are directional; files
    // report what open() stored.
    // Every command on a nonexistent fd is EBADF (a sloppy return-0 on
    // F_SETFD let close-all-fds loops walk the whole fd space believing
    // every fd exists).
    fcntl: (fd, cmd, argPtr) => { const e = fdEntry(fd); if (!e) { setErrno(9); return -1; } const arg = argPtr ? view().getInt32(argPtr, true) : 0; if (cmd === 0 || cmd === 17) { e.ofd.refs++; const nf = installFd(allocFd(arg | 0), e.ofd, cmd === 17); proc.pio.fds.get(nf).flags = e.flags | 0; return nf; } if (cmd === 1) return e.cloexec ? 1 : 0; if (cmd === 2) { e.cloexec = !!(arg & 1); return 0; } if (cmd === 3) { const kind = e.ofd.kind; const acc = (kind === "char" || kind === "sock" || kind === "usock") ? 2 : kind === "pipe" ? (e.ofd.end === "r" ? 0 : 1) : ((e.flags | 0) & 3); return ((e.flags | 0) & ~3) | acc; } if (cmd === 4) { e.flags = arg | 0; return 0; } return 0; },
    dup: (fd) => { const e = fdEntry(fd); if (!e) { setErrno(9); return -1; } e.ofd.refs++; return installFd(allocFd(), e.ofd, false); },
    dup2: (oldfd, newfd) => { const e = fdEntry(oldfd); if (!e) { setErrno(9); return -1; } if (oldfd === newfd) return newfd; if (proc.pio.fds.has(newfd)) closeFd(newfd); e.ofd.refs++; return installFd(newfd, e.ofd, false); },
    pipe: (ptr) => { const p = newPipeBuf(); const rfd = installFd(allocFd(), { kind: "pipe", pipe: p, end: "r", refs: 1 }); const wfd = installFd(allocFd(), { kind: "pipe", pipe: p, end: "w", refs: 1 }); view().setUint32(ptr, rfd, true); view().setUint32(ptr + 4, wfd, true); return 0; },
    // pipe2 flags MUST be honored: libuv's uv_spawn signals "exec succeeded"
    // by the O_CLOEXEC status pipe closing in the child — if the flag is
    // dropped the parent blocks forever in read() on that pipe (nvim's
    // client/server handshake deadlocked exactly there).
    pipe2: (ptr, fl) => { const r = env.pipe(ptr); if (r === 0 && fl) { const cl = !!(fl & 0x100000); const nb = (fl & 0x4) ? 0x4 : 0; for (const off of [0, 4]) { const e = fdEntry(view().getUint32(ptr + off, true)); if (e) { if (cl) e.cloexec = true; if (nb) e.flags = (e.flags | 0) | nb; } } } return r; },
    socketpair: (dom, type, proto, svPtr) => {
      // FreeBSD folds SOCK_NONBLOCK (0x20000000) / SOCK_CLOEXEC (0x10000000)
      // into the type argument; strip them and re-apply as O_NONBLOCK (0x4) /
      // FD_CLOEXEC on each returned fd so fcntl(F_GETFL/F_GETFD) sees them.
      const nb = (type & 0x20000000) ? 0x4 : 0;
      const cl = !!(type & 0x10000000);
      const sotype = (type & 0xff) || 1; // SOCK_STREAM=1 / SOCK_DGRAM=2
      const a = newPipeBuf(), b = newPipeBuf();
      const e0 = installFd(allocFd(), { kind: "sock", rx: a, tx: b, refs: 1, sotype }, cl);
      const e1 = installFd(allocFd(), { kind: "sock", rx: b, tx: a, refs: 1, sotype }, cl);
      proc.pio.fds.get(e0).flags = nb; proc.pio.fds.get(e1).flags = nb;
      view().setUint32(svPtr, e0, true); view().setUint32(svPtr + 4, e1, true); return 0;
    },
    // --- sockets: unix-domain (path) AND AF_INET loopback (port) ---
    // FreeBSD sockaddr_un: sun_len@0, sun_family@1, sun_path@2. AF_INET is
    // emulated in-process: bind assigns an ephemeral port, connect finds the
    // listener by port, accept pairs them — all on 127.0.0.1.
    socket: (domain, type, proto) => installFd(allocFd(), { kind: "usock", domain: domain >>> 0, sotype: (type & 0xff) || 1, refs: 1, bound: null, listening: false, acceptQueue: [], opts: {} }),
    bind: (fd, addrPtr) => {
      const e = fdEntry(fd); if (!e || e.ofd.kind !== "usock") { setErrno(9); return -1; }
      if (u8()[addrPtr + 1] === 2) { // AF_INET
        let port = readSinPort(addrPtr); if (port === 0) port = nextEphemeralPort();
        e.ofd.bound = { family: 2, port, addr: [u8()[addrPtr + 4], u8()[addrPtr + 5], u8()[addrPtr + 6], u8()[addrPtr + 7]] };
        return 0;
      }
      e.ofd.bound = readSunPath(addrPtr); return 0;
    },
    listen: (fd) => {
      const e = fdEntry(fd); if (!e || e.ofd.kind !== "usock") { setErrno(9); return -1; }
      e.ofd.listening = true;
      if (e.ofd.bound != null) { if (typeof e.ofd.bound === "object") (mgr.inetSockets = mgr.inetSockets || new Map()).set(e.ofd.bound.port, e.ofd); else mgr.unixSockets.set(e.ofd.bound, e.ofd); }
      return 0;
    },
    connect: (fd, addrPtr) => {
      const e = fdEntry(fd); if (!e) { setErrno(9); return -1; }
      if (u8()[addrPtr + 1] === 2) { // AF_INET
        const port = readSinPort(addrPtr);
        const listener = (mgr.inetSockets || new Map()).get(port);
        if (!listener || !listener.listening) { setErrno(61); return -1; } // ECONNREFUSED
        const a = newPipeBuf(), b = newPipeBuf(), localPort = nextEphemeralPort();
        e.ofd = { kind: "sock", domain: 2, rx: a, tx: b, refs: 1, sotype: 1, localPort, peerPort: port };
        listener.acceptQueue.push({ rx: b, tx: a, peerPort: localPort });
        return 0;
      }
      const path = readSunPath(addrPtr); const listener = mgr.unixSockets.get(path);
      if (!listener || !listener.listening) { setErrno(61); return -1; } // ECONNREFUSED
      const a = newPipeBuf(), b = newPipeBuf();
      e.ofd = { kind: "sock", rx: a, tx: b, refs: 1 };
      listener.acceptQueue.push({ rx: b, tx: a });
      return 0;
    },
    accept: (fd, addrPtr, lenPtr) => {
      const e = fdEntry(fd); if (!e || e.ofd.kind !== "usock") { setErrno(9); return -1; }
      if (e.ofd.acceptQueue.length === 0) { if (proc.interactive) { if (resuming()) proc.inst.exports.asyncify_stop_rewind(); else { beginBlock("accept", () => e.ofd.acceptQueue.length > 0); return 0; } } else { setErrno(35); return -1; } }
      const conn = e.ofd.acceptQueue.shift(); if (!conn) { setErrno(35); return -1; }
      if (lenPtr) view().setUint32(lenPtr, 0, true);
      return installFd(allocFd(), { kind: "sock", domain: e.ofd.domain, rx: conn.rx, tx: conn.tx, refs: 1, sotype: 1, peerPort: conn.peerPort, localPort: e.ofd.bound && e.ofd.bound.port });
    },
    accept4: (fd, a, l, fl) => env.accept(fd, a, l),
    // SO_TYPE (0x1008) must report the socket type (SOCK_STREAM=1) — libuv's
    // uv_guess_handle and many event loops branch on it. Other options read 0.
    getsockopt: (fd, lvl, opt, valPtr, lenPtr) => {
      if (valPtr && lenPtr) {
        const n = view().getUint32(lenPtr, true);
        const e = fdEntry(fd); const o = e && e.ofd;
        let val = 0;
        if ((opt >>> 0) === 0x1008 && o) val = o.sotype || 1;          // SO_TYPE
        else if (o && o.opts && (opt in o.opts)) val = o.opts[opt];    // e.g. SO_REUSEADDR
        if (n >= 4) view().setUint32(valPtr, val, true);
      }
      return 0;
    },
    setsockopt: (fd, lvl, opt, valPtr, len) => { const e = fdEntry(fd); if (e && e.ofd && valPtr) (e.ofd.opts = e.ofd.opts || {})[opt] = view().getUint32(valPtr, true); return 0; },
    shutdown: () => 0,
    // sockaddr: AF_INET (port/addr) for inet sockets, else a minimal AF_UNIX
    // sockaddr (sa_len@0, sa_family@1=AF_UNIX — a BYTE at offset 1, FreeBSD).
    getsockname: (fd, addrPtr, lenPtr) => {
      const e = fdEntry(fd), o = e && e.ofd;
      if (addrPtr) {
        if (o && o.domain === 2) writeSockaddrIn(addrPtr, o.localPort || (o.bound && o.bound.port) || 0, o.localAddr);
        else { const cap = lenPtr ? view().getUint32(lenPtr, true) : 16; if (cap >= 2) { const m = u8(); m.fill(0, addrPtr, addrPtr + Math.min(cap, 16)); m[addrPtr] = 16; m[addrPtr + 1] = 1; } }
      }
      if (lenPtr) view().setUint32(lenPtr, 16, true);
      return 0;
    },
    getpeername: (fd, addrPtr, lenPtr) => {
      const e = fdEntry(fd), o = e && e.ofd;
      if (addrPtr && o && o.domain === 2) writeSockaddrIn(addrPtr, o.peerPort || 0, o.peerAddr);
      else return env.getsockname(fd, addrPtr, lenPtr);
      if (lenPtr) view().setUint32(lenPtr, 16, true);
      return 0;
    },
    // sendmsg/recvmsg with SCM_RIGHTS fd passing (how tmux hands its tty fd to
    // the server). FreeBSD msghdr: name@0 namelen@4 iov@8 iovlen@12
    // control@16 controllen@20 flags@24. iovec: base@0 len@4. cmsghdr:
    // len@0 level@4 type@8 data@12. SOL_SOCKET=0xffff, SCM_RIGHTS=1.
    sendmsg: (fd, msgPtr, flags) => {
      const e = fdEntry(fd); if (!e || (e.ofd.kind !== "sock" && e.ofd.kind !== "pipe")) { setErrno(9); return -1; }
      const tx = e.ofd.kind === "sock" ? e.ofd.tx : e.ofd.pipe;
      const iov = view().getUint32(msgPtr + 8, true), iovlen = view().getInt32(msgPtr + 12, true);
      let total = 0;
      for (let i = 0; i < iovlen; i++) { const base = view().getUint32(iov + i * 8, true), len = view().getUint32(iov + i * 8 + 4, true); if (len) { bufPush(tx, u8().subarray(base, base + len)); total += len; } }
      const ctrl = view().getUint32(msgPtr + 16, true), ctrllen = view().getUint32(msgPtr + 20, true);
      if (ctrl && ctrllen >= 12) { const ctype = view().getInt32(ctrl + 8, true); if (ctype === 1) { const nfds = (view().getUint32(ctrl, true) - 12) >> 2; for (let i = 0; i < nfds; i++) { const gfd = view().getInt32(ctrl + 12 + i * 4, true); const fe = fdEntry(gfd); if (fe) { fe.ofd.refs++; tx.ancFds.push(fe.ofd); } } } }
      return total;
    },
    recvmsg: (fd, msgPtr, flags) => {
      const e = fdEntry(fd); if (!e || (e.ofd.kind !== "sock" && e.ofd.kind !== "pipe")) { setErrno(9); return -1; }
      const rx = e.ofd.kind === "sock" ? e.ofd.rx : e.ofd.pipe;
      const iov = view().getUint32(msgPtr + 8, true), iovlen = view().getInt32(msgPtr + 12, true);
      let got = 0;
      for (let i = 0; i < iovlen; i++) { const base = view().getUint32(iov + i * 8, true), len = view().getUint32(iov + i * 8 + 4, true); const tmp = new Uint8Array(len); const n = bufDrain(rx, tmp, len); u8().set(tmp.subarray(0, n), base); got += n; if (n < len) break; }
      const ctrl = view().getUint32(msgPtr + 16, true), ctrllen = view().getUint32(msgPtr + 20, true);
      if (ctrl && ctrllen >= 16 && rx.ancFds.length) { const ofd = rx.ancFds.shift(); const newfd = installFd(allocFd(), ofd, false); view().setUint32(ctrl, 16, true); view().setInt32(ctrl + 4, 0xffff, true); view().setInt32(ctrl + 8, 1, true); view().setInt32(ctrl + 12, newfd, true); view().setUint32(msgPtr + 20, 16, true); } else { view().setUint32(msgPtr + 20, 0, true); }
      return got;
    },
    setsid: () => { proc.sid = proc.pid; proc.pgid = proc.pid; return proc.pid; },
    daemon: () => 0,
    // pty: master<->slave bidirectional channel (like socketpair). The pane's
    // shell runs on the slave; tmux reads/writes the master.
    openpty: (masterPtr, slavePtr, namePtr, termPtr, winPtr) => { const pty = newPty(); const mfd = installFd(allocFd(), { kind: "sock", rx: pty.toMaster, tx: pty.toSlave, refs: 1, isPty: true, pty, ptyMaster: true }); const sfd = installFd(allocFd(), { kind: "sock", rx: pty.toSlave, tx: pty.toMaster, refs: 1, isPty: true, pty, ptyMaster: false }); view().setUint32(masterPtr, mfd, true); view().setUint32(slavePtr, sfd, true); return 0; },
    posix_openpt: (flags) => { const pty = newPty(); const id = mgr.nextIno++; if (!mgr.ptys) mgr.ptys = new Map(); mgr.ptys.set(id, pty); return installFd(allocFd(), { kind: "sock", rx: pty.toMaster, tx: pty.toSlave, refs: 1, isPty: true, ptyId: id, pty, ptyMaster: true }); },
    grantpt: () => 0, unlockpt: () => 0,
    ptsname: (fd) => { const e = fdEntry(fd); return putStr("/dev/pts/" + ((e && e.ofd.ptyId) || 0)); },
    ptsname_r: (fd, buf, n) => { const e = fdEntry(fd); u8().set(enc.encode("/dev/pts/" + ((e && e.ofd.ptyId) || 0) + "\0"), buf); return 0; },
    // login_tty: release the old 0/1/2 first (dup2 semantics) — plain map
    // overwrite leaks their refcounts and an inherited outer-pty slave then
    // never EOFs its master (same bug forkpty's child fixup had).
    login_tty: (fd) => { const e = fdEntry(fd); if (e) { proc.sid = proc.pid; e.ofd.refs += 3; for (const stdFd of [0, 1, 2]) { if (proc.pio.fds.has(stdFd)) closeFd(stdFd); proc.pio.fds.set(stdFd, { ofd: e.ofd, cloexec: false }); } } return 0; },
    glob: (pat, flags, errfn, pglob) => { if (pglob) { view().setUint32(pglob, 0, true); view().setUint32(pglob + 16, 0, true); } return -3; /* GLOB_NOMATCH */ }, globfree: () => 0,
    fnmatch: (patPtr, strPtr) => { const pat = cstr(patPtr), str = cstr(strPtr); const re = new RegExp("^" + pat.replace(/[.+^${}()|[\]\\]/g, "\\$&").replace(/\*/g, ".*").replace(/\?/g, ".") + "$"); return re.test(str) ? 0 : 1; },
    poll: (fdsPtr, nfds, timeout) => {
      const compute = () => { let cnt = 0; for (let i = 0; i < nfds; i++) { const base = fdsPtr + i * 8; const fd = view().getInt32(base, true); const ev = view().getUint16(base + 4, true); const re = pollOne(fd, ev); view().setUint16(base + 6, re, true); if (re) cnt++; } return cnt; };
      if (proc.interactive && resuming()) proc.inst.exports.asyncify_stop_rewind();
      if (deliverPendingSignals()) { setErrno(4); return -1; } // EINTR — a signal (e.g. SIGWINCH) interrupted poll
      const cnt = compute();
      if (cnt > 0 || timeout === 0) return cnt;
      if (proc.timedOut) { proc.timedOut = false; return cnt; } // the scheduler fast-forwarded our timeout
      if (proc.interactive) { beginBlock("poll", () => compute() > 0, timeout > 0 ? mgr.now() + timeout : Infinity); return 0; } // suspend until ready or timeout
      return 0;
    },
    ppoll: (fdsPtr, nfds) => env.poll(fdsPtr, nfds, -1),
    // pselect = select with a timespec + sigmask. The engine delivers
    // signals at safe points regardless of the mask, so only the
    // timeout needs translating (nsec → usec into a cached scratch
    // timeval — fzy calls this per keystroke). fzy's picker waits for
    // /dev/tty input here.
    pselect: (nfds, rPtr, wPtr, ePtr, tsPtr, maskPtr) => {
      let tvPtr = 0;
      if (tsPtr) {
        if (!proc.pselectScratch) proc.pselectScratch = state.alloc(8);
        tvPtr = proc.pselectScratch;
        const sec = view().getUint32(tsPtr, true), nsec = view().getUint32(tsPtr + 4, true);
        view().setUint32(tvPtr, sec, true);
        view().setUint32(tvPtr + 4, Math.floor(nsec / 1000), true);
      }
      return env.select(nfds, rPtr, wPtr, ePtr, tvPtr);
    },
    select: (nfds, rPtr, wPtr, ePtr, tvPtr) => {
      const reqRead = [], reqWrite = [];
      const collect = (setPtr, arr) => { if (!setPtr) return; for (let fd = 0; fd < nfds; fd++) { const word = setPtr + (fd >> 5) * 4; if (view().getUint32(word, true) & (1 << (fd & 31))) arr.push(fd); } };
      collect(rPtr, reqRead); collect(wPtr, reqWrite);
      const ready = () => { const rset = new Set(), wset = new Set(); for (const fd of reqRead) { const e = fdEntry(fd); if (e && ofdReadable(e.ofd)) rset.add(fd); } for (const fd of reqWrite) { const e = fdEntry(fd); if (e && ofdWritable(e.ofd)) wset.add(fd); } return { c: rset.size + wset.size, rset, wset }; };
      const writeResult = (rset, wset) => {
        const apply = (setPtr, keep) => { if (!setPtr) return; for (let fd = 0; fd < nfds; fd++) { const word = setPtr + (fd >> 5) * 4; const bit = 1 << (fd & 31); const cur = view().getUint32(word, true); if ((cur & bit) && !keep.has(fd)) view().setUint32(word, cur & ~bit, true); } };
        apply(rPtr, rset); apply(wPtr, wset); if (ePtr) for (let w = 0; w < ((nfds + 31) >> 5); w++) view().setUint32(ePtr + w * 4, 0, true);
      };
      if (proc.interactive && resuming()) proc.inst.exports.asyncify_stop_rewind();
      if (deliverPendingSignals()) { setErrno(4); return -1; } // EINTR
      const r = ready();
      let timeout0 = false, deadline = Infinity;
      if (tvPtr) { const sec = view().getUint32(tvPtr, true), usec = view().getUint32(tvPtr + 4, true); timeout0 = sec === 0 && usec === 0; deadline = mgr.now() + sec * 1000 + usec / 1000; }
      if (r.c > 0 || timeout0) { writeResult(r.rset, r.wset); return r.c; }
      if (proc.timedOut) { proc.timedOut = false; writeResult(new Set(), new Set()); return 0; }
      if (proc.interactive) { beginBlock("select", () => ready().c > 0, deadline); return 0; }
      writeResult(new Set(), new Set()); return 0;
    },
    // kqueue/kevent — the BSD event loop libuv uses (so nvim's main loop runs).
    // struct kevent (wasm32): ident@0 filter@4(i16) flags@6(u16) fflags@8
    // data@16(i64) udata@24 ext@32, sizeof 64. Filters: READ=-1 WRITE=-2
    // TIMER=-7 SIGNAL=-6 USER=-11. Actions: EV_ADD=1 DELETE=2 ENABLE=4
    // DISABLE=8 ONESHOT=0x10 CLEAR=0x20.
    kqueue: () => installFd(allocFd(), { kind: "kqueue", filters: new Map(), refs: 1 }),
    kqueue1: () => env.kqueue(), kqueuex: () => env.kqueue(),
    kevent: (kq, changelist, nchanges, eventlist, nevents, timeoutPtr) => {
      const e = fdEntry(kq); if (!e || e.ofd.kind !== "kqueue") { setErrno(9); return -1; }
      const filters = e.ofd.filters;
      const KEV = 64;
      // 1) apply the changelist
      for (let i = 0; i < nchanges; i++) {
        const base = changelist + i * KEV;
        const ident = view().getUint32(base, true), filter = view().getInt16(base + 4, true), flags = view().getUint16(base + 6, true);
        const fflags = view().getUint32(base + 8, true), data = view().getUint32(base + 16, true), udata = view().getUint32(base + 24, true);
        const key = filter + ":" + ident;
        if (flags & 0x2) filters.delete(key); // EV_DELETE
        else if (flags & 0x1) { const f = { ident, filter, fflags, udata, data, oneshot: !!(flags & 0x10), enabled: true, deadline: filter === -7 ? mgr.now() + (data || 0) : 0 }; filters.set(key, f); } // EV_ADD
        else if (flags & 0x4) { const f = filters.get(key); if (f) f.enabled = true; }   // EV_ENABLE
        else if (flags & 0x8) { const f = filters.get(key); if (f) f.enabled = false; }  // EV_DISABLE
      }
      const fired = () => { const out = []; for (const f of filters.values()) { if (!f.enabled) continue;
        if (f.filter === -1) { const fe = fdEntry(f.ident); if (fe && ofdReadable(fe.ofd)) out.push(f); }        // EVFILT_READ
        else if (f.filter === -2) { const fe = fdEntry(f.ident); if (fe && ofdWritable(fe.ofd)) out.push(f); }   // EVFILT_WRITE
        else if (f.filter === -5) { const t = mgr.procs.find((q) => q.pid === f.ident); if (t && t.exited) out.push(f); } // EVFILT_PROC — how libuv on kqueue learns a spawned child died (NOTE_EXIT); without it nvim's TUI never notices the embedded server exiting on :q
        else if (f.filter === -7) { if (mgr.now() >= f.deadline) out.push(f); }                                  // EVFILT_TIMER
        else if (f.filter === -11) { if (f.triggered) out.push(f); }                                             // EVFILT_USER
      } return out; };
      const writeEvents = (list) => { const n = Math.min(list.length, nevents); for (let i = 0; i < n; i++) { const f = list[i]; const base = eventlist + i * KEV; u8().fill(0, base, base + KEV); view().setUint32(base, f.ident, true); view().setInt16(base + 4, f.filter, true); view().setUint16(base + 6, f.oneshot ? 0x10 : 0, true); view().setUint32(base + 8, f.fflags, true); view().setUint32(base + 16, 1, true); view().setUint32(base + 24, f.udata, true); if (f.filter === -7) { f.deadline = mgr.now() + (f.data || 0); } if (f.oneshot) filters.delete(f.filter + ":" + f.ident); } return n; };
      const nearestTimer = () => { let d = Infinity; for (const f of filters.values()) if (f.enabled && f.filter === -7) d = Math.min(d, f.deadline); return d; };
      let block0 = false, deadline = Infinity;
      if (timeoutPtr) { const sec = view().getUint32(timeoutPtr, true), nsec = view().getUint32(timeoutPtr + 4, true); block0 = sec === 0 && nsec === 0; deadline = mgr.now() + sec * 1000 + nsec / 1e6; }
      if (proc.interactive && resuming()) proc.inst.exports.asyncify_stop_rewind();
      if (deliverPendingSignals()) { setErrno(4); return -1; } // EINTR
      const list = fired();
      if (list.length || block0 || nevents === 0) return writeEvents(list);
      if (proc.timedOut) { proc.timedOut = false; return writeEvents(fired()); }
      if (proc.interactive) { beginBlock("kevent", () => fired().length > 0, Math.min(deadline, nearestTimer())); return 0; }
      return 0;
    },
    // forkpty = openpty + fork + login_tty in one bridge (nvim's :terminal).
    // First entry (asyncify NORMAL): create the pty pair, write *amaster and
    // the slave name, stash the two fds, then unwind through the ordinary
    // fork path. BOTH sides re-execute this bridge while REWINDING (the wasm
    // replays down to the call); mgr.fork() stops the rewind and returns
    // which side we are, and each side runs its own fixup exactly once — the
    // child wires the slave as stdio + controlling tty (login_tty semantics)
    // and drops both original fds, the parent drops the slave and keeps the
    // master. doForkSched hands the child its copy of the stash.
    forkpty: (amasterPtr, namePtr, termPtr, winPtr) => {
      if (!proc.interactive) { setErrno(78); return -1; } // needs the scheduler's fork
      const wasResuming = resuming();
      if (!wasResuming) {
        const pty = newPty();
        const id = mgr.nextIno++;
        if (!mgr.ptys) mgr.ptys = new Map();
        mgr.ptys.set(id, pty);
        // caller-supplied termios/winsize apply to the slave, like a real
        // forkpty. FreeBSD termios: c_oflag@4, c_lflag@12; winsize: rows@0
        // cols@2 (u16). The size lives on the SHARED pty object so the
        // child's TIOCGWINSZ on the slave sees what the master set.
        if (termPtr) { pty.termios.oflag = view().getUint32(termPtr + 4, true); pty.termios.lflag = view().getUint32(termPtr + 12, true); }
        if (winPtr) pty.winsize = { rows: view().getUint16(winPtr, true), cols: view().getUint16(winPtr + 2, true) };
        const masterFd = installFd(allocFd(), { kind: "sock", rx: pty.toMaster, tx: pty.toSlave, refs: 1, isPty: true, ptyId: id, pty, ptyMaster: true });
        const slaveFd = installFd(allocFd(), { kind: "sock", rx: pty.toSlave, tx: pty.toMaster, refs: 1, isPty: true, ptyId: id, pty, ptyMaster: false });
        if (amasterPtr) view().setUint32(amasterPtr, masterFd, true);
        if (namePtr) u8().set(enc.encode("/dev/pts/" + id + "\0"), namePtr);
        proc.forkPtyPending = { masterFd, slaveFd };
      }
      const r = mgr.fork(proc);
      if (!wasResuming) { if (r < 0) proc.forkPtyPending = null; return r; } // unwinding (0, ignored) or fork failed
      const fp = proc.forkPtyPending; proc.forkPtyPending = null;
      if (fp) {
        if (r === 0) {
          // child: new session, slave is the controlling tty on fds 0/1/2.
          // RELEASE the inherited 0/1/2 first (dup2 semantics) — overwriting
          // the map entries leaks their refcounts, and a leaked outer pty
          // slave (nvim in a tmux pane inherits the PANE's slave as stderr)
          // keeps that pane's master from ever seeing EOF, so tmux never
          // tears the pane down after its shell exits.
          proc.sid = proc.pid; proc.pgid = proc.pid;
          const slaveEntry = proc.pio.fds.get(fp.slaveFd);
          if (slaveEntry) {
            slaveEntry.ofd.refs += 3;
            for (const stdFd of [0, 1, 2]) {
              if (proc.pio.fds.has(stdFd)) closeFd(stdFd);
              proc.pio.fds.set(stdFd, { ofd: slaveEntry.ofd, cloexec: false });
            }
          }
          closeFd(fp.slaveFd);
          closeFd(fp.masterFd);
          proc.viaForkpty = true;
        } else if (r > 0) {
          closeFd(fp.slaveFd); // parent keeps only the master
        }
      }
      return r;
    },
    ttyname: () => 0, ttyname_r: (fd, buf, n) => { u8().set(enc.encode("/dev/tty\0"), buf); return 0; },
    // termios: report success so zsh/curses set raw mode; we always deliver
    // exactly the bytes xterm sends, so cooked/raw distinction is a no-op.
    // FreeBSD termios: c_iflag@0, c_oflag@4, c_cflag@8, c_lflag@12, c_cc@16.
    // termios is PER terminal: a pty fd carries its own (so a pane shell keeps a
    // cooked pty while tmux runs the outer tty raw); every other tty fd is the
    // shared outer terminal (mgr.tty).
    // c_oflag@4 (OPOST|ONLCR) is tracked too: it drives the output discipline
    // (ttyOpost in ofdWrite). Default cooked: OPOST|ONLCR set.
    tcgetattr: (fd, tp) => { const e = fdEntry(fd); const t = (e && e.ofd && e.ofd.pty) ? e.ofd.pty.termios : (proc.tty || mgr.tty); if (tp) { u8().fill(0, tp, tp + 44); view().setUint32(tp + 4, (t && t.oflag != null ? t.oflag : (OPOST | ONLCR)), true); view().setUint32(tp + 12, (t ? t.lflag : (0x8 | 0x2 | 0x100 | 0x80 | 0x400)), true); } return 0; },
    tcsetattr: (fd, action, tp) => { if (!tp) return 0; const e = fdEntry(fd); const oflag = view().getUint32(tp + 4, true); const lflag = view().getUint32(tp + 12, true); const t = (e && e.ofd && e.ofd.pty) ? e.ofd.pty.termios : (proc.tty || mgr.tty); if (t) { t.lflag = lflag; t.oflag = oflag; } return 0; },
    cfmakeraw: (tp) => { if (tp) { view().setUint32(tp + 4, view().getUint32(tp + 4, true) & ~OPOST, true); view().setUint32(tp + 12, view().getUint32(tp + 12, true) & ~(0x8 | 0x100 | 0x80 | 0x400), true); } return 0; },
    cfsetispeed: () => 0, cfsetospeed: () => 0, cfgetispeed: () => 0, cfgetospeed: () => 0, tcflush: () => 0, tcdrain: () => 0, tcflow: () => 0, tcsendbreak: () => 0,
    tcgetpgrp: () => proc.pgid, tcsetpgrp: () => 0, tcgetsid: () => proc.sid,
    // ioctl(fd, request, ...): the guest declares ioctl VARIADIC
    // (`int ioctl(int, unsigned long, ...)`, FreeBSD <sys/ioccom.h>), so clang
    // passes the third argument through a va-list buffer. The wasm `argp` is a
    // pointer to that buffer; the caller's real argument (a `struct winsize *`,
    // `int *`, …) is the FIRST entry stored in it. Dereference once to recover
    // it — writing into `argp` itself only corrupts the va-buffer and leaves the
    // caller's struct untouched. That bug made TIOCGWINSZ deliver a 0x0 winsize:
    // tmux read it as failure and fell back to 80x24, so every full-screen app
    // rendered at the wrong size (status bar stranded mid-screen) regardless of
    // the real terminal dimensions.
    ioctl: (fd, req, argp) => {
      const arg = argp ? view().getUint32(argp, true) : 0; // caller's real pointer
      if ((req >>> 0) === 0x8004667e) { // FIONBIO — *arg != 0 sets O_NONBLOCK
        const e = fdEntry(fd); if (!e) { setErrno(9); return -1; }
        const on = arg ? view().getInt32(arg, true) : 0;
        e.flags = on ? ((e.flags | 0) | 0x4) : ((e.flags | 0) & ~0x4);
        return 0;
      }
      if ((req >>> 0) === 0x40087468) { // TIOCGWINSZ — report the terminal size
        // A pane pty reports the size tmux set on it (stored on the ofd);
        // every other terminal fd reports the real terminal (mgr.tty). This
        // consistency matters: tmux sets a pane's winsize then reads it back,
        // and returning the main terminal size for a pane confused its sizing.
        // winsize resolution: this fd's own record, else the SHARED pty
        // record (so a forkpty child's TIOCGWINSZ on the slave sees the size
        // the master set — nvim resizes its :terminal via the master), else
        // the real terminal.
        const e = fdEntry(fd); const ws = (e && e.ofd && e.ofd.winsize) || (e && e.ofd && e.ofd.pty && e.ofd.pty.winsize); const t = ws || proc.tty || mgr.tty || { rows: 24, cols: 80 };
        if (arg) { view().setUint16(arg, t.rows, true); view().setUint16(arg + 2, t.cols, true); view().setUint16(arg + 4, 0, true); view().setUint16(arg + 6, 0, true); }
        return 0;
      }
      if ((req >>> 0) === 0x80087467) { // TIOCSWINSZ — remember the size on this fd's ofd (e.g. a pane pty)
        const e = fdEntry(fd); if (e && e.ofd && arg) { const ws = { rows: view().getUint16(arg, true), cols: view().getUint16(arg + 2, true) }; e.ofd.winsize = ws; if (e.ofd.pty) e.ofd.pty.winsize = ws; }
        return 0;
      }
      return 0;
    },
    // POSIX: the EMPTY path is ENOENT, never the cwd. vfsPath("") resolves to
    // the cwd, and stat("") == "a directory" made netrw's FileExplorer autocmd
    // treat nvim's unnamed startup buffer (isdirectory(expand("<amatch>")) ==
    // isdirectory("")) as a directory — bare `nvim` opened a netrw listing of
    // / instead of the intro screen.
    stat: (pathPtr, statBuf) => { const raw = cstr(pathPtr); if (!raw) { setErrno(2); return -1; } const p = mgr.vfsPath(raw, proc.pio.cwd); if (p === "/dev/null") { fillStat(state, statBuf, { type: "char" }); return 0; } const node = mgr.vfs[p]; if (!node) { setErrno(2); return -1; } fillStat(state, statBuf, node); return 0; },
    lstat: (pathPtr, statBuf) => env.stat(pathPtr, statBuf),
    fstatat: (dfd, pathPtr, statBuf) => {
      const name = cstr(pathPtr);
      const base = proc.pio.dirs.get(dfd); const e = fdEntry(dfd);
      const baseDir = name.startsWith("/") ? "" : base ? base.path : (e && e.ofd && e.ofd.path) ? e.ofd.path : proc.pio.cwd;
      const full = name.startsWith("/") ? name : (baseDir === "/" ? "" : baseDir) + "/" + name;
      const node = mgr.vfs[mgr.vfsPath(full, proc.pio.cwd)];
      if (!node) { setErrno(2); return -1; }
      fillStat(state, statBuf, node); return 0;
    },
    fstat: (fd, statBuf) => {
      const e = fdEntry(fd);
      if (e) {
        const ofd = e.ofd;
        // Report the real file type so fstat()/S_ISSOCK/S_ISFIFO work (libuv's
        // uv_guess_handle, socket-vs-tty probes). A pty is a tty (char); a
        // plain unix socket / socketpair end is a socket; a pipe is a FIFO.
        const node = (ofd.kind === "file" || ofd.kind === "dir") ? ofd.node
          : ofd.kind === "sock" ? { type: ofd.isPty ? "char" : "sock" }
          : ofd.kind === "pipe" ? { type: "pipe" }
          : { type: "char" };
        fillStat(state, statBuf, node); return 0;
      }
      const d = proc.pio.dirs.get(fd); if (d) { fillStat(state, statBuf, d.node); return 0; } return -1;
    },
    fstatfs: (fd, buf) => { u8().fill(0, buf, buf + 256); return 0; },
    statfs: (p, buf) => { u8().fill(0, buf, buf + 256); return 0; },
    unlink: (pathPtr) => { const raw = cstrBounded(pathPtr); if (raw === null) { setErrno(14); return -1; } const path = mgr.vfsPath(raw, proc.pio.cwd); if (!mgr.vfs[path]) { setErrno(2); return -1; } delete mgr.vfs[path]; const slash = path.lastIndexOf("/"); const parent = mgr.vfs[slash === 0 ? "/" : path.slice(0, slash)]; const name = path.slice(slash + 1); if (parent && parent.entries) { const i = parent.entries.indexOf(name); if (i >= 0) parent.entries.splice(i, 1); } return 0; },
    // opendir/fdopendir return a real guest DIR* (a small guest-memory struct
    // whose dd_fd at offset 0 is a valid directory fd). The FreeBSD dirfd() is
    // an inline macro that reads dirp->dd_fd directly from guest memory, so
    // returning a bare integer handle made _dirfd() read garbage — which broke
    // fts (ls -alrt, find, du): fts_safe_changedir fstats _dirfd(dirp) and
    // compares st_dev/st_ino, so a garbage fd made every entry FTS_NS ("Error
    // 2"). Backing the DIR* with a real fd (kind "dir") makes fstat()/fchdir()
    // on dd_fd resolve to this directory with matching dev/ino.
    opendir: (pathPtr) => {
      const path = mgr.vfsPath(cstr(pathPtr), proc.pio.cwd); const node = mgr.vfs[path];
      if (!node || node.type !== "dir") { setErrno(2); return 0; }
      const fd = installFd(allocFd(), { kind: "dir", node, path, refs: 1 }, false);
      const dirp = state.alloc(16, 4); view().setInt32(dirp, fd, true); // dd_fd @ 0
      const d = { node, names: [".", "..", ...node.entries], idx: 0, direntBuf: state.alloc(280, 8), path, fd, dirp };
      proc.pio.dirs.set(dirp, d); proc.pio.dirs.set(fd, d);
      return dirp;
    },
    fdopendir: (fd) => {
      const e = fdEntry(fd);
      if (!e || e.ofd.kind !== "dir") { setErrno(9); return 0; }
      const dirp = state.alloc(16, 4); view().setInt32(dirp, fd, true); // reuse caller's fd as dd_fd
      const d = { node: e.ofd.node, names: [".", "..", ...e.ofd.node.entries], idx: 0, direntBuf: state.alloc(280, 8), path: e.ofd.path, fd, dirp };
      proc.pio.dirs.set(dirp, d); proc.pio.dirs.set(fd, d);
      return dirp;
    },
    readdir: (dirp) => {
      const d = proc.pio.dirs.get(dirp);
      if (!d || d.idx >= d.names.length) return 0;
      const name = d.names[d.idx++];
      const child = name === "." || name === ".." ? d.node : mgr.vfs[(d.path === "/" ? "" : d.path) + "/" + name];
      fillDirent(state, d.direntBuf, name, child && child.type === "dir", child && child.ino);
      return d.direntBuf;
    },
    closedir: (dirp) => { const d = proc.pio.dirs.get(dirp); if (d) { proc.pio.dirs.delete(d.dirp); proc.pio.dirs.delete(d.fd); proc.pio.fds.delete(d.fd); } else proc.pio.dirs.delete(dirp); return 0; },
    dirfd: (dirp) => { const d = proc.pio.dirs.get(dirp); return d ? d.fd : view().getInt32(dirp, true); },
    umask: (m) => { const old = proc.pio.umask; proc.pio.umask = m & 0o777; return old; }, getcwd: (buf, n) => { u8().set(enc.encode(proc.pio.cwd + "\0"), buf); return buf; },
    chdir: (pathPtr) => { const p = mgr.vfsPath(cstr(pathPtr), proc.pio.cwd); if (mgr.vfs[p] && mgr.vfs[p].type === "dir") { proc.pio.cwd = p; return 0; } setErrno(2); return -1; },
    // fts descends via fchdir(dirfd) then lstats entries by relative name,
    // so fchdir MUST move the cwd or every entry's lstat misses.
    fchdir: (fd) => { const d = proc.pio.dirs.get(fd); const e = fdEntry(fd); const path = d ? d.path : (e && e.ofd && e.ofd.path); if (path) { proc.pio.cwd = path; return 0; } return -1; },
    readlink: (pathPtr, buf, bufsize) => {
      const node = mgr.vfs[mgr.vfsPath(cstr(pathPtr), proc.pio.cwd)];
      if (!node || node.type !== "symlink") { setErrno(22); return -1; } // EINVAL — not a symlink
      const b = enc.encode(node.target); const n = Math.min(b.length, bufsize | 0);
      u8().set(b.subarray(0, n), buf); return n; // readlink does NOT NUL-terminate
    },
    realpath: (pathPtr, out) => { const p = mgr.vfsPath(cstr(pathPtr), proc.pio.cwd); u8().set(enc.encode(p + "\0"), out); return out; },
    // Signals: we record the SIGCHLD handler (FreeBSD sa_handler is a wasm
    // function-pointer at offset 0 of struct sigaction) and DELIVER it when a
    // forked child exits — otherwise an interactive shell's wait loop
    // (sigprocmask/sigsuspend waiting for SIGCHLD) spins forever, because in
    // our cooperative model the child already ran to completion.
    // sigaction: sa_handler is at struct offset 0 (a wasm function-table index;
    // 0=SIG_DFL, 1=SIG_IGN). Report the previous action into oldact, then
    // install the new one.
    sigaction: (sig, actPtr, oldactPtr) => {
      proc.sigHandlers = proc.sigHandlers || {};
      const old = proc.sigHandlers[sig] || 0;
      if (oldactPtr) { u8().fill(0, oldactPtr, oldactPtr + 24); view().setUint32(oldactPtr, old >>> 0, true); }
      if (actPtr) proc.sigHandlers[sig] = view().getUint32(actPtr, true);
      return 0;
    },
    // raise(sig): deliver a signal to self — run the installed handler now
    // (synchronous, like the real raise(3)). Default/ignore are no-ops here.
    raise: (sig) => { runSignalHandler(proc, sig); return 0; },
    signal: (sig, handler) => { proc.sigHandlers = proc.sigHandlers || {}; const prev = proc.sigHandlers[sig] || 0; proc.sigHandlers[sig] = handler; return prev; },
    // FreeBSD sigset_t is 16 bytes (4 x uint32, 128 bits). Signal N occupies
    // bit (N-1): word (N-1)>>5, bit (N-1)&31.
    sigemptyset: (setPtr) => { if (setPtr) for (let i = 0; i < 4; i++) view().setUint32(setPtr + i * 4, 0, true); return 0; },
    sigfillset: (setPtr) => { if (setPtr) for (let i = 0; i < 4; i++) view().setUint32(setPtr + i * 4, 0xffffffff, true); return 0; },
    sigaddset: (setPtr, sig) => { if (setPtr && sig > 0) { const w = setPtr + ((sig - 1) >> 5) * 4; view().setUint32(w, (view().getUint32(w, true) | (1 << ((sig - 1) & 31))) >>> 0, true); } return 0; },
    sigdelset: (setPtr, sig) => { if (setPtr && sig > 0) { const w = setPtr + ((sig - 1) >> 5) * 4; view().setUint32(w, (view().getUint32(w, true) & ~(1 << ((sig - 1) & 31))) >>> 0, true); } return 0; },
    sigismember: (setPtr, sig) => { if (!setPtr || sig <= 0) return 0; return (view().getUint32(setPtr + ((sig - 1) >> 5) * 4, true) & (1 << ((sig - 1) & 31))) ? 1 : 0; },
    // sigprocmask: maintain a per-process blocked-signal mask so SIG_BLOCK /
    // SIG_UNBLOCK / SIG_SETMASK round-trip (ssh saves and restores it every
    // channel-loop iteration).
    sigprocmask: (how, setPtr, oldPtr) => {
      deliverChildSignals();
      const m = proc.sigMask || (proc.sigMask = [0, 0, 0, 0]);
      if (oldPtr) for (let i = 0; i < 4; i++) view().setUint32(oldPtr + i * 4, m[i] >>> 0, true);
      if (setPtr) {
        const s = [0, 1, 2, 3].map((i) => view().getUint32(setPtr + i * 4, true) >>> 0);
        for (let i = 0; i < 4; i++) m[i] = (how === 2 ? (m[i] & ~s[i]) : how === 3 ? s[i] : (m[i] | s[i])) >>> 0;
      }
      return 0;
    },
    sigsuspend: () => {
      if (proc.interactive && resuming()) proc.inst.exports.asyncify_stop_rewind();
      deliverChildSignals();
      const running = () => mgr.procs.some((p) => p.ppid === proc.pid && !p.exited && !p.reaped);
      const undelivered = () => mgr.procs.some((p) => p.ppid === proc.pid && p.exited && !p.sigchldDelivered);
      if (proc.interactive && running() && !undelivered()) { beginBlock("sigsuspend", () => undelivered() || !running()); return 0; }
      setErrno(4); return -1; // EINTR
    },
    sigwait: (setPtr, sigPtr) => { deliverChildSignals(); if (sigPtr) view().setUint32(sigPtr, 20, true); return 0; },
    alarm: () => 0,
    // kill: if the target installed a handler for sig, deliver to the handler
    // (run now for self, queue for others) instead of terminating; SIG_IGN is a
    // no-op; otherwise fall back to the default terminate action. sig 0 is just
    // an existence check.
    kill: (pid, sig) => {
      const target = pid === proc.pid ? proc : mgr.procs.find((p) => p.pid === pid && !p.exited);
      if (!target) { setErrno(3); return -1; } // ESRCH
      if (sig === 0) return 0;
      const action = runSignalHandler(target, sig);
      if (action === "default") return mgr.kill(pid, sig);
      return 0;
    },
    killpg: () => 0, setpgid: (pid, pgid) => { const p = pid ? mgr.procs.find((q) => q.pid === pid) : proc; if (p) p.pgid = pgid || p.pid; return 0; }, getlogin: () => 0,
    // getrlimit MUST fill the struct (rlim_cur@0, rlim_max@8 — rlim_t is 64-bit
    // even on wasm32/i386). Returning 0 without writing left the caller reading
    // stack garbage as the limit: nvim's pty spawn loops fcntl(F_SETFD) up to
    // RLIMIT_NOFILE and span 2.9 MILLION fds before the runaway guard killed
    // it. NOFILE matches sysconf(_SC_OPEN_MAX); everything else is "infinity".
    getrlimit: (resource, rlp) => { if (rlp) { const v = resource === 8 ? 1024n : 0x7fffffffffffffffn; view().setBigUint64(rlp, v, true); view().setBigUint64(rlp + 8, v, true); } return 0; },
    setrlimit: () => 0,
    // getrusage: fill ru_utime (tv_sec@0, tv_usec@4). The single-thread engine
    // has no real CPU accounting, but callers expect usage to be MONOTONIC
    // (utime-after-work > utime-before), so advance a per-process counter each
    // call. The rest of struct rusage stays zeroed.
    getrusage: (who, buf) => {
      proc.pio.ruUsec = (proc.pio.ruUsec || 0) + 1000;
      if (buf) { u8().fill(0, buf, buf + 144); view().setUint32(buf, Math.floor(proc.pio.ruUsec / 1e6), true); view().setUint32(buf + 4, proc.pio.ruUsec % 1e6, true); }
      return 0;
    },
    // lpathconf/pathconf/fpathconf: we don't model per-path limits; report the
    // "unknown name" failure faithfully (return -1 AND set errno=EINVAL) so a
    // caller that checks errno after -1 (fts, ls) sees EINVAL, not stale errno.
    lpathconf: () => { setErrno(22); return -1; },
    pathconf: () => { setErrno(22); return -1; },
    fpathconf: () => { setErrno(22); return -1; },
    gethostname: (buf, len) => { u8().set(enc.encode("yos-web".slice(0, len - 1) + "\0"), buf); return 0; },
    // Virtual clock: real time + mgr.clockSkew. The scheduler advances
    // clockSkew when it fast-forwards a timed poll/select (so libevent's
    // timer deadlines, which it computes from clock_gettime, actually
    // elapse). nanosleep/usleep advance it directly.
    time: (t) => { const s = Math.floor(mgr.now() / 1000); if (t) view().setUint32(t, s, true); return s; },
    gettimeofday: (tv) => { const ms = mgr.now(); if (tv) { view().setUint32(tv, Math.floor(ms / 1000), true); view().setUint32(tv + 4, Math.floor((ms % 1000) * 1000), true); } return 0; },
    clock_gettime: (id, ts) => { const ms = mgr.now(); if (ts) { view().setUint32(ts, Math.floor(ms / 1000), true); view().setUint32(ts + 4, Math.floor((ms % 1000) * 1e6), true); } return 0; },
    nanosleep: (req, rem) => { if (req) mgr.clockSkew += view().getUint32(req, true) * 1000 + view().getUint32(req + 4, true) / 1e6; if (rem) { view().setUint32(rem, 0, true); view().setUint32(rem + 4, 0, true); } return 0; },
    sleep: (s) => { mgr.clockSkew += (s | 0) * 1000; return 0; },
    // Per-process rand state (copied on fork) so a child's draws cannot
    // change the parent's sequence.
    srand: (s) => { proc.pio.rand = s >>> 0; }, rand: () => { proc.pio.rand = (Math.imul(proc.pio.rand, 1103515245) + 12345) >>> 0; return (proc.pio.rand >>> 16) & 0x7fff; },
    random: () => { proc.pio.rand = (Math.imul(proc.pio.rand, 1103515245) + 12345) >>> 0; return proc.pio.rand & 0x7fffffff; },
    // Cooperative threads: this engine has one JS thread, so a created
    // thread runs its start routine to completion inline and join returns
    // its value. Enough for create/join/mutex/once and any workload that
    // does not depend on two threads running at once. A real condvar wait
    // (which needs a second thread to signal it) still aborts — that is the
    // Worker-pool model (mt_engine.mjs), not this single-process engine.
    pthread_create: (thrPtr, attr, startIdx, arg) => {
      const table = proc.inst.exports.__indirect_function_table;
      if (!table) return 11; // EAGAIN — no callable thread table exported
      const tid = proc.pio.nextTid++;
      if (thrPtr) view().setUint32(thrPtr, tid, true);
      let rv = 0;
      try { rv = table.get(startIdx)(arg) >>> 0; }
      catch (e) { if (e && e.isThreadExit) rv = e.code | 0; else throw e; }
      proc.pio.threadRv.set(tid, rv);
      return 0;
    },
    pthread_join: (tid, retPtr) => { const rv = proc.pio.threadRv.get(tid) || 0; if (retPtr) view().setUint32(retPtr, rv, true); return 0; },
    pthread_detach: () => 0, pthread_self: () => 1, pthread_equal: (a, b) => (a === b ? 1 : 0),
    pthread_exit: (rv) => { const e = new Error("texit"); e.isThreadExit = true; e.code = rv | 0; throw e; },
    pthread_mutex_init: () => 0, pthread_mutex_lock: () => 0, pthread_mutex_unlock: () => 0, pthread_mutex_destroy: () => 0, pthread_mutex_trylock: () => 0,
    pthread_cond_init: () => 0, pthread_cond_signal: () => 0, pthread_cond_broadcast: () => 0, pthread_cond_destroy: () => 0,
    pthread_cond_wait: () => { const e = new Error("pthread_cond_wait: no concurrent threads in this engine"); e.isExit = true; e.code = 75; throw e; },
    pthread_cond_timedwait: () => -1,
    pthread_rwlock_init: () => 0, pthread_rwlock_rdlock: () => 0, pthread_rwlock_wrlock: () => 0, pthread_rwlock_unlock: () => 0, pthread_rwlock_destroy: () => 0,
    pthread_key_create: () => 0, pthread_key_delete: () => 0, pthread_setspecific: () => 0, pthread_getspecific: () => 0,
    pthread_once: (oncePtr, fnIdx) => { const seen = view().getUint32(oncePtr, true); if (!seen) { view().setUint32(oncePtr, 1, true); const table = proc.inst.exports.__indirect_function_table; if (table) table.get(fnIdx)(); } return 0; },
    pthread_attr_init: () => 0, pthread_attr_destroy: () => 0, pthread_attr_setdetachstate: () => 0, pthread_attr_setstacksize: () => 0,
    pthread_attr_getstacksize: (a, out) => { if (out) view().setUint32(out, 8 << 20, true); return 0; }, pthread_attr_setguardsize: () => 0, pthread_attr_getschedparam: () => 0, pthread_attr_setschedparam: () => 0, pthread_attr_setschedpolicy: () => 0, pthread_attr_setscope: () => 0, pthread_attr_setinheritsched: () => 0,
    pthread_mutexattr_init: () => 0, pthread_mutexattr_destroy: () => 0, pthread_mutexattr_settype: () => 0, pthread_mutexattr_setpshared: () => 0, pthread_mutexattr_setprotocol: () => 0,
    pthread_condattr_init: () => 0, pthread_condattr_destroy: () => 0, pthread_condattr_setclock: () => 0, pthread_condattr_setpshared: () => 0,
    pthread_rwlockattr_init: () => 0, pthread_rwlockattr_destroy: () => 0, pthread_rwlock_tryrdlock: () => 0, pthread_rwlock_trywrlock: () => 0,
    pthread_sigmask: () => 0, pthread_atfork: () => 0, pthread_getschedparam: () => 0, pthread_setschedparam: () => 0,
    pthread_setaffinity_np: () => 0, pthread_getaffinity_np: () => 0, pthread_setname_np: () => 0, pthread_getname_np: () => 0, pthread_set_name_np: () => 0,
    // POSIX semaphores over the cooperative model: a plain counter. sem_wait
    // blocks (asyncify) until the count is positive; sem_post bumps it. No
    // real cross-thread contention here — one runnable context at a time.
    sem_init: (sem) => { if (sem) view().setInt32(sem, view().getInt32(sem + 4, true) || 0, true); return 0; },
    sem_destroy: () => 0,
    sem_post: (sem) => { if (sem) view().setInt32(sem, view().getInt32(sem, true) + 1, true); return 0; },
    sem_trywait: (sem) => { const v = sem ? view().getInt32(sem, true) : 0; if (v > 0) { view().setInt32(sem, v - 1, true); return 0; } setErrno(35); return -1; },
    sem_wait: (sem) => { const v = sem ? view().getInt32(sem, true) : 0; if (v > 0) { view().setInt32(sem, v - 1, true); return 0; } if (proc.interactive) { if (resuming()) proc.inst.exports.asyncify_stop_rewind(); else { beginBlock("sem_wait", () => (sem ? view().getInt32(sem, true) : 1) > 0); return 0; } const v2 = view().getInt32(sem, true); if (v2 > 0) view().setInt32(sem, v2 - 1, true); return 0; } setErrno(35); return -1; },
    sem_timedwait: (sem) => env.sem_trywait(sem),

    // sysctl(CTL_KERN, KERN_PROC, ...) → stream the process table as
    // FreeBSD-i386 kinfo_proc records (768 bytes each) so ps sees real
    // processes. Two-call protocol: size query (oldp=0) then fill.
    sysctl: (namePtr, namelen, oldp, oldlenp) => {
      const mib = [];
      for (let i = 0; i < namelen; i++) mib.push(view().getInt32(namePtr + i * 4, true));
      // CTL_KERN.KERN_PROC.KERN_PROC_PATHNAME — uv_exepath()/nvim's v:progpath.
      if (mib[0] === 1 && mib[1] === 14 && mib[2] === 12) {
        const path = proc.argv[0] && proc.argv[0].includes("/") ? proc.argv[0] : "/usr/bin/" + (proc.argv[0] || "nvim");
        const bytes = enc.encode(path + "\0");
        const buflen = oldlenp ? view().getUint32(oldlenp, true) : 0;
        if (oldp && buflen >= bytes.length) u8().set(bytes, oldp);
        if (oldlenp) view().setUint32(oldlenp, bytes.length, true);
        return 0;
      }
      if (mib[0] === 1 && mib[1] === 14) { // CTL_KERN.KERN_PROC
        const selector = mib[2], pidArg = mib[3]; // PID=1, ALL=0, PROC=8
        const live = mgr.procs.filter((p) => !p.reaped && (selector !== 1 || p.pid === pidArg));
        const need = live.length * 768;
        const buflen = oldlenp ? view().getUint32(oldlenp, true) : 0;
        if (!oldp || buflen < need) {
          if (oldlenp) view().setUint32(oldlenp, need, true);
          if (!oldp) return 0;
          if (state.errnoPtr) view().setUint32(state.errnoPtr, 12, true); // ENOMEM
          return -1;
        }
        let out = oldp;
        for (const p of live) { fillKinfoProc(state, out, p); out += 768; }
        if (oldlenp) view().setUint32(oldlenp, need, true);
        return 0;
      }
      if (oldlenp) view().setUint32(oldlenp, 0, true);
      return 0;
    },
    __xuname: () => 0,
    // getpwuid/getpwnam: return a (cached, per-process) struct passwd* for the
    // single root user this engine models. FreeBSD wasm32 layout (4-byte ptrs,
    // 4-byte time_t): pw_name@0 pw_passwd@4 pw_uid@8 pw_gid@12 pw_change@16
    // pw_class@20 pw_gecos@24 pw_dir@28 pw_shell@32 pw_expire@36 pw_fields@40.
    getpwuid: () => {
      if (proc.pio.pwBuf) return proc.pio.pwBuf;
      const namep = putStr("root"), pwp = putStr("*"), classp = putStr(""), gecosp = putStr("Charlie &"), dirp = putStr("/root"), shellp = putStr("/bin/sh");
      const p = state.alloc(48, 4); u8().fill(0, p, p + 48);
      view().setUint32(p, namep, true); view().setUint32(p + 4, pwp, true);
      view().setUint32(p + 8, 0, true); view().setUint32(p + 12, 0, true);
      view().setUint32(p + 20, classp, true); view().setUint32(p + 24, gecosp, true);
      view().setUint32(p + 28, dirp, true); view().setUint32(p + 32, shellp, true);
      proc.pio.pwBuf = p; return p;
    },
    getpwnam: () => env.getpwuid(0), getgrgid: () => 0, getgrnam: () => 0,
    // readpassphrase(prompt, buf, bufsize, flags): read a passphrase into buf,
    // return buf. There is no interactive tty in batch mode, so deliver an
    // empty line (buf[0]=NUL) and return the buffer — a non-NULL result is what
    // ssh/ssh-add check before using it.
    readpassphrase: (promptPtr, buf, bufsize, flags) => {
      if (!buf || (bufsize | 0) <= 0) return 0;
      const e = fdEntry(0); // RPP_STDIN reads the line from stdin
      let i = 0;
      if (e) { const one = new Uint8Array(1); while (i < bufsize - 1) { const g = ofdRead(e.ofd, one, 1); if (g <= 0) break; if (one[0] === 10) break; u8()[buf + i++] = one[0]; } }
      u8()[buf + i] = 0;
      return buf;
    },
    // getservbyname(name, proto): resolve a well-known service to a struct
    // servent* whose s_port is in network byte order. servent (wasm32):
    // s_name@0 s_aliases@4 s_port@8(int, net order) s_proto@12.
    getservbyname: (namePtr, protoPtr) => {
      const ports = { ssh: 22, http: 80, https: 443, ftp: 21, ftpdata: 20, telnet: 23, smtp: 25, domain: 53, pop3: 110, imap: 143 };
      const port = ports[cstr(namePtr)];
      if (port === undefined) return 0;
      const namep = putStr(cstr(namePtr)), protop = putStr(protoPtr ? cstr(protoPtr) : "tcp");
      const se = state.alloc(16, 4); u8().fill(0, se, se + 16);
      view().setUint32(se, namep, true); view().setUint32(se + 4, 0, true);
      view().setInt32(se + 8, ((port & 0xff) << 8) | ((port >> 8) & 0xff), true); // htons(port)
      view().setUint32(se + 12, protop, true);
      return se;
    },
    getservbyport: () => 0,
    // getaddrinfo/getnameinfo: resolve a numeric/loopback host:port to a
    // FreeBSD sockaddr_in and back. No real DNS — a dotted-quad is parsed
    // directly and anything else defaults to 127.0.0.1, which is all the
    // sandbox can offer. sockaddr_in: sin_len@0 sin_family@1=AF_INET(2)
    // sin_port@2(net order) sin_addr@4(net order) sin_zero@8. addrinfo (32B):
    // ai_flags@0 ai_family@4 ai_socktype@8 ai_protocol@12 ai_addrlen@16
    // ai_canonname@20 ai_addr@24 ai_next@28.
    getaddrinfo: (nodePtr, servicePtr, hintsPtr, resPtr) => {
      const node = nodePtr ? cstr(nodePtr) : "127.0.0.1";
      const port = (servicePtr ? parseInt(cstr(servicePtr), 10) : 0) || 0;
      let oct = node.split(".").map((n) => parseInt(n, 10));
      if (oct.length !== 4 || oct.some((o) => isNaN(o) || o < 0 || o > 255)) oct = [127, 0, 0, 1];
      const m = u8();
      const sa = state.alloc(16, 4); m.fill(0, sa, sa + 16);
      m[sa] = 16; m[sa + 1] = 2; m[sa + 2] = (port >> 8) & 0xff; m[sa + 3] = port & 0xff;
      m[sa + 4] = oct[0]; m[sa + 5] = oct[1]; m[sa + 6] = oct[2]; m[sa + 7] = oct[3];
      const ai = state.alloc(32, 4); m.fill(0, ai, ai + 32);
      view().setUint32(ai + 4, 2, true); view().setUint32(ai + 8, 1, true); view().setUint32(ai + 12, 6, true);
      view().setUint32(ai + 16, 16, true); view().setUint32(ai + 24, sa, true);
      if (resPtr) view().setUint32(resPtr, ai, true);
      return 0;
    },
    freeaddrinfo: () => 0, gai_strerror: () => putStr("unknown error"),
    getnameinfo: (saPtr, salen, hostPtr, hostlen, servPtr, servlen, flags) => {
      const m = u8();
      if (hostPtr && hostlen > 0) { const ip = `${m[saPtr + 4]}.${m[saPtr + 5]}.${m[saPtr + 6]}.${m[saPtr + 7]}`; const b = enc.encode(ip.slice(0, hostlen - 1) + "\0"); m.set(b, hostPtr); }
      if (servPtr && servlen > 0) { const port = (m[saPtr + 2] << 8) | m[saPtr + 3]; const b = enc.encode(String(port).slice(0, servlen - 1) + "\0"); m.set(b, servPtr); }
      return 0;
    },
    // *_r forms: report "no entry" (set *result=NULL, return 0) so nvim falls
    // back to $HOME/$USER from the environment instead of the passwd db.
    getpwuid_r: (uid, pwd, buf, buflen, resultPtr) => { if (resultPtr) view().setUint32(resultPtr, 0, true); return 0; },
    getpwnam_r: (name, pwd, buf, buflen, resultPtr) => { if (resultPtr) view().setUint32(resultPtr, 0, true); return 0; },
    getgrgid_r: (gid, grp, buf, buflen, resultPtr) => { if (resultPtr) view().setUint32(resultPtr, 0, true); return 0; },
    getgrnam_r: (name, grp, buf, buflen, resultPtr) => { if (resultPtr) view().setUint32(resultPtr, 0, true); return 0; },
    // dynamic loading: no .so support in the engine — fail cleanly. nvim only
    // needs this for C lua modules / treesitter parsers, not core startup.
    dlopen: () => 0, dlsym: () => 0, dlclose: () => 0, dlerror: () => putStr("dlopen unsupported"),
    dup3: (oldfd, newfd, flags) => { const r = env.dup2(oldfd, newfd); if (r >= 0 && (flags & 0x100000)) { const e = fdEntry(r); if (e) e.cloexec = true; } return r; },
    // scandir(path, namelist, filter, compar): read the dir, malloc an array
    // of struct dirent* (via the guest's malloc), fill, return the count.
    scandir: (pathPtr, namelistPtr, filterIdx, comparIdx) => {
      const path = mgr.vfsPath(cstr(pathPtr), proc.pio.cwd); const node = mgr.vfs[path];
      if (!node || node.type !== "dir") { setErrno(2); return -1; }
      // Allocate the namelist + dirents in the guest's heap so the guest can
      // free() them — but fall back to the engine's allocator for minimal test
      // guests that don't link malloc (scandir would otherwise spuriously fail).
      const mallocFn = proc.inst.exports.malloc;
      const alloc = mallocFn ? (sz) => mallocFn(sz) : (sz) => state.alloc(sz, 8);
      const table = proc.inst.exports.__indirect_function_table;
      const filter = filterIdx && table ? table.get(filterIdx) : null;
      const names = [".", "..", ...node.entries];
      const dents = [];
      for (const name of names) {
        const child = name === "." || name === ".." ? node : mgr.vfs[(path === "/" ? "" : path) + "/" + name];
        const dent = alloc((24 + name.length + 1 + 7) & ~7);
        fillDirent(state, dent, name, child && child.type === "dir", child && child.ino);
        if (filter) { try { if (!filter(dent)) continue; } catch { /* filter trap → keep the entry */ } }
        dents.push(dent);
      }
      const arr = alloc(Math.max(1, dents.length) * 4);
      for (let i = 0; i < dents.length; i++) view().setUint32(arr + i * 4, dents[i], true);
      view().setUint32(namelistPtr, arr, true);
      return dents.length;
    },
    // misc filesystem / process stubs nvim touches at startup.
    sysctlbyname: (namePtr, oldp, oldlenp) => { if (oldlenp) view().setUint32(oldlenp, 0, true); setErrno(2); return -1; },
    copy_file_range: () => { setErrno(78); return -1; }, sendfile: () => { setErrno(78); return -1; },
    futimes: () => 0, utimes: () => 0, lutimes: () => 0, futimens: () => 0, utimensat: () => 0,
    mkdtemp: (tplPtr) => { let s = cstr(tplPtr); s = s.replace(/XXXXXX$/, (Math.floor((mgr.now() % 1e6)) + "").padStart(6, "0")); u8().set(enc.encode(s + "\0"), tplPtr); const p = mgr.vfsPath(s, proc.pio.cwd); mgr.vfs[p] = { type: "dir", entries: [], mode: 0o700, mtime: Math.floor(mgr.now() / 1000), ino: mgr.nextIno++ }; return tplPtr; },
    pathconf: () => -1, fpathconf: () => -1,
    preadv: (fd, iov, cnt, off) => env.readv(fd, iov, cnt), pwritev: (fd, iov, cnt, off) => env.writev(fd, iov, cnt),
    recvmmsg: () => { setErrno(78); return -1; }, sendmmsg: () => { setErrno(78); return -1; },
    if_indextoname: (idx, buf) => { u8().set(enc.encode("lo0\0"), buf); return buf; }, if_nametoindex: () => 1,
    __assert: (funcPtr, filePtr, line, exprPtr) => { const e = new Error(`assertion failed: ${cstr(exprPtr)} in ${cstr(funcPtr)} (${cstr(filePtr)}:${line})`); e.isExit = true; e.code = 134; throw e; },
    __assert_fail: (exprPtr) => { const e = new Error(`assertion failed: ${cstr(exprPtr)}`); e.isExit = true; e.code = 134; throw e; },
    user_from_uid: (uid) => putStr(uid === 0 ? "root" : String(uid)),
    group_from_gid: (gid) => putStr(gid === 0 ? "wheel" : String(gid)),
    // multibyte: treat input as single-byte (C/ASCII locale).
    mbrtowc: (pwc, s, n, ps) => { if (!s || n === 0) return 0; const c = u8()[s]; if (pwc) view().setUint32(pwc, c, true); return c === 0 ? 0 : 1; },
    mbtowc: (pwc, s, n) => { if (!s) return 0; const c = u8()[s]; if (pwc) view().setUint32(pwc, c, true); return c === 0 ? 0 : 1; },
    wcwidth: () => 1, wcrtomb: (s, wc) => { if (s) u8()[s] = wc & 0xff; return 1; },
    getbsize: (hdr, lenp) => { if (lenp) view().setUint32(lenp, 512, true); return putStr("512"); },
    humanize_number: () => -1,
    // strmode(3): render a mode_t as the 11-char `ls -l` string + trailing
    // space (e.g. 0100755 -> "-rwxr-xr-x "), including setuid/setgid/sticky.
    strmode: (mode, buf) => {
      mode >>>= 0;
      const ifmt = mode & 0o170000;
      const type = ifmt === S_IFDIR ? "d" : ifmt === S_IFCHR ? "c" : ifmt === S_IFBLK ? "b"
        : ifmt === S_IFIFO ? "p" : ifmt === S_IFLNK ? "l" : ifmt === S_IFSOCK ? "s"
        : ifmt === S_IFREG ? "-" : "?";
      const rwx = (bits, special, lo, hi) => {
        const r = (bits & 4) ? "r" : "-";
        const w = (bits & 2) ? "w" : "-";
        const x = special ? ((bits & 1) ? lo : hi) : ((bits & 1) ? "x" : "-");
        return r + w + x;
      };
      const s = type
        + rwx((mode >> 6) & 7, mode & 0o4000, "s", "S")
        + rwx((mode >> 3) & 7, mode & 0o2000, "s", "S")
        + rwx(mode & 7, mode & 0o1000, "t", "T")
        + " ";
      u8().set(enc.encode(s + "\0"), buf);
      return 0;
    },

    // --- additional libc fills over the VFS / fd table ---
    usleep: (us) => { mgr.clockSkew += (us | 0) / 1000; return 0; }, fsync: () => 0, fdatasync: () => 0, sync: () => 0, flock: () => 0,
    mkdir: (pathPtr, mode) => { const raw = cstrBounded(pathPtr); if (raw === null) { setErrno(14); return -1; } return mkdirAt(mgr.vfsPath(raw, proc.pio.cwd), mode); },
    // mkdirat: an absolute path or AT_FDCWD ignores dfd; otherwise resolve the
    // path under the directory the dfd refers to (so the new dir lands there,
    // not in cwd) — same dir-fd resolution fstatat uses.
    mkdirat: (dfd, pathPtr, mode) => {
      const name = cstr(pathPtr);
      if (name.startsWith("/") || (dfd | 0) === -100) return mkdirAt(mgr.vfsPath(name, proc.pio.cwd), mode);
      const base = proc.pio.dirs.get(dfd), e = fdEntry(dfd);
      const baseDir = base ? base.path : (e && e.ofd && e.ofd.path) ? e.ofd.path : proc.pio.cwd;
      return mkdirAt(mgr.vfsPath((baseDir === "/" ? "" : baseDir) + "/" + name, proc.pio.cwd), mode);
    },
    rmdir: (pathPtr) => env.unlink(pathPtr),
    rename: (oldPtr, newPtr) => { const op = mgr.vfsPath(cstr(oldPtr), proc.pio.cwd); const np = mgr.vfsPath(cstr(newPtr), proc.pio.cwd); const node = mgr.vfs[op]; if (!node) { setErrno(2); return -1; } delete mgr.vfs[op]; mgr.vfs[np] = node; const od = op.lastIndexOf("/"), nd = np.lastIndexOf("/"); const oparent = mgr.vfs[od === 0 ? "/" : op.slice(0, od)], nparent = mgr.vfs[nd === 0 ? "/" : np.slice(0, nd)]; const oname = op.slice(od + 1), nname = np.slice(nd + 1); if (oparent && oparent.entries) { const i = oparent.entries.indexOf(oname); if (i >= 0) oparent.entries.splice(i, 1); } if (nparent && nparent.entries && !nparent.entries.includes(nname)) nparent.entries.push(nname); return 0; },
    link: (oldPtr, newPtr) => { const op = mgr.vfsPath(cstr(oldPtr), proc.pio.cwd); const np = mgr.vfsPath(cstr(newPtr), proc.pio.cwd); const node = mgr.vfs[op]; if (!node) { setErrno(2); return -1; } mgr.vfs[np] = node; const nd = np.lastIndexOf("/"); const nparent = mgr.vfs[nd === 0 ? "/" : np.slice(0, nd)]; const nname = np.slice(nd + 1); if (nparent && nparent.entries && !nparent.entries.includes(nname)) nparent.entries.push(nname); return 0; },
    // symlink/readlink: create a real symlink node in the vfs (type "symlink",
    // mode 0777 like FreeBSD) so lstat() reports S_IFLNK with lrwxrwxrwx and
    // readlink() returns the target.
    symlink: (targetPtr, linkPtr) => {
      const target = cstr(targetPtr);
      const path = mgr.vfsPath(cstr(linkPtr), proc.pio.cwd);
      if (mgr.vfs[path]) { setErrno(17); return -1; } // EEXIST
      const slash = path.lastIndexOf("/");
      const parent = mgr.vfs[slash === 0 ? "/" : path.slice(0, slash)];
      if (!parent || parent.type !== "dir") { setErrno(2); return -1; }
      mgr.vfs[path] = { type: "symlink", target, mode: 0o777, mtime: Math.floor(Date.now() / 1000), ino: mgr.nextIno++ };
      const name = path.slice(slash + 1);
      if (!parent.entries.includes(name)) parent.entries.push(name);
      return 0;
    },
    symlinkat: (targetPtr, dfd, linkPtr) => env.symlink(targetPtr, linkPtr),
    ftruncate: (fd, len) => { const e = fdEntry(fd); if (!e || e.ofd.kind !== "file") { setErrno(9); return -1; } const n = typeof len === "bigint" ? Number(len) : len; const node = e.ofd.node; const nd = new Uint8Array(n); nd.set(node.data.subarray(0, Math.min(n, node.data.length))); node.data = nd; return 0; },
    pread: (fd, buf, n, off) => { const e = fdEntry(fd); if (!e || e.ofd.kind !== "file") { setErrno(9); return -1; } const o = typeof off === "bigint" ? Number(off) : off; const data = e.ofd.node.data, end = Math.min(o + n, data.length); u8().set(data.subarray(o, end), buf); return Math.max(0, end - o); },
    pwrite: (fd, buf, n, off) => { const e = fdEntry(fd); if (!e || e.ofd.kind !== "file") { setErrno(9); return -1; } const o = typeof off === "bigint" ? Number(off) : off; const node = e.ofd.node; const end = o + n; const merged = new Uint8Array(Math.max(node.data.length, end)); merged.set(node.data); merged.set(u8().subarray(buf, buf + n), o); node.data = merged; return n; },
    closefrom: (low) => { for (const fd of [...proc.pio.fds.keys()]) if (fd >= low) closeFd(fd); for (const h of [...proc.pio.dirs.keys()]) if (h >= low) proc.pio.dirs.delete(h); return 0; },
    posix_memalign: (memptrPtr, align, size) => { const a = align < 8 ? 8 : align; state.brk = (state.brk + (a - 1)) & ~(a - 1); const p = state.alloc(size, a); view().setUint32(memptrPtr, p, true); return 0; },
    aligned_alloc: (align, size) => { const a = align < 8 ? 8 : align; state.brk = (state.brk + (a - 1)) & ~(a - 1); return state.alloc(size, a); },
    // PRNG: per-process xorshift so a child's draws cannot perturb the parent.
    arc4random: () => { let x = proc.pio.rng ?? (proc.pio.rng = (proc.pid * 2654435761) >>> 0 || 1); x ^= x << 13; x ^= x >>> 17; x ^= x << 5; proc.pio.rng = x >>> 0; return proc.pio.rng; },
    arc4random_uniform: (bound) => (bound ? env.arc4random() % bound : 0),
    arc4random_buf: (ptr, n) => { for (let i = 0; i < n; i++) u8()[ptr + i] = env.arc4random() & 0xff; },
    getentropy: (ptr, n) => { if (n > 256) { setErrno(22); return -1; } if (ptr === 0 && n > 0) { setErrno(14); return -1; } for (let i = 0; i < n; i++) u8()[ptr + i] = env.arc4random() & 0xff; return 0; },
    // time: a stable per-process struct tm / string buffer so the iso_*
    // "buffer stable across calls" tests see one reused buffer.
    localtime: (tPtr) => fillTm(state, proc, tPtr ? view().getUint32(tPtr, true) : Math.floor(Date.now() / 1000)),
    gmtime: (tPtr) => fillTm(state, proc, tPtr ? view().getUint32(tPtr, true) : Math.floor(Date.now() / 1000)),
    localtime_r: (tPtr, tmPtr) => fillTmAt(state, tmPtr, tPtr ? view().getUint32(tPtr, true) : 0),
    gmtime_r: (tPtr, tmPtr) => fillTmAt(state, tmPtr, tPtr ? view().getUint32(tPtr, true) : 0),
    mktime: () => Math.floor(Date.now() / 1000),
    ctime: (tPtr) => { if (!proc.pio.ctimeBuf) proc.pio.ctimeBuf = state.alloc(32, 1); u8().set(enc.encode("Thu Jan  1 00:00:00 1970\n\0"), proc.pio.ctimeBuf); return proc.pio.ctimeBuf; },
    ctime_r: (tPtr, buf) => { u8().set(enc.encode("Thu Jan  1 00:00:00 1970\n\0"), buf); return buf; },
    asctime: () => { if (!proc.pio.ctimeBuf) proc.pio.ctimeBuf = state.alloc(32, 1); u8().set(enc.encode("Thu Jan  1 00:00:00 1970\n\0"), proc.pio.ctimeBuf); return proc.pio.ctimeBuf; },
    difftime: (a, b) => a - b, mktime: () => Math.floor(Date.now() / 1000),
    strftime: (dst, max, fmtPtr, tmPtr) => { const s = "Thu Jan  1 00:00:00 1970".slice(0, max - 1); const b = enc.encode(s); u8().set(b, dst); u8()[dst + b.length] = 0; return b.length; },
    strptime: () => 0, times: (bufPtr) => { if (bufPtr) u8().fill(0, bufPtr, bufPtr + 16); return 0; },

    // math
    pow: (a, b) => Math.pow(a, b), fmod: (a, b) => a % b, __isinf: (x) => (x === Infinity || x === -Infinity ? 1 : 0), __isnan: (x) => (x !== x ? 1 : 0), __isfinite: (x) => (Number.isFinite(x) ? 1 : 0),
    // math helpers liblua.wasm needs (shared memory: view() is the guest's).
    frexp: (x, eptr) => { let ex = 0, m = x; if (x !== 0 && isFinite(x)) { while (Math.abs(m) >= 1) { m /= 2; ex++; } while (Math.abs(m) < 0.5) { m *= 2; ex--; } } if (eptr) view().setInt32(eptr, ex, true); return m; },
    ldexp: (x, e) => x * 2 ** e, modf: (x, iptr) => { const i = x < 0 ? Math.ceil(x) : Math.floor(x); if (iptr) view().setFloat64(iptr, i, true); return x - i; },

    // ids / creds — single user (root); setters are accepted no-ops.
    setuid: () => 0, seteuid: () => 0, setgid: () => 0, setegid: () => 0, setreuid: () => 0, setregid: () => 0, setgroups: () => 0, setpriority: () => 0, getpriority: () => 0, nice: () => 0, setproctitle: () => 0, issetugid: () => 0,
    getpwent: () => 0, setpwent: () => 0, endpwent: () => 0, fchmod: () => 0, fchown: () => 0, chmod: () => 0, chown: () => 0, lchown: () => 0,

    // temp files in the in-memory /tmp
    mkstemp: (tplPtr) => { let s = cstr(tplPtr); s = s.replace(/XXXXXX$/, (Math.floor(Date.now() % 1e6) + "").padStart(6, "0")); u8().set(enc.encode(s + "\0"), tplPtr); const node = mgr.vfsCreateFile(mgr.vfsPath(s, proc.pio.cwd)); return installFd(allocFd(), { kind: "file", node, off: 0, append: false, refs: 1 }); },
    _mktemp: (tplPtr) => { let s = cstr(tplPtr); s = s.replace(/XXXXXX$/, (Math.floor(Date.now() % 1e6) + "").padStart(6, "0")); u8().set(enc.encode(s + "\0"), tplPtr); return tplPtr; },
    mktemp: (tplPtr) => env._mktemp(tplPtr),

    // iconv: C/UTF-8 passthrough (no transliteration). Copies bytes through
    // the (inbuf**, inleft*, outbuf**, outleft*) protocol.
    iconv_open: () => 1, iconv_close: () => 0,
    iconv: (cd, inbufpp, inleftp, outbufpp, outleftp) => { if (!inbufpp || !view().getUint32(inbufpp, true)) return 0; let inp = view().getUint32(inbufpp, true); let inl = view().getUint32(inleftp, true); let outp = view().getUint32(outbufpp, true); let outl = view().getUint32(outleftp, true); const k = Math.min(inl, outl); for (let i = 0; i < k; i++) u8()[outp + i] = u8()[inp + i]; view().setUint32(inbufpp, inp + k, true); view().setUint32(inleftp, inl - k, true); view().setUint32(outbufpp, outp + k, true); view().setUint32(outleftp, outl - k, true); return k; },

    // setjmp/longjmp: zsh installs these for interrupt recovery. The initial
    // setjmp returns 0; a longjmp (rare in normal command flow) ends the
    // process cleanly rather than corrupting the stack.
    setjmp: () => 0, _setjmp: () => 0, sigsetjmp: () => 0,
    longjmp: (env_, val) => { const e = new Error("longjmp"); e.isExit = true; e.code = 0; throw e; }, _longjmp: (env_, val) => { const e = new Error("longjmp"); e.isExit = true; e.code = 0; throw e; }, siglongjmp: (env_, val) => { const e = new Error("longjmp"); e.isExit = true; e.code = 0; throw e; },
  });
  // Wrap every libc fn with the runaway-loop guard so a busy-looping guest
  // (an unsupported blocking syscall, e.g. tmux's server poll loop) aborts
  // with an error instead of freezing the synchronous main thread forever.
  for (const key of Object.keys(env)) {
    const fn = env[key];
    if (typeof fn === "function") env[key] = (...args) => { mgr.tick(key); if (!mgr.trace) return fn(...args); const ret = fn(...args); mgr.trace.push("pid" + proc.pid + " " + key + "(" + args.join(",") + ")=" + ret); if (!mgr.traceAll && mgr.trace.length > 40) mgr.trace.shift(); return ret; };
  }
  // Provide the Lua C API to lua_*-importing guests (nvim) by sharing this
  // guest's memory + table with liblua.wasm. Lazy: liblua only instantiates if
  // the guest actually calls a lua_* function — but the data+stack window is
  // reserved up-front so guest heap and Lua state can never overlap.
  try { installLuaForwarders(env, () => proc.inst, env, (n) => { while (state.u8.length < n) { state.mem.grow(Math.ceil((n - state.u8.length) / 65536)); state.refresh(); } }, state.reserve); } catch { /* liblua not loaded */ }
  return env;
}

// A tiny in-memory filesystem so ls/cat see real files. dirs hold child
// names; files hold byte data.
function buildVfs(toolNames) {
  const now = Math.floor(Date.now() / 1000);
  let ino = 2;
  const dir = (entries) => ({ type: "dir", entries, mtime: now, ino: ino++ });
  const file = (text) => ({ type: "file", data: new TextEncoder().encode(text), mtime: now, ino: ino++ });
  const vfs = {
    "/": dir(["bin", "etc", "home", "README"]),
    "/bin": dir(toolNames.slice().sort()),
    "/etc": dir(["motd", "hostname"]),
    "/etc/motd": file("Welcome to yos in the browser.\n"),
    "/etc/hostname": file("yos-web\n"),
    "/README": file("This is a real file on a real (in-memory) filesystem,\nread by the real cat/ls wasm through env.open/read/readdir.\n"),
    "/home": dir(["user"]),
    "/home/user": dir([".profile"]),
    "/home/user/.profile": file("export PATH=/bin\n"),
    "/tmp": dir([]),
  };
  // Every tool gets a REAL executable file node, not just a name in the /bin
  // listing: programs check a command's execute bits via stat() before
  // spawning it (nvim's os_can_exe stats $SHELL and refuses :terminal with
  // "'/bin/sh' is not executable" when the node is missing — access() alone
  // special-cases the tool map, stat() resolves VFS nodes).
  for (const name of toolNames) {
    vfs["/bin/" + name] = { type: "file", data: new Uint8Array(0), mode: 0o755, mtime: now, ino: ino++ };
  }
  return vfs;
}

const S_IFDIR = 0o040000, S_IFREG = 0o100000, S_IFCHR = 0o020000;
const S_IFSOCK = 0o140000, S_IFIFO = 0o010000, S_IFLNK = 0o120000, S_IFBLK = 0o060000;

// Drop one reference to an open file description; on the last reference, mark
// pipe/socket ends closed so peers see EOF (readers) / EPIPE (writers). Used
// by close() and by process exit (which closes all of a process's fds).
function releaseOfd(ofd) {
  if (--ofd.refs > 0) return;
  if (ofd.kind === "pipe") { if (ofd.end === "r") ofd.pipe.readClosed = true; else ofd.pipe.writeClosed = true; }
  else if (ofd.kind === "sock") { ofd.rx.readClosed = true; ofd.tx.writeClosed = true; }
}

// A fresh per-process I/O table: fds 0/1/2 wired to stdin/stdout/stderr
// char devices, an empty dir-handle table, cwd "/", and per-process thread
// bookkeeping. fork copies this with clonePio (sharing open descriptions).
function newPio() {
  const fds = new Map();
  fds.set(0, { ofd: { kind: "char", dev: "in", refs: 1 }, cloexec: false });
  fds.set(1, { ofd: { kind: "char", dev: "out", refs: 1 }, cloexec: false });
  fds.set(2, { ofd: { kind: "char", dev: "err", refs: 1 }, cloexec: false });
  return { fds, dirs: new Map(), files: new Map(), nextFile: 1, env: DEFAULT_ENV.slice(), cwd: "/", umask: 0o22, rand: 1, nextTid: 1, threadRv: new Map() };
}
// Copy a pio for a forked child: fd numbers and cloexec flags are copied,
// but each fd points at the SAME open file description (refcount bumped) so
// a pipe/socket written by one side is read by the other. cwd is a copied
// string. Threads are not inherited across fork.
function clonePio(parent) {
  const fds = new Map();
  for (const [fd, entry] of parent.fds) { entry.ofd.refs++; fds.set(fd, { ofd: entry.ofd, cloexec: entry.cloexec, flags: entry.flags | 0 }); }
  const dirs = new Map();
  for (const [handle, dir] of parent.dirs) dirs.set(handle, { ...dir });
  const files = new Map();
  for (const [handle, file] of parent.files) files.set(handle, { ...file });
  // The child's linear memory is a copy of the parent's, so its mmap regions
  // are valid at the same offsets; deep-copy the bookkeeping so the two then
  // evolve independently (a child mmap/munmap must not perturb the parent).
  const mm = parent.mm ? { base: parent.mm.base, top: parent.mm.top, live: parent.mm.live.map((r) => ({ ...r })), free: parent.mm.free.map((r) => ({ ...r })) } : undefined;
  return { fds, dirs, files, nextFile: parent.nextFile, env: parent.env.slice(), cwd: parent.cwd, umask: parent.umask, locale: parent.locale, rand: parent.rand, nextTid: 1, threadRv: new Map(), mm };
}

export class Manager {
  constructor(io, tools) {
    this.io = io; this.tools = tools || new Map();
    this.nextPid = 1; this.procs = []; this.depth = 0;
    this.vfs = buildVfs([...this.tools.keys()]);
    // yfs (docs/yfs.md): the lazily-fetched web filesystem. Set up by
    // mountYfs(); moduleCache holds compiled wasm keyed by body hash so
    // exec-on-demand compiles each distinct binary once.
    this.yfsClient = null;
    this.compileModule = null;
    this.moduleCache = new Map();
    this.openFiles = new Map(); // fd -> { path, off }
    this.openDirs = new Map();  // handle -> { path, names, idx }
    this.nextFdNum = 5;
    this.nextIno = 100;
    this.cwd = "/";
    // Runaway-loop guard. The guest runs synchronously on the main thread, so
    // a program that busy-loops (e.g. tmux's event loop with a perpetually-due
    // timer) would freeze the page. tick() aborts the process once a single
    // run exceeds a WALL-CLOCK budget — capping any freeze at ~1.5s regardless
    // of how fast the loop spins — plus a huge syscall-count backstop.
    this.budget = 0;
    this.budgetLimit = 200_000_000;
    this.timeLimitMs = 1500;
    this.turnStart = 0;
    this.hist = null; // set to {} to record a syscall histogram for diagnosis
    this.runQueue = [];      // scheduler: processes ready to run (interactive)
    this.tty = null;         // batch-mode fallback terminal; interactive
                             // sessions each carry their own proc.tty,
                             // inherited down the fork tree
    this.interactive = false;
    this.unixSockets = new Map(); // bound path -> listening socket ofd
    // Fully LOGICAL virtual clock (ms): NOT wall-clock. Time advances only when
    // the scheduler fast-forwards a timer or a guest sleeps — never from
    // Date.now(). This makes the whole engine DETERMINISTIC: identical guest
    // input produces identical scheduling, so a shell in a tmux pane behaves
    // exactly like a shell on the bare tty. Any Date.now() in the scheduling
    // path would make the tmux server's render-timer firing race against real
    // time and drop pane output non-deterministically.
    this.clockBase = 1704067200000; // fixed epoch (2024-01-01) for realistic, stable timestamps
    this.clockSkew = 0;             // logical advance (ms) from fast-forwards / sleeps
  }
  now() { return this.clockBase + this.clockSkew; }
  // Reset the per-process syscall count (called per pumpProc). The wall-clock
  // budget (turnStart) spans the WHOLE scheduler turn — set in schedule() —
  // so several busy processes can't add up to a long freeze.
  resetBudget() { this.budget = 0; if (this.hist) this.hist = {}; }
  tick(name) {
    if (this.hist) this.hist[name] = (this.hist[name] || 0) + 1;
    this.budget++;
    // Wall-clock check every ~256k syscalls keeps the freeze bounded without
    // calling Date.now() on every single syscall. Interactive only.
    if (this.interactive && (this.budget & 0x3ffff) === 0 && Date.now() - this.turnStart > this.timeLimitMs) this.abortRunaway();
    if (this.budget > this.budgetLimit) this.abortRunaway();
  }
  abortRunaway() {
    let detail = "";
    if (this.hist) detail = " (top: " + Object.entries(this.hist).sort((a, b) => b[1] - a[1]).slice(0, 4).map(([n, c]) => `${n}×${c}`).join(" ") + ")";
    const e = new Error(`aborted after ${this.timeLimitMs}ms: a command busy-looped on an unsupported operation${detail}`);
    e.runaway = true;
    throw e;
  }
  vfsPath(path, cwd = "/") {
    if (!path) return cwd;
    if (path[0] !== "/") path = (cwd === "/" ? "" : cwd) + "/" + path;
    const stack = [];
    for (const part of path.split("/")) {
      if (!part || part === ".") continue;
      else if (part === "..") stack.pop();
      else stack.push(part);
    }
    return "/" + stack.join("/");
  }
  vfsResolve(path, cwd = "/") { return this.vfs[this.vfsPath(path, cwd)] || null; }
  // Mount a read-only external file tree into the VFS at `at` (e.g. nvim's
  // $VIMRUNTIME). entries: [{ path: "lua/vim/termcap.lua", data?: Uint8Array,
  // read?: () => Uint8Array|null }]. File bytes load LAZILY on first access
  // through a caching `data` getter (node: readFileSync; browser: a slice of
  // the prefetched /fs/pack.bin), so mounting ~2000 runtime files costs
  // nothing until a file is actually opened. Intermediate directories are
  // created; existing VFS nodes are never overwritten.
  mountFiles(at, entries) {
    const base = this.vfsPath(at);
    const now = Math.floor(this.now() / 1000);
    const ensureDir = (path) => {
      if (this.vfs[path]) return;
      this.vfs[path] = { type: "dir", entries: [], mtime: now, ino: this.nextIno++ };
      const slash = path.lastIndexOf("/");
      const parentPath = slash === 0 ? "/" : path.slice(0, slash);
      ensureDir(parentPath);
      const parent = this.vfs[parentPath];
      const name = path.slice(slash + 1);
      if (parent && parent.entries && !parent.entries.includes(name)) parent.entries.push(name);
    };
    ensureDir(base);
    for (const entry of entries) {
      const full = this.vfsPath(base + "/" + entry.path);
      if (this.vfs[full]) continue;
      const slash = full.lastIndexOf("/");
      const dirPath = slash === 0 ? "/" : full.slice(0, slash);
      ensureDir(dirPath);
      const node = { type: "file", mtime: now, ino: this.nextIno++ };
      if (entry.data) node.data = entry.data;
      else {
        const load = entry.read;
        let cached = null;
        Object.defineProperty(node, "data", {
          get() { if (cached === null) cached = load() || new Uint8Array(0); return cached; },
          set(value) { cached = value; },
          enumerable: true, configurable: true,
        });
      }
      this.vfs[full] = node;
      const parent = this.vfs[dirPath];
      const name = full.slice(slash + 1);
      if (parent.entries && !parent.entries.includes(name)) parent.entries.push(name);
    }
  }
  vfsCreateFile(path, mode) {
    const node = { type: "file", data: new Uint8Array(0), mtime: Math.floor(Date.now() / 1000), ino: this.nextIno++ };
    if (typeof mode === "number") node.mode = mode & 0o777;
    this.vfs[path] = node;
    const slash = path.lastIndexOf("/");
    const parent = this.vfs[slash === 0 ? "/" : path.slice(0, slash)];
    const name = path.slice(slash + 1);
    if (parent && parent.type === "dir" && !parent.entries.includes(name)) parent.entries.push(name);
    return node;
  }

  // ---- yfs: the lazily-fetched web filesystem (docs/yfs.md) ----
  //
  // Metadata is EAGER: mountYfs materializes every directory and file
  // node from the client's aggregate manifest, so stat/readdir/access
  // and PATH walks stay synchronous. Bodies are LAZY: a file node
  // carries node.yfs and no data until the first open() (which
  // suspends the process via the same asyncify block used for tty
  // reads) or the first execve (which parks the process in the
  // async-boot state). Writes never reach yfs — the in-memory VFS is
  // the overlay, exactly as before.
  mountYfs(client) {
    this.yfsClient = client;
    const dirs = client.dirs;
    // Directory nodes first (parents before children — sort by depth).
    const dirPaths = Object.keys(dirs).sort(
      (a, b) => a.split("/").length - b.split("/").length);
    const metaFor = (dirPath) => {
      if (dirPath === "") return null;
      const slash = dirPath.lastIndexOf("/");
      const parentPath = slash === -1 ? "" : dirPath.slice(0, slash);
      const name = slash === -1 ? dirPath : dirPath.slice(slash + 1);
      return (dirs[parentPath] || []).find(
        (entry) => entry.n === name && entry.t === "d");
    };
    for (const dirPath of dirPaths) {
      const full = dirPath === "" ? "/" : "/" + dirPath;
      const existing = this.vfs[full];
      const meta = metaFor(dirPath);
      if (existing && existing.type === "dir") {
        if (meta && typeof meta.m === "number" && existing.mode === undefined) existing.mode = meta.m;
        continue;
      }
      this.vfs[full] = {
        type: "dir", entries: [],
        mtime: (meta && meta.mt) || 0,
        mode: meta && typeof meta.m === "number" ? meta.m : 0o755,
        ino: (meta && meta.i) || this.nextIno++,
      };
      const slash = full.lastIndexOf("/");
      const parent = this.vfs[slash === 0 ? "/" : full.slice(0, slash)];
      const name = full.slice(slash + 1);
      if (parent && parent.entries && !parent.entries.includes(name)) parent.entries.push(name);
    }
    // File nodes. The boot seed plants empty 0-byte placeholders under
    // /bin (execute-bit stat probes) — a yfs node REPLACES those; any
    // other pre-existing node wins (RAM overlay semantics).
    const symlinks = [];
    for (const dirPath of dirPaths) {
      const base = dirPath === "" ? "" : "/" + dirPath;
      const parentNode = this.vfs[base === "" ? "/" : base];
      for (const entry of dirs[dirPath]) {
        if (entry.t === "d") {
          if (parentNode.entries && !parentNode.entries.includes(entry.n)) parentNode.entries.push(entry.n);
          continue;
        }
        const full = base + "/" + entry.n;
        if (entry.t === "l") { symlinks.push({ full, dirPath, entry, parentNode }); continue; }
        const existing = this.vfs[full];
        const placeholder = existing && existing.type === "file" &&
          existing.data && existing.data.length === 0 && !existing.yfs;
        if (existing && !placeholder) continue;
        this.vfs[full] = {
          type: "file",
          ino: entry.i, mtime: entry.mt || 0,
          mode: typeof entry.m === "number" ? entry.m : 0o644,
          size: entry.s || 0,
          yfs: { dir: dirPath, entry },
        };
        if (parentNode.entries && !parentNode.entries.includes(entry.n)) parentNode.entries.push(entry.n);
      }
    }
    // Symlinks whose target resolves inside the mount alias the target
    // node (hardlink semantics — shared body cache); see docs/yfs.md.
    for (const { full, dirPath, entry, parentNode } of symlinks) {
      const target = this.vfsPath(entry.tgt, dirPath === "" ? "/" : "/" + dirPath);
      let node = this.vfs[target], hops = 0;
      while (node && node.type === "symlink" && ++hops < 8) node = this.vfs[this.vfsPath(node.target, "/")];
      if (!node || this.vfs[full]) continue;
      this.vfs[full] = node;
      if (parentNode.entries && !parentNode.entries.includes(entry.n)) parentNode.entries.push(entry.n);
    }
  }

  // Kick the body fetch for a yfs-backed file node. Idempotent. In node
  // (sync client) the body lands before this returns; in the browser
  // completion wakes the scheduler through the async-boot hook, and the
  // suspended open's ready() predicate sees node.data.
  yfsStart(node) {
    if (!node.yfs || node.data || node.yfs.error) return;
    const client = this.yfsClient;
    if (client.readBodySync) {
      try { node.data = client.readBodySync(node.yfs.dir, node.yfs.entry); }
      catch (err) { node.yfs.error = (err && err.message) || "yfs read failed"; }
      return;
    }
    if (node.yfs.promise) return;
    node.yfs.promise = client.fetchBody(node.yfs.dir, node.yfs.entry).then(
      (bytes) => { node.data = bytes; },
      (err) => { node.yfs.error = (err && err.message) || "yfs fetch failed"; },
    ).finally(() => { if (this.onAsyncBoot) this.onAsyncBoot(); });
  }

  // Resolve an execve path to a runnable file node: the literal VFS
  // path, then /bin/<basename> (mirrors the tool-map-by-basename
  // convention). Mounted symlinks are already aliased to their target.
  yfsExecNode(rawPath, cwd) {
    const direct = this.vfs[this.vfsPath(rawPath, cwd)];
    if (direct && direct.type === "file" && (direct.yfs || (direct.data && direct.data.length))) return direct;
    const name = rawPath.replace(/^.*\//, "");
    const inBin = this.vfs["/bin/" + name];
    if (inBin && inBin.type === "file" && (inBin.yfs || (inBin.data && inBin.data.length))) return inBin;
    return null;
  }

  // execve of a cold (or not-yet-compiled) yfs binary: park the process
  // in the async-boot state, fetch + compile, swap the image in. The
  // compiled module is cached by content hash — sh and zsh (symlink,
  // same body) compile once.
  prepareExecYfs(proc, node, argv) {
    this.finishAsyncBoot(proc, async () => {
      if (!node.data) {
        this.yfsStart(node);
        if (node.yfs && node.yfs.promise) await node.yfs.promise;
      }
      if (!node.data) throw new Error("exec: yfs fetch failed: " + ((node.yfs && node.yfs.error) || "?"));
      const hash = node.yfs && node.yfs.entry.h;
      let mod = hash ? this.moduleCache.get(hash) : null;
      if (!mod) {
        mod = this.compileModule ? await this.compileModule(node.data)
          : await WebAssembly.compile(node.data);
        if (hash) this.moduleCache.set(hash, mod);
      }
      const state = makeState();
      const env = buildLibc(state, proc.pio.env, this.io, this, proc);
      strictImportEnv(env, mod, { label: "exec", onCall: this.io.onUnimpl });
      const inst = await WebAssembly.instantiate(mod, { env });
      this.commitExec(proc, mod, argv, state, inst);
    });
  }

  // Synchronous variant for the non-scheduler path (node test harnesses
  // running a child program to completion): sync body + sync compile.
  yfsModuleSync(node) {
    if (!node.data) this.yfsStart(node);
    if (!node.data) return null;
    const hash = node.yfs && node.yfs.entry.h;
    let mod = hash ? this.moduleCache.get(hash) : null;
    if (!mod) {
      mod = new WebAssembly.Module(node.data);
      if (hash) this.moduleCache.set(hash, mod);
    }
    return mod;
  }

  spawn(mod, argv, ppid, env_vars) {
    const { proc, env } = this.spawnShell(mod, argv, ppid, env_vars);
    this.attachInstance(proc, new WebAssembly.Instance(mod, { env }));
    return proc;
  }

  // Everything spawn() does short of instantiating the wasm — split out so a
  // module past Chrome's main-thread SYNC instantiation size limit (nvim is
  // 14 MB; the limit bites somewhere above ~6 MB) can boot ASYNCHRONOUSLY:
  // build the shell now, `await WebAssembly.instantiate` later, attach on
  // resolve. Node and small modules keep the synchronous path.
  spawnShell(mod, argv, ppid, env_vars) {
    const pid = this.nextPid++;
    const comm = (argv[0] || "?").replace(/^.*\//, "").slice(0, 19);
    const proc = { pid, ppid, pgid: pid, sid: pid, comm, mod, argv, exited: false, reaped: false, exitCode: 0, signal: 0, forkReturn: 0, forkPending: false, pendingRewind: false, asyncifyPtr: 0, state: null, inst: null, pio: newPio(), sched: "new", blocked: null, pendingSignals: [] };
    if (env_vars) proc.pio.env = env_vars.slice();
    const state = makeState();
    proc.state = state;
    const env = buildLibc(state, env_vars || DEFAULT_ENV, this.io, this, proc);
    strictImportEnv(env, mod, { label: "proc", onCall: this.io.onUnimpl });
    this.procs.push(proc);
    return { proc, env };
  }

  attachInstance(proc, inst) {
    proc.inst = inst;
    const state = proc.state;
    state.mem = inst.exports.memory;
    state.main = inst.exports.main;
    state.refresh();
    const hb = inst.exports.__heap_base;
    state.brk = (typeof hb === "object" ? hb.value : hb) ?? (1 << 20);
    state.errnoPtr = state.alloc(4);
    return proc;
  }

  // Chrome (V8) refuses `new WebAssembly.Instance` on the main thread for
  // large modules — the async `WebAssembly.instantiate` is the only route.
  isSyncInstantiationLimit(err) {
    return !!(err && typeof err.message === "string" && /disallowed on the main thread/i.test(err.message));
  }

  // A process is booting: its wasm instance is resolving asynchronously. It
  // is neither runnable nor blocked; when the promise settles it becomes
  // runnable and onAsyncBoot (wired to the page's runSched) re-enters the
  // scheduler.
  finishAsyncBoot(proc, ready) {
    proc.sched = "booting";
    (async () => {
      await ready();
      proc.sched = "runnable";
      if (!this.runQueue.includes(proc)) this.runQueue.push(proc);
    })().catch((err) => {
      proc.exited = true; proc.sched = "zombie"; proc.exitCode = 139; proc.error = err && err.message;
      this.onProcExit(proc);
    }).finally(() => { if (this.onAsyncBoot) this.onAsyncBoot(); });
  }

  asyncifyState(proc) { const f = proc.inst.exports.asyncify_get_state; return f ? f() : -1; }

  // Drive one process's _start with the asyncify fork pump.
  run(proc) {
    this.resetBudget();
    if (++this.depth > 4096) { this.depth--; throw new Error("fork depth exceeded"); }
    try {
      for (;;) {
        try {
          if (proc.pendingRewind) { proc.pendingRewind = false; proc.inst.exports.asyncify_start_rewind(proc.asyncifyPtr); }
          proc.inst.exports._start();
        } catch (e) {
          if (e.isExit) { proc.exited = true; proc.exitCode = e.code; return; }
          proc.error = e.message; proc.exited = true; proc.exitCode = 139; proc.signal = 11; return;
        }
        if (proc.forkPending) { this.doFork(proc); continue; } // re-enter parent via rewind
        proc.exited = true; return; // _start returned without exit/fork
      }
    } finally {
      this.depth--;
      // Non-scheduler exit: release the process's fds the moment it exits —
      // normal _exit, a trap, or a forked child run to completion here — exactly
      // as the scheduler's onProcExit() does. Without this an inherited
      // pipe/socket end held only by the dead child stays refcounted, so its
      // peer never sees EOF/EPIPE. (Runs before doFork()/runChildProgram() null
      // out inst/state.) Only when the process actually exited: a parent's run()
      // returns only on its own exit, never mid-fork.
      if (proc.exited) this.releaseProcFds(proc);
    }
  }

  // Interactive event-driven pump: run the guest until it blocks on terminal
  // input (then YIELD to the browser event loop) or exits. The page resumes
  // it by feeding keystrokes (runInteractive's write()), which re-enters here
  // and asyncify-rewinds back into the blocked read()/poll(). This is what
  // turns the universal binary into a long-lived interactive process.
  pump(proc) {
    if (proc.exited) return;
    this.resetBudget();
    proc.blocked = null;
    for (;;) {
      try {
        if (proc.pendingRewind) { proc.pendingRewind = false; proc.inst.exports.asyncify_start_rewind(proc.asyncifyPtr); }
        proc.inst.exports._start();
      } catch (e) {
        proc.exited = true;
        if (e.isExit) proc.exitCode = e.code; else { proc.exitCode = 139; proc.error = e.message; }
        if (proc.onExit) proc.onExit(proc.exitCode, proc.error);
        return;
      }
      if (proc.forkPending) { this.doFork(proc); continue; }
      if (proc.blocked) { proc.pendingRewind = true; return; } // waiting for input — resume on keystroke
      proc.exited = true;
      if (proc.onExit) proc.onExit(proc.exitCode ?? 0);
      return;
    }
  }

  // env.fork: NORMAL → trigger unwind; REWINDING → return the stored value.
  fork(proc) {
    const st = this.asyncifyState(proc);
    if (st < 0) return -1; // not asyncified — fork unsupported
    if (st === ASYNCIFY_REWINDING) { proc.inst.exports.asyncify_stop_rewind(); return proc.forkReturn; }
    if (proc.asyncifyPtr === 0) proc.asyncifyPtr = proc.state.alloc(ASYNCIFY_BUF_SIZE, 8);
    const buf = proc.asyncifyPtr;
    proc.state.view.setUint32(buf, buf + 8, true);
    proc.state.view.setUint32(buf + 4, buf + ASYNCIFY_BUF_SIZE, true);
    proc.forkPending = true;
    proc.inst.exports.asyncify_start_unwind(buf);
    return 0; // ignored — unwinding
  }

  // ============ cooperative scheduler (interactive concurrency) ============
  // Multiple processes are alive at once. Each runs (its own wasm instance +
  // asyncify state) until it blocks on I/O or exits, then the scheduler runs
  // the next runnable one. A blocked process carries a readiness predicate;
  // after every step the scheduler wakes any whose predicate is now true
  // (e.g. a pipe got data, a child exited, a key was typed). This is what
  // makes pipelines, subshells, $(...) and background jobs work.
  wake(proc) { if (proc.sched === "blocked" || proc.sched === "new") { proc.sched = "runnable"; if (!this.runQueue.includes(proc)) this.runQueue.push(proc); } }

  schedule() {
    this.turnStart = Date.now(); // wall-clock budget spans the whole turn
    let guard = 0, fastForwards = 0;
    // Virtual time the fast-forward path may burn in THIS turn. A burst-settle
    // timer chain (tmux: 10ms render kick → repeat-timeout → status refresh)
    // finishes well inside this; a PERIODIC timer that re-arms itself every
    // fire (top's 2s select loop) exhausts it after one or two firings instead
    // of spinning 5000 repaints per keystroke and racing the virtual clock
    // hours ahead. Once idle, real wall-clock pacing takes over (the
    // deadline tick in runInteractive re-enters the scheduler when the
    // nearest timeout actually elapses).
    let fastForwardTimeBudget = 3000;
    for (;;) {
      if (++guard > 5_000_000) throw Object.assign(new Error("scheduler runaway"), { runaway: true });
      // wake blocked procs whose I/O is now ready
      for (const p of this.procs) if (p.sched === "blocked" && p.blocked && p.blocked.ready && p.blocked.ready()) this.wake(p);
      const proc = this.runQueue.shift();
      if (!proc) {
        // Nobody is I/O-runnable. Before yielding, fast-forward the virtual
        // clock to the nearest poll/select TIMEOUT so the guest's event-loop
        // timers (e.g. tmux's redraw/status timers) actually fire. Without
        // this a timed poll would block forever and the screen never draws.
        // Bounded per turn so a perpetually-due timer can't freeze the page.
        let soonest = null;
        for (const p of this.procs) if (p.sched === "blocked" && p.blocked && isFinite(p.blocked.deadline) && (!soonest || p.blocked.deadline < soonest.blocked.deadline)) soonest = p;
        // Only fast-forward NEAR-term timers (render kicks, terminal-query
        // escape timeouts, ~sub-second). Far-out periodic timers (tmux's 15s
        // status redraw) are left for real wall-clock to reach on the next
        // interaction — otherwise we'd spin through days of virtual ticks.
        const FASTFWD_HORIZON_MS = 2000;
        if (soonest) {
          const advance = Math.max(0, soonest.blocked.deadline - this.now());
          if (advance <= FASTFWD_HORIZON_MS && advance <= fastForwardTimeBudget && fastForwards++ < 5000) {
            fastForwardTimeBudget -= advance;
            this.clockSkew += advance;
            soonest.timedOut = true;
            this.wake(soonest);
            continue;
          }
        }
        if (this.debug) { const b = this.procs.filter((p) => p.sched === "blocked"); if (b.length) console.error("SCHED-IDLE: " + b.map((p) => `pid${p.pid}(ppid${p.ppid},${p.comm}):${p.blocked && p.blocked.kind}`).join(" | ")); }
        break; // everyone is blocked (idle) or done — yield to the page
      }
      if (proc.sched !== "runnable" || proc.exited) continue;
      this.pumpProc(proc);
    }
  }
  // The nearest finite deadline any blocked process is waiting on, or
  // Infinity. runInteractive uses this to arm a REAL timer so a periodic
  // guest timeout (top's 2s refresh, tmux's status clock) elapses at real
  // wall-clock pace while the page is idle — the virtual clock stays frozen
  // between events, so without the tick an idle full-screen tool never
  // repaints and its clock never moves.
  nearestDeadline() {
    let d = Infinity;
    for (const p of this.procs) if (p.sched === "blocked" && p.blocked && isFinite(p.blocked.deadline)) d = Math.min(d, p.blocked.deadline);
    return d;
  }

  pumpProc(proc) {
    proc.sched = "running";
    proc.blocked = null;
    this.resetBudget();
    for (;;) {
      try {
        if (proc.pendingRewind) { proc.pendingRewind = false; proc.inst.exports.asyncify_start_rewind(proc.asyncifyPtr); }
        proc.inst.exports._start();
      } catch (e) {
        if (e && e.isExec) { if (proc.sched === "booting") return; continue; } // execve replaced the image in place — run the new _start (async boot: resume when the instance resolves)
        // A runaway (or any trap) aborts ONLY this process, never the whole
        // scheduler — so a misbehaving command (e.g. tmux's spin loop) can't
        // take the interactive shell down with it.
        proc.exited = true; proc.sched = "zombie";
        if (e && e.runaway) { proc.exitCode = 137; proc.error = e.message; if (this.hist) this.lastRunaway = Object.entries(this.hist).sort((a, b) => b[1] - a[1]).slice(0, 8).map(([n, c]) => n + "×" + c); }
        else if (e && e.isExit) proc.exitCode = e.code;
        else { proc.exitCode = 139; proc.error = e && e.message; }
        this.onProcExit(proc);
        return;
      }
      if (proc.forkPending) { this.doForkSched(proc); continue; } // parent keeps running
      if (proc.blocked) { proc.sched = "blocked"; proc.pendingRewind = true; return; }
      proc.exited = true; proc.sched = "zombie"; proc.exitCode = proc.exitCode ?? 0; this.onProcExit(proc); return;
    }
  }

  // fork under the scheduler: snapshot the parent, make the child a separate
  // runnable process. Parent resumes (fork → child pid); child resumes
  // later (fork → 0). Both are independently schedulable.
  doForkSched(parent) {
    parent.forkPending = false;
    // One shell, then try the sync instantiation; if the module is past
    // Chrome's main-thread sync-instantiation limit (a 14 MB nvim forking its
    // embedded server), boot the same shell asynchronously instead.
    const { proc: child, env: childEnv0 } = this.spawnShell(parent.mod, parent.argv, parent.pid, undefined);
    let childEnv = null;
    try {
      this.attachInstance(child, new WebAssembly.Instance(parent.mod, { env: childEnv0 }));
    } catch (err) {
      if (!this.isSyncInstantiationLimit(err)) throw err;
      childEnv = childEnv0;
    }
    // Fork state is snapshotted NOW — the parent resumes immediately and
    // keeps mutating its memory/fd table, so an async-booting child must not
    // read them later.
    const src = childEnv ? parent.state.u8.slice() : parent.state.u8;
    const savedBrk = parent.state.brk, savedErrnoPtr = parent.state.errnoPtr;
    child.asyncifyPtr = parent.asyncifyPtr;
    child.pio = clonePio(parent.pio);
    child.pgid = parent.pgid; child.sid = parent.sid;
    child.sigHandlers = parent.sigHandlers ? { ...parent.sigHandlers } : undefined; child.sigMask = parent.sigMask ? [...parent.sigMask] : undefined;
    child.interactive = parent.interactive;
    // The controlling terminal is inherited: a session's whole process tree
    // reads/writes/echoes on that session's tty, never a sibling session's.
    child.tty = parent.tty;
    // forkpty: the child's rewound bridge needs the pty fd stash too (the
    // parent clears only its own copy on its rewind).
    child.forkPtyPending = parent.forkPtyPending;
    child.forkReturn = 0; child.pendingRewind = true;
    const copyIntoChild = () => {
      while (child.state.u8.length < src.length) { const before = child.state.u8.length; child.state.mem.grow(64); child.state.refresh(); if (child.state.u8.length === before) throw new Error("out of wasm memory (fork)"); }
      child.state.u8.set(src);
      child.state.brk = savedBrk;
      child.state.errnoPtr = savedErrnoPtr;
    };
    if (childEnv) {
      this.finishAsyncBoot(child, async () => {
        const instance = await WebAssembly.instantiate(parent.mod, { env: childEnv });
        this.attachInstance(child, instance);
        copyIntoChild();
      });
    } else {
      copyIntoChild();
      child.sched = "runnable";
      this.runQueue.push(child);
    }
    // parent continues now, fork returns the child pid
    parent.forkReturn = child.pid; parent.pendingRewind = true;
  }

  // Release every fd a process holds: drop each open file description's
  // refcount, and on the last reference mark a pipe/socket end closed so its
  // peer sees EOF (readers) / EPIPE (writers). Idempotent — the table is
  // emptied, so a second call is a no-op. EVERY process-exit path funnels
  // through here: the scheduler via onProcExit(), the non-scheduler
  // run()/doFork()/runChildProgram() path via run()'s exit cleanup.
  releaseProcFds(proc) {
    if (!proc.pio) return;
    for (const entry of proc.pio.fds.values()) releaseOfd(entry.ofd);
    proc.pio.fds = new Map();
  }

  onProcExit(proc) {
    // a child exited: its parent's wait / SIGCHLD predicates may now be ready;
    // the scheduler's per-step rescan will wake it.
    if (proc.onExit) proc.onExit(proc.exitCode, proc.error);
    // close all fds so pipe/socket peers see EOF / EPIPE and unblock.
    this.releaseProcFds(proc);
    proc.inst = null; proc.state = null;
    // NOTE: no GENERAL SIGCHLD push here. A kqueue-based parent (libuv)
    // learns of the exit via EVFILT_PROC (see kevent's fired()); a wait-based
    // parent (zsh) via its wait/sigsuspend predicates. Queueing SIGCHLD for
    // every child EINTRs unrelated blocked syscalls and broke tmux's detach.
    //
    // The ONE exception is a forkpty child (nvim's :terminal shell): it is a
    // hand-rolled fork, not a uv_spawn, so no EVFILT_PROC filter watches it —
    // SIGCHLD → waitpid is the only way its parent (which registered a
    // sigaction for it) learns the exit status and can paint
    // "[Process exited N]". Only nvim imports forkpty, so this cannot
    // perturb tmux/zsh child handling.
    if (proc.viaForkpty) {
      const parent = this.procs.find((p) => p.pid === proc.ppid && !p.exited);
      if (parent && parent.interactive && parent.sigHandlers && parent.sigHandlers[20] > 1) {
        (parent.pendingSignals = parent.pendingSignals || []).push(20);
        this.wake(parent);
      }
    }
  }

  // True execve: replace a process's wasm image in place. pid, fd table, cwd
  // and env (all in pio) are inherited; only the program (instance + memory)
  // is swapped. The scheduler then runs the new image's _start.
  prepareExec(proc, mod, argv) {
    // Build the new image FIRST — a large module makes the sync
    // instantiation throw on Chrome's main thread (caller falls back to
    // prepareExecAsync), and nothing of the old process may be mutated yet.
    const state = makeState();
    const env = buildLibc(state, proc.pio.env, this.io, this, proc);
    strictImportEnv(env, mod, { label: "exec", onCall: this.io.onUnimpl });
    const inst = new WebAssembly.Instance(mod, { env });
    this.commitExec(proc, mod, argv, state, inst);
  }

  // The exec-image swap, shared by the sync and async paths. POSIX execve
  // closes every descriptor marked close-on-exec before the new image runs
  // (O_CLOEXEC / F_DUPFD_CLOEXEC / F_SETFD(FD_CLOEXEC)); the rest of the fd
  // table, cwd and env carry across the exec. Release each such descriptor's
  // open file description so its refcount drops — a pipe/socket end held only
  // through a cloexec fd then closes (peer sees EOF/EPIPE) and a saved fd
  // cannot leak into the exec'd program. Snapshot the entries first since we
  // mutate the map while iterating.
  commitExec(proc, mod, argv, state, inst) {
    for (const [fd, entry] of [...proc.pio.fds]) {
      if (entry.cloexec) { proc.pio.fds.delete(fd); releaseOfd(entry.ofd); }
    }
    proc.mod = mod;
    proc.argv = argv;
    proc.comm = (argv[0] || "?").replace(/^.*\//, "").slice(0, 19);
    proc.state = state;
    proc.inst = inst;
    state.mem = inst.exports.memory;
    state.main = inst.exports.main;
    state.refresh();
    const hb = inst.exports.__heap_base;
    state.brk = (typeof hb === "object" ? hb.value : hb) ?? (1 << 20);
    state.errnoPtr = state.alloc(4);
    proc.asyncifyPtr = 0; proc.forkPending = false; proc.blocked = null; proc.pendingRewind = false;
  }

  // execve of a module past the sync-instantiation limit: the process goes
  // into "booting" and the image swap happens when the instance resolves.
  prepareExecAsync(proc, mod, argv) {
    const state = makeState();
    const env = buildLibc(state, proc.pio.env, this.io, this, proc);
    strictImportEnv(env, mod, { label: "exec", onCall: this.io.onUnimpl });
    this.finishAsyncBoot(proc, async () => {
      // (module, imports) form: resolves to the Instance itself.
      const instance = await WebAssembly.instantiate(mod, { env });
      this.commitExec(proc, mod, argv, state, instance);
    });
  }

  doFork(parent) {
    parent.forkPending = false;
    const child = this.spawn(parent.mod, parent.argv, parent.pid, undefined);
    // Snapshot parent linear memory into the child (grow to match, copy).
    const src = parent.state.u8;
    while (child.state.u8.length < src.length) { const before = child.state.u8.length; child.state.mem.grow(64); child.state.refresh(); if (child.state.u8.length === before) throw new Error("out of wasm memory (fork)"); }
    child.state.u8.set(src);
    child.state.brk = parent.state.brk;
    child.state.errnoPtr = parent.state.errnoPtr;
    child.asyncifyPtr = parent.asyncifyPtr;
    // POSIX fork: the child inherits the fd table and cwd. Open-file
    // descriptions (and pipe/socket buffers) are SHARED with the parent;
    // cwd/env/umask/rand are copied by value so a child cannot perturb the
    // parent. The child also inherits the parent's process group + session.
    child.pio = clonePio(parent.pio);
    child.pgid = parent.pgid; child.sid = parent.sid;
    child.sigHandlers = parent.sigHandlers ? { ...parent.sigHandlers } : undefined; child.sigMask = parent.sigMask ? [...parent.sigMask] : undefined;
    child.tty = parent.tty;
    // Rewind the child to the fork callsite; it returns 0 there.
    child.forkReturn = 0;
    child.pendingRewind = true;
    this.run(child); // cooperative: child (and its subtree) runs to completion
    // Release the child's wasm instance + linear memory now that it has
    // exited — keep only pid/ppid/comm/exitCode for ps and waitpid.
    // Without this, a 111-fork tree allocates 111 live instances and the
    // browser runs out of wasm memory.
    child.inst = null; child.state = null;
    // Now resume the parent; fork returns the child pid.
    parent.forkReturn = child.pid;
    parent.pendingRewind = true;
  }

  waitpid(parent, pid, statusPtr) {
    // A specific-pid wait returns that child's status even if it was already
    // reaped by the SIGCHLD handler (idempotent), so the shell's handler-reap
    // followed by a foreground waitpid both succeed instead of "wait failed".
    // A wait-for-any only returns not-yet-reaped children.
    let kid;
    if (pid > 0) kid = this.procs.find((p) => p.pid === pid && p.ppid === parent.pid && p.exited);
    else kid = this.procs.find((p) => p.ppid === parent.pid && p.exited && !p.reaped);
    if (!kid) { if (parent.state && parent.state.errnoPtr) parent.state.view.setUint32(parent.state.errnoPtr, 10, true); return -1; } // ECHILD
    kid.reaped = true;
    if (statusPtr) parent.state.view.setUint32(statusPtr, ((kid.exitCode & 0xff) << 8) | (kid.signal & 0x7f), true);
    return kid.pid;
  }

  kill(pid, sig) { const p = this.procs.find((q) => q.pid === pid); if (p && !p.exited) { p.exited = true; p.signal = sig; p.exitCode = 128 + sig; } return 0; }

  // execve / nested program run: fresh root-like process tree, returns exit.
  runChildProgram(mod, argv, ppid) {
    const child = this.spawn(mod, argv, ppid, undefined);
    this.run(child);
    child.reaped = true;
    child.inst = null; child.state = null; // free the exec'd image's memory
    return { exitCode: child.exitCode };
  }
}

// Run a program (with optional tools for exec), returning its exit code.
// Same signature shape as the simple runner so callers swap easily.
export function runProgram(mod, argv, onOutput, onUnimpl, opts = {}) {
  const mgr = new Manager({ onOutput, onUnimpl: onUnimpl || (() => {}) }, opts.tools || new Map());
  for (const m of opts.mounts || []) mgr.mountFiles(m.at, m.entries);
  const root = mgr.spawn(mod, argv, 0, opts.env);
  // Optional fixed stdin for a non-interactive guest. Back fd 0 with a
  // pre-filled, write-closed pipe so read()/poll() on stdin returns these bytes
  // and then EOF — exactly the shape a command sees on the receiving end of a
  // shell pipe (`printf … | grep …`), and the same pipe a native `yos wasm`
  // run gets when its stdin is fed. Absent opts.stdin, fd 0 stays the
  // EOF-on-read char device newPio() wires up, so existing callers are
  // unaffected.
  if (opts.stdin != null) {
    const data = typeof opts.stdin === "string" ? new TextEncoder().encode(opts.stdin) : new Uint8Array(opts.stdin);
    const pipe = { chunks: data.length ? [data] : [], total: data.length, readClosed: false, writeClosed: true, ancFds: [] };
    root.pio.fds.set(0, { ofd: { kind: "pipe", end: "r", pipe, refs: 1 }, cloexec: false });
  }
  try { mgr.run(root); } catch (e) { return { exitCode: "trap", error: e.message, procs: mgr.procs.length }; }
  return { exitCode: typeof root.exitCode === "number" ? root.exitCode : 0, error: root.error, procs: mgr.procs.length };
}

// ---- Interactive engine: ONE process tree, many terminal sessions ------
// A single Manager (one pid namespace, one vfs, one scheduler, one virtual
// clock) hosts any number of long-lived interactive SESSIONS. Each session
// is a root process (e.g. a fresh zsh) with its OWN tty — line discipline,
// window size, output sink — inherited by everything it forks, so a
// session's output/echo/SIGWINCH never leak to a sibling session. Process
// state is engine-global: ps/top enumerate every session's processes and
// kill() reaches across sessions, exactly like logins on one machine.
//
//   opts.tools               name -> compiled module, for exec/fork
//   opts.mounts              [{at, entries}] read-only trees (mounted once)
//   opts.onOutput(fd, text)  fallback sink for tty-less processes
//   opts.onUnimpl, opts.debug, opts.trace
//
// engine.startSession(mod, argv, {cols, rows, env, onOutput, onExit})
//   -> controller { write, writeBytes, resize, running, dispose, proc, mgr }
export function createInteractiveEngine(opts = {}) {
  const mgr = new Manager({ onOutput: opts.onOutput || (() => {}), onUnimpl: opts.onUnimpl || (() => {}) }, opts.tools || new Map());
  mgr.interactive = true;
  mgr.debug = !!opts.debug;
  if (opts.trace) mgr.trace = [];
  for (const m of opts.mounts || []) mgr.mountFiles(m.at, m.entries);
  // yfs — the lazily-fetched web filesystem (docs/yfs.md). `client`
  // comes from yfs_client.mjs openYfs()/openYfsDir(); `compile` is the
  // module compiler for exec-on-demand (the iframe passes compileGuest
  // so lazily-executed tools get the same wasm patching as boot tools).
  if (opts.yfs && opts.yfs.client) {
    mgr.compileModule = opts.yfs.compile || null;
    mgr.mountYfs(opts.yfs.client);
  }

  const sessions = [];
  const liveSessions = () => sessions.filter((session) => !session.root.exited);

  // Drive the scheduler: run every runnable process until all are idle
  // (blocked) or done, then yield to the page. Re-entered on each keystroke
  // of ANY session — the scheduler itself is engine-wide.
  const runSched = () => {
    try { mgr.schedule(); }
    catch (e) {
      // A scheduler-level failure (runaway abort, trap outside a process
      // turn) is fatal for the shared engine — every live session learns
      // its root died.
      for (const session of sessions) {
        if (session.root.exited) continue;
        session.root.exited = true;
        session.root.onExit(e && e.runaway ? 137 : 139, e && e.message);
      }
    }
    if (!liveSessions().length) { stopSettle(); stopTick(); }
    else armTick();
  };
  // An async-booting process (module past Chrome's sync-instantiation limit)
  // became runnable — re-enter the scheduler exactly like a keystroke does.
  mgr.onAsyncBoot = () => { if (liveSessions().length) runSched(); };

  // DEADLINE TICK — real-time pacing for periodic guest timers while IDLE.
  // The virtual clock is frozen between events, so a full-screen tool that
  // sleeps in select()/kevent with a timeout (top's 2s refresh, tmux's status
  // clock) would repaint only on keystrokes. After each scheduler turn, arm
  // ONE real timer for the nearest finite guest deadline; when it fires
  // (input has been quiet — every keystroke cancels it, the settle timer
  // re-arms it), advance the clock by the real elapsed time and run the
  // scheduler: the due timeout elapses at wall-clock pace, top repaints once,
  // blocks again, and the next tick is armed for its next deadline. No finite
  // deadlines → no timer → the page stays fully frozen as before.
  let tickTimer = null;
  const stopTick = () => { if (tickTimer) { clearTimeout(tickTimer); tickTimer = null; } };
  const armTick = () => {
    stopTick();
    if (!liveSessions().length) return;
    const deadline = mgr.nearestDeadline();
    if (!isFinite(deadline)) return;
    const delay = Math.min(Math.max(deadline - mgr.now(), 200), 60_000);
    tickTimer = setTimeout(onTick, delay);
    if (tickTimer && tickTimer.unref) tickTimer.unref();
  };
  const onTick = () => {
    tickTimer = null;
    if (!liveSessions().length) return;
    const wall = Date.now();
    const gap = wall - lastWall;
    lastWall = wall;
    if (gap > 0) mgr.clockSkew += gap;
    runSched(); // re-arms the next tick
  };

  // One-shot SETTLE timer. While the user types at speed the clock stays FROZEN:
  // every keystroke is delivered at the same clock and each repaint is
  // deterministic. That is essential — tmux renders its pane incrementally, and
  // advancing the clock between two keystrokes of a burst (or continuously, via
  // a background heartbeat) fires timer work that desyncs the incremental redraw
  // so the typed line / command output never paints.
  //
  // Each keystroke (re-)arms a single timer. While keystrokes keep arriving it
  // never fires (clock frozen → deterministic). Once input has been quiet for
  // SETTLE_MS the burst is over and the pane is idle/stable, so the timer fires
  // ONCE: it advances the virtual clock by the real elapsed time and runs the
  // scheduler a single time. That lone advance is what lets a guest timer that
  // came due during the pause elapse — in particular tmux's repeat-timeout,
  // which resets the "prefix" key table to "root" after a -r binding
  // (next/previous-window); without it the next C-b is swallowed by send-prefix
  // and detach / new-pane / … silently stop working. It does NOT re-arm: a
  // continuously-ticking clock is what corrupts tmux's render, so the clock goes
  // back to frozen until the next keystroke. (Timers are unref'd so they never
  // keep a node test process alive past its own exit.) The clock is
  // engine-global, so a burst in one session freezes it for all — the same
  // determinism argument applies unchanged.
  const SETTLE_MS = 600;
  let lastWall = Date.now(), settleTimer = null;
  const stopSettle = () => { if (settleTimer) { clearTimeout(settleTimer); settleTimer = null; } };
  const onSettle = () => {
    settleTimer = null;
    if (!liveSessions().length) return;
    const wall = Date.now();
    const gap = wall - lastWall;
    lastWall = wall;
    if (gap > 0) mgr.clockSkew += gap;
    runSched();
  };
  const armSettle = () => {
    if (!liveSessions().length) return;
    lastWall = Date.now();
    stopSettle();
    // Typing burst: the deadline tick must not fire (a clock advance between
    // burst keystrokes desyncs tmux's incremental render). The settle timer
    // takes over and its runSched re-arms the tick once input is quiet.
    stopTick();
    settleTimer = setTimeout(onSettle, SETTLE_MS);
    if (settleTimer && settleTimer.unref) settleTimer.unref();
  };

  const ECHO = 0x8, ICANON = 0x100;

  // End a session: kill its root and every descendant (the session's whole
  // process tree), leaving sibling sessions untouched.
  const killTree = (rootPid) => {
    const doomed = new Set([rootPid]);
    let grew = true;
    while (grew) {
      grew = false;
      for (const p of mgr.procs) if (!doomed.has(p.pid) && doomed.has(p.ppid)) { doomed.add(p.pid); grew = true; }
    }
    for (const p of mgr.procs) {
      if (!doomed.has(p.pid) || p.exited) continue;
      p.exited = true; p.sched = "zombie"; p.signal = 9; p.exitCode = 137;
      mgr.onProcExit(p);
    }
  };

  function startSession(mod, argv, sessionOpts = {}) {
    // Per-session terminal: line discipline state, size, and the output
    // sink. lflag defaults to a cooked terminal (ECHO|ECHOE|ICANON|ISIG|
    // IEXTEN); the guest's editor clears bits via tcsetattr to take over
    // echo/editing. oflag 0x3 = OPOST|ONLCR until an app sets raw.
    const tty = {
      inbuf: [], lineBuf: [],
      lflag: 0x8 | 0x2 | 0x100 | 0x80 | 0x400,
      oflag: 0x3,
      cols: sessionOpts.cols || 80, rows: sessionOpts.rows || 24,
      onOutput: sessionOpts.onOutput || (() => {}),
    };
    const root = mgr.spawn(mod, argv, 0, sessionOpts.env);
    root.interactive = true;
    root.tty = tty;
    root.onExit = sessionOpts.onExit || (() => {});
    const session = { root, tty };
    sessions.push(session);

    // Feed one input byte through THIS session's tty line discipline, then
    // run the (shared) scheduler so whichever process is waiting on this
    // terminal wakes up.
    const feed = (code) => {
      if (root.exited) return;
      const echo = !!(tty.lflag & ECHO);
      const canon = !!(tty.lflag & ICANON);
      // ICRNL belongs to the COOKED input discipline: terminals send CR
      // for Enter and canonical mode maps it to NL. A raw-mode app gets
      // the real 0x0d — fzy binds its accept key to '\r' and a blanket
      // translation made Enter a no-op in its picker.
      if (code === 13 && canon) code = 10;
      if (!canon) { // raw: the guest's editor echoes + edits
        tty.inbuf.push(code);
        if (echo) tty.onOutput(1, code === 10 ? "\r\n" : String.fromCharCode(code));
        runSched();
        return;
      }
      if (code === 127 || code === 8) { if (tty.lineBuf.length) { tty.lineBuf.pop(); if (echo) tty.onOutput(1, "\b \b"); } return; }
      if (code === 21) { while (tty.lineBuf.length) { tty.lineBuf.pop(); if (echo) tty.onOutput(1, "\b \b"); } return; } // ^U kill line
      if (code === 10) { tty.lineBuf.push(10); if (echo) tty.onOutput(1, "\r\n"); for (const b of tty.lineBuf) tty.inbuf.push(b); tty.lineBuf = []; runSched(); return; }
      if (code === 3 || code === 4) { tty.inbuf.push(code); runSched(); return; } // ^C/^D pass through
      tty.lineBuf.push(code);
      if (echo) tty.onOutput(1, code < 32 ? "^" + String.fromCharCode(code + 64) : String.fromCharCode(code));
    };

    mgr.wake(root);
    Promise.resolve().then(runSched); // start after the controller is returned

    return {
      write(str) { for (let i = 0; i < str.length; i++) feed(str.charCodeAt(i) & 0xff); armSettle(); },
      writeBytes(bytes) { for (const b of bytes) feed(b & 0xff); armSettle(); },
      resize(cols, rows) {
        if (tty.cols === cols && tty.rows === rows) return;
        tty.cols = cols; tty.rows = rows;
        // Deliver SIGWINCH (FreeBSD = 28) to every live process ON THIS
        // SESSION'S TTY that installed a handler — that's how a full-screen
        // app (tmux, an editor) learns the terminal resized and re-reads
        // TIOCGWINSZ. Sibling sessions keep their own sizes.
        for (const p of mgr.procs) { if (!p.exited && p.tty === tty && p.sigHandlers && p.sigHandlers[28] > 1) { (p.pendingSignals = p.pendingSignals || []).push(28); mgr.wake(p); } }
        runSched();
        armSettle();
      },
      running: () => !root.exited,
      dispose: () => { killTree(root.pid); if (!liveSessions().length) { stopSettle(); stopTick(); } },
      proc: root,
      mgr,
    };
  }

  return { mgr, startSession };
}

// Run a guest as a LONG-LIVED INTERACTIVE process: stdin is a terminal, and
// read()/poll() on it block (asyncify-suspend) until the page feeds a
// keystroke. Returns a controller; the guest keeps its memory, fds, cwd,
// env and shell state across keystrokes (a real session, not per-command).
// Single-session convenience wrapper over createInteractiveEngine() — for
// multiple shells on ONE process tree, create the engine once and call
// startSession per terminal.
//
//   opts.onOutput(fd, text)   terminal bytes from the guest
//   opts.onExit(code, err)    the process ended
//   opts.tools                name -> compiled module, for exec/fork
//   opts.env, cols, rows
export function runInteractive(mod, argv, opts = {}) {
  const engine = createInteractiveEngine({
    tools: opts.tools, mounts: opts.mounts, onOutput: opts.onOutput,
    onUnimpl: opts.onUnimpl, debug: opts.debug, trace: opts.trace,
  });
  return engine.startSession(mod, argv, {
    cols: opts.cols, rows: opts.rows, env: opts.env,
    onOutput: opts.onOutput, onExit: opts.onExit,
  });
}

export { loadLiblua };
