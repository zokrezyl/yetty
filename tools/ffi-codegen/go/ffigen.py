#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///
"""FFI binding generator (Go) — model.yaml → bindings/go/ydraw.

Emits the `ydraw` Go package: the friendly namespace over the v2 client
interface (domains ydrawlist2, ysdf2, api_yplot, ycomplex2), matching the
demo sketches' value-struct surface:

    dlist := ydraw.NewDrawableList()
    dlist.Add(ydraw.Circle{CenterX: 96, Fill: "#6BA892"})
    dlist.DcsEmit()
    dlist.Destroy()

Every content class becomes a plain value struct; Add() (and list-valued
fields like Functions/Buffers) MATERIALIZE it: create the C object, apply
the non-zero fields through the same generated setters and property
accessors every language uses, dispatch, free. The field plan is fully
model-driven:

  - settable property        -> field of the property's Go type (zero = skip)
  - set_<x>(one scalar/str)  -> field <X>; empty string = skip
  - set_<x>(a, b, ...)       -> field <X> []float64
  - set_<x>(ycore_buffer)    -> field <X> []float64 (packed as f32)
  - add_<x>(object)          -> field <X>s []<X>, applied per element
  - the primary slot binds under its C ARGUMENT name (set_glb's arg is
    `path` -> field Path) — how every path-sourced kind exposes Path
  - args/props with boolean-flavored names (BOOL_FLAG_NAMES) -> Go bool

Linkage is cgo over libyetty_ffi.so; the preamble reproduces the result
structs + only the prototypes the package calls (all returning
yetty_ycore_void_result / yetty_yclass_object_ptr_result), and unwraps
the anonymous result unions through static C helper functions so Go never
touches union layout. Build/run:

    CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi" \
    LD_LIBRARY_PATH=<build>/src/yetty/yffi go run <demo>.go
"""

from __future__ import annotations

import pathlib
import re
import sys

import yaml

REPO = pathlib.Path(__file__).resolve().parents[3]
SRC_ROOTS = [REPO / "src" / "yetty", REPO / "src" / "api"]
OUT_DIR = REPO / "bindings" / "go" / "ydraw"

# The friendly-namespace domains (the go surface mirrors yetty.ydraw).
DOMAINS = ["ydrawlist2", "ysdf2", "api_yplot", "ycomplex2"]

# C arg/property names that are on/off flags: Go surfaces them as bool.
BOOL_FLAG_NAMES = {"disabled", "enabled", "wireframe"}

# Feature-gated classes: (domain, class) -> feature name. Their symbols are
# declared __attribute__((weak)) so the package links against a feature-off
# libyetty_ffi.so (the references resolve to NULL); materialize() guards on
# presence and Has<Class>() is the discovery helper.
FEATURE_GATED = {("ycomplex2", "video"): "yvideo"}

CAMEL_ACRONYMS = {"id": "ID", "fps": "FPS", "wgsl": "WGSL", "h264": "H264",
                  "nogrid": "NoGrid", "noaxes": "NoAxes",
                  "nolabels": "NoLabels"}


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
        sys.stderr.write(f"ffigen-go: missing models for {missing}\n")
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


def camel(name: str) -> str:
    return "".join(
        CAMEL_ACRONYMS.get(part, part.capitalize()) for part in name.split("_"))


def scalar_c(type_str: str) -> str:
    return re.sub(r"^(const|volatile)\s+", "", (type_str or "").strip())


GO_SCALAR = {"float": "float64", "double": "float64", "uint32_t": "uint32",
             "int32_t": "int32", "int": "int32", "size_t": "uint64",
             "uint64_t": "uint64"}
