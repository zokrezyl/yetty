#!/usr/bin/env python3
"""ygui2 from Python — live dashboard (a ytop2-lite). RUNNABLE inside a
yetty pane:

    PYTHONPATH=bindings/python python3 demo/ffi/ygui2/python/dashboard.py

Reads /proc every second and updates per-core bars, a memory bar, and a
process table. The wire cost is the point: each tick ships only the
handful of addressed reopens for widgets whose value actually changed —
a few hundred bytes, never the tree. Ctrl-C quits (also `q` while no text input holds focus).
"""
import os

from yetty import ygui2


def cpu_samples():
    samples = []
    with open("/proc/stat") as stat:
        for line in stat:
            if not line.startswith("cpu"):
                break
            fields = line.split()
            values = [int(value) for value in fields[1:9]]
            idle = values[3] + values[4]
            samples.append((sum(values), idle))
    return samples  # [0] = aggregate, [1:] = per core


def memory_fraction():
    info = {}
    with open("/proc/meminfo") as meminfo:
        for line in meminfo:
            key, _, rest = line.partition(":")
            info[key] = int(rest.split()[0])
    total = info.get("MemTotal", 1)
    available = info.get("MemAvailable", total)
    return (total - available) / total


def top_processes(count=8):
    entries = []
    for pid in filter(str.isdigit, os.listdir("/proc")):
        try:
            with open(f"/proc/{pid}/stat") as stat:
                fields = stat.read().rsplit(")", 1)[1].split()
                comm = open(f"/proc/{pid}/comm").read().strip()
                rss_pages = int(fields[21])
            entries.append((rss_pages, pid, comm))
        except (OSError, IndexError, ValueError):
            continue
    entries.sort(reverse=True)
    page_kb = os.sysconf("SC_PAGE_SIZE") // 1024
    return [(pid, comm, f"{rss * page_kb // 1024} MB")
            for rss, pid, comm in entries[:count]]


app = ygui2.App()
column = app.root.column(grow=1, gap=6, pad=16)
column.label(text="ygui2 dashboard — /proc over the drawable contract",
             fg="#74C5A5", basis=22)
column.separator(basis=6)

previous = cpu_samples()
core_bars = []
for core_index in range(1, len(previous)):
    row = column.row(basis=18, gap=8)
    row.label(text=f"cpu{core_index - 1}", fg="#9FA7A8", basis=60)
    core_bars.append(row.progress(value=0.0, basis=260, cross=10))

memory_row = column.row(basis=18, gap=8)
memory_row.label(text="mem", fg="#9FA7A8", basis=60)
memory_bar = memory_row.progress(value=memory_fraction(), accent="#5A8979",
                                 basis=260, cross=10)

table = column.table(columns=("pid", "process", "rss"), widths=(70.0, 220.0, 0.0),
                     grow=1.0)
column.statusbar(left="dashboard.py — 1s ticks, incremental wire", right="Ctrl-C: quit",
                 basis=22)

tick_state = {"skip": 0}


def tick():
    global previous
    tick_state["skip"] = (tick_state["skip"] + 1) % 2
    if tick_state["skip"]:
        return  # the loop ticks at 500ms; sample at 1s
    current = cpu_samples()
    for bar, (now, then) in zip(core_bars, zip(current[1:], previous[1:])):
        busy = (now[0] - then[0]) - (now[1] - then[1])
        total = now[0] - then[0]
        bar.set_value(busy / total if total else 0.0)
    previous = current
    memory_bar.set_value(memory_fraction())
    table.clear_rows()
    for pid, comm, rss in top_processes():
        table.add_row((pid, comm, rss))


app.run(tick=tick, tick_ms=500)
