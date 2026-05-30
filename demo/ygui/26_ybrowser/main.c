/*
 * Demo 26_ybrowser: ybrowser — a real HTML / CSS / JavaScript engine
 * (lexbor + libcss + QuickJS) rendered straight into ygui's GPU draw
 * list.
 *
 * A tabbar across the top switches between several full HTML documents,
 * each showcasing a different slice of the engine: typography, HTML5
 * forms, flexbox grids, CSS cards (custom properties / var()), and DOM
 * built live by JavaScript at page load.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree and
 * wires the tabbar's VALUE_CHANGED event to ybrowser_set_html().
 * Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runner.h"
#include <yetty/ygui/event.h>
#include <yetty/ygui/ygui.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
}

/*-----------------------------------------------------------------------------
 * Shared chrome — brand palette baked into every page so each document is
 * self-contained and blends with the demo's dark canvas. The engine reads
 * <style> blocks via libcss, so plain class selectors work throughout.
 *---------------------------------------------------------------------------*/
#define COMMON_CSS                                                                                  \
    "html,body{margin:0;padding:0;}"                                                                \
    "body{background:#0B1014;color:#E0E5E4;font-size:15px;padding:18px 22px;}"                      \
    "h1{color:#74C5A5;}h2{color:#6BA892;}h3{color:#9FA7A8;}"                                        \
    "p{margin:0 0 10px;}"                                                                           \
    "a{color:#6BA892;}"                                                                             \
    ".muted{color:#9FA7A8;}"                                                                        \
    ".accent{color:#74C5A5;}"                                                                       \
    "code{color:#74C5A5;}"                                                                          \
    "hr{border:0;border-top:1px solid #364A47;margin:16px 0;}"                                      \
    ".card{background:#141A1F;border:1px solid #364A47;border-radius:10px;"                         \
    "padding:16px 18px;margin:0 0 14px;}"

/* DOC(extra_css, body) — assemble a complete HTML document. extra_css and
 * body are string literals; adjacent concatenation stitches them in. */
#define DOC(EXTRA_CSS, BODY)                                                                        \
    "<!doctype html><html lang=en><head><meta charset=utf-8><style>" COMMON_CSS EXTRA_CSS           \
    "</style></head><body>" BODY "</body></html>"

/*-----------------------------------------------------------------------------
 * The pages. Held as a static-const local table inside an accessor so no
 * file-scope data symbol is emitted; the labels run parallel to the pages.
 *---------------------------------------------------------------------------*/
