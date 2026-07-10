// Browser-engine directory-listing regression test (epic #32).
//
// Guards the opendir/DIR* fix: the browser engine used to return a bare integer
// handle from opendir, but the FreeBSD dirfd() macro reads dd_fd from the DIR
// struct in guest memory. That made fts (ls -alrt / find / du) read a garbage
// fd in fts_safe_changedir, fail the st_dev/st_ino consistency check, and mark
// every entry FTS_NS — "yos-tool: <name>: Error 2" with "total 0". opendir now
// returns a real DIR* whose dd_fd is a valid directory fd.
//
// Runs the SAME ls.wasm the desktop runs, through the browser process engine.
//
// Run: node ls_dir_test.mjs   (or `make test-browser-ls`)
import { runYos } from "./yos_run.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, "..", "..", "..", "..");
const lsWasm = join(repo, "result", "libexec", "ls");

async function run(args) {
	const mod = await compileGuest(readFileSync(lsWasm));
	let out = "", err = "";
	const r = await runYos(mod, ["ls", ...args], { onOutput: (fd, t) => { if (fd === 2) err += t; else out += t; } });
	return { out, err, code: r?.exitCode ?? r };
}

const failures = [];
const check = (name, cond, detail) => { if (!cond) failures.push(`${name}: ${detail}`); };

// 1. ls -alrt (the reported failure): must produce a long listing, no fts error.
{
	const { out, err } = await run(["-alrt"]);
	check("ls -alrt no fts error", !/Error 2/.test(out + err), `saw "Error 2": ${(out + err).split("\n")[0]}`);
	check("ls -alrt long format", /drwx/.test(out), "no directory (drwx) line in long listing");
	check("ls -alrt lists README", /README/.test(out), "README missing from listing");
	check("ls -alrt lists dotdirs", /\s\.\.\s|\s\.\.$/.test(out) || /\s\.\s|\s\.$/m.test(out), ". / .. missing (fts entries not stat'd)");
}

// 2. ls -l of a subdir with only files.
{
	const { out } = await run(["-l", "/etc"]);
	check("ls -l /etc lists entries", /hostname/.test(out) && /motd/.test(out), `expected hostname+motd, got: ${out.replace(/\n/g, "|").slice(0, 60)}`);
	check("ls -l /etc long format", /-rw/.test(out), "no file (-rw) line");
}

// 3. Plain ls still works (names only).
{
	const { out } = await run([]);
	check("ls names", /README/.test(out) && /bin/.test(out), `expected README+bin, got: ${out.replace(/\n/g, "|").slice(0, 60)}`);
}

if (failures.length) {
	process.stderr.write("ls_dir_test: FAIL\n" + failures.map((f) => "  - " + f).join("\n") + "\n");
	process.exit(1);
}
process.stderr.write("ls_dir_test: PASS — ls / ls -l / ls -alrt list the VFS correctly (fts dir walk works)\n");
process.exit(0);
