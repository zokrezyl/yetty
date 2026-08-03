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

#include <yetty/api/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static void set_grow(struct yetty_yclass_object *w, float grow)
{
    if (!w) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
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

/* Add one ydiagram leaf under `sec`, fed `mermaid`. The widget sizes its
 * own layout box to the diagram's intrinsic extent in set_source, so the
 * section height (summed below) reserves exactly the right space. */
static struct yetty_yclass_object *add_diagram(struct yetty_yclass_object *sec, const char *mermaid)
{
    if (!sec) {
        return NULL;
    }
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_add(sec, yetty_ygui_ydiagram_class_get().value);
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
static void finalize_section(struct yetty_yclass_object *sec)
{
    if (!sec) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result section_layout_res =
        yetty_ygui_widget_layout_get(sec);
    if (YETTY_IS_ERR(section_layout_res)) {
        yetty_ycore_error_destroy(section_layout_res.error);
        return;
    }
    const struct yetty_ygui_layout *sl = section_layout_res.value;
    float total = sl->padding_top + sl->padding_bottom;
    int n = 0;
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(sec);
    if (YETTY_IS_ERR(child_res)) {
        yetty_ycore_error_destroy(child_res.error);
        return;
    }
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ygui_layout_const_ptr_result child_layout_res =
            yetty_ygui_widget_layout_get(c);
        if (YETTY_IS_ERR(child_layout_res)) {
            yetty_ycore_error_destroy(child_layout_res.error);
            return;
        }
        const struct yetty_ygui_layout *cl = child_layout_res.value;
        total += cl->height > 0.0f ? cl->height : 0.0f;
        n++;
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        if (YETTY_IS_ERR(next_res)) {
            yetty_ycore_error_destroy(next_res.error);
            return;
        }
        c = next_res.value;
    }
    if (n > 1) {
        total += sl->gap * (float)(n - 1);
    }
    struct yetty_ygui_layout_const_ptr_result final_layout_res = yetty_ygui_widget_layout_get(sec);
    if (YETTY_IS_ERR(final_layout_res)) {
        yetty_ycore_error_destroy(final_layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *final_layout_res.value;
    l.height = total;
    err_ok(yetty_ygui_widget_layout_set(sec, &l));
}

/* ---- Diagram sources (all use only parser-supported Mermaid) ---- */

static const char *k_flowchart = "graph TD\n"
                                 "  A[Start] --> B{Decision}\n"
                                 "  B -->|Yes| C(Process)\n"
                                 "  B -->|No|  D((Done))\n"
                                 "  C --> D\n";

static const char *k_pipeline = "graph LR\n"
                                "  A[node a] --> B[node b]\n"
                                "  B --> C[node c]\n"
                                "  C --> D[node d]\n"
                                "  A --> D\n";

static const char *k_shapes = "graph TD\n"
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

static const char *k_subgraphs = "graph TD\n"
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

static const char *k_state_machine = "flowchart TD\n"
                                     "  Start((start)) --> Idle[Idle]\n"
                                     "  Idle -->|connect|     Connecting{{Connecting}}\n"
                                     "  Connecting -->|ok|    Ready(Ready)\n"
                                     "  Connecting -->|fail|  Failed[/Failed/]\n"
                                     "  Ready  -->|disconnect| Idle\n"
                                     "  Ready  -->|crash|      Failed\n"
                                     "  Failed -->|retry|      Connecting\n"
                                     "  Failed -->|abort|      Done((done))\n";

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:36_ydiagram")]] [[clang::annotate("parent@yguiapp:app")]]
yetty_demoygui_36_ydiagram {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_36_ydiagram_ptr, struct yetty_demoygui_36_ydiagram *);
struct yetty_yclass_ptr_result yetty_demoygui_36_ydiagram_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;

    /* Scrollarea fills the body and stacks the sections vertically. */
    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(root, yetty_ygui_scrollarea_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build: scrollarea");
    set_grow(sr.value, 1.0f);
    struct yetty_yclass_object *area = sr.value;

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
        struct yetty_yclass_object *sec = add_section(area, sections[i].title);
        add_diagram(sec, sections[i].src);
        finalize_section(sec);
    }

    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_36_ydiagram_class_get().value);
}

#include "yetty/gen/impl/demoygui/36_ydiagram/main.c"
