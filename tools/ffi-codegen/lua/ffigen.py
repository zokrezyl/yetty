#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///
"""FFI binding generator — model.yaml → per-language bindings.

Reads each yclass module's generated `model.yaml` (the canonical, fully
self-describing API + type contract: methods, classes, data_fields, exposed,
and `types` — struct layouts, inlined Result unions, enum values) and emits
idiomatic language bindings. No libclang: model.yaml is the only input.

Layout (canonical, language-major; never under src/yetty):

    bindings/<lang>/yetty/runtime.<ext>          HAND-WRITTEN (not touched)
    bindings/<lang>/yetty/generated/_types.<ext> GENERATED — deduped types
    bindings/<lang>/yetty/generated/<module>.<ext> GENERATED — funcs + classes

The emitter only ever writes inside `generated/`; the hand-written runtime
(library load, Result decode, foundational handles) lives outside it.

Usage:
    tools/ffi-codegen/lua/ffigen.py [module ...]   # default: all modules
"""

from __future__ import annotations

import pathlib
import re
import sys

import yaml

REPO = pathlib.Path(__file__).resolve().parents[3]
# model.yaml lives under the implementation tree (src/yetty/<module>/) and the
# public-API facade tree (src/api/<group>/); models are keyed by DOMAIN (the
# yetty_<domain>_* symbol prefix), mirroring the python generator.
SRC_ROOTS = [REPO / "src" / "yetty", REPO / "src" / "api"]

# The ydraw client-interface domains whose classes are VALUE-LIKE: created,
# packed by a copying add()/adder, never handed to an ownership-taking C
# API. Only these get the automatic GC finalizer (rt.own); everywhere else
# a wrapper may be the last reference to an object some C-side container
# has taken ownership of, where a finalizer would free it behind the
# container's back. Other modules use explicit destroy().
FINALIZED_DOMAINS = {"ydrawlist2", "ysdf2", "api_yplot", "ycomplex2"}


# ---------------------------------------------------------------------------
# Model loading
# ---------------------------------------------------------------------------

def model_domain(model: dict) -> str | None:
    for entry in model.get("classes", []) or []:
        if entry.get("domain"):
            return entry["domain"]
    for entry in model.get("methods", []) or []:
        if entry.get("domain"):
            return entry["domain"]
    return None


def discover_models(selected: list[str]) -> dict[str, dict]:
    """Load every model.yaml under both source roots, keyed by domain
    (default: all; `selected` restricts)."""
    found: dict[str, dict] = {}
    for root in SRC_ROOTS:
        if not root.is_dir():
            continue
        for path in root.glob("*/model.yaml"):
            model = yaml.safe_load(path.read_text()) or {}
            domain = model_domain(model)
            if domain:
                found[domain] = model
    names = selected or sorted(found)
    models: dict[str, dict] = {}
    for name in names:
        if name not in found:
            sys.stderr.write(f"ffigen: no model.yaml for domain '{name}'\n")
            sys.exit(1)
        models[name] = found[name]
    return models


def global_types(models: dict[str, dict]) -> list[dict]:
    """Dedup every module's `types` by name into one ordered list."""
    by_name: dict[str, dict] = {}
    for model in models.values():
        for entry in model.get("types", []) or []:
            by_name.setdefault(entry["name"], entry)
    return list(by_name.values())


# ---------------------------------------------------------------------------
# C type classification (shared across emitters)
# ---------------------------------------------------------------------------

def split_array(type_str: str) -> tuple[str, str]:
    """Split a C type into (element_type, array_suffix). 'char[64]' →
    ('char', '[64]'); 'struct X[4][2]' → ('struct X', '[4][2]'); 'int' →
    ('int', '')."""
    import re
    m = re.match(r"^(.*?)((?:\s*\[[0-9]+\])+)\s*$", (type_str or "").strip())
    if m:
        return m.group(1).strip(), m.group(2).replace(" ", "")
    return (type_str or "").strip(), ""


def classify(type_str: str) -> tuple[str, str | None]:
    """(category, tag): 'ptr' (opaque handle), 'struct'/'union'/'enum' (+tag),
    or 'scalar' (primitive/typedef). Array extents are stripped first."""
    import re
    t, _ = split_array(type_str or "")
    if "*" in t:
        return ("ptr", None)
    t = re.sub(r"^(const|volatile)\s+", "", t).strip()
    for kw in ("struct", "union", "enum"):
        m = re.match(rf"^{kw}\s+(\w+)$", t)
        if m:
            return (kw, m.group(1))
    return ("scalar", None)


def is_char_ptr(type_str: str) -> bool:
    t = (type_str or "").replace("const", "").replace("volatile", "").strip()
    return t in ("char *", "char*")




def type_deps(entry: dict, type_names: set[str]) -> set[str]:
    deps: set[str] = set()

    def scan(fields):
        for f in fields:
            if "fields" in f:
                scan(f["fields"])
            elif "type" in f:
                category, tag = classify(f["type"])
                if category in ("struct", "union") and tag in type_names and tag != entry["name"]:
                    deps.add(tag)

    if entry["kind"] in ("struct", "result"):
        scan(entry.get("fields", []))
    return deps


