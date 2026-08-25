// yetty FFI runtime for the generated TypeScript/JavaScript bindings.
// Hand-written: library discovery, result decoding, and the generic
// spec-application rules the generated classes share. All per-class
// knowledge lives in the generated spec tables (ydraw.mjs) — never here.
import koffi from "koffi";
import { existsSync, readdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

function libraryPath() {
  const override = process.env.YETTY_FFI_LIB;
  if (override) {
    return override;
  }
  // The checkout root sits two levels above bindings/typescript/.
  const repo = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
  const candidates = [];
  let entries = [];
  try {
    entries = readdirSync(repo);
  } catch {
    entries = [];
  }
  // Every build-desktop-* configuration is a candidate (release, debug,
  // asan, ...); release trees first — they are the documented dev default.
  const buildTrees = entries.filter((entry) => entry.startsWith("build-desktop-"));
  buildTrees.sort((left, right) => {
    const leftRelease = left.endsWith("-release") ? 0 : 1;
    const rightRelease = right.endsWith("-release") ? 0 : 1;
    return leftRelease - rightRelease || left.localeCompare(right);
  });
  for (const entry of buildTrees) {
    candidates.push(join(repo, entry, "src", "yetty", "yffi", "libyetty_ffi.so"));
  }
  candidates.push("libyetty_ffi.so");
  for (const candidate of candidates) {
    if (candidate === "libyetty_ffi.so" || existsSync(candidate)) {
      return candidate;
    }
  }
  throw new Error(
    "yetty FFI: libyetty_ffi.so not found; set YETTY_FFI_LIB or build the desktop tree");
}

const lib = koffi.load(libraryPath());

// Flat 48-byte view of `struct <T>_result { int ok; union { value;
// struct yetty_ycore_error error; }; }` — word0 overlays both the
// success value (pointer/int) and error.msg, so one view decodes all
// result types the bindings touch.
koffi.struct("yetty_result_view", {
  ok: "int32", word0: "void *", word1: "void *", word2: "void *",
  word3: "int32", word4: "void *",
});
koffi.struct("yetty_ycore_buffer", {
  data: "void *", capacity: "size_t", size: "size_t",
});

const signatures = {};
const bound = new Map();

export function registerSignatures(table) {
  Object.assign(signatures, table);
}

function funcFor(symbol) {
  let fn = bound.get(symbol);
  if (!fn) {
    const signature = signatures[symbol];
    if (!signature) {
      throw new Error(`yetty FFI: no signature registered for ${symbol}`);
    }
    fn = lib.func(signature);
    bound.set(symbol, fn);
  }
  return fn;
}

// Whether the loaded FFI library exports `symbol` (its signature must be
// registered). Used to gate feature-dependent classes on the actual build.
export function hasSymbol(symbol) {
  try {
    funcFor(symbol);
    return true;
  } catch {
    return false;
  }
}

// Constructor guard for feature-gated classes: fail with the feature name
// instead of a raw missing-symbol error from the first native call.
export function requireFeature(className, symbol, feature) {
  if (!hasSymbol(symbol)) {
    throw new Error(
      `${className} requires a build with the ${feature} feature enabled ` +
      `(native symbol ${symbol} is missing from the loaded library)`);
  }
}

function errorMessage(view, fallback) {
  if (view.word0 === null) {
    return fallback;
  }
  return koffi.decode(view.word0, "char", 512);
}

// Call a result-returning FFI function; throw on error, return the
// success word (object pointer for creates, unused otherwise).
export function invoke(symbol, ...args) {
  const view = funcFor(symbol)(...args);
  if (!view.ok) {
    throw new Error(errorMessage(view, `${symbol} failed`));
  }
  return view.word0;
}

export function construct(spec, primaryOrOptions, maybeOptions) {
  const handle = invoke(spec.create, null);
  // Everything after create must not leak the native object: a rejected
  // setter or an unknown option throws before the wrapper is registered
  // with the finalizer, so free through the class destructor here.
  try {
    let options = primaryOrOptions;
    if (typeof primaryOrOptions === "string") {
      if (!spec.primary) {
        throw new Error(`${spec.className}: no primary value accepted`);
      }
      invoke(spec.primary, handle, primaryOrOptions);
      options = maybeOptions;
    }
    if (options !== undefined && options !== null) {
      applySpec(spec, handle, options);
    }
  } catch (error) {
    try {
      invoke(spec.destroy ?? "yetty_yclass_object_free", handle);
    } catch {
      // The construction error is the one worth surfacing.
    }
    throw error;
  }
  return handle;
}

export function applySpec(spec, handle, options) {
  for (const [key, value] of Object.entries(options)) {
    const field = spec.fields[key];
    if (!field) {
      throw new Error(`${spec.className}: unknown option '${key}'`);
    }
    if (field.kind === "scalar") {
      const numeric = typeof value === "boolean" ? (value ? 1 : 0) : value;
      invoke(field.sym, handle, numeric);
    } else if (field.kind === "cstr") {
      invoke(field.sym, handle, String(value));
    } else if (field.kind === "multi") {
      // One nesting level flattened: view: [[x0, x1], [y0, y1]] and
      // view: [x0, x1, y0, y1] both fit a 4-scalar slot. The slot's
      // arity is fixed — reject wrong lengths with a clear error
      // instead of a low-level argument-count failure.
      const flat = value.flat(1);
      if (flat.length !== field.n) {
        throw new Error(
          `${spec.className}: ${key} expects ${field.n} values, got ${flat.length}`);
      }
      invoke(field.sym, handle, ...flat);
    } else if (field.kind === "buffer") {
      const floats = Float32Array.from(value);
      invoke(field.sym, handle, {
        data: floats, capacity: floats.byteLength, size: floats.byteLength,
      });
    } else if (field.kind === "adder") {
      for (const element of value) {
        invoke(field.sym, handle, element.handle);
      }
    } else {
      throw new Error(`${spec.className}: bad field kind '${field.kind}'`);
    }
  }
}

export function requireHandle(object, className) {
  if (!object.handle) {
    throw new Error(`${className}: object already destroyed`);
  }
}

// Reclaim owned native objects when their wrapper is collected, so
// temporaries like dlist.add(new Circle({...})) do not accumulate in
// long-running hosts. Explicit destroy() unregisters first.
const finalizers = new FinalizationRegistry((entry) => {
  try {
    invoke(entry.destroy, entry.handle);
  } catch {
    // Finalization is best-effort; a teardown race must not throw.
  }
});

export function adopt(object, spec) {
  const entry = {
    destroy: spec.destroy ?? "yetty_yclass_object_free",
    handle: object.handle,
  };
  object.finalizerEntry = entry;
  finalizers.register(object, entry, entry);
}

export function destroyObject(object, spec) {
  if (object.handle) {
    if (object.finalizerEntry) {
      finalizers.unregister(object.finalizerEntry);
      object.finalizerEntry = null;
    }
    invoke(spec.destroy ?? "yetty_yclass_object_free", object.handle);
    object.handle = null;
  }
}
