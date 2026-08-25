// yfs_client.mjs — client for the yfs static web filesystem.
//
// yfs is the lazily-fetched, HTTP-served guest filesystem (layout and
// listing format: docs/yfs.md in the yetty tree). This module knows the
// URL scheme and the aggregate manifest; it does no VFS work — the
// engine (yos_proc.mjs mountYfs) materializes nodes from `dirs` and
// calls fetchBody / readBodySync for lazy bodies.
//
//   const yfs = await openYfs("/yfs");              // browser
//   const yfs = openYfsDir("/path/to/bundle/yfs");  // node (sync bodies)
//
// Shape of a client:
//   version        resolved tree version
//   dirs           { "": [entry...], "bin": [entry...], ... }
//   fetchBody(dirPath, entry) -> Promise<Uint8Array>
//   readBodySync(dirPath, entry) -> Uint8Array | null   (node only)
//   readBodySyncFallback(dirPath, entry) -> Uint8Array | null
//     last-resort SYNCHRONOUS body read for contexts that cannot
//     asyncify-suspend (a lua_* forwarder on the stack — liblua is not
//     asyncify-instrumented). Browser: sync XHR (blocks the thread — use
//     only when suspension would corrupt the guest); node: readBodySync.
//   entryAt(path) -> { dirPath, entry } | null
//   fetchBodyAtPath(path) -> Promise<Uint8Array>

function bodyLocation(dirPath, entry) {
  if (entry.b) return { blob: entry.h };
  const relative = dirPath ? dirPath + "/" + entry.n : entry.n;
  return { mirrored: relative };
}

function encodePath(relative) {
  return relative.split("/").map(encodeURIComponent).join("/");
}

function makeEntryLookup(dirs) {
  return (path) => {
    const clean = path.replace(/^\/+/, "").replace(/\/+$/, "");
    const slash = clean.lastIndexOf("/");
    const dirPath = slash === -1 ? "" : clean.slice(0, slash);
    const name = slash === -1 ? clean : clean.slice(slash + 1);
    const entries = dirs[dirPath];
    if (!entries) return null;
    const entry = entries.find((candidate) => candidate.n === name);
    return entry ? { dirPath, entry } : null;
  };
}

// Browser client — bodies via fetch(), relative to the yfs root URL
// (e.g. "/yfs"). `version` pins a tree; default follows current.json.
export async function openYfs(rootUrl, options = {}) {
  const getJson = async (url) => {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`yfs: ${url} -> ${response.status}`);
    return response.json();
  };
  const version = options.version ||
    (await getJson(`${rootUrl}/current.json`)).version;
  const manifest = await getJson(`${rootUrl}/${version}/guest.yfs`);
  const dirs = manifest.dirs;
  const entryAt = makeEntryLookup(dirs);

  const bodyUrl = (dirPath, entry) => {
    const location = bodyLocation(dirPath, entry);
    return location.blob !== undefined
      ? `${rootUrl}/blob/${location.blob}`
      : `${rootUrl}/${version}/guest/${encodePath(location.mirrored)}`;
  };

  const fetchBody = async (dirPath, entry) => {
    const url = bodyUrl(dirPath, entry);
    const response = await fetch(url);
    if (!response.ok) throw new Error(`yfs: ${url} -> ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
  };

  // Synchronous XHR body read. Sync XHR forbids responseType in window
  // context, so the bytes come back through the charset=x-user-defined
  // text path (one char per byte, low 8 bits significant).
  const readBodySyncFallback = typeof XMLHttpRequest !== "function" ? null :
    (dirPath, entry) => {
      const url = bodyUrl(dirPath, entry);
      const request = new XMLHttpRequest();
      request.open("GET", url, false);
      request.overrideMimeType("text/plain; charset=x-user-defined");
      request.send(null);
      if (request.status !== 200) throw new Error(`yfs: ${url} -> ${request.status}`);
      const text = request.responseText;
      const bytes = new Uint8Array(text.length);
      for (let i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i) & 0xff;
      return bytes;
    };

  return {
    version, dirs, entryAt, fetchBody,
    readBodySync: null,
    readBodySyncFallback,
    fetchBodyAtPath: async (path) => {
      const hit = entryAt(path);
      if (!hit) throw new Error(`yfs: no such entry: ${path}`);
      return fetchBody(hit.dirPath, hit.entry);
    },
  };
}

// Node client — the yfs directory on the local filesystem (an unpacked
// bundle). Bodies read synchronously, so engine opens never suspend.
// Needs node >= 20.16 (process.getBuiltinModule) — the test harnesses
// that use it are node-only anyway.
export function openYfsDir(rootDir, options = {}) {
  const builtin = typeof process !== "undefined" &&
    process.getBuiltinModule ? process.getBuiltinModule("node:fs") : null;
  if (!builtin) {
    throw new Error("yfs: openYfsDir needs node with process.getBuiltinModule");
  }

  const readJson = (path) => JSON.parse(builtin.readFileSync(path, "utf8"));
  const version = options.version ||
    readJson(`${rootDir}/current.json`).version;
  const manifest = readJson(`${rootDir}/${version}/guest.yfs`);
  const dirs = manifest.dirs;
  const entryAt = makeEntryLookup(dirs);

  const bodyPath = (dirPath, entry) => {
    const location = bodyLocation(dirPath, entry);
    return location.blob !== undefined
      ? `${rootDir}/blob/${location.blob}`
      : `${rootDir}/${version}/guest/${location.mirrored}`;
  };
  const readBodySync = (dirPath, entry) => {
    const bytes = builtin.readFileSync(bodyPath(dirPath, entry));
    return new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  };

  return {
    version, dirs, entryAt, readBodySync,
    readBodySyncFallback: readBodySync,
    fetchBody: async (dirPath, entry) => readBodySync(dirPath, entry),
    fetchBodyAtPath: async (path) => {
      const hit = entryAt(path);
      if (!hit) throw new Error(`yfs: no such entry: ${path}`);
      return readBodySync(hit.dirPath, hit.entry);
    },
  };
}