CGO_CAST = {"float": "C.float", "double": "C.double", "uint32_t": "C.uint32_t",
            "int32_t": "C.int32_t", "int": "C.int", "size_t": "C.size_t",
            "uint64_t": "C.uint64_t"}


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
    """The model-driven field mapping for one class.

    Returns (fields, destroy_symbol) where each field is
    (go_name, go_type, kind, payload); kind in prop/prop_flag/cstr/scalar/
    scalar_flag/buffer/multi/adder.
    """
    fields: list[tuple[str, str, str, dict]] = []
    seen_names: set[str] = set()
    destroy_symbol = None

    def add_field(go_name, go_type, kind, payload):
        if go_name in seen_names:
            return
        seen_names.add(go_name)
        fields.append((go_name, go_type, kind, payload))

    primary = cls.get("primary_slot")
    for link in ancestry(registry, domain, cls["name"]):
        link_cls = link["cls"]
        # Method-backed fields first: a string setter like set_color wins
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
                continue
            arg_defs = method["args"][1:]
            if slot.startswith("set_") and len(arg_defs) == 1:
                arg = arg_defs[0]
                category, tag = classify(arg["type"])
                go_name = camel(arg["name"] if slot == primary else slot[4:])
                if category == "ptr":
                    add_field(go_name, "string", "cstr", {"sym": symbol})
                elif (category, tag) == ("struct", "yetty_ycore_buffer"):
                    add_field(go_name, "[]float64", "buffer", {"sym": symbol})
                elif arg["name"] in BOOL_FLAG_NAMES:
                    ctype = scalar_c(arg["type"])
                    add_field(go_name, "bool", "scalar_flag", {
                        "sym": symbol, "ctype": ctype,
                        "cast": CGO_CAST.get(ctype, "C.uint32_t")})
                else:
                    ctype = scalar_c(arg["type"])
                    add_field(go_name, GO_SCALAR.get(ctype, "float64"),
                              "scalar", {"sym": symbol, "ctype": ctype,
                                         "cast": CGO_CAST.get(ctype, "C.float")})
            elif slot.startswith("set_") and len(arg_defs) > 1:
                add_field(camel(slot[4:]), "[]float64", "multi", {
                    "sym": symbol,
                    "ctypes": [scalar_c(arg["type"]) for arg in arg_defs],
                    "casts": [CGO_CAST.get(scalar_c(arg["type"]), "C.float")
                              for arg in arg_defs]})
            elif slot.startswith("add_") and len(arg_defs) == 1:
                child = camel(slot[4:])
                add_field(child + "s", f"[]{child}", "adder", {"sym": symbol})
        for prop in link_cls.get("data_fields", []) or []:
            if not prop.get("set"):
                continue
            ctype = scalar_c(prop["type"])
            symbol = (f"yetty_{link_cls['domain']}_{link_cls['name']}"
                      f"_{prop['name']}_set")
            if prop["name"] in BOOL_FLAG_NAMES:
                add_field(camel(prop["name"]), "bool", "prop_flag", {
                    "sym": symbol, "ctype": ctype,
                    "cast": CGO_CAST.get(ctype, "C.uint32_t")})
            else:
                add_field(camel(prop["name"]), GO_SCALAR.get(ctype, "float64"),
                          "prop", {"sym": symbol, "ctype": ctype,
                                   "cast": CGO_CAST.get(ctype, "C.float")})
    return fields, destroy_symbol


