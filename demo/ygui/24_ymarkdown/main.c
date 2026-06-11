/*
 * Demo 24_ymarkdown: ymarkdown feature catalog.
 *
 * A scrollarea holding one collapsing_header per markdown feature —
 * Overview, Headings, Inline styles, Lists, Blockquotes, Tables, Code
 * blocks and Horizontal rules. Each section's body is a ymarkdown widget
 * rendering just that feature, so the demo doubles as a visual catalog and
 * as the regression surface for the renderer. Click a section header to
 * fold it away; the body disappears and the section collapses to its strip.
 *
 * The flex layout has no intrinsic content sizing, so each ymarkdown body
 * carries an authored height derived from its source line count (a safe
 * over-estimate — tables and code fences render fewer lines than they
 * occupy in source), and each section's height is the sum of its children.
 *
 * Standalone-mode ygui demo. Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include "../runner.h"
#include <yetty/ygui/ygui.h>

#include <string.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Add `cls` under `parent` and author its main-axis height. Returns NULL on
 * allocation failure (the demo simply skips that widget). */
static struct yetty_yclass_object *add_w(struct yetty_yclass_object *parent,
                                       const struct yetty_yclass *cls, float height)
{
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(parent, cls);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(r.value);
    l.height = height;
    err_ok(yetty_ygui_widget_layout_set(r.value, &l));
    return r.value;
}

static void set_grow(struct yetty_yclass_object *w, float grow)
{
    if (!w) {
        return;
    }
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
    l.flex_grow = grow;
    err_ok(yetty_ygui_widget_layout_set(w, &l));
}

/* Open collapsing_header section, titled, initially expanded. */
static struct yetty_yclass_object *add_section(struct yetty_yclass_object *area, const char *title)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_add(area, yetty_ygui_collapsing_header_class_get().value);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    err_ok(yetty_ygui_collapsing_header_set_title(r.value, title));
    err_ok(yetty_ygui_collapsing_header_set_open(r.value, 1));
    return r.value;
}

/* Derive a section's open height from its children: header strip + paddings +
 * the sum of authored child heights + inter-row gaps. */
static void finalize_section(struct yetty_yclass_object *sec)
{
    if (!sec) {
        return;
    }
    const struct yetty_ygui_layout *sl = yetty_ygui_widget_layout_get(sec);
    float total = sl->padding_top + sl->padding_bottom;
    int n = 0;
    for (struct yetty_yclass_object *c = yetty_ygui_widget_first_child(sec); c;
         c = yetty_ygui_widget_next_sibling(c)) {
        const struct yetty_ygui_layout *cl = yetty_ygui_widget_layout_get(c);
        total += cl->height > 0.0f ? cl->height : 0.0f;
        n++;
    }
    if (n > 1) {
        total += sl->gap * (float)(n - 1);
    }
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(sec);
    l.height = total;
    err_ok(yetty_ygui_widget_layout_set(sec, &l));
}

/* One titled section whose whole body is a single ymarkdown widget rendering
 * `src`. The body height is derived from the source's line count: the
 * renderer advances ~22.4px per line at the demo's 16px cell, and tables /
 * code fences render fewer lines than they span in source, so line_count * 24
 * plus a small pad never clips. */
static void add_md_section(struct yetty_yclass_object *area, const char *title, const char *src)
{
    struct yetty_yclass_object *sec = add_section(area, title);
    if (!sec) {
        return;
    }
    size_t len = strlen(src);
    int lines = 1;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\n') {
            lines++;
        }
    }
    float h = (float)lines * 24.0f + 12.0f;
    struct yetty_yclass_object *md = add_w(sec, yetty_ygui_ymarkdown_class_get().value, h);
    if (md) {
        err_ok(yetty_ygui_ymarkdown_set_source(md, src, len));
    }
    finalize_section(sec);
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_yclass_object *root)
{
    (void)runner;

    /* Scrollarea fills the window and stacks the feature sections. */
    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(root, yetty_ygui_scrollarea_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build: scrollarea");
    set_grow(sr.value, 1.0f);
    struct yetty_yclass_object *area = sr.value;

    add_md_section(area, "Overview",
                   "# ymarkdown\n"
                   "\n"
                   "Each section below renders one **markdown** feature.\n"
                   "Click a section header to *fold* it away, then click\n"
                   "again to bring it back.\n");

    add_md_section(area, "Headings",
                   "# Heading 1\n"
                   "## Heading 2\n"
                   "### Heading 3\n"
                   "#### Heading 4\n"
                   "##### Heading 5\n"
                   "###### Heading 6\n"
                   "\n"
                   "Paragraph text sits between headings at the base size.\n");

    add_md_section(area, "Inline styles",
                   "Markdown runs can be **bold**, *italic*, or\n"
                   "***bold and italic*** at once.\n"
                   "\n"
                   "Inline `code` sits on its own background box, text can\n"
                   "be ~~struck through~~, and a [link](https://example.com)\n"
                   "is drawn in the accent colour.\n");

    add_md_section(area, "Lists",
                   "Bulleted list:\n"
                   "\n"
                   "- first bullet\n"
                   "- second bullet with **emphasis**\n"
                   "- third bullet\n"
                   "\n"
                   "Ordered list:\n"
                   "\n"
                   "1. step one\n"
                   "2. step two\n"
                   "3. step three\n");

    add_md_section(area, "Blockquotes",
                   "> Terminals can show rich content, not just text.\n"
                   "> > Nested quotes get their own accent gutter bar.\n"
                   "\n"
                   "Body text resumes after the quote.\n");

    add_md_section(area, "Tables",
                   "Per-column alignment with a drawn grid:\n"
                   "\n"
                   "| Feature     | Status | Notes                |\n"
                   "|:------------|:------:|---------------------:|\n"
                   "| Headings    |   ok   |          six levels  |\n"
                   "| Tables      |   ok   |   aligned + bordered |\n"
                   "| Code blocks |   ok   |       shared panel   |\n");

    add_md_section(area, "Code blocks",
                   "Fenced blocks render verbatim on a shared panel:\n"
                   "\n"
                   "```\n"
                   "fn main() {\n"
                   "    println!(\"hello, ymarkdown\");\n"
                   "}\n"
                   "```\n");

    add_md_section(area, "Horizontal rules",
                   "Text above the rule.\n"
                   "\n"
                   "---\n"
                   "\n"
                   "Text below the rule.\n");

    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "24_ymarkdown", build);
}