static const char *const *demo_pages(int *count)
{
    static const char *const pages[] = {
        /* 0 — Welcome ------------------------------------------------------ */
        DOC(".feat{padding:7px 0;border-top:1px solid #1E262C;color:#E0E5E4;}"
            ".feat:first-child{border-top:0;}"
            ".lead{color:#9FA7A8;font-size:16px;}",
            "<div class=card>"
            "<h1>yetty &middot; ybrowser</h1>"
            "<p class=lead>A real HTML / CSS / JavaScript engine &mdash; lexbor parses, "
            "libcss cascades, QuickJS scripts &mdash; rendered straight into ygui's GPU "
            "draw list.</p>"
            "</div>"
            "<div class=card>"
            "<h2>Pick a tab above</h2>"
            "<p class=muted>Each tab hands a full HTML document to "
            "<code>ybrowser_set_html()</code>, which re-lays-out and repaints on the GPU.</p>"
            "<div class=feat>Typography &mdash; headings, quotes, preformatted code</div>"
            "<div class=feat>Forms &mdash; inputs, select, textarea, checkboxes, buttons</div>"
            "<div class=feat>Grid &mdash; flexbox rows with even-split columns</div>"
            "<div class=feat>Cards &mdash; backgrounds, borders, radius, CSS var()</div>"
            "<div class=feat>JavaScript &mdash; DOM generated live at page load</div>"
            "</div>"),

        /* 1 — Typography --------------------------------------------------- */
        DOC("blockquote{border-left:3px solid #6BA892;background:#141A1F;"
            "padding:10px 16px;margin:0 0 14px;color:#9FA7A8;border-radius:0 8px 8px 0;}"
            "pre{background:#0B1014;border:1px solid #364A47;border-radius:8px;"
            "padding:12px 14px;color:#74C5A5;margin:0 0 14px;}"
            "ul,ol{padding-left:22px;margin:0 0 12px;}"
            "li{padding:3px 0;}"
            ".h-demo{padding:0;}",
            "<div class=card>"
            "<h1>Heading 1</h1>"
            "<h2>Heading 2</h2>"
            "<h3>Heading 3</h3>"
            "<h4>Heading 4</h4>"
            "<p>Body text flows as wrapped inline runs. The cascade resolves "
            "color, size and spacing per block element.</p>"
            "</div>"
            "<div class=card>"
            "<h3>Blockquote</h3>"
            "<blockquote>Terminals were never meant to stay monochrome rectangles. "
            "ybrowser proves a terminal can host the actual web platform.</blockquote>"
            "<h3>Preformatted &amp; code</h3>"
            "<pre>struct yetty_ycore_void_result\n"
            "yetty_ygui_ybrowser_set_html(obj, html, len);</pre>"
            "<h3>Lists</h3>"
            "<ul><li>Block-flow vertical stacking</li>"
            "<li>Inline text wrapping</li>"
            "<li>Flex-row even split</li></ul>"
            "</div>"),

        /* 2 — Forms -------------------------------------------------------- */
        DOC("form{margin:0;}"
            "label{display:block;color:#9FA7A8;font-size:13px;margin:12px 0 5px;}"
            "input,select,textarea{display:block;background:#0B1014;color:#E0E5E4;"
            "border:1px solid #364A47;border-radius:7px;padding:9px 11px;margin:0;}"
            "textarea{height:60px;}"
            ".chkrow{display:flex;flex-direction:row;align-items:center;margin:14px 0 0;}"
            ".box{width:18px;height:18px;border:1px solid #364A47;border-radius:5px;"
            "background:#0B1014;}"
            ".box.on{background:#6BA892;border-color:#6BA892;}"
            ".chklbl{flex-grow:1;color:#E0E5E4;padding-left:10px;}"
            ".btns{display:flex;flex-direction:row;margin:20px 0 0;}"
            "button{display:block;flex-grow:1;border:0;border-radius:7px;padding:11px 0;"
            "text-align:center;background:#1E262C;color:#E0E5E4;margin-right:10px;}"
            "button.primary{background:#6BA892;color:#0B1014;}",
            "<div class=card>"
            "<h2>Create account</h2>"
            "<form>"
            "<label>Full name</label><input type=text>"
            "<label>Email</label><input type=email>"
            "<label>Password</label><input type=password>"
            "<label>Plan</label>"
            "<select><option>Free &mdash; community</option>"
            "<option>Pro</option><option>Team</option></select>"
            "<label>Notes</label>"
            "<textarea>Ships GPU-rendered HTML inside the terminal.</textarea>"
            "<div class=chkrow><div class=\"box on\"></div>"
            "<div class=chklbl>Email me product updates</div></div>"
            "<div class=chkrow><div class=box></div>"
            "<div class=chklbl>Enable experimental features</div></div>"
            "<div class=btns><button class=primary>Create account</button>"
            "<button>Cancel</button></div>"
            "</form>"
            "</div>"),

        /* 3 — Grid (flexbox rows) ----------------------------------------- */
        DOC(".grid{border:1px solid #364A47;border-radius:9px;background:#141A1F;"
            "padding:0;margin:0;}"
            ".tr{display:flex;flex-direction:row;}"
            ".tr .c{flex-grow:1;padding:10px 14px;border-top:1px solid #1E262C;color:#E0E5E4;}"
            ".tr .name{flex-grow:2;}"
            ".head{background:#1E262C;border-radius:9px 9px 0 0;}"
            ".head .c{border-top:0;color:#74C5A5;}"
            ".odd{background:#0F151A;}"
            ".num{color:#9FA7A8;}",
            "<div class=card>"
            "<h2>Flexbox grid</h2>"
            "<p class=muted>Each row is <code>display:flex</code>; cells share the width via "
            "<code>flex-grow</code>. The name column grows twice as fast.</p>"
            "<div class=grid>"
            "<div class=\"tr head\"><div class=\"c name\">Component</div>"
            "<div class=c>Backend</div><div class=\"c num\">KLOC</div></div>"
            "<div class=tr><div class=\"c name\">Parser</div>"
            "<div class=c>lexbor</div><div class=\"c num\">1.4</div></div>"
            "<div class=\"tr odd\"><div class=\"c name\">Cascade</div>"
            "<div class=c>libcss</div><div class=\"c num\">0.9</div></div>"
            "<div class=tr><div class=\"c name\">Scripting</div>"
            "<div class=c>QuickJS-NG</div><div class=\"c num\">2.1</div></div>"
            "<div class=\"tr odd\"><div class=\"c name\">Layout</div>"
            "<div class=c>block + flex</div><div class=\"c num\">1.5</div></div>"
            "<div class=tr><div class=\"c name\">Paint</div>"
            "<div class=c>ydraw</div><div class=\"c num\">1.2</div></div>"
            "</div>"
            "</div>"),

        /* 4 — CSS cards (custom properties) ------------------------------- */
        DOC(":root{--lift:#141A1F;--row:#1E262C;--accent:#6BA892;--bright:#74C5A5;"
            "--border:#364A47;}"
            ".deck{display:flex;flex-direction:row;}"
            ".col{flex-grow:1;background:var(--lift);border:1px solid var(--border);"
            "border-radius:12px;padding:16px;margin-right:12px;}"
            ".col.two{background:var(--row);}"
            ".col.three{background:var(--accent);}"
            ".col h3{color:var(--bright);margin:0 0 8px;}"
            ".col.three h3{color:#0B1014;}"
            ".col p{color:#9FA7A8;margin:0;}"
            ".col.three p{color:#0B1014;}"
            ".swatch{height:34px;border-radius:8px;margin:0 0 10px;background:var(--accent);}"
            ".col.two .swatch{background:var(--bright);}"
            ".col.three .swatch{background:#0B1014;}",
            "<div class=card>"
            "<h2>CSS custom properties</h2>"
            "<p class=muted>Colors below come from <code>var(--accent)</code> &amp; friends, "
            "declared once on <code>:root</code> and resolved during the box pass.</p>"
            "<div class=deck>"
            "<div class=col><div class=swatch></div><h3>Surface</h3>"
            "<p>Raised panel on the brand background ladder.</p></div>"
            "<div class=\"col two\"><div class=swatch></div><h3>Row</h3>"
            "<p>One step brighter for hover / selection.</p></div>"
            "<div class=\"col three\"><div class=swatch></div><h3>Accent</h3>"
            "<p>The brand mint, driving every focus highlight.</p></div>"
            "</div>"
            "</div>"),

        /* 5 — JavaScript (DOM built at load) ------------------------------ */
        DOC("pre{background:#0B1014;border:1px solid #364A47;border-radius:8px;"
            "padding:12px 14px;color:#74C5A5;margin:0 0 14px;}"
            ".jrow{display:flex;flex-direction:row;}"
            ".jrow .jc{flex-grow:1;padding:8px 12px;border-top:1px solid #1E262C;color:#E0E5E4;}"
            ".jrow.jhead{background:#1E262C;border-radius:8px 8px 0 0;}"
            ".jrow.jhead .jc{border-top:0;color:#74C5A5;}"
            "#out{border:1px solid #364A47;border-radius:8px;background:#141A1F;margin:0 0 12px;}"
            ".note{padding:9px 12px;color:#9FA7A8;border-top:1px solid #1E262C;}",
            "<div class=card>"
            "<h2>JavaScript at load</h2>"
            "<p class=muted>QuickJS runs inline scripts while the page loads and mutates "
            "the DOM before paint. Everything in the box below was produced by this script:</p>"
            "<pre>var out = document.querySelector('#out');\n"
            "for (var n = 1; n &lt;= 6; n++)\n"
            "  out.innerHTML += row(n, n*n, Math.pow(2,n));\n"
            "out.appendChild(stamp(new Date()));</pre>"
            /* Fallback content — the script below replaces it via
             * innerHTML when QuickJS is compiled in. Builds without the
             * lib-quickjs prebuilt show this line instead of an empty box. */
            "<div id=out><div class=note>This panel is generated by JavaScript "
            "at page load. Seeing this line means the build was configured "
            "without QuickJS (the lib-quickjs prebuilt wasn't available).</div></div>"
            "</div>"
            "<script>"
            "function cell(t){return '<div class=\"jc\">'+t+'</div>';}"
            "var out = document.querySelector('#out');"
            "var html = '<div class=\"jrow jhead\">'+cell('n')+cell('n squared')+cell('2^n')+'</div>';"
            "for (var n = 1; n <= 6; n++) {"
            "  html += '<div class=\"jrow\">'+cell(n)+cell(n*n)+cell(Math.pow(2,n))+'</div>';"
            "}"
            "out.innerHTML = html;"
            "var note = document.createElement('div');"
            "note.className = 'note';"
            "note.textContent = 'document.createElement + new Date(): ' + new Date().toString();"
            "out.appendChild(note);"
            "</script>"),
    };
    *count = (int)(sizeof(pages) / sizeof(pages[0]));
    return pages;
}