def emit(models: dict[str, dict]):
    registry = class_registry(models)

    # ---- collect the plans (drives both prototypes and Go code) ----
    plans: list[dict] = []
    list_plan = None
    for domain, model in models.items():
        for cls in model.get("classes", []) or []:
            if cls.get("type") != "regular":
                continue
            fields, destroy_symbol = field_plan(registry, domain, cls)
            entry = {"domain": domain, "cls": cls, "fields": fields,
                     "destroy": destroy_symbol,
                     "go_name": camel(cls["name"])}
            if cls["name"] == "drawable_list":
                list_plan = entry
            elif entry["go_name"] == "Drawable":
                # The abstract base: its Go surface IS the Drawable
                # interface, never a value struct.
                continue
            else:
                plans.append(entry)
    if list_plan is None:
        sys.stderr.write("ffigen-go: no drawable_list class in the model\n")
        sys.exit(1)

    # ---- cgo preamble: structs + only the prototypes we call ----
    protos: dict[str, str] = {}
    weak_symbols: set[str] = set()
    current_entry_gated = False

    def proto(symbol: str, signature: str):
        protos[symbol] = signature
        if current_entry_gated:
            weak_symbols.add(symbol)

    for entry in plans + [list_plan]:
        domain, cls = entry["domain"], entry["cls"]
        entry["feature"] = FEATURE_GATED.get((domain, cls["name"]))
        current_entry_gated = entry["feature"] is not None
        create_symbol = f"yetty_{domain}_{cls['name']}_create"
        proto(create_symbol,
              f"struct yetty_yclass_object_ptr_result {create_symbol}"
              "(void *ctx);")
        if entry["destroy"]:
            proto(entry["destroy"],
                  f"struct yetty_ycore_void_result {entry['destroy']}"
                  "(struct yetty_yclass_object *obj);")
        for _, _, kind, payload in entry["fields"]:
            symbol = payload["sym"]
            if kind in ("prop", "prop_flag", "scalar", "scalar_flag"):
                proto(symbol,
                      f"struct yetty_ycore_void_result {symbol}"
                      f"(struct yetty_yclass_object *obj,"
                      f" {payload['ctype']} value);")
            elif kind == "cstr":
                proto(symbol,
                      f"struct yetty_ycore_void_result {symbol}"
                      "(struct yetty_yclass_object *obj, const char *value);")
            elif kind == "buffer":
                proto(symbol,
                      f"struct yetty_ycore_void_result {symbol}"
                      "(struct yetty_yclass_object *obj, "
                      "struct yetty_ycore_buffer value);")
            elif kind == "multi":
                params = ", ".join(
                    f"{ctype} value{index}"
                    for index, ctype in enumerate(payload["ctypes"]))
                proto(symbol,
                      f"struct yetty_ycore_void_result {symbol}"
                      f"(struct yetty_yclass_object *obj, {params});")
            elif kind == "adder":
                proto(symbol,
                      f"struct yetty_ycore_void_result {symbol}"
                      "(struct yetty_yclass_object *obj, "
                      "struct yetty_yclass_object *child);")
    current_entry_gated = False
    list_methods = registry[(list_plan["domain"],
                             list_plan["cls"]["name"])]["methods"]
    for op in list_plan["cls"].get("ops", []):
        method = list_methods.get(op["slot"])
        if not method:
            continue
        if classify(method["return_type"])[1] != "yetty_ycore_void_result":
            continue
        symbol = f"yetty_{method['domain']}_{op['slot']}"
        arg_defs = method["args"][1:]
        if len(arg_defs) == 0:
            proto(symbol, f"struct yetty_ycore_void_result {symbol}"
                  "(struct yetty_yclass_object *obj);")
        elif len(arg_defs) == 1 and classify(arg_defs[0]["type"])[0] == "ptr":
            proto(symbol, f"struct yetty_ycore_void_result {symbol}"
                  "(struct yetty_yclass_object *obj, "
                  "struct yetty_yclass_object *child);")
    proto("yetty_yclass_object_free",
          "struct yetty_ycore_void_result yetty_yclass_object_free"
          "(struct yetty_yclass_object *obj);")

    preamble = [
        "// #cgo LDFLAGS: -lyetty_ffi",
        "// #include <stdlib.h>",
        "// #include <stdint.h>",
        "// #include <stddef.h>",
        "// struct yetty_yclass_object;",
        "// struct yetty_ycore_error { const char *msg; const char *file;"
        " const char *func; int line; struct yetty_ycore_error *cause; };",
        "// struct yetty_ycore_buffer { uint8_t *data; size_t capacity;"
        " size_t size; };",
        "// struct yetty_ycore_void_result { int ok; union { int value;"
        " struct yetty_ycore_error error; }; };",
        "// struct yetty_yclass_object_ptr_result { int ok; union {"
        " struct yetty_yclass_object *value;"
        " struct yetty_ycore_error error; }; };",
    ]
    for symbol in sorted(protos):
        # Feature-gated symbols are weak: absent from a feature-off
        # libyetty_ffi.so, the references resolve to NULL instead of
        # failing the link — materialize() guards on presence.
        weak = "__attribute__((weak)) " if symbol in weak_symbols else ""
        preamble.append("// " + weak + protos[symbol])
    for entry in plans:
        if entry["feature"]:
            create_symbol = f"yetty_{entry['domain']}_{entry['cls']['name']}_create"
            preamble.append(
                f"// static int yetty_bind_has_{entry['cls']['name']}(void)"
                f" {{ return {create_symbol} != 0; }}")
    preamble += [
        "// static const char *yetty_bind_check(struct"
        " yetty_ycore_void_result result) { return result.ok ? (const char"
        " *)0 : (result.error.msg ? result.error.msg : \"yetty error\"); }",
        "// static struct yetty_yclass_object *yetty_bind_object_value(struct"
        " yetty_yclass_object_ptr_result result) { return result.ok ?"
        " result.value : (struct yetty_yclass_object *)0; }",
        "// static const char *yetty_bind_object_check(struct"
        " yetty_yclass_object_ptr_result result) { return result.ok ? (const"
        " char *)0 : (result.error.msg ? result.error.msg :"
        " \"yetty create failed\"); }",
    ]

    lines: list[str] = [
        "// Package ydraw — the ydraw client interface for Go: one drawable",
        "// list, immediate appends in call order; Add() manages nothing and",
        "// returns nothing; font ids are user-chosen record fields.",
        "//",
        "// Feature gating: feature-gated classes (Video, when the build",
        "// sets YETTY_ENABLE_FEATURE_YVIDEO=OFF) declare their native",
        "// symbols weak, so this package links and runs against a",
        "// feature-off libyetty_ffi.so. Discovery is Has<Class>()",
        "// (e.g. HasVideo); constructing a gated class without its",
        "// feature returns/panics with a feature-named error.",
        "//",
        "// GENERATED from model.yaml by tools/ffi-codegen/go/ffigen.py"
        " — do not edit.",
        "package ydraw",
        "",
    ] + preamble + [
        'import "C"',
        "",
        "import (",
        '\t"errors"',
        '\t"unsafe"',
        ")",
        "",
        "// Drawable is anything Add() can pack into a DrawableList.",
        "type Drawable interface {",
        "\tmaterialize() (*C.struct_yetty_yclass_object, error)",
        "\t// release frees a materialized object through the class's own",
        "\t// destructor (a class with owned resources, like Plot's DSL",
        "\t// source buffer, declares a destroy slot; the plain object free",
        "\t// would leak those).",
        "\trelease(object *C.struct_yetty_yclass_object)",
        "}",
        "",
        "func applyVoid(result C.struct_yetty_ycore_void_result) error {",
        "\tmessage := C.yetty_bind_check(result)",
        "\tif message != nil {",
        "\t\treturn errors.New(C.GoString(message))",
        "\t}",
        "\treturn nil",
        "}",
        "",
        "func createObject(result C.struct_yetty_yclass_object_ptr_result)"
        " (*C.struct_yetty_yclass_object, error) {",
        "\tmessage := C.yetty_bind_object_check(result)",
        "\tif message != nil {",
        "\t\treturn nil, errors.New(C.GoString(message))",
        "\t}",
        "\treturn C.yetty_bind_object_value(result), nil",
        "}",
        "",
        "func newBuffer(values []float64) C.struct_yetty_ycore_buffer {",
        "\tvar buffer C.struct_yetty_ycore_buffer",
        "\tif len(values) == 0 {",
        "\t\treturn buffer",
        "\t}",
        "\tbyteCount := C.size_t(len(values) * 4)",
        "\tdata := C.malloc(byteCount)",
        "\tfloats := unsafe.Slice((*float32)(data), len(values))",
        "\tfor index, value := range values {",
        "\t\tfloats[index] = float32(value)",
        "\t}",
        "\tbuffer.data = (*C.uint8_t)(data)",
        "\tbuffer.size = byteCount",
        "\tbuffer.capacity = byteCount",
        "\treturn buffer",
        "}",
        "",
        "func freeBuffer(buffer C.struct_yetty_ycore_buffer) {",
        "\tif buffer.data != nil {",
        "\t\tC.free(unsafe.Pointer(buffer.data))",
        "\t}",
        "}",
        "",
    ]

    for entry in plans:
        emit_value_class(lines, entry)
    emit_list_class(lines, list_plan, list_methods)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "ydraw.go").write_text("\n".join(lines) + "\n")
    class_count = len(plans) + 1
    print(f"ffigen: go → package ydraw ({class_count} classes) under "
          f"{OUT_DIR.relative_to(REPO)}")


