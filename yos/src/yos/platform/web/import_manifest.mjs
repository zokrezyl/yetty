// Prototype boundary for the browser yos runtime (issue #21, milestone 1).
//
// The browser runtime is a PROTOTYPE. It hand-implements a subset of the
// FreeBSD/yos `env.*` libc surface in JavaScript instead of reusing the
// desktop yos C bridge + subsystems. The production direction is the
// reverse: a shared yos runtime/bridge layer owns the FreeBSD/yos ABI
// semantics, and JavaScript only supplies browser effects (Worker
// lifecycle, terminal I/O, storage, timers).
//
// To keep that boundary explicit and stop it from silently drifting, this
// module enforces one rule: NOTHING returns a silent 0 for an import the
// prototype does not implement. Every guest import is either backed by a
// real implementation in the engine's `env`, or it FAILS LOUDLY the first
// time the guest calls it. That makes missing semantics test-visible
// instead of hidden behind a permissive fallback.
//
// This replaces the previous catch-all `new Proxy(env, ...)` and the
// "pre-fill every import with a no-op that returns 0" loops.

// Non-function imports (memory, tables, dylink globals) are passed through
// untouched — they are not libc calls and must not be wrapped.
const PASSTHROUGH_KINDS = new Set(["global", "memory", "table"]);

// Imports the prototype DOES provide, but with KNOWN-PARTIAL / non-faithful
// semantics: placeholders that return success or dummy values rather than
// implementing real behaviour. They are tracked here (not to change their
// behaviour, but) so reviewers and the parity contract can see exactly
// which semantics are not yet real on the browser target. The issue calls
// these out specifically: "Returning success or dummy values here will
// hide missing semantics and make later parity bugs harder to find."
export const PROTOTYPE_PARTIAL = new Set([
  // fd plumbing with no real backing
  "pipe", "dup", "dup2", "fcntl",
  // readiness — always reports "nothing ready"
  "poll", "select", "pselect",
  // memory mapping — no real mapping, advisory no-ops
  "mmap", "munmap", "mprotect", "madvise",
  // tty / ioctl — no pty
  "ioctl", "tcgetattr", "tcsetattr", "tcgetpgrp", "tcsetpgrp", "ttyname",
  // signals — accepted but not delivered
  "signal", "sigaction", "sigprocmask", "sigsuspend",
  "sigemptyset", "sigfillset", "sigaddset", "alarm", "raise", "killpg",
  // cwd — single fixed "/" in some engines
  "chdir", "fchdir", "getcwd",
  // process state placeholders
  "setpgid", "getrusage", "getrlimit", "setrlimit", "times", "nice",
]);

// Classify the function imports a compiled module declares against the env
// the prototype actually provides. Returns the lists, never throws.
export function classifyImports(module, env) {
  const imports = WebAssembly.Module.imports(module);
  const wanted = imports
    .filter((entry) => !PASSTHROUGH_KINDS.has(entry.kind) && entry.module === "env")
    .map((entry) => entry.name);
  const supported = [];
  const partial = [];
  const unsupported = [];
  for (const name of wanted) {
    if (typeof env[name] === "function") {
      (PROTOTYPE_PARTIAL.has(name) ? partial : supported).push(name);
    } else {
      unsupported.push(name);
    }
  }
  return { wanted, supported, partial, unsupported };
}

// The loud-failure stub installed for every unsupported import. It throws
// the first time the guest actually calls the function — a deliberate hard
// failure with a clear diagnostic, never a silent no-op. `onCall` (if given)
// is notified before the throw so engines can log the worklist.
function makeUnsupportedStub(name, label, onCall) {
  const stub = (...args) => {
    if (typeof onCall === "function") onCall(name, args);
    throw new Error(
      `yos[${label}]: unsupported libc import 'env.${name}' called with ` +
        `${args.length} arg(s). The browser yos prototype has no ` +
        `implementation for this function; it fails loudly instead of ` +
        `returning a silent 0 (issue #21 prototype boundary). Implement it ` +
        `in the shared yos bridge, or mark it as a deliberate unsupported ` +
        `import.`,
    );
  };
  stub.yosUnsupported = true;
  return stub;
}

// Harden a real env in place: install a loud-failure stub for every guest
// import the prototype does not implement, so the module still instantiates
// (WebAssembly requires every import be satisfied) but any call to a missing
// libc function throws a clear diagnostic instead of silently returning 0.
//
//   opts.label   — short tag for diagnostics (e.g. "mt", "proc", "zsh")
//   opts.onCall  — (name, args) => void, notified when a loud stub fires
//   opts.report  — (info) => void, notified once at link time with the split
//   opts.strict  — if true, throw at link time when any import is unsupported
//
// Returns { env, info }. `env` is the same object passed in (mutated), so
// callers may use either the return value or the original reference.
export function strictImportEnv(realEnv, module, opts = {}) {
  const label = opts.label || "web";
  const info = classifyImports(module, realEnv);
  for (const name of info.unsupported) {
    realEnv[name] = makeUnsupportedStub(name, label, opts.onCall);
  }
  if (typeof opts.report === "function") opts.report(info);
  if (opts.strict && info.unsupported.length) {
    throw new Error(
      `yos[${label}]: ${info.unsupported.length} unsupported libc import(s) ` +
        `required by this module: ${info.unsupported.slice().sort().join(", ")}`,
    );
  }
  return { env: realEnv, info };
}

// Pretty one-line summary for logging the link-time split.
export function describeImports(info) {
  return (
    `imports: ${info.wanted.length} total, ` +
    `${info.supported.length} supported, ` +
    `${info.partial.length} partial, ` +
    `${info.unsupported.length} unsupported` +
    (info.unsupported.length
      ? ` [${info.unsupported.slice().sort().join(", ")}]`
      : "")
  );
}
