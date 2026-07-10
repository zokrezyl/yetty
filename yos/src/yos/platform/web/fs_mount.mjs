// fs_mount.mjs — sources for Manager.mountFiles() (yos_proc.mjs): expose a
// real read-only file tree (nvim's $VIMRUNTIME, zsh's share dir, …) to the
// guest VFS.
//
// Two sources, one entry shape ({ path, data? , read? }):
//   - node:    entriesFromDir(root) walks the real directory and returns
//              LAZY entries — file bytes readFileSync only when the guest
//              actually opens the file.
//   - browser: the server packs the tree into ONE fetchable blob
//              (packFromDir on the serve side, parsePack on the page side;
//              entry data are zero-copy subarray views into the blob).
//
// Pack format ("YFS1"): magic(4) | u32le index-length | index JSON | blobs.
// index = [{ p: "nvim/runtime/lua/vim/termcap.lua", o: blob-offset, s: size }]
// with offsets relative to the end of the index.

const PACK_MAGIC = "YFS1";

// node-only: lazy entries from a real directory tree (follows symlinks — nix
// store trees are symlink-heavy).
export async function entriesFromDir(rootDir) {
	const { readdirSync, readFileSync, statSync } = await import("node:fs");
	const { join } = await import("node:path");
	const entries = [];
	const walk = (dir, rel) => {
		for (const name of readdirSync(dir)) {
			const full = join(dir, name);
			const relPath = rel ? rel + "/" + name : name;
			let st;
			try { st = statSync(full); } catch { continue; } // dangling symlink
			if (st.isDirectory()) walk(full, relPath);
			else if (st.isFile()) entries.push({ path: relPath, read: () => { try { return readFileSync(full); } catch { return null; } } });
		}
	};
	walk(rootDir, "");
	return entries;
}

// node-only: pack a directory tree into one blob (served as /fs/pack.bin).
export async function packFromDir(rootDir) {
	const list = await entriesFromDir(rootDir);
	const blobs = [];
	const index = [];
	let off = 0;
	for (const entry of list) {
		const data = entry.read();
		if (!data) continue;
		index.push({ p: entry.path, o: off, s: data.length });
		blobs.push(data);
		off += data.length;
	}
	const enc = new TextEncoder();
	const indexBytes = enc.encode(JSON.stringify(index));
	const out = new Uint8Array(8 + indexBytes.length + off);
	out.set(enc.encode(PACK_MAGIC), 0);
	new DataView(out.buffer).setUint32(4, indexBytes.length, true);
	out.set(indexBytes, 8);
	let w = 8 + indexBytes.length;
	for (const b of blobs) { out.set(b, w); w += b.length; }
	return out;
}

// browser + node: entries from a fetched pack (zero-copy subarray views).
export function parsePack(buffer) {
	const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
	const dec = new TextDecoder();
	if (dec.decode(bytes.subarray(0, 4)) !== PACK_MAGIC) throw new Error("fs pack: bad magic");
	const indexLen = new DataView(bytes.buffer, bytes.byteOffset).getUint32(4, true);
	const index = JSON.parse(dec.decode(bytes.subarray(8, 8 + indexLen)));
	const blobBase = 8 + indexLen;
	return index.map((e) => ({ path: e.p, data: bytes.subarray(blobBase + e.o, blobBase + e.o + e.s) }));
}