def emit_value_class(lines: list[str], entry: dict):
    domain, cls = entry["domain"], entry["cls"]
    go_name = entry["go_name"]
    fields = entry["fields"]
    free_symbol = entry["destroy"] or "yetty_yclass_object_free"

    lines.append(f"// {go_name} — yclass {domain}:{cls['name']}"
                 " as a value struct.")
    lines.append(f"type {go_name} struct {{")
    for field_name, field_type, _, _ in fields:
        lines.append(f"\t{field_name} {field_type}")
    lines.append("}")
    lines.append("")
    lines.append(f"func (value {go_name}) materialize()"
                 " (*C.struct_yetty_yclass_object, error) {")
    if entry.get("feature"):
        lines.append(f"\tif C.yetty_bind_has_{cls['name']}() == 0 {{")
        lines.append(f"\t\treturn nil, errors.New(\"{go_name} requires a"
                     f" build with the {entry['feature']} feature enabled"
                     " (its native symbols are missing from"
                     " libyetty_ffi.so)\")")
        lines.append("\t}")
    lines.append(f"\tobject, err := createObject("
                 f"C.yetty_{domain}_{cls['name']}_create(nil))")
    lines.append("\tif err != nil {")
    lines.append("\t\treturn nil, err")
    lines.append("\t}")
    fail = [f"\t\t\t_ = applyVoid(C.{free_symbol}(object))",
            "\t\t\treturn nil, err"]
    for field_name, _, kind, payload in fields:
        symbol = payload["sym"]
        if kind in ("prop", "scalar"):
            cast = payload["cast"]
            lines.append(f"\tif value.{field_name} != 0 {{")
            lines.append(f"\t\tif err := applyVoid(C.{symbol}(object,"
                         f" {cast}(value.{field_name}))); err != nil {{")
            lines.extend(fail)
            lines.append("\t\t}")
            lines.append("\t}")
        elif kind in ("prop_flag", "scalar_flag"):
            cast = payload["cast"]
            lines.append(f"\tif value.{field_name} {{")
            lines.append(f"\t\tif err := applyVoid(C.{symbol}(object,"
                         f" {cast}(1))); err != nil {{")
            lines.extend(fail)
            lines.append("\t\t}")
            lines.append("\t}")
        elif kind == "cstr":
            lines.append(f"\tif value.{field_name} != \"\" {{")
            lines.append(f"\t\ttext := C.CString(value.{field_name})")
            lines.append(f"\t\terr := applyVoid(C.{symbol}(object, text))")
            lines.append("\t\tC.free(unsafe.Pointer(text))")
            lines.append("\t\tif err != nil {")
            lines.extend(fail)
            lines.append("\t\t}")
            lines.append("\t}")
        elif kind == "buffer":
            lines.append(f"\tif len(value.{field_name}) > 0 {{")
            lines.append(f"\t\tbuffer := newBuffer(value.{field_name})")
            lines.append(f"\t\terr := applyVoid(C.{symbol}(object, buffer))")
            lines.append("\t\tfreeBuffer(buffer)")
            lines.append("\t\tif err != nil {")
            lines.extend(fail)
            lines.append("\t\t}")
            lines.append("\t}")
        elif kind == "multi":
            arity = len(payload["casts"])
            arguments = ", ".join(
                f"{cast}(value.{field_name}[{index}])"
                for index, cast in enumerate(payload["casts"]))
            lines.append(f"\tif len(value.{field_name}) > 0 {{")
            lines.append(f"\t\tif len(value.{field_name}) != {arity} {{")
            lines.append(f"\t\t\terr := errors.New(\"{go_name}:"
                         f" {field_name} expects exactly {arity} values\")")
            lines.extend(fail)
            lines.append("\t\t}")
            lines.append(f"\t\tif err := applyVoid(C.{symbol}(object,"
                         f" {arguments})); err != nil {{")
            lines.extend(fail)
            lines.append("\t\t}")
            lines.append("\t}")
        elif kind == "adder":
            lines.append(f"\tfor _, element := range value.{field_name} {{")
            lines.append("\t\tchild, err := element.materialize()")
            lines.append("\t\tif err != nil {")
            lines.extend(fail)
            lines.append("\t\t}")
            lines.append(f"\t\terr = applyVoid(C.{symbol}(object, child))")
            lines.append("\t\telement.release(child)")
            lines.append("\t\tif err != nil {")
            lines.extend(fail)
            lines.append("\t\t}")
            lines.append("\t}")
    lines.append("\treturn object, nil")
    lines.append("}")
    lines.append("")
    lines.append(f"func (value {go_name}) release(object"
                 " *C.struct_yetty_yclass_object) {")
    lines.append(f"\t_ = applyVoid(C.{free_symbol}(object))")
    lines.append("}")
    lines.append("")
    if entry.get("feature"):
        lines.append(f"// Has{go_name} reports whether the loaded"
                     f" libyetty_ffi.so carries the {entry['feature']}"
                     " feature (feature-discovery contract).")
        lines.append(f"func Has{go_name}() bool {{")
        lines.append(f"\treturn C.yetty_bind_has_{cls['name']}() != 0")
        lines.append("}")
        lines.append("")