def topo_types(types: list[dict]) -> list[dict]:
    """Order types so every by-value dependency precedes its user."""
    names = {t["name"] for t in types}
    by_name = {t["name"]: t for t in types}
    ordered: list[dict] = []
    placed: set[str] = set()
    visiting: set[str] = set()

    def visit(name: str):
        if name in placed or name not in by_name:
            return
        visiting.add(name)
        for dep in sorted(type_deps(by_name[name], names)):
            if dep not in visiting:
                visit(dep)
        visiting.discard(name)
        if name not in placed:
            placed.add(name)
            ordered.append(by_name[name])

    for name in sorted(by_name):
        visit(name)
    return ordered

# ===========================================================================
# Lua (LuaJIT ffi) emitter — for neovim. cdef takes C declarations verbatim,
# so types/prototypes are reproduced straight from the model's C strings.
# ===========================================================================

def prop_result_id(field_type: str) -> str:
    """The YRESULT_DECLARE id for a property's value type (mirrors codegen)."""
    r = (field_type or "").strip()
    if r in ("int", "int32_t"):
        return "yetty_ycore_int"
    if r == "size_t":
        return "yetty_ycore_size"
    if r == "uint32_t":
        return "uint32"
    m = re.match(r"^struct\s+(\w+)\s*\*\s*$", r)
    if m:
        return f"{m.group(1)}_ptr"
    m = re.match(r"^struct\s+(\w+)\s*$", r)
    if m:
        overrides = {
            "yetty_ycore_rectangle": "rectangle",
            "yetty_ycore_pixel_size": "pixel_size",
            "yetty_ycore_pixel_coord": "pixel_coord",
        }
        return overrides.get(m.group(1), m.group(1))
    return r


def prop_result_name(field_type: str) -> str:
    return f"{prop_result_id(field_type)}_result"


def add_property_result_types(models: dict[str, dict], types: list[dict]) -> None:
    """Synthesize result-type entries for property getters the models don't
    already carry (same layout as YETTY_YRESULT_DECLARE)."""
    present = {t["name"] for t in types}
    for model in models.values():
        for cls in model.get("classes", []) or []:
            for field in cls.get("data_fields", []) or []:
                if not field.get("get"):
                    continue
                name = prop_result_name(field.get("type", ""))
                if name in present:
                    continue
                types.append({
                    "name": name,
                    "kind": "result",
                    "fields": [
                        {"name": "ok", "type": "int"},
                        {"kind": "union", "fields": [
                            {"name": "value", "type": field.get("type", "")},
                            {"name": "error", "type": "struct yetty_ycore_error"},
                        ]},
                    ],
                })
                present.add(name)


def class_registry(models: dict[str, dict]) -> dict[tuple[str, str], dict]:
    """(domain, class) -> {cls, methods} across every loaded model, for
    cross-module ancestry walks."""
    registry: dict[tuple[str, str], dict] = {}
    for model in models.values():
        methods = {m["slot"]: m for m in model.get("methods", [])}
        for cls in model.get("classes", []) or []:
            registry[(cls.get("domain"), cls["name"])] = {
                "cls": cls, "methods": methods}
    return registry


def ancestry(registry: dict, domain: str, name: str) -> list[dict]:
    """The class and its parents, child-first."""
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


def lua_class_name(name: str) -> str:
    return "".join(w.capitalize() for w in name.split("_"))


def lua_strip_atomic(type_str: str) -> str:
    """LuaJIT's cdef has no _Atomic support; `_Atomic(T)` / `_Atomic T` have
    T's layout for our read-only purposes — unwrap them."""
    stripped = re.sub(r"_Atomic\s*\(\s*([^)]+?)\s*\)", r"\1", type_str or "")
    return re.sub(r"_Atomic\s+", "", stripped)


def lua_c_field(field: dict, indent: str = "  ") -> str:
    if "fields" in field:  # inlined anonymous union/struct
        kind = field.get("kind", "struct")
        inner = "\n".join(lua_c_field(sf, indent + "  ") for sf in field["fields"])
        member = field.get("name") or ""
        tail = f" {member}" if member else ""
        return f"{indent}{kind} {{\n{inner}\n{indent}}}{tail};"
    base, arr = split_array(lua_strip_atomic(field["type"]))
    # Enums aren't cdef'd (their constants live in the Lua table); a field of
    # `enum X` would have unknown size, so emit it as int (enum's storage type).
    if classify(base)[0] == "enum":
        base = "int"
    n = field["name"] or "_pad"
    sep = "" if base.endswith("*") else " "
    return f"{indent}{base}{sep}{n}{arr};"


