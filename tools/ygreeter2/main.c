/*
 * ygreeter2 — the ygui2 feature showcase on the yguiapp2 host. One page
 * exercising every widget demo: chips, toggle, radio group, slider +
 * progress binding, spinner, stepper, textinput, dropdown, checkbox, a
 * click counter, a clipped wheel-scrolling area (offset-only updates), a
 * dialog and a tooltip in the overlay, separators, and a statusbar
 * mirroring state. Tab/Shift-Tab walk focus, Esc closes overlays,
 * q quits. (ytop2 is the separate real-world dashboard app.)
 */
#include <math.h>
#include <stdio.h>

#include <yetty/api/ygui2/framework.h>
#include <yetty/api/ygui2/widget.h>
#include <yetty/api/ygui2/widgets/button.h>
#include <yetty/api/ygui2/widgets/checkbox.h>
#include <yetty/api/ygui2/widgets/chip.h>
#include <yetty/api/ygui2/widgets/dialog.h>
#include <yetty/api/ygui2/widgets/dropdown.h>
#include <yetty/api/ygui2/widgets/label.h>
#include <yetty/api/ygui2/widgets/panel.h>
#include <yetty/api/ygui2/widgets/plot.h>
#include <yetty/api/ygui2/widgets/progress.h>
#include <yetty/api/ygui2/widgets/radio.h>
#include <yetty/api/ygui2/widgets/scrollarea.h>
#include <yetty/api/ygui2/widgets/separator.h>
#include <yetty/api/ygui2/widgets/slider.h>
#include <yetty/api/ygui2/widgets/spinner.h>
#include <yetty/api/ygui2/widgets/statusbar.h>
#include <yetty/api/ygui2/widgets/stepper.h>
#include <yetty/api/ygui2/widgets/textinput.h>
#include <yetty/api/ygui2/widgets/toggle.h>
#include <yetty/api/ygui2/widgets/tooltip.h>
#include <yetty/ygui2/defs.h>
#include <yetty/yguiapp2/run.h>

enum { YGREETER2_RADIO_COUNT = 3 };

struct ygreeter2_state {
    struct yetty_yclass_object *framework;
    struct yetty_yclass_object *slider_bar; /* progress mirroring the slider */
    struct yetty_yclass_object *radios[YGREETER2_RADIO_COUNT];
    struct yetty_yclass_object *stepper;
    struct yetty_yclass_object *statusbar;
    struct yetty_yclass_object *dialog;
    struct yetty_yclass_object *tooltip;
    int dialog_opens;
    int clicks;
    /* Streaming-plot demo: the tick pushes a moving sine window. */
    struct yetty_yclass_object *wave_plot;
    float wave_phase;
};

enum { YGREETER2_WAVE_SAMPLES = 64 };

static void ygreeter2_status(struct ygreeter2_state *state, const char *event_text)
{
    yetty_ygui2_statusbar_set_left(state->statusbar, event_text);
}

static void on_slider_change(struct yetty_yclass_object *slider, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    struct yetty_ycore_float_result value_res = yetty_ygui2_slider_value(slider);
    if (YETTY_IS_ERR(value_res)) {
        yetty_ycore_error_destroy(value_res.error);
        return;
    }
    yetty_ygui2_progress_set_value(state->slider_bar, value_res.value);
    char text[64];
    snprintf(text, sizeof(text), "slider: %d%%", (int)(value_res.value * 100.0f));
    ygreeter2_status(state, text);
}

static void on_radio_select(struct yetty_yclass_object *selected_radio, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    for (int index = 0; index < YGREETER2_RADIO_COUNT; ++index) {
        if (state->radios[index] != selected_radio) {
            yetty_ygui2_radio_set_selected(state->radios[index], 0);
        } else {
            yetty_ygui2_stepper_set_current(state->stepper, (uint32_t)index);
            char text[64];
            snprintf(text, sizeof(text), "radio: option %d", index + 1);
            ygreeter2_status(state, text);
        }
    }
}

static void on_toggle_flip(struct yetty_yclass_object *toggle, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    struct yetty_ycore_int_result checked_res = yetty_ygui2_toggle_checked(toggle);
    int checked = YETTY_IS_OK(checked_res) ? checked_res.value : 0;
    if (YETTY_IS_ERR(checked_res)) {
        yetty_ycore_error_destroy(checked_res.error);
    }
    yetty_ygui2_widget_set_visible(state->tooltip, checked);
    ygreeter2_status(state, checked ? "toggle: on (tooltip shown)" : "toggle: off");
}