def emit_list_class(lines: list[str], entry: dict, methods: dict):
    domain, cls = entry["domain"], entry["cls"]
    free_symbol = entry["destroy"] or "yetty_yclass_object_free"

    lines.append("// DrawableList — the drawable list: one list, immediate")
    lines.append("// appends in call order.")
    lines.append("type DrawableList struct {")
    lines.append("\thandle *C.struct_yetty_yclass_object")
    lines.append("}")
    lines.append("")
    lines.append("func NewDrawableList() *DrawableList {")
    lines.append(f"\tobject, err := createObject("
                 f"C.yetty_{domain}_{cls['name']}_create(nil))")
    lines.append("\tif err != nil {")
    lines.append("\t\tpanic(err)")
    lines.append("\t}")
    lines.append("\treturn &DrawableList{handle: object}")
    lines.append("}")
    lines.append("")
    for op in cls.get("ops", []):
        slot = op["slot"]
        method = methods.get(slot)
        if not method:
            continue
        if classify(method["return_type"])[1] != "yetty_ycore_void_result":
            continue
        symbol = f"yetty_{method['domain']}_{slot}"
        if slot == "add":
            lines.append("// Add packs the drawable's record into the list,")
            lines.append("// immediately. It manages nothing and returns"
                         " nothing.")
            lines.append("func (list *DrawableList) Add(drawable Drawable) {")
            lines.append("\tif list.handle == nil {")
            lines.append("\t\tpanic(\"DrawableList: already destroyed\")")
            lines.append("\t}")
            lines.append("\tobject, err := drawable.materialize()")
            lines.append("\tif err != nil {")
            lines.append("\t\tpanic(err)")
            lines.append("\t}")
            lines.append(f"\terr = applyVoid(C.{symbol}(list.handle, object))")
            lines.append("\tdrawable.release(object)")
            lines.append("\tif err != nil {")
            lines.append("\t\tpanic(err)")
            lines.append("\t}")
            lines.append("}")
        elif slot == "destroy":
            lines.append("func (list *DrawableList) Destroy() {")
            lines.append("\tif list.handle != nil {")
            lines.append(f"\t\t_ = applyVoid(C.{free_symbol}(list.handle))")
            lines.append("\t\tlist.handle = nil")
            lines.append("\t}")
            lines.append("}")
        elif len(method["args"]) == 1:
            lines.append(f"func (list *DrawableList) {camel(slot)}() {{")
            lines.append("\tif list.handle == nil {")
            lines.append("\t\tpanic(\"DrawableList: already destroyed\")")
            lines.append("\t}")
            lines.append(f"\tif err := applyVoid(C.{symbol}(list.handle));"
                         " err != nil {")
            lines.append("\t\tpanic(err)")
            lines.append("\t}")
            lines.append("}")
        else:
            continue
        lines.append("")


def main():
    emit(discover_models())


if __name__ == "__main__":
    main()
