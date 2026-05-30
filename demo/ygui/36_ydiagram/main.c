/*
 * Demo 36_ydiagram: ydiagram — Mermaid diagrams under collapsing headers.
 *
 * A scrollarea stacks one collapsing_header section per diagram kind
 * (flowchart, left-to-right pipeline, every node shape, subgraphs /
 * clusters, and a small state machine). Each section holds a single
 * ydiagram figure widget fed a different bit of Mermaid source. Click a
 * section header to fold it away; the diagram disappears and the stack
 * resizes.
 *
 * A diagram's layout has an intrinsic size that the flex engine can't
 * derive, so each ydiagram leaf carries an authored height and the
 * section height is summed from its child (see finalize_section, the same
 * approach demo 35 uses for the widget catalog).
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree.
 * Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include "../runner.h"
#include <yetty/ygui/ygui.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
}

static void set_grow(struct yetty_ygui_object *w, float grow)
{
    if (!w) return;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
    l.flex_grow = grow;
    err_ok(yetty_ygui_widget_layout_set(w, &l));
}

/* Open collapsing_header section, titled, initially expanded. */
static struct yetty_ygui_object *add_section(struct yetty_ygui_object *area, const char *title)
{
    struct yetty_ygui_object_ptr_result r =
        yetty_ygui_add(yetty_ygui_collapsing_header_class_get().value, area);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    err_ok(yetty_ygui_collapsing_header_set_title(r.value, title));
    err_ok(yetty_ygui_collapsing_header_set_open(r.value, 1));
    return r.value;
}

/* Add one ydiagram leaf under `sec`, fed `mermaid`. The widget sizes its
 * own layout box to the diagram's intrinsic extent in set_source, so the
 * section height (summed below) reserves exactly the right space. */
static struct yetty_ygui_object *add_diagram(struct yetty_ygui_object *sec, const char *mermaid)
{
    if (!sec) return NULL;
    struct yetty_ygui_object_ptr_result r =
        yetty_ygui_add(yetty_ygui_ydiagram_class_get().value, sec);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    err_ok(yetty_ygui_ydiagram_set_source(r.value, mermaid));
    return r.value;
}

/* Derive a section's open height from its child diagram(s): header strip
 * paddings + the sum of authored child heights + inter-row gaps. Must run
 * after every child is added. */
static void finalize_section(struct yetty_ygui_object *sec)
{
    if (!sec) return;
    const struct yetty_ygui_layout *sl = yetty_ygui_widget_layout_get(sec);
    float total = sl->padding_top + sl->padding_bottom;
    int n = 0;
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(sec); c;
         c = yetty_ygui_object_next_sibling(c)) {
        const struct yetty_ygui_layout *cl = yetty_ygui_widget_layout_get(c);
        total += cl->height > 0.0f ? cl->height : 0.0f;
        n++;
    }
    if (n > 1) total += sl->gap * (float)(n - 1);
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(sec);
    l.height = total;
    err_ok(yetty_ygui_widget_layout_set(sec, &l));
}

/* ---- Diagram sources (all use only parser-supported Mermaid) ---- */

static const char *k_flowchart =
    "graph TD\n"
    "  A[Start] --> B{Decision}\n"
    "  B -->|Yes| C(Process)\n"
    "  B -->|No|  D((Done))\n"
    "  C --> D\n";

static const char *k_pipeline =
    "graph LR\n"
    "  A[node a] --> B[node b]\n"
    "  B --> C[node c]\n"
    "  C --> D[node d]\n"
    "  A --> D\n";

static const char *k_shapes =
    "graph TD\n"
    "  R[rectangle]\n"
    "  RR(rounded)\n"
    "  C((circle))\n"
    "  D{diamond}\n"
    "  H{{hexagon}}\n"
    "  CY[(cylinder)]\n"
    "  S([stadium])\n"
    "  PR[/parallelogram/]\n"
    "  R  --> RR\n"
    "  RR --> C\n"
    "  C  --> D\n"
    "  D  --> H\n"
    "  H  --> CY\n"
    "  CY --> S\n"
    "  S  --> PR\n";

static const char *k_subgraphs =
    "graph TD\n"
    "  subgraph frontend [Frontend]\n"
    "    UI[UI layer]\n"
    "    API[API client]\n"
    "    UI --> API\n"
    "  end\n"
    "  subgraph backend [Backend services]\n"
    "    Gateway[Gateway]\n"
    "    Auth[Auth]\n"
    "    Store[(Store)]\n"
    "    Gateway --> Auth\n"
    "    Auth    --> Store\n"
    "  end\n"
    "  API --> Gateway\n";

static const char *k_state_machine =
    "flowchart TD\n"
    "  Start((start)) --> Idle[Idle]\n"
    "  Idle -->|connect|     Connecting{{Connecting}}\n"
    "  Connecting -->|ok|    Ready(Ready)\n"
    "  Connecting -->|fail|  Failed[/Failed/]\n"
    "  Ready  -->|disconnect| Idle\n"
    "  Ready  -->|crash|      Failed\n"
    "  Failed -->|retry|      Connecting\n"
    "  Failed -->|abort|      Done((done))\n";

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_ygui_object *root)
{
    (void)runner;

    /* Scrollarea fills the body and stacks the sections vertically. */
    struct yetty_ygui_object_ptr_result sr =
        yetty_ygui_add(yetty_ygui_scrollarea_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build: scrollarea");
    set_grow(sr.value, 1.0f);
    struct yetty_ygui_object *area = sr.value;

    struct {
        const char *title;
        const char *src;
    } sections[] = {
        {"Flowchart (top-down)", k_flowchart},
        {"Pipeline (left-to-right)", k_pipeline},
        {"Node shapes", k_shapes},
        {"Subgraphs / clusters", k_subgraphs},
        {"State machine", k_state_machine},
    };

    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
        struct yetty_ygui_object *sec = add_section(area, sections[i].title);
        add_diagram(sec, sections[i].src);
        finalize_section(sec);
    }

    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "36_ydiagram", build);
}