def lua_opaque_forward_decls(models: dict, types: list) -> list:
    """`struct X;` forward declarations for pointer-target tags that are never
    defined as a `types` entry (ctx, object, drawable_list, …). LuaJIT tolerates
    incomplete pointers, but declaring them keeps the cdef unambiguous."""
    import re
    type_names = {t["name"] for t in types}
    tags: set[str] = set()

    def scan_type(type_str: str):
        m = re.search(r"struct\s+(\w+)\s*\*", type_str or "")
        if m and m.group(1) not in type_names:
            tags.add(m.group(1))

    for t in types:
        def walk(fields):
            for f in fields:
                if "fields" in f:
                    walk(f["fields"])
                elif "type" in f:
                    scan_type(f["type"])
        if t["kind"] in ("struct", "result"):
            walk(t["fields"])
    for model in models.values():
        for m in model.get("methods", []):
            scan_type(m.get("return_type", ""))
            for a in m.get("args", []):
                scan_type(a.get("type", ""))
    return [f"struct {tag};" for tag in sorted(tags)]


# Scalar types LuaJIT's cdef predefines. Anything else seen in a field (e.g.
# glibc-internal __time_t / __syscall_slong_t from struct timespec) gets a
# `typedef long …;` shim — those are all `long` on LP64, so the struct layout
# stays correct.
LUA_KNOWN_SCALARS = {
    "int", "unsigned int", "unsigned", "char", "signed char", "unsigned char",
    "short", "unsigned short", "long", "unsigned long", "long long",
    "unsigned long long", "float", "double", "bool", "_Bool", "void",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t", "wchar_t",
}


def lua_unknown_scalars(types: list) -> list:
    import re
    found: set[str] = set()

    def walk(fields):
        for f in fields:
            if "fields" in f:
                walk(f["fields"])
            elif "type" in f and classify(f["type"])[0] == "scalar":
                elem, _ = split_array(f["type"])
                base = re.sub(r"^(const|volatile)\s+", "", elem).strip()
                if base and base not in LUA_KNOWN_SCALARS and base != "FILE":
                    found.add(base)

    for t in types:
        if t["kind"] in ("struct", "result"):
            walk(t["fields"])
    return sorted(found)


def lua_emit_types(models: dict, types: list, out_path: pathlib.Path):
    cdef = ["typedef struct _IO_FILE FILE;"]
    cdef += [f"typedef long {name};" for name in lua_unknown_scalars(types)]
    cdef += lua_opaque_forward_decls(models, types)
    for entry in topo_types([t for t in types if t["kind"] in ("struct", "result")]):
        cdef.append(f"struct {entry['name']} {{")
        for f in entry.get("fields", []):
            cdef.append(lua_c_field(f))
        cdef.append("};")
    parts = [
        "-- Foundational + shared ABI types — GENERATED, do not edit.",
        'local ffi = require("ffi")',
        "ffi.cdef[[",
        "\n".join(cdef),
        "]]",
        "local M = {}",
    ]
    for entry in (t for t in types if t["kind"] == "enum"):
        parts.append(f"M.{entry['name']} = {{")
        for v in entry.get("values", []):
            parts.append(f"  {v['name']} = {v['value']},")
        parts.append("}")
    parts.append("return M")
    out_path.write_text("\n".join(parts) + "\n")


def lua_cdef_param(type_str: str) -> str:
    """A function-parameter type for cdef: enum → int (enums aren't cdef'd)."""
    base, arr = split_array(lua_strip_atomic(type_str))
    if classify(base)[0] == "enum":
        base = "int"
    return f"{base}{arr}"


def lua_proto(return_type: str, name: str, args: list) -> str:
    params = ", ".join(lua_cdef_param(a["type"]) for a in args) if args else "void"
    sep = "" if return_type.endswith("*") else " "
    return f"{return_type}{sep}{name}({params});"


LUA_RESERVED = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
    "then", "true", "until", "while",
}


def lua_param_name(name: str) -> str:
    """C argument names that collide with lua keywords get an `_arg` suffix
    (e.g. add_function's `function` parameter)."""
    return f"{name}_arg" if name in LUA_RESERVED else name


def lua_arg_expr(arg: dict) -> str:
    """Argument conversion for a method wrapper: yclass object args accept
    wrapped objects or raw handles; by-value ycore buffers coerce from lua
    tables (f32 arrays) / strings; everything else passes through (LuaJIT
    converts lua strings to const char* itself)."""
    name = lua_param_name(arg["name"])
    category, tag = classify(arg.get("type", ""))
    if category == "ptr" and "yclass_object" in (arg.get("type") or ""):
        return f"rt.unwrap({name})"
    if (category, tag) == ("struct", "yetty_ycore_buffer"):
        return f"rt.as_buffer({name})"
    return name