/* Tab labels — index-aligned with demo_pages(). */
static const char *const *demo_labels(int *count)
{
    static const char *const labels[] = {
        "Welcome", "Typography", "Forms", "Grid", "CSS Cards", "JavaScript",
    };
    *count = (int)(sizeof(labels) / sizeof(labels[0]));
    return labels;
}

/* Tabbar VALUE_CHANGED → swap the ybrowser's document to the picked page.
 * userdata is the ybrowser widget itself (it outlives the subscription). */
static struct yetty_ycore_void_result on_tab_change(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event,
                                                     void *userdata)
{
    (void)ctx;
    (void)target;
    struct yetty_ygui_object *browser = userdata;
    if (!browser || !event) {
        return YETTY_OK_VOID();
    }
    int count = 0;
    const char *const *pages = demo_pages(&count);
    int idx = event->i0;
    if (idx < 0 || idx >= count) {
        return YETTY_OK_VOID();
    }
    return yetty_ygui_ybrowser_set_html(browser, pages[idx], strlen(pages[idx]));
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_ygui_object *root)
{
    (void)runner;

    /* Tabbar across the top. */
    struct yetty_ygui_object_ptr_result tbr =
        yetty_ygui_add(yetty_ygui_tabbar_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tbr, "tabbar");
    struct yetty_ygui_object *tabbar = tbr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(tabbar);
        l.height = 36;
        l.gap = 4;
        err_ok(yetty_ygui_widget_layout_set(tabbar, &l));
    }

    int label_count = 0;
    const char *const *labels = demo_labels(&label_count);
    for (int i = 0; i < label_count; ++i) {
        struct yetty_ygui_object_ptr_result hr =
            yetty_ygui_tabbar_add_tab(tabbar, labels[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "tabbar add_tab");
    }

    /* The single ybrowser that fills the rest of the body. */
    struct yetty_ygui_object_ptr_result br =
        yetty_ygui_add(yetty_ygui_ybrowser_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ybrowser");
    struct yetty_ygui_object *browser = br.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(browser);
        l.flex_grow = 1.0f;
        l.min_height = 240.0f;
        err_ok(yetty_ygui_widget_layout_set(browser, &l));
    }

    /* Switch documents when the active tab changes. */
    err_ok(yetty_ygui_object_subscribe(tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_change,
                                       browser));

    /* Show the first page initially. */
    int page_count = 0;
    const char *const *pages = demo_pages(&page_count);
    return yetty_ygui_ybrowser_set_html(browser, pages[0], strlen(pages[0]));
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "26_ybrowser", build);
}
