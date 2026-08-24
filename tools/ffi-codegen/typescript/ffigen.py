#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///
"""FFI binding generator (TypeScript) — model.yaml → bindings/typescript.

Emits the `@yetty/ydraw` package surface: ydraw.mjs (runnable ESM, node
type-strips the demo .ts files that import it) plus ydraw.d.ts (the typed
surface for editors/tsc). The demo sketches' constructor convention:

    const dlist = new DrawableList();
    dlist.add(new Circle({ centerX: 96, fill: "#6BA892" }));
    dlist.add(new Text("hello ydraw", { x: 40, fontSize: 24 }));
    dlist.dcsEmit();
    dlist.destroy();

Per class the generator emits a spec table (create/destroy/primary
symbols + the option-key map) and a thin class whose constructor routes
through the shared hand-written runtime (runtime.mjs). Option keys are
fully model-driven, lowerCameled from the C names:

  - settable property      -> key of the property name (font_id -> fontId)
  - set_<x>(scalar/string) -> key <x>; booleans coerce to 0/1
  - set_<x>(a, b, ...)     -> key <x>: number[], one nesting level flattened
  - set_<x>(ycore_buffer)  -> key <x>: number[] (packed as f32)
  - add_<x>(object)        -> key <x>s: instance[]
  - the primary slot is the optional positional string first argument

FFI is koffi over libyetty_ffi.so; every result struct is decoded through
the flat yetty_result_view (see runtime.mjs).
"""

from __future__ import annotations

import pathlib
import re
import sys

import yaml

REPO = pathlib.Path(__file__).resolve().parents[3]
SRC_ROOTS = [REPO / "src" / "yetty", REPO / "src" / "api"]
OUT_DIR = REPO / "bindings" / "typescript"

# The friendly-namespace domains (the ts surface mirrors yetty.ydraw).
DOMAINS = ["ydrawlist2", "ysdf2", "api_yplot", "ycomplex2"]

# Single C words that camel as two (the flag setters).
CAMEL_WORDS = {"nogrid": "NoGrid", "noaxes": "NoAxes", "nolabels": "NoLabels"}

# Feature-gated classes: (domain, class) -> (probe symbol, feature name).
# ESM exports are static, so the class is always exported; its constructor
# guards on the probe and throws a feature-named error on a build without
# it. Discovery: the generated has<Class>() helper.
FEATURE_GATED = {
    ("ycomplex2", "video"): ("yetty_ycomplex2_video_create", "yvideo"),
}


def model_domain(model: dict) -> str | None:
    for entry in model.get("classes", []) or []:
        if entry.get("domain"):
            return entry["domain"]
    for entry in model.get("methods", []) or []:
        if entry.get("domain"):
            return entry["domain"]
    return None


def discover_models() -> dict[str, dict]:
    found: dict[str, dict] = {}
    for root in SRC_ROOTS:
        if not root.is_dir():
            continue
        for path in root.glob("*/model.yaml"):
            model = yaml.safe_load(path.read_text()) or {}
            domain = model_domain(model)
            if domain:
                found[domain] = model
    missing = [domain for domain in DOMAINS if domain not in found]
    if missing:
        sys.stderr.write(f"ffigen-ts: missing models for {missing}\n")
        sys.exit(1)
    return {domain: found[domain] for domain in DOMAINS}


def classify(type_str: str) -> tuple[str, str | None]:
    text = (type_str or "").strip()
    if "*" in text:
        return ("ptr", None)
    text = re.sub(r"^(const|volatile)\s+", "", text)
    for keyword in ("struct", "union", "enum"):
        match = re.match(rf"^{keyword}\s+(\w+)$", text)
        if match:
            return (keyword, match.group(1))
    return ("scalar", None)


def pascal(name: str) -> str:
    return "".join(
        CAMEL_WORDS.get(part, part.capitalize()) for part in name.split("_"))


def lower_camel(name: str) -> str:
    text = pascal(name)
    return text[0].lower() + text[1:]


def scalar_c(type_str: str) -> str:
    return re.sub(r"^(const|volatile)\s+", "", (type_str or "").strip())


def class_registry(models: dict[str, dict]) -> dict[tuple[str, str], dict]:
    registry: dict[tuple[str, str], dict] = {}
    for model in models.values():
        methods = {method["slot"]: method for method in model.get("methods", [])}
        for cls in model.get("classes", []) or []:
            registry[(cls.get("domain"), cls["name"])] = {
                "cls": cls, "methods": methods}
    return registry


