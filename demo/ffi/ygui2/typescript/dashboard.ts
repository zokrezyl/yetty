// ygui2 from TypeScript — live dashboard (a ytop2-lite). RUNNABLE
// inside a yetty pane:
//
//     cd demo/ffi/ygui2/typescript && npm install
//     node dashboard.ts
//
// Reads /proc every second and updates per-core bars, a memory bar, and
// a process table. The wire cost is the point: each tick ships only the
// handful of addressed reopens for widgets whose value actually
// changed — a few hundred bytes, never the tree. Ctrl-C quits (also `q`
// while no text input holds focus).
import { readFileSync, readdirSync } from "node:fs";
import { App, type Node } from "@yetty/ydraw/ygui2";

interface CpuSample {
  total: number;
  idle: number;
}

function cpuSamples(): CpuSample[] {
  const samples: CpuSample[] = [];
  for (const line of readFileSync("/proc/stat", "utf8").split("\n")) {
    if (!line.startsWith("cpu")) {
      break;
    }
    const values = line.split(/\s+/).slice(1, 9).map(Number);
    samples.push({ total: values.reduce((sum, value) => sum + value, 0),
      idle: values[3] + values[4] });
  }
  return samples; // [0] = aggregate, [1:] = per core
}

function memoryFraction(): number {
  const info = new Map<string, number>();
  for (const line of readFileSync("/proc/meminfo", "utf8").split("\n")) {
    const [key, rest] = line.split(":");
    if (rest !== undefined) {
      info.set(key, parseInt(rest.trim(), 10));
    }
  }
  const total = info.get("MemTotal") ?? 1;
  const available = info.get("MemAvailable") ?? total;
  return (total - available) / total;
}

// Display-only rounding: node exposes no page-size API; 4 KiB pages are
// the norm on the Linux desktops these demos target.
const PAGE_KB = 4;

function topProcesses(count: number): string[][] {
  const entries: Array<{ rssPages: number; pid: string; comm: string }> = [];
  for (const pid of readdirSync("/proc")) {
    if (!/^\d+$/.test(pid)) {
      continue;
    }
    try {
      const stat = readFileSync(`/proc/${pid}/stat`, "utf8");
      const comm = readFileSync(`/proc/${pid}/comm`, "utf8").trim();
      const tail = stat.slice(stat.lastIndexOf(")") + 1).trim().split(/\s+/);
      entries.push({ rssPages: parseInt(tail[21], 10) || 0, pid, comm });
    } catch {
      continue; // the process exited between readdir and read
    }
  }
  entries.sort((left, right) => right.rssPages - left.rssPages);
  return entries.slice(0, count).map((entry) => [entry.pid, entry.comm,
    `${Math.floor((entry.rssPages * PAGE_KB) / 1024)} MB`]);
}

const app = new App();
const column = app.root.column({ grow: 1, gap: 6, pad: 16 });
column.label({ text: "ygui2 dashboard — /proc over the drawable contract",
  fg: "#74C5A5", basis: 22 });
column.separator({ basis: 6 });

let previous = cpuSamples();
const coreBars: Node[] = [];
for (let coreIndex = 1; coreIndex < previous.length; coreIndex++) {
  const row = column.row({ basis: 18, gap: 8 });
  row.label({ text: `cpu${coreIndex - 1}`, fg: "#9FA7A8", basis: 60 });
  coreBars.push(row.progress({ value: 0.0, basis: 260, cross: 10 }));
}

const memoryRow = column.row({ basis: 18, gap: 8 });
memoryRow.label({ text: "mem", fg: "#9FA7A8", basis: 60 });
const memoryBar = memoryRow.progress({ value: memoryFraction(), accent: "#5A8979",
  basis: 260, cross: 10 });

const table = column.table({ columns: ["pid", "process", "rss"], widths: [70.0, 220.0, 0.0],
  grow: 1.0 });
column.statusbar({ left: "dashboard.ts — 1s ticks, incremental wire", right: "Ctrl-C: quit",
  basis: 22 });

let skip = 0;

function tick(): void {
  skip = (skip + 1) % 2;
  if (skip === 1) {
    return; // the loop ticks at 500ms; sample at 1s
  }
  const current = cpuSamples();
  coreBars.forEach((bar, barIndex) => {
    const now = current[barIndex + 1];
    const before = previous[barIndex + 1];
    if (now === undefined || before === undefined) {
      return;
    }
    const total = now.total - before.total;
    const busy = total - (now.idle - before.idle);
    bar.setValue(total > 0 ? busy / total : 0.0);
  });
  previous = current;
  memoryBar.setValue(memoryFraction());
  table.clearRows();
  for (const row of topProcesses(8)) {
    table.addRow(row);
  }
}

await app.run({ tick, tickMs: 500 });