def lua_emit_module(domain: str, model: dict, registry: dict,
                    out_path: pathlib.Path):
    methods = {m["slot"]: m for m in model.get("methods", [])}
    classes = [c for c in model.get("classes", []) if c.get("type") == "regular"]

    protos: list[str] = []
    for cls in classes:
        protos.append(lua_proto("struct yetty_yclass_object_ptr_result",
                                f"yetty_{cls['domain']}_{cls['name']}_create",
                                [{"type": "struct yetty_yclass_ctx *"}]))
        for field in cls.get("data_fields", []) or []:
            base_sym = f"yetty_{cls['domain']}_{cls['name']}_{field['name']}"
            if field.get("get"):
                protos.append(lua_proto(f"struct {prop_result_name(field['type'])}",
                                        f"{base_sym}_get",
                                        [{"type": "struct yetty_yclass_object *"}]))
            if field.get("set"):
                protos.append(lua_proto("struct yetty_ycore_void_result",
                                        f"{base_sym}_set",
                                        [{"type": "struct yetty_yclass_object *"},
                                         {"type": field["type"]}]))
    for m in model.get("methods", []):
        protos.append(lua_proto(m["return_type"], f"yetty_{m['domain']}_{m['slot']}", m["args"]))

    # Foreign ancestor domains: require their modules so the parent slots'
    # cdefs are in place before any inherited call.
    foreign: list[str] = []
    for cls in classes:
        for link in ancestry(registry, cls["domain"], cls["name"])[1:]:
            dom = link["cls"].get("domain")
            if dom and dom != domain and dom not in foreign:
                foreign.append(dom)

    parts = [
        f"-- yetty.{domain} bindings — GENERATED from model.yaml, do not edit.",
        'local ffi = require("ffi")',
        'local rt = require("yetty.runtime")',
        'require("yetty.generated._types")',
    ]
    for dom in foreign:
        parts.append(f'require("yetty.generated.{dom}")')
    parts += [
        "local unpack = unpack or table.unpack",
        "ffi.cdef[[",
        "\n".join(protos),
        "]]",
        "local M = {}",
    ]

    def returns_value(return_type: str) -> bool:
        _, tag = classify(return_type)
        return not (tag or "").endswith("void_result")

    for cls in classes:
        cname = lua_class_name(cls["name"])
        create_sym = f"yetty_{cls['domain']}_{cls['name']}_create"
        chain = ancestry(registry, cls["domain"], cls["name"])
        parts.append(f"local {cname} = {{}}")
        parts.append(f"{cname}.__prop_get = {{}}")
        parts.append(f"{cname}.__prop_set = {{}}")
        parts.append(f"local {cname}_instance_mt = {{")
        parts.append("  __index = function(obj, key)")
        parts.append(f"    local member = {cname}[key]")
        parts.append("    if member ~= nil then return member end")
        parts.append(f"    local getter = {cname}.__prop_get[key]")
        parts.append("    if getter then return getter(obj) end")
        parts.append("    return nil")
        parts.append("  end,")
        parts.append("  __newindex = function(obj, key, value)")
        parts.append(f"    local setter = {cname}.__prop_set[key]")
        parts.append("    if setter then setter(obj, value) else rawset(obj, key, value) end")
        parts.append("  end,")
        parts.append("}")
        parts.append(f"function {cname}.new()")
        parts.append("  local res = rt.C()." + create_sym + "(nil)")
        parts.append("  rt.check(res)")
        parts.append(f"  local obj = setmetatable({{ handle = res.value }}, {cname}_instance_mt)")
        if cls["domain"] in FINALIZED_DOMAINS:
            parts.append(f"  rt.own(obj, {cname})")
        parts.append("  return obj")
        parts.append("end")

        emitted_slots: set[str] = set()
        setter_meta: dict[str, dict] = {}
        adder_meta: dict[str, str] = {}
        has_destroy = False
        for link in chain:
            link_dom = link["cls"].get("domain")
            for op in link["cls"].get("ops", []):
                slot = op["slot"]
                if slot in emitted_slots:
                    continue
                m = link["methods"].get(slot)
                if not m:
                    continue
                emitted_slots.add(slot)
                if slot == "destroy":
                    has_destroy = True
                arg_defs = m["args"][1:]
                params = ", ".join(lua_param_name(a["name"]) for a in arg_defs)
                sym = f"yetty_{m['domain']}_{slot}"
                call_args = ", ".join(["self.handle"] +
                                      [lua_arg_expr(a) for a in arg_defs])
                parts.append(f"function {cname}:{slot}({params})")
                if slot == "destroy":
                    # The destroy slot consumes the handle: invalidate the
                    # wrapper before surfacing any error; repeat calls no-op.
                    parts.append("  if self.handle == nil then return end")
                    parts.append("  rt.disown(self)")
                    parts.append(f"  local res = rt.C().{sym}(self.handle)")
                    parts.append('  rawset(self, "handle", nil)')
                    parts.append("  rt.check(res)")
                else:
                    parts.append(f'  rt.live(self, "{cname}:{slot}")')
                    parts.append(f"  local res = rt.C().{sym}({call_args})")
                    parts.append("  rt.check(res)")
                    if returns_value(m["return_type"]):
                        parts.append("  return res.value")
                parts.append("end")
                if slot.startswith("set_"):
                    setter_meta[slot[4:]] = {"fn": slot, "n": len(arg_defs)}
                if slot.startswith("add_"):
                    adder_meta[slot[4:] + "s"] = slot

        # Properties (own + inherited), exposed as plain fields.
        prop_names: list[str] = []
        for link in chain:
            link_cls = link["cls"]
            for field in link_cls.get("data_fields", []) or []:
                fname = field["name"]
                if fname in prop_names:
                    continue
                prop_names.append(fname)
                base_sym = (f"yetty_{link_cls['domain']}_{link_cls['name']}_"
                            f"{fname}")
                if field.get("get"):
                    parts.append(f"{cname}.__prop_get.{fname} = function(obj)")
                    parts.append(f'  rt.live(obj, "{cname}.{fname}")')
                    parts.append(f"  local res = rt.C().{base_sym}_get(obj.handle)")
                    parts.append("  rt.check(res)")
                    parts.append("  return res.value")
                    parts.append("end")
                if field.get("set"):
                    parts.append(f"{cname}.__prop_set.{fname} = function(obj, value)")
                    parts.append(f'  rt.live(obj, "{cname}.{fname}")')
                    parts.append(f"  local res = rt.C().{base_sym}_set(obj.handle, value)")
                    parts.append("  rt.check(res)")
                    parts.append("end")

        if has_destroy:
            # The GC finalizer (rt.own) frees through the class destroy slot.
            destroy_sym = None
            for link in chain:
                m = link["methods"].get("destroy")
                if m:
                    destroy_sym = f"yetty_{m['domain']}_destroy"
                    break
            parts.append(f'{cname}.__destroy_sym = "{destroy_sym}"')
        else:
            parts.append(f"function {cname}:destroy()")
            parts.append("  rt.object_free(self)")
            parts.append("end")

        # Constructor spec (table-call): primary from the model, setters with
        # arity, settable properties, add_* pluralized adders.
        primary = cls.get("primary_slot")
        if not primary:
            for probe in ("set_expression", "set_body"):
                if probe in emitted_slots:
                    primary = probe
                    break
        settable = [f["name"] for link in chain
                    for f in link["cls"].get("data_fields", []) or []
                    if f.get("set")]
        parts.append(f"{cname}.__spec = {{")
        if primary:
            parts.append(f'  primary = "{primary}",')
        parts.append("  setters = {")
        for key in sorted(setter_meta):
            meta = setter_meta[key]
            parts.append(f'    {key} = {{ fn = "{meta["fn"]}", n = {meta["n"]} }},')
        parts.append("  },")
        parts.append("  props = {")
        for fname in sorted(set(settable)):
            parts.append(f"    {fname} = true,")
        parts.append("  },")
        parts.append("  adders = {")
        for key in sorted(adder_meta):
            parts.append(f'    {key} = "{adder_meta[key]}",')
        parts.append("  },")
        parts.append("}")
        parts.append(f"setmetatable({cname}, {{ __call = function(cls, spec)")
        parts.append("  local obj = cls.new()")
        parts.append("  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end")
        parts.append("  return obj")
        parts.append("end })")
        parts.append(f"M.{cname} = {cname}")
    parts.append("return M")
    out_path.write_text("\n".join(parts) + "\n")