static void on_dropdown_change(struct yetty_yclass_object *dropdown, uint32_t index, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    (void)dropdown;
    char text[64];
    snprintf(text, sizeof(text), "dropdown: item %u", index + 1u);
    ygreeter2_status(state, text);
}

static void on_counter_click(struct yetty_yclass_object *button, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    (void)button;
    state->clicks++;
    char text[64];
    snprintf(text, sizeof(text), "clicks: %d", state->clicks);
    ygreeter2_status(state, text);
}

static void on_checkbox_toggle(struct yetty_yclass_object *checkbox, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    struct yetty_ycore_int_result checked_res = yetty_ygui2_checkbox_checked(checkbox);
    int checked = YETTY_IS_OK(checked_res) ? checked_res.value : 0;
    if (YETTY_IS_ERR(checked_res)) {
        yetty_ycore_error_destroy(checked_res.error);
    }
    ygreeter2_status(state, checked ? "checkbox: on" : "checkbox: off");
}

static void on_dialog_button(struct yetty_yclass_object *button, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    (void)button;
    state->dialog_opens++;
    yetty_ygui2_widget_set_visible(state->dialog, 1);
    char text[64];
    snprintf(text, sizeof(text), "dialog opened (#%d)", state->dialog_opens);
    ygreeter2_status(state, text);
}

static void on_dialog_close(struct yetty_yclass_object *dialog, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    (void)dialog;
    ygreeter2_status(state, "dialog closed");
}

static void on_name_submit(struct yetty_yclass_object *textinput, void *userdata)
{
    struct ygreeter2_state *state = userdata;
    char name[64];
    struct yetty_ycore_size_result length_res =
        yetty_ygui2_textinput_text_copy(textinput, name, sizeof(name));
    if (YETTY_IS_ERR(length_res)) {
        yetty_ycore_error_destroy(length_res.error);
        return;
    }
    char text[96];
    snprintf(text, sizeof(text), "hello, %s", name[0] ? name : "stranger");
    ygreeter2_status(state, text);
}

static struct yetty_yclass_object *ygreeter2_row(struct yetty_yclass_object *column,
                                                 const char *caption)
{
    struct yetty_yclass_object_ptr_result row_res = yetty_ygui2_row_add(column);
    if (YETTY_IS_ERR(row_res)) {
        yetty_ycore_error_destroy(row_res.error);
        return NULL;
    }
    struct yetty_ygui2_layout row_layout = {
        .basis = 28.0f, .gap = 10.0f, .direction = YETTY_YGUI2_DIRECTION_ROW};
    yetty_ygui2_widget_layout_set(row_res.value, &row_layout);
    if (caption) {
        struct yetty_yclass_object_ptr_result caption_res =
            yetty_ygui2_widget_add(row_res.value, yetty_ygui2_label_class_get().value);
        if (YETTY_IS_OK(caption_res)) {
            struct yetty_ygui2_layout caption_layout = {.basis = 110.0f};
            yetty_ygui2_widget_layout_set(caption_res.value, &caption_layout);
            yetty_ygui2_label_set_text(caption_res.value, caption);
            yetty_ygui2_label_set_color(caption_res.value, 0xFFA8A79Fu);
        } else {
            yetty_ycore_error_destroy(caption_res.error);
        }
    }
    return row_res.value;
}