def ancestry(registry: dict, domain: str, name: str) -> list[dict]:
    chain: list[dict] = []
    key = (domain, name)
    seen: set[tuple[str, str]] = set()
    while key in registry and key not in seen:
        seen.add(key)
        chain.append(registry[key])
        parent = registry[key]["cls"].get("parent")
        if not parent:
            break
        key = (parent.get("domain"), parent["name"])
    return chain


def field_plan(registry, domain, cls):
    """Option-key map + primary/destroy symbols + signatures for one class.

    Each field is (ts_key, kind, symbol, signature, ts_type, adder_class).
    """
    fields: list[tuple] = []
    seen_keys: set[str] = set()
    destroy_symbol = None
    primary_symbol = None
    signatures: dict[str, str] = {}

    def add_field(ts_key, kind, symbol, signature, ts_type,
                  adder_class=None, arity=None):
        if ts_key in seen_keys:
            return
        seen_keys.add(ts_key)
        fields.append((ts_key, kind, symbol, signature, ts_type,
                       adder_class, arity))
        signatures[symbol] = signature

    primary = cls.get("primary_slot")
    for link in ancestry(registry, domain, cls["name"]):
        link_cls = link["cls"]
        # Method-backed keys first: a string setter like set_color wins
        # over a raw packed-word property of the same name.
        for op in link_cls.get("ops", []):
            slot = op["slot"]
            method = link["methods"].get(slot)
            if not method:
                continue
            symbol = f"yetty_{method['domain']}_{slot}"
            if slot == "destroy":
                if destroy_symbol is None:
                    destroy_symbol = symbol
                    signatures[symbol] = (
                        f"yetty_result_view {symbol}(void *obj)")
                continue
            arg_defs = method["args"][1:]
            if slot.startswith("set_") and len(arg_defs) == 1:
                arg = arg_defs[0]
                category, tag = classify(arg["type"])
                ts_key = lower_camel(slot[4:])
                if category == "ptr":
                    signature = (f"yetty_result_view {symbol}"
                                 "(void *obj, const char *value)")
                    add_field(ts_key, "cstr", symbol, signature, "string")
                    if slot == primary:
                        primary_symbol = symbol
                elif (category, tag) == ("struct", "yetty_ycore_buffer"):
                    signature = (f"yetty_result_view {symbol}"
                                 "(void *obj, yetty_ycore_buffer value)")
                    add_field(ts_key, "buffer", symbol, signature, "number[]")
                else:
                    ctype = scalar_c(arg["type"])
                    signature = (f"yetty_result_view {symbol}"
                                 f"(void *obj, {ctype} value)")
                    add_field(ts_key, "scalar", symbol, signature,
                              "number | boolean")
            elif slot.startswith("set_") and len(arg_defs) > 1:
                params = ", ".join(
                    f"{scalar_c(arg['type'])} value{index}"
                    for index, arg in enumerate(arg_defs))
                signature = (f"yetty_result_view {symbol}"
                             f"(void *obj, {params})")
                # Fixed arity: the type is a tuple (plus the pair-nested
                # form for 4-scalar slots like view), and the runtime
                # validates the flattened length.
                arity = len(arg_defs)
                flat_tuple = "[" + ", ".join(["number"] * arity) + "]"
                ts_type = flat_tuple
                if arity >= 4 and arity % 2 == 0:
                    pairs = ", ".join(["[number, number]"] * (arity // 2))
                    ts_type = f"{flat_tuple} | [{pairs}]"
                add_field(lower_camel(slot[4:]), "multi", symbol, signature,
                          ts_type, arity=arity)
            elif slot.startswith("add_") and len(arg_defs) == 1:
                signature = (f"yetty_result_view {symbol}"
                             "(void *obj, void *child)")
                add_field(lower_camel(slot[4:]) + "s", "adder", symbol,
                          signature, None, pascal(slot[4:]))
        for prop in link_cls.get("data_fields", []) or []:
            if not prop.get("set"):
                continue
            ctype = scalar_c(prop["type"])
            symbol = (f"yetty_{link_cls['domain']}_{link_cls['name']}"
                      f"_{prop['name']}_set")
            signature = (f"yetty_result_view {symbol}"
                         f"(void *obj, {ctype} value)")
            add_field(lower_camel(prop["name"]), "scalar", symbol, signature,
                      "number | boolean")
    return fields, primary_symbol, destroy_symbol, signatures


def emit(models: dict[str, dict]):
    registry = class_registry(models)

    plans: list[dict] = []
    list_plan = None
    signatures: dict[str, str] = {
        "yetty_yclass_object_free":
            "yetty_result_view yetty_yclass_object_free(void *obj)",
    }
    for domain, model in models.items():
        for cls in model.get("classes", []) or []:
            if cls.get("type") != "regular":
                continue
            fields, primary_symbol, destroy_symbol, class_signatures = (
                field_plan(registry, domain, cls))
            create_symbol = f"yetty_{domain}_{cls['name']}_create"
            class_signatures[create_symbol] = (
                f"yetty_result_view {create_symbol}(void *ctx)")
            signatures.update(class_signatures)
            entry = {"domain": domain, "cls": cls, "fields": fields,
                     "create": create_symbol, "primary": primary_symbol,
                     "destroy": destroy_symbol,
                     "ts_name": pascal(cls["name"])}
            if cls["name"] == "drawable_list":
                list_plan = entry
            elif entry["ts_name"] == "Drawable":
                # The abstract base: exported as the marker base class,
                # never as a spec-table value class.
                continue
            else:
                plans.append(entry)
    if list_plan is None:
        sys.stderr.write("ffigen-ts: no drawable_list class in the model\n")
        sys.exit(1)
    for _, _, method, symbol in list_method_symbols(list_plan, registry):
        if len(method["args"]) == 1:
            signatures[symbol] = f"yetty_result_view {symbol}(void *obj)"
        else:
            signatures[symbol] = (f"yetty_result_view {symbol}"
                                  "(void *obj, void *child)")

    emit_module(plans, list_plan, registry, signatures)
    emit_types(plans, list_plan, registry)
    class_count = len(plans) + 2
    print(f"ffigen: typescript → @yetty/ydraw ({class_count} classes) under "
          f"{OUT_DIR.relative_to(REPO)}")


def list_method_symbols(list_plan: dict, registry: dict):
    """(method_name, slot, symbol) for the drawable-list void methods."""
    methods = registry[(list_plan["domain"],
                        list_plan["cls"]["name"])]["methods"]
    result = []
    for op in list_plan["cls"].get("ops", []):
        slot = op["slot"]
        method = methods.get(slot)
        if not method:
            continue
        if classify(method["return_type"])[1] != "yetty_ycore_void_result":
            continue
        result.append((lower_camel(slot), slot, method,
                       f"yetty_{method['domain']}_{slot}"))
    return result


def emit_module(plans, list_plan, registry, signatures):
    lines = [
        "// @yetty/ydraw — the ydraw client interface: one drawable list,",
        "// immediate appends in call order; add() manages nothing and",
        "// returns nothing; font ids are user-chosen record fields.",
        "//",
        "// GENERATED from model.yaml by tools/ffi-codegen/typescript/"
        "ffigen.py — do not edit.",
        'import * as runtime from "./runtime.mjs";',
        "",
        "runtime.registerSignatures({",
    ]
    for symbol in sorted(signatures):
        lines.append(f'  {symbol}: "{signatures[symbol]}",')
    lines += [
        "});",
        "",
        "// The abstract drawable base — a marker class; every concrete",
        "// class below is a Drawable in the draw-list sense.",
        "export class Drawable {",
        "  constructor() {",
        "    this.handle = null;",
        "  }",
        "}",
        "",
    ]
    for entry in plans:
        spec_name = f"{entry['cls']['name'].upper()}_SPEC"
        lines.append(f"const {spec_name} = {{")
        lines.append(f'  className: "{entry["ts_name"]}",')
        lines.append(f'  create: "{entry["create"]}",')
        if entry["primary"]:
            lines.append(f'  primary: "{entry["primary"]}",')
        else:
            lines.append("  primary: null,")
        if entry["destroy"]:
            lines.append(f'  destroy: "{entry["destroy"]}",')
        else:
            lines.append("  destroy: null,")
        lines.append("  fields: {")
        for ts_key, kind, symbol, _, _, _, arity in entry["fields"]:
            if kind == "multi":
                lines.append(f'    {ts_key}: {{ kind: "{kind}",'
                             f' sym: "{symbol}", n: {arity} }},')
            else:
                lines.append(f'    {ts_key}: {{ kind: "{kind}",'
                             f' sym: "{symbol}" }},')
        lines.append("  },")
        lines.append("};")
        lines.append("")
        gate = FEATURE_GATED.get((entry["domain"], entry["cls"]["name"]))
        if gate:
            probe_symbol, feature = gate
            lines.append(f"// Discovery for the feature-gated {entry['ts_name']}"
                         f" class ({feature} build feature).")
            lines.append(f"export function has{entry['ts_name']}() {{")
            lines.append(f'  return runtime.hasSymbol("{probe_symbol}");')
            lines.append("}")
            lines.append("")
        lines.append(f"export class {entry['ts_name']} extends Drawable {{")
        lines.append("  constructor(primaryOrOptions, options) {")
        lines.append("    super();")
        if gate:
            probe_symbol, feature = gate
            lines.append(f'    runtime.requireFeature("{entry["ts_name"]}",'
                         f' "{probe_symbol}", "{feature}");')
        lines.append(f"    this.handle = runtime.construct({spec_name},"
                     " primaryOrOptions, options);")
        lines.append(f"    runtime.adopt(this, {spec_name});")
        lines.append("  }")
        lines.append("")
        lines.append("  destroy() {")
        lines.append(f"    runtime.destroyObject(this, {spec_name});")
        lines.append("  }")
        lines.append("}")
        lines.append("")

    entry = list_plan
    spec_name = "DRAWABLE_LIST_SPEC"
    lines.append(f"const {spec_name} = {{")
    lines.append('  className: "DrawableList",')
    lines.append(f'  create: "{entry["create"]}",')
    lines.append("  primary: null,")
    if entry["destroy"]:
        lines.append(f'  destroy: "{entry["destroy"]}",')
    else:
        lines.append("  destroy: null,")
    lines.append("  fields: {},")
    lines.append("};")
    lines.append("")
    lines.append("// DrawableList — the drawable list: one list, immediate")
    lines.append("// appends in call order.")
    lines.append("export class DrawableList {")
    lines.append("  constructor() {")
    lines.append(f"    this.handle = runtime.invoke({spec_name}.create, null);")
    lines.append(f"    runtime.adopt(this, {spec_name});")
    lines.append("  }")
    for method_name, slot, method, symbol in list_method_symbols(
            list_plan, registry):
        lines.append("")
        if slot == "add":
            lines.append("  // add packs the drawable's record into the list,")
            lines.append("  // immediately. It manages nothing and returns"
                         " nothing.")
            lines.append("  add(drawable) {")
            lines.append("    runtime.requireHandle(this, \"DrawableList\");")
            lines.append(f'    runtime.invoke("{symbol}", this.handle,'
                         " drawable.handle);")
            lines.append("  }")
        elif slot == "destroy":
            lines.append("  destroy() {")
            lines.append(f"    runtime.destroyObject(this, {spec_name});")
            lines.append("  }")
        elif len(method["args"]) == 1:
            lines.append(f"  {method_name}() {{")
            lines.append("    runtime.requireHandle(this, \"DrawableList\");")
            lines.append(f'    runtime.invoke("{symbol}", this.handle);')
            lines.append("  }")
    lines.append("}")
    (OUT_DIR / "ydraw.mjs").write_text("\n".join(lines) + "\n")


def emit_types(plans, list_plan, registry):
    lines = [
        "// Type surface of @yetty/ydraw.",
        "// GENERATED from model.yaml by tools/ffi-codegen/typescript/"
        "ffigen.py — do not edit.",
        "",
        "export declare class Drawable {",
        "  handle: unknown;",
        "}",
        "",
    ]
    for entry in plans:
        ts_name = entry["ts_name"]
        if (entry["domain"], entry["cls"]["name"]) in FEATURE_GATED:
            lines.append(f"export declare function has{ts_name}(): boolean;")
            lines.append("")
        lines.append(f"export interface {ts_name}Options {{")
        for ts_key, kind, _, _, ts_type, adder_class, _ in entry["fields"]:
            value_type = f"{adder_class}[]" if kind == "adder" else ts_type
            lines.append(f"  {ts_key}?: {value_type};")
        lines.append("}")
        lines.append("")
        lines.append(f"export declare class {ts_name} extends Drawable {{")
        if entry["primary"]:
            lines.append(f"  constructor(primary?: string | {ts_name}Options,"
                         f" options?: {ts_name}Options);")
        else:
            lines.append(f"  constructor(options?: {ts_name}Options);")
        lines.append("  destroy(): void;")
        lines.append("}")
        lines.append("")
    lines.append("export declare class DrawableList {")
    lines.append("  constructor();")
    for method_name, slot, method, _ in list_method_symbols(
            list_plan, registry):
        if slot == "add":
            lines.append("  add(drawable: Drawable): void;")
        elif slot == "destroy":
            lines.append("  destroy(): void;")
        elif len(method["args"]) == 1:
            lines.append(f"  {method_name}(): void;")
    lines.append("}")
    (OUT_DIR / "ydraw.d.ts").write_text("\n".join(lines) + "\n")


def main():
    emit(discover_models())


if __name__ == "__main__":
    main()
