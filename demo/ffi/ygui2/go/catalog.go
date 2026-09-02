// ygui2 from Go — the widget catalog (a ygreeter2 port). RUNNABLE
// inside a yetty pane:
//
//	cd demo/ffi/ygui2/go
//	CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi" \
//	LD_LIBRARY_PATH=<build>/src/yetty/yffi go run catalog.go
//
// Every phase-6 widget wired from Go: chips, a toggle driving an
// overlay tooltip, a radio group driving a stepper, slider→progress
// binding, a spinner, a textinput that greets on Enter, a dropdown, a
// dialog in the overlay, and a statusbar mirroring every event.
// Tab/Shift-Tab walk focus, Esc closes overlays, Ctrl-C quits (also `q`
// while no text input holds focus).
package main

import (
	"fmt"
	"os"

	"github.com/zokrezyl/yetty/bindings/go/ygui2"
)

func main() {
	app := ygui2.NewApp()

	column := app.Root.Column(ygui2.Layout{Grow: 1, Gap: 10, Pad: 16})

	titleRow := column.Row(ygui2.Layout{Basis: 28, Gap: 10})
	titleRow.Label(ygui2.LabelOpts{Text: "ygui2 catalog — Go edition", Fg: "#74C5A5"},
		ygui2.Layout{Basis: 260})
	for index, chipText := range []string{"drawable", "contract", "toolkit"} {
		titleRow.Chip(ygui2.ChipOpts{Label: chipText, Selectable: true, Selected: index == 0},
			ygui2.Layout{Basis: 76, Cross: 22})
	}

	column.Separator(ygui2.Layout{Basis: 8})

	var status *ygui2.Node // created last; the closures capture the slot
	show := func(text string) {
		if status != nil {
			status.SetStatusLeft(text)
		}
	}

	tooltip := app.Tooltip(ygui2.TooltipOpts{Text: "the toggle controls me", X: 150, Y: 66})

	switchRow := column.Row(ygui2.Layout{Basis: 28, Gap: 10})
	switchRow.Label(ygui2.LabelOpts{Text: "switches", Fg: "#9FA7A8"}, ygui2.Layout{Basis: 110})
	switchRow.Toggle(ygui2.ToggleOpts{Label: "tooltip", OnToggle: func(node *ygui2.Node) {
		checked := node.ToggleChecked()
		tooltip.SetVisible(checked)
		if checked {
			show("toggle: on")
		} else {
			show("toggle: off")
		}
	}}, ygui2.Layout{Basis: 120})
	var stepper *ygui2.Node
	group := ygui2.NewRadioGroup()
	for option := 0; option < 3; option++ {
		switchRow.Radio(ygui2.RadioOpts{Label: fmt.Sprintf("opt %d", option+1), Group: group,
			Selected: option == 0, OnSelect: func(index int) {
				stepper.StepperCurrent(index)
				show(fmt.Sprintf("radio: option %d", index+1))
			}}, ygui2.Layout{Basis: 90})
	}
	stepper = switchRow.Stepper(ygui2.StepperOpts{Count: 3, Current: 0}, ygui2.Layout{Basis: 80})

	valueRow := column.Row(ygui2.Layout{Basis: 28, Gap: 10})
	valueRow.Label(ygui2.LabelOpts{Text: "values", Fg: "#9FA7A8"}, ygui2.Layout{Basis: 110})
	var bar *ygui2.Node
	valueRow.Slider(ygui2.SliderOpts{Value: 0.35, OnChange: func(node *ygui2.Node) {
		value := node.SliderValue()
		bar.SetValue(value)
		show(fmt.Sprintf("slider: %.0f%%", value*100))
	}}, ygui2.Layout{Basis: 160})
	bar = valueRow.Progress(ygui2.ProgressOpts{Value: 0.35}, ygui2.Layout{Basis: 140, Cross: 12})
	valueRow.Spinner(ygui2.SpinnerOpts{Value: 3, Minimum: 0, Maximum: ygui2.Float(10),
		Step: ygui2.Float(1),
		OnChange: func(node *ygui2.Node) {
			show(fmt.Sprintf("spinner: %g", node.SpinnerValue()))
		}}, ygui2.Layout{Basis: 110})

	entryRow := column.Row(ygui2.Layout{Basis: 28, Gap: 10})
	entryRow.Label(ygui2.LabelOpts{Text: "entry", Fg: "#9FA7A8"}, ygui2.Layout{Basis: 110})
	entryRow.Textinput(ygui2.TextinputOpts{Placeholder: "type a name, Enter greets",
		OnSubmit: func(node *ygui2.Node) {
			name := node.InputText()
			if name == "" {
				name = "stranger"
			}
			show("hello, " + name)
		}}, ygui2.Layout{Basis: 180})

	entryRow.Dropdown(ygui2.DropdownOpts{Items: []string{"plasma", "aurora", "nebula"},
		Selected: -1, OnChange: func(index int) {
			show(fmt.Sprintf("dropdown: item %d", index+1))
		}}, ygui2.Layout{Basis: 130})

	dialog := app.Dialog(ygui2.DialogOpts{Title: "about the catalog",
		X: ygui2.Float(140), Y: ygui2.Float(90), Width: ygui2.Float(300),
		Height: ygui2.Float(150), OnClose: func() { show("dialog closed") }})
	dialog.Label(ygui2.LabelOpts{Text: "every widget, one wire contract"},
		ygui2.Layout{Basis: 20})
	entryRow.Button(ygui2.ButtonOpts{Label: "open dialog", OnClick: func() {
		dialog.SetVisible(true)
		show("dialog opened")
	}}, ygui2.Layout{Basis: 110})

	column.Column(ygui2.Layout{Grow: 1})
	status = column.Statusbar(ygui2.StatusbarOpts{Left: "ready",
		Right: "Tab: focus  Esc: close  Ctrl-C: quit"}, ygui2.Layout{Basis: 24})

	if runError := app.Run(ygui2.RunOpts{}); runError != nil {
		fmt.Fprintln(os.Stderr, "catalog:", runError)
		os.Exit(1)
	}
}