static struct yetty_ycore_void_result ygreeter2_build(struct yetty_yclass_object *framework,
                                                      void *userdata)
{
    struct ygreeter2_state *state = userdata;
    state->framework = framework;
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, root_res, "ygreeter2: root");
    yetty_ygui2_panel_set_bg(root_res.value, 0xFF14100Bu);

    struct yetty_yclass_object_ptr_result column_res = yetty_ygui2_column_add(root_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, column_res, "ygreeter2: column");
    struct yetty_ygui2_layout column_layout = {
        .grow = 1.0f, .gap = 10.0f, .pad_left = 16.0f, .pad_top = 14.0f, .pad_right = 16.0f};
    yetty_ygui2_widget_layout_set(column_res.value, &column_layout);
    struct yetty_yclass_object *column = column_res.value;

    /* Title + chip row. */
    struct yetty_yclass_object *title_row = ygreeter2_row(column, NULL);
    if (!title_row) {
        return YETTY_ERR(yetty_ycore_void, "ygreeter2: title row");
    }
    struct yetty_yclass_object_ptr_result title_res =
        yetty_ygui2_widget_add(title_row, yetty_ygui2_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_res, "ygreeter2: title");
    struct yetty_ygui2_layout title_layout = {.basis = 240.0f};
    yetty_ygui2_widget_layout_set(title_res.value, &title_layout);
    yetty_ygui2_label_set_text(title_res.value, "ygreeter2 — widget catalog");
    yetty_ygui2_label_set_color(title_res.value, 0xFFA5C574u);
    static const char *chip_names[3] = {"drawable", "contract", "toolkit"};
    for (int index = 0; index < 3; ++index) {
        struct yetty_yclass_object_ptr_result chip_res =
            yetty_ygui2_widget_add(title_row, yetty_ygui2_chip_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, chip_res, "ygreeter2: chip");
        struct yetty_ygui2_layout chip_layout = {.basis = 76.0f, .cross_size = 22.0f};
        yetty_ygui2_widget_layout_set(chip_res.value, &chip_layout);
        yetty_ygui2_chip_set_label(chip_res.value, chip_names[index]);
        yetty_ygui2_chip_set_selectable(chip_res.value, 1);
        if (index == 0) {
            yetty_ygui2_chip_set_selected(chip_res.value, 1);
        }
    }

    struct yetty_yclass_object_ptr_result separator_res =
        yetty_ygui2_widget_add(column, yetty_ygui2_separator_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, separator_res, "ygreeter2: separator");
    struct yetty_ygui2_layout separator_layout = {.basis = 8.0f};
    yetty_ygui2_widget_layout_set(separator_res.value, &separator_layout);

    /* Toggle + radio group + stepper. */
    struct yetty_yclass_object *switch_row = ygreeter2_row(column, "switches");
    if (!switch_row) {
        return YETTY_ERR(yetty_ycore_void, "ygreeter2: switch row");
    }
    struct yetty_yclass_object_ptr_result toggle_res =
        yetty_ygui2_widget_add(switch_row, yetty_ygui2_toggle_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, toggle_res, "ygreeter2: toggle");
    struct yetty_ygui2_layout toggle_layout = {.basis = 120.0f};
    yetty_ygui2_widget_layout_set(toggle_res.value, &toggle_layout);
    yetty_ygui2_toggle_set_label(toggle_res.value, "tooltip");
    yetty_ygui2_toggle_on_toggle_set(toggle_res.value, on_toggle_flip, state);
    yetty_ygui2_widget_set_focusable(toggle_res.value, 1);
    for (int index = 0; index < YGREETER2_RADIO_COUNT; ++index) {
        struct yetty_yclass_object_ptr_result radio_res =
            yetty_ygui2_widget_add(switch_row, yetty_ygui2_radio_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, radio_res, "ygreeter2: radio");
        struct yetty_ygui2_layout radio_layout = {.basis = 90.0f};
        yetty_ygui2_widget_layout_set(radio_res.value, &radio_layout);
        char radio_label[16];
        snprintf(radio_label, sizeof(radio_label), "opt %d", index + 1);
        yetty_ygui2_radio_set_label(radio_res.value, radio_label);
        yetty_ygui2_radio_on_select_set(radio_res.value, on_radio_select, state);
        yetty_ygui2_widget_set_focusable(radio_res.value, 1);
        state->radios[index] = radio_res.value;
    }
    yetty_ygui2_radio_set_selected(state->radios[0], 1);
    struct yetty_yclass_object_ptr_result stepper_res =
        yetty_ygui2_widget_add(switch_row, yetty_ygui2_stepper_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stepper_res, "ygreeter2: stepper");
    struct yetty_ygui2_layout stepper_layout = {.basis = 80.0f};
    yetty_ygui2_widget_layout_set(stepper_res.value, &stepper_layout);
    yetty_ygui2_stepper_set_count(stepper_res.value, YGREETER2_RADIO_COUNT);
    state->stepper = stepper_res.value;

    /* Slider + mirroring progress + spinner. */
    struct yetty_yclass_object *value_row = ygreeter2_row(column, "values");
    if (!value_row) {
        return YETTY_ERR(yetty_ycore_void, "ygreeter2: value row");
    }
    struct yetty_yclass_object_ptr_result slider_res =
        yetty_ygui2_widget_add(value_row, yetty_ygui2_slider_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slider_res, "ygreeter2: slider");
    struct yetty_ygui2_layout slider_layout = {.basis = 160.0f};
    yetty_ygui2_widget_layout_set(slider_res.value, &slider_layout);
    yetty_ygui2_slider_set_value(slider_res.value, 0.35f);
    yetty_ygui2_slider_on_change_set(slider_res.value, on_slider_change, state);
    yetty_ygui2_widget_set_focusable(slider_res.value, 1);
    struct yetty_yclass_object_ptr_result bar_res =
        yetty_ygui2_widget_add(value_row, yetty_ygui2_progress_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bar_res, "ygreeter2: bar");
    struct yetty_ygui2_layout bar_layout = {.basis = 140.0f, .cross_size = 12.0f};
    yetty_ygui2_widget_layout_set(bar_res.value, &bar_layout);
    yetty_ygui2_progress_set_value(bar_res.value, 0.35f);
    state->slider_bar = bar_res.value;
    struct yetty_yclass_object_ptr_result spinner_res =
        yetty_ygui2_widget_add(value_row, yetty_ygui2_spinner_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, spinner_res, "ygreeter2: spinner");
    struct yetty_ygui2_layout spinner_layout = {.basis = 110.0f};
    yetty_ygui2_widget_layout_set(spinner_res.value, &spinner_layout);
    yetty_ygui2_spinner_configure(spinner_res.value, 0.0f, 10.0f, 1.0f);
    yetty_ygui2_spinner_set_value(spinner_res.value, 3.0f);
    yetty_ygui2_widget_set_focusable(spinner_res.value, 1);

    /* Text input + dropdown + dialog button. */
    struct yetty_yclass_object *entry_row = ygreeter2_row(column, "entry");
    if (!entry_row) {
        return YETTY_ERR(yetty_ycore_void, "ygreeter2: entry row");
    }
    struct yetty_yclass_object_ptr_result input_res =
        yetty_ygui2_widget_add(entry_row, yetty_ygui2_textinput_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_res, "ygreeter2: input");
    struct yetty_ygui2_layout input_layout = {.basis = 180.0f};
    yetty_ygui2_widget_layout_set(input_res.value, &input_layout);
    yetty_ygui2_textinput_set_placeholder(input_res.value, "type a name, Enter greets");
    yetty_ygui2_textinput_on_submit_set(input_res.value, on_name_submit, state);
    yetty_ygui2_widget_set_focusable(input_res.value, 1);
    struct yetty_yclass_object_ptr_result dropdown_res =
        yetty_ygui2_widget_add(entry_row, yetty_ygui2_dropdown_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dropdown_res, "ygreeter2: dropdown");
    struct yetty_ygui2_layout dropdown_layout = {.basis = 130.0f};
    yetty_ygui2_widget_layout_set(dropdown_res.value, &dropdown_layout);
    yetty_ygui2_dropdown_item_add(dropdown_res.value, "plasma");
    yetty_ygui2_dropdown_item_add(dropdown_res.value, "aurora");
    yetty_ygui2_dropdown_item_add(dropdown_res.value, "nebula");
    yetty_ygui2_dropdown_on_change_set(dropdown_res.value, on_dropdown_change, state);
    yetty_ygui2_widget_set_focusable(dropdown_res.value, 1);
    struct yetty_yclass_object_ptr_result open_res =
        yetty_ygui2_widget_add(entry_row, yetty_ygui2_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, open_res, "ygreeter2: open button");
    struct yetty_ygui2_layout open_layout = {.basis = 110.0f};
    yetty_ygui2_widget_layout_set(open_res.value, &open_layout);
    yetty_ygui2_button_set_label(open_res.value, "open dialog");
    yetty_ygui2_button_on_click_set(open_res.value, on_dialog_button, state);
    yetty_ygui2_widget_set_focusable(open_res.value, 1);

    /* Interaction: click counter + checkbox + the clipped scroll viewport
     * (wheel over it ships ONE content-group offset update per tick). */
    struct yetty_yclass_object *scroll_row = ygreeter2_row(column, "scroll");
    if (!scroll_row) {
        return YETTY_ERR(yetty_ycore_void, "ygreeter2: scroll row");
    }
    struct yetty_yclass_object_ptr_result counter_res =
        yetty_ygui2_widget_add(scroll_row, yetty_ygui2_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, counter_res, "ygreeter2: counter button");
    struct yetty_ygui2_layout counter_layout = {.basis = 110.0f};
    yetty_ygui2_widget_layout_set(counter_res.value, &counter_layout);
    yetty_ygui2_button_set_label(counter_res.value, "click me");
    yetty_ygui2_button_on_click_set(counter_res.value, on_counter_click, state);
    yetty_ygui2_widget_set_focusable(counter_res.value, 1);
    struct yetty_yclass_object_ptr_result checkbox_res =
        yetty_ygui2_widget_add(scroll_row, yetty_ygui2_checkbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, checkbox_res, "ygreeter2: checkbox");
    struct yetty_ygui2_layout checkbox_layout = {.basis = 190.0f};
    yetty_ygui2_widget_layout_set(checkbox_res.value, &checkbox_layout);
    yetty_ygui2_checkbox_set_label(checkbox_res.value, "wheel scroll below");
    yetty_ygui2_checkbox_on_toggle_set(checkbox_res.value, on_checkbox_toggle, state);
    yetty_ygui2_widget_set_focusable(checkbox_res.value, 1);

    struct yetty_yclass_object_ptr_result scroll_res =
        yetty_ygui2_widget_add(column, yetty_ygui2_scrollarea_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scroll_res, "ygreeter2: scrollarea");
    struct yetty_ygui2_layout scroll_layout = {.basis = 120.0f, .cross_size = 360.0f, .gap = 4.0f};
    yetty_ygui2_widget_layout_set(scroll_res.value, &scroll_layout);
    yetty_ygui2_scrollarea_configure(scroll_res.value, 24.0f, 500.0f);
    for (int line = 0; line < 12; ++line) {
        struct yetty_yclass_object_ptr_result item_res =
            yetty_ygui2_widget_add(scroll_res.value, yetty_ygui2_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ygreeter2: scroll item");
        struct yetty_ygui2_layout item_layout = {.basis = 44.0f};
        yetty_ygui2_widget_layout_set(item_res.value, &item_layout);
        char item_text[64];
        snprintf(item_text, sizeof(item_text), "scrollable row %02d — offsets only", line);
        yetty_ygui2_label_set_text(item_res.value, item_text);
        yetty_ygui2_label_set_color(item_res.value, (line % 2) ? 0xFFA8A79Fu : 0xFF92A86Bu);
    }

    /* Streaming plot: expression curve baked into the creation record,
     * live sine window fed by the tick — each append is ONE envelope
     * carrying the new sample chunk plus a ring-head op, zero repaints.
     * Resize is one addressed geometry op; nothing re-ships. */
    struct yetty_yclass_object *plot_row = ygreeter2_row(column, "plot");
    if (!plot_row) {
        return YETTY_ERR(yetty_ycore_void, "ygreeter2: plot row");
    }
    struct yetty_yclass_object_ptr_result plot_hint_res =
        yetty_ygui2_widget_add(plot_row, yetty_ygui2_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_hint_res, "ygreeter2: plot hint");
    struct yetty_ygui2_layout plot_hint_layout = {.basis = 340.0f};
    yetty_ygui2_widget_layout_set(plot_hint_res.value, &plot_hint_layout);
    yetty_ygui2_label_set_text(plot_hint_res.value, "streamed buffer + expression curve below");
    yetty_ygui2_label_set_color(plot_hint_res.value, 0xFFA8A79Fu);
    struct yetty_yclass_object_ptr_result plot_res =
        yetty_ygui2_widget_add(column, yetty_ygui2_plot_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_res, "ygreeter2: plot");
    struct yetty_ygui2_layout plot_layout = {.basis = 170.0f, .cross_size = 460.0f};
    yetty_ygui2_widget_layout_set(plot_res.value, &plot_layout);
    yetty_ygui2_plot_set_title(plot_res.value, "live wave");
    yetty_ygui2_plot_set_y_range(plot_res.value, -1.4f, 1.4f);
    yetty_ygui2_plot_set_expression(plot_res.value, "sin(3*x) * 0.8");
    yetty_ygui2_plot_add_stream_buffer(plot_res.value, "live", YGREETER2_WAVE_SAMPLES, "#6BA892");
    state->wave_plot = plot_res.value;

    /* Statusbar pinned to the flex end. */
    struct yetty_yclass_object_ptr_result spacer_res = yetty_ygui2_column_add(column);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, spacer_res, "ygreeter2: spacer");
    struct yetty_ygui2_layout spacer_layout = {.grow = 1.0f};
    yetty_ygui2_widget_layout_set(spacer_res.value, &spacer_layout);
    struct yetty_yclass_object_ptr_result statusbar_res =
        yetty_ygui2_widget_add(column, yetty_ygui2_statusbar_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, statusbar_res, "ygreeter2: statusbar");
    struct yetty_ygui2_layout statusbar_layout = {.basis = 24.0f};
    yetty_ygui2_widget_layout_set(statusbar_res.value, &statusbar_layout);
    yetty_ygui2_statusbar_set_left(statusbar_res.value, "ready");
    yetty_ygui2_statusbar_set_right(statusbar_res.value, "Tab: focus  Esc: close  q: quit");
    state->statusbar = statusbar_res.value;

    /* Overlay: dialog (hidden) + tooltip (hidden). */
    struct yetty_yclass_object_ptr_result dialog_res =
        yetty_ygui2_framework_overlay_add(framework, yetty_ygui2_dialog_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dialog_res, "ygreeter2: dialog");
    state->dialog = dialog_res.value;
    yetty_ygui2_widget_set_position(state->dialog, 140.0f, 90.0f);
    yetty_ygui2_widget_set_size(state->dialog, 280.0f, 150.0f);
    yetty_ygui2_dialog_set_title(state->dialog, "about ygreeter2");
    yetty_ygui2_dialog_on_close_set(state->dialog, on_dialog_close, state);
    struct yetty_ygui2_layout dialog_layout = {
        .gap = 6.0f, .pad_left = 12.0f, .pad_top = 40.0f, .pad_right = 12.0f};
    yetty_ygui2_widget_layout_set(state->dialog, &dialog_layout);
    struct yetty_yclass_object_ptr_result dialog_text_res =
        yetty_ygui2_widget_add(state->dialog, yetty_ygui2_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dialog_text_res, "ygreeter2: dialog text");
    struct yetty_ygui2_layout dialog_text_layout = {.basis = 20.0f};
    yetty_ygui2_widget_layout_set(dialog_text_res.value, &dialog_text_layout);
    yetty_ygui2_label_set_text(dialog_text_res.value, "every widget, one wire contract");
    yetty_ygui2_widget_set_visible(state->dialog, 0);

    struct yetty_yclass_object_ptr_result tooltip_res =
        yetty_ygui2_framework_overlay_add(framework, yetty_ygui2_tooltip_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tooltip_res, "ygreeter2: tooltip");
    state->tooltip = tooltip_res.value;
    yetty_ygui2_widget_set_position(state->tooltip, 150.0f, 66.0f);
    yetty_ygui2_widget_set_size(state->tooltip, 190.0f, 24.0f);
    yetty_ygui2_tooltip_set_text(state->tooltip, "the toggle controls me");
    yetty_ygui2_widget_set_visible(state->tooltip, 0);

    return YETTY_OK_VOID();
}

/* Animation tick: stream the next sine window into the live plot — one
 * addressed update, no repaint (the catalog stays clean otherwise). */
static struct yetty_ycore_void_result ygreeter2_tick(struct yetty_yclass_object *framework,
                                                     void *userdata)
{
    struct ygreeter2_state *state = userdata;
    (void)framework;
    if (!state->wave_plot) {
        return YETTY_OK_VOID();
    }
    state->wave_phase += 0.25f;
    /* APPEND one sample per tick: ~40 bytes on the wire, the receiver's
     * ring unwrap scrolls the window (steady state re-sends nothing;
     * only a structural replacement replays the cached window once). */
    float sample = sinf(state->wave_phase) * (0.65f + 0.35f * sinf(state->wave_phase * 0.31f));
    return yetty_ygui2_plot_append_samples(state->wave_plot, &sample, 1u);
}

int main(void)
{
    struct ygreeter2_state state = {0};
    return yetty_yguiapp2_terminal_main(ygreeter2_build, ygreeter2_tick, 250, &state);
}
