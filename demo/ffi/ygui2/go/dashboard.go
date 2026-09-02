// ygui2 from Go — live dashboard (a ytop2-lite). RUNNABLE inside a
// yetty pane:
//
//	cd demo/ffi/ygui2/go
//	CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi" \
//	LD_LIBRARY_PATH=<build>/src/yetty/yffi go run dashboard.go
//
// Reads /proc every second and updates per-core bars, a memory bar, and
// a process table. The wire cost is the point: each tick ships only the
// handful of addressed reopens for widgets whose value actually
// changed — a few hundred bytes, never the tree. Ctrl-C quits (also `q`
// while no text input holds focus).
package main

import (
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"

	"github.com/zokrezyl/yetty/bindings/go/ygui2"
)

type cpuSample struct {
	total uint64
	idle  uint64
}

func cpuSamples() []cpuSample {
	content, readError := os.ReadFile("/proc/stat")
	if readError != nil {
		return nil
	}
	var samples []cpuSample
	for _, line := range strings.Split(string(content), "\n") {
		if !strings.HasPrefix(line, "cpu") {
			break
		}
		fields := strings.Fields(line)
		var values []uint64
		for _, field := range fields[1:] {
			if len(values) == 8 {
				break
			}
			value, _ := strconv.ParseUint(field, 10, 64)
			values = append(values, value)
		}
		var total uint64
		for _, value := range values {
			total += value
		}
		idle := values[3] + values[4]
		samples = append(samples, cpuSample{total: total, idle: idle})
	}
	return samples // [0] = aggregate, [1:] = per core
}

func memoryFraction() float64 {
	content, readError := os.ReadFile("/proc/meminfo")
	if readError != nil {
		return 0
	}
	total, available := uint64(1), uint64(0)
	haveAvailable := false
	for _, line := range strings.Split(string(content), "\n") {
		key, rest, found := strings.Cut(line, ":")
		if !found {
			continue
		}
		value, _ := strconv.ParseUint(strings.Fields(rest)[0], 10, 64)
		if key == "MemTotal" {
			total = value
		} else if key == "MemAvailable" {
			available = value
			haveAvailable = true
		}
	}
	if !haveAvailable {
		available = total
	}
	return float64(total-available) / float64(total)
}

type processEntry struct {
	rssPages uint64
	pid      string
	comm     string
}

func topProcesses(count int) [][]string {
	entries, readError := os.ReadDir("/proc")
	if readError != nil {
		return nil
	}
	var processes []processEntry
	for _, entry := range entries {
		pid := entry.Name()
		if _, numeric := strconv.Atoi(pid); numeric != nil {
			continue
		}
		statBytes, statError := os.ReadFile("/proc/" + pid + "/stat")
		commBytes, commError := os.ReadFile("/proc/" + pid + "/comm")
		if statError != nil || commError != nil {
			continue
		}
		tailIndex := strings.LastIndex(string(statBytes), ")")
		if tailIndex < 0 {
			continue
		}
		fields := strings.Fields(string(statBytes)[tailIndex+1:])
		if len(fields) < 22 {
			continue
		}
		rssPages, _ := strconv.ParseUint(fields[21], 10, 64)
		processes = append(processes, processEntry{rssPages: rssPages, pid: pid,
			comm: strings.TrimSpace(string(commBytes))})
	}
	sort.Slice(processes, func(left, right int) bool {
		return processes[left].rssPages > processes[right].rssPages
	})
	pageKB := uint64(os.Getpagesize()) / 1024
	var rows [][]string
	for index := 0; index < count && index < len(processes); index++ {
		entry := processes[index]
		rows = append(rows, []string{entry.pid, entry.comm,
			fmt.Sprintf("%d MB", entry.rssPages*pageKB/1024)})
	}
	return rows
}

func main() {
	app := ygui2.NewApp()
	column := app.Root.Column(ygui2.Layout{Grow: 1, Gap: 6, Pad: 16})
	column.Label(ygui2.LabelOpts{Text: "ygui2 dashboard — /proc over the drawable contract",
		Fg: "#74C5A5"}, ygui2.Layout{Basis: 22})
	column.Separator(ygui2.Layout{Basis: 6})

	previous := cpuSamples()
	var coreBars []*ygui2.Node
	for coreIndex := 1; coreIndex < len(previous); coreIndex++ {
		row := column.Row(ygui2.Layout{Basis: 18, Gap: 8})
		row.Label(ygui2.LabelOpts{Text: fmt.Sprintf("cpu%d", coreIndex-1), Fg: "#9FA7A8"},
			ygui2.Layout{Basis: 60})
		coreBars = append(coreBars, row.Progress(ygui2.ProgressOpts{Value: 0},
			ygui2.Layout{Basis: 260, Cross: 10}))
	}

	memoryRow := column.Row(ygui2.Layout{Basis: 18, Gap: 8})
	memoryRow.Label(ygui2.LabelOpts{Text: "mem", Fg: "#9FA7A8"}, ygui2.Layout{Basis: 60})
	memoryBar := memoryRow.Progress(ygui2.ProgressOpts{Value: memoryFraction(),
		Accent: "#5A8979"}, ygui2.Layout{Basis: 260, Cross: 10})

	table := column.Table(ygui2.TableOpts{Columns: []string{"pid", "process", "rss"},
		Widths: []float32{70, 220, 0}}, ygui2.Layout{Grow: 1})
	column.Statusbar(ygui2.StatusbarOpts{Left: "dashboard.go — 1s ticks, incremental wire",
		Right: "Ctrl-C: quit"}, ygui2.Layout{Basis: 22})

	skip := 0
	tick := func() {
		skip = (skip + 1) % 2
		if skip == 1 {
			return // the loop ticks at 500ms; sample at 1s
		}
		current := cpuSamples()
		for barIndex, bar := range coreBars {
			if barIndex+1 >= len(current) || barIndex+1 >= len(previous) {
				break
			}
			now, before := current[barIndex+1], previous[barIndex+1]
			total := now.total - before.total
			busy := total - (now.idle - before.idle)
			if total > 0 {
				bar.SetValue(float64(busy) / float64(total))
			} else {
				bar.SetValue(0)
			}
		}
		previous = current
		memoryBar.SetValue(memoryFraction())
		table.ClearRows()
		for _, row := range topProcesses(8) {
			table.AddRow(row)
		}
	}

	if runError := app.Run(ygui2.RunOpts{Tick: tick, TickMillis: 500}); runError != nil {
		fmt.Fprintln(os.Stderr, "dashboard:", runError)
		os.Exit(1)
	}
}