# ---------------------------------------------------------------------------
# Connection facade (#439). The reactor-seam contract every binding rides —
# fd()/pump()/channels over yetty_ywire_connection — is a fixed plain-C surface
# below the yclass model, identical for every consumer, so it is emitted as a
# template rather than derived per-module. It lands in generated/ next to the
# model-driven files: same lifecycle (re-emitted on every run, never edited).
# ---------------------------------------------------------------------------

LUA_CONNECTION_TEMPLATE = r'''-- GENERATED reactor-seam facade — do not edit.
--
-- The multiplexed wire connection (yetty_ywire_connection over
-- yetty_yclass_transport_pty): sole owner of the terminal byte stream,
-- demuxing rpc / input / raw / dynamic channels. All the dangerous byte
-- handling lives in C; Lua only watches fd() and calls pump() on readiness.
--
-- Drivers:
--   sync:  Connection:run{on_tick=..., should_stop=...} — a poll(2) loop.
--   async: register Connection:fd() with the host loop (nvim: vim.uv
--          new_poll) and call Connection:pump_readable()/pump_writable()
--          from the readiness callback — same seam, host loop owns the fd.

local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")

ffi.cdef[[
struct yetty_yclass_transport;
struct yetty_yclass_transport_pty;
struct yetty_ywire_connection;
struct yetty_ywire_channel;

struct yetty_yclass_transport_reactor {
  void *userdata;
  const void *ops;
};

struct yetty_yclass_transport_pty_ptr_result {
  int ok;
  union { struct yetty_yclass_transport_pty *value; struct yetty_ycore_error error; };
};
struct yetty_yclass_transport_generic_ptr_result {
  int ok;
  union { struct yetty_yclass_transport *value; struct yetty_ycore_error error; };
};
struct yetty_ywire_connection_ptr_result {
  int ok;
  union { struct yetty_ywire_connection *value; struct yetty_ycore_error error; };
};
struct yetty_ywire_channel_ptr_result {
  int ok;
  union { struct yetty_ywire_channel *value; struct yetty_ycore_error error; };
};

struct yetty_yclass_transport_pty_ptr_result yetty_yclass_transport_pty_create(int fd_in, int fd_out);
struct yetty_yclass_transport_pty_ptr_result yetty_yclass_transport_pty_create_from_env(int fallback_fd_in, int fallback_fd_out);
struct yetty_ycore_void_result yetty_yclass_transport_pty_enable_raw_mode(struct yetty_yclass_transport_pty *transport);
struct yetty_yclass_transport_reactor yetty_yclass_transport_pty_reactor(struct yetty_yclass_transport_pty *transport);
struct yetty_ycore_void_result yetty_yclass_transport_pty_flush_blocking(struct yetty_yclass_transport_pty *transport);
struct yetty_ycore_void_result yetty_yclass_transport_pty_destroy(struct yetty_yclass_transport_pty *transport);

typedef void (*yetty_ywire_channel_envelope_sink)(void *user, int wire_code, const uint8_t *args, size_t args_len, const uint8_t *payload, size_t payload_len);
typedef void (*yetty_ywire_channel_raw_sink)(void *user, const uint8_t *bytes, size_t n);
typedef void (*yetty_ywire_channel_event_cb)(void *user, struct yetty_ywire_channel *channel, int event);
typedef void (*yetty_ywire_resize_cb)(void *user, int width_px, int height_px, int cols, int rows);
typedef int (*yetty_ywire_accept_cb)(void *user, struct yetty_ywire_channel *channel);

struct yetty_ywire_connection_ptr_result yetty_ywire_connection_create(struct yetty_yclass_transport_reactor reactor, int compressed);
struct yetty_ywire_channel *yetty_ywire_connection_channel(struct yetty_ywire_connection *connection, uint32_t channel_id);
struct yetty_ywire_channel_ptr_result yetty_ywire_connection_open_channel(struct yetty_ywire_connection *connection, uint32_t initial_recv_window);
struct yetty_ycore_void_result yetty_ywire_connection_set_role(struct yetty_ywire_connection *connection, int acceptor);
struct yetty_ycore_void_result yetty_ywire_connection_set_accept_cb(struct yetty_ywire_connection *connection, yetty_ywire_accept_cb cb, void *user);
int yetty_ywire_connection_fd(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_out_fd(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_want_write(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_is_eof(struct yetty_ywire_connection *connection);
struct yetty_ycore_size_result yetty_ywire_connection_pump_readable(struct yetty_ywire_connection *connection);
struct yetty_ycore_size_result yetty_ywire_connection_pump_writable(struct yetty_ywire_connection *connection);
struct yetty_ycore_void_result yetty_ywire_connection_set_resize_cb(struct yetty_ywire_connection *connection, yetty_ywire_resize_cb cb, void *user);
struct yetty_ycore_void_result yetty_ywire_connection_pickup_winsize(struct yetty_ywire_connection *connection);
struct yetty_ycore_void_result yetty_ywire_connection_destroy(struct yetty_ywire_connection *connection);

uint32_t yetty_ywire_channel_id(const struct yetty_ywire_channel *channel);
struct yetty_ycore_size_result yetty_ywire_channel_write(struct yetty_ywire_channel *channel, const void *bytes, size_t len);
struct yetty_ycore_void_result yetty_ywire_channel_flush(struct yetty_ywire_channel *channel);
struct yetty_ycore_size_result yetty_ywire_channel_read(struct yetty_ywire_channel *channel, void *buf, size_t max);
struct yetty_ycore_size_result yetty_ywire_channel_recv_blocking(struct yetty_ywire_channel *channel, void *buf, size_t max);
struct yetty_ycore_void_result yetty_ywire_channel_set_envelope_sink(struct yetty_ywire_channel *channel, yetty_ywire_channel_envelope_sink sink, void *user);
struct yetty_ycore_void_result yetty_ywire_channel_set_raw_sink(struct yetty_ywire_channel *channel, yetty_ywire_channel_raw_sink sink, void *user);
struct yetty_ycore_void_result yetty_ywire_channel_set_event_cb(struct yetty_ywire_channel *channel, yetty_ywire_channel_event_cb cb, void *user);
struct yetty_ycore_void_result yetty_ywire_channel_send_eof(struct yetty_ywire_channel *channel);
struct yetty_ycore_void_result yetty_ywire_channel_close(struct yetty_ywire_channel *channel);
int64_t yetty_ywire_channel_send_window(const struct yetty_ywire_channel *channel);
int yetty_ywire_channel_remote_eof(const struct yetty_ywire_channel *channel);
size_t yetty_ywire_channel_pending_out(const struct yetty_ywire_channel *channel);
struct yetty_yclass_transport_generic_ptr_result yetty_ywire_channel_transport(struct yetty_ywire_channel *channel);

struct pollfd { int fd; short events; short revents; };
int poll(struct pollfd *fds, unsigned long nfds, int timeout);
]]

local POLLIN = 1

local M = {}

M.CHANNEL_RPC = 1
M.CHANNEL_INPUT = 2
M.CHANNEL_RAW = 3
M.CHANNEL_DYNAMIC_BASE = 16
M.WINDOW_DEFAULT = 256 * 1024
M.CHUNK_MAX = 16 * 1024
M.EVENT_REMOTE_EOF = 1
M.EVENT_CLOSED = 2

local Channel = {}
Channel.__index = Channel

local function wrap_channel(connection, pointer)
  if pointer == nil then
    return nil
  end
  return setmetatable({ conn = connection, ptr = pointer, anchors = {} }, Channel)
end

function Channel:id()
  return tonumber(rt.C().yetty_ywire_channel_id(self.ptr))
end

function Channel:write(data)
  local res = rt.C().yetty_ywire_channel_write(self.ptr, data, #data)
  rt.check(res)
  return tonumber(res.value)
end

function Channel:flush()
  rt.check(rt.C().yetty_ywire_channel_flush(self.ptr))
end

function Channel:read(max_bytes)
  max_bytes = max_bytes or 65536
  local buf = ffi.new("uint8_t[?]", max_bytes)
  local res = rt.C().yetty_ywire_channel_read(self.ptr, buf, max_bytes)
  rt.check(res)
  return ffi.string(buf, tonumber(res.value))
end

function Channel:recv_blocking(max_bytes)
  max_bytes = max_bytes or 65536
  local buf = ffi.new("uint8_t[?]", max_bytes)
  local res = rt.C().yetty_ywire_channel_recv_blocking(self.ptr, buf, max_bytes)
  rt.check(res)
  return ffi.string(buf, tonumber(res.value))
end

function Channel:transport()
  local res = rt.C().yetty_ywire_channel_transport(self.ptr)
  rt.check(res)
  return res.value
end

function Channel:set_envelope_sink(sink)
  local trampoline = ffi.cast("yetty_ywire_channel_envelope_sink",
    function(_user, wire_code, args, args_len, payload, payload_len)
      local args_str = (args ~= nil and args_len > 0) and ffi.string(args, args_len) or ""
      local payload_str = (payload ~= nil and payload_len > 0)
        and ffi.string(payload, payload_len) or ""
      sink(wire_code, args_str, payload_str)
    end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_channel_set_envelope_sink(self.ptr, trampoline, nil))
end

function Channel:set_raw_sink(sink)
  local trampoline = ffi.cast("yetty_ywire_channel_raw_sink", function(_user, bytes, n)
    sink((bytes ~= nil and n > 0) and ffi.string(bytes, n) or "")
  end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_channel_set_raw_sink(self.ptr, trampoline, nil))
end

function Channel:set_event_cb(callback)
  local this = self
  local trampoline = ffi.cast("yetty_ywire_channel_event_cb",
    function(_user, _channel, event)
      callback(this, tonumber(event))
    end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_channel_set_event_cb(self.ptr, trampoline, nil))
end

function Channel:send_eof()
  rt.check(rt.C().yetty_ywire_channel_send_eof(self.ptr))
end

function Channel:close()
  rt.check(rt.C().yetty_ywire_channel_close(self.ptr))
end

function Channel:send_window()
  return tonumber(rt.C().yetty_ywire_channel_send_window(self.ptr))
end

function Channel:remote_eof()
  return rt.C().yetty_ywire_channel_remote_eof(self.ptr) ~= 0
end

function Channel:pending_out()
  return tonumber(rt.C().yetty_ywire_channel_pending_out(self.ptr))
end

local Connection = {}
Connection.__index = Connection

-- opts: { in_fd=0, out_fd=1, compressed=true, raw_mode=true, side_channel_env=true }
function Connection.new(opts)
  opts = opts or {}
  local in_fd = opts.in_fd or 0
  local out_fd = opts.out_fd or 1
  local creator = (opts.side_channel_env ~= false)
    and rt.C().yetty_yclass_transport_pty_create_from_env
    or rt.C().yetty_yclass_transport_pty_create
  local transport_res = creator(in_fd, out_fd)
  rt.check(transport_res)
  local self = setmetatable({
    transport = transport_res.value,
    ptr = nil,
    channels = {},
    anchors = {},
    closed = false,
  }, Connection)
  if opts.raw_mode ~= false then
    rt.check(rt.C().yetty_yclass_transport_pty_enable_raw_mode(self.transport))
  end
  local reactor = rt.C().yetty_yclass_transport_pty_reactor(self.transport)
  local conn_res = rt.C().yetty_ywire_connection_create(reactor,
    (opts.compressed ~= false) and 1 or 0)
  rt.check(conn_res)
  self.ptr = conn_res.value
  return self
end

function Connection:channel(channel_id)
  local pointer = rt.C().yetty_ywire_connection_channel(self.ptr, channel_id)
  if pointer == nil then
    self.channels[channel_id] = nil
    return nil
  end
  local cached = self.channels[channel_id]
  if cached == nil or cached.ptr ~= pointer then
    cached = wrap_channel(self, pointer)
    self.channels[channel_id] = cached
  end
  return cached
end

function Connection:rpc() return self:channel(M.CHANNEL_RPC) end
function Connection:input() return self:channel(M.CHANNEL_INPUT) end
function Connection:raw() return self:channel(M.CHANNEL_RAW) end

function Connection:open_channel(initial_recv_window)
  local res = rt.C().yetty_ywire_connection_open_channel(self.ptr, initial_recv_window or 0)
  rt.check(res)
  local channel = wrap_channel(self, res.value)
  self.channels[channel:id()] = channel
  return channel
end

function Connection:set_role(acceptor)
  rt.check(rt.C().yetty_ywire_connection_set_role(self.ptr, acceptor and 1 or 0))
end

function Connection:set_accept_cb(callback)
  local this = self
  local trampoline = ffi.cast("yetty_ywire_accept_cb", function(_user, channel_ptr)
    local channel = wrap_channel(this, channel_ptr)
    if callback(channel) then
      this.channels[channel:id()] = channel
      return 1
    end
    return 0
  end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_connection_set_accept_cb(self.ptr, trampoline, nil))
end

function Connection:fd()
  return rt.C().yetty_ywire_connection_fd(self.ptr)
end

function Connection:out_fd()
  return rt.C().yetty_ywire_connection_out_fd(self.ptr)
end

function Connection:want_write()
  return rt.C().yetty_ywire_connection_want_write(self.ptr) ~= 0
end

function Connection:is_eof()
  return rt.C().yetty_ywire_connection_is_eof(self.ptr) ~= 0
end

function Connection:pump_readable()
  local res = rt.C().yetty_ywire_connection_pump_readable(self.ptr)
  rt.check(res)
  return tonumber(res.value)
end

function Connection:pump_writable()
  local res = rt.C().yetty_ywire_connection_pump_writable(self.ptr)
  rt.check(res)
  return tonumber(res.value)
end

function Connection:set_resize_cb(callback)
  local trampoline = ffi.cast("yetty_ywire_resize_cb",
    function(_user, width_px, height_px, cols, rows)
      callback(tonumber(width_px), tonumber(height_px), tonumber(cols), tonumber(rows))
    end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_connection_set_resize_cb(self.ptr, trampoline, nil))
end

function Connection:pickup_winsize()
  rt.check(rt.C().yetty_ywire_connection_pickup_winsize(self.ptr))
end

-- One sync step: poll(2) the fd up to timeout_ms, pump both directions.
-- Returns false once the peer hung up.
function Connection:step(timeout_ms)
  local fds = ffi.new("struct pollfd[1]")
  fds[0].fd = self:fd()
  fds[0].events = POLLIN
  local ready = ffi.C.poll(fds, 1, timeout_ms or 33)
  if ready > 0 then
    self:pump_readable()
  end
  self:pump_writable()
  return not self:is_eof()
end

-- Sync facade for loop-less callers. opts: { on_tick=fn, should_stop=fn,
-- tick_ms=33 }. For async hosts (nvim), skip run() and drive fd()/pump_*()
-- from the host loop's readiness callback instead.
function Connection:run(opts)
  opts = opts or {}
  local tick_ms = opts.tick_ms or 33
  while true do
    local alive = self:step(tick_ms)
    if opts.on_tick then
      opts.on_tick()
    end
    if not alive or (opts.should_stop and opts.should_stop()) then
      return 0
    end
  end
end

-- Destroy connection then transport (restores raw mode). Idempotent.
function Connection:close()
  if self.closed then
    return
  end
  self.closed = true
  if self.ptr ~= nil then
    rt.C().yetty_ywire_connection_destroy(self.ptr)
    self.ptr = nil
  end
  if self.transport ~= nil then
    rt.C().yetty_yclass_transport_pty_flush_blocking(self.transport)
    rt.C().yetty_yclass_transport_pty_destroy(self.transport)
    self.transport = nil
  end
  self.channels = {}
end

M.Channel = Channel
M.Connection = Connection
return M
'''


def lua_emit_connection(out_path: pathlib.Path):
    out_path.write_text(LUA_CONNECTION_TEMPLATE)


def emit_lua(models: dict[str, dict]):
    gen = REPO / "bindings" / "lua" / "yetty" / "generated"
    gen.mkdir(parents=True, exist_ok=True)
    types = global_types(models)
    add_property_result_types(models, types)
    registry = class_registry(models)
    lua_emit_types(models, types, gen / "_types.lua")
    lua_emit_connection(gen / "connection.lua")
    count = 0
    for domain, model in models.items():
        if not model.get("classes"):
            continue
        lua_emit_module(domain, model, registry, gen / f"{domain}.lua")
        count += 1
    print(f"ffigen: lua → {count} modules + connection facade, {len(types)} types "
          f"under bindings/lua/yetty/generated/")



def main():
    models = discover_models(sys.argv[1:])
    emit_lua(models)


if __name__ == "__main__":
    main()
