#!/usr/bin/env python3
"""yshadertoy — import a Shadertoy shader into yetty's WebGPU shader pipeline.

What it does
------------
Given a Shadertoy shader id/URL (or a local GLSL file), this tool:

  1. downloads the shader definition via `curl` from the Shadertoy REST API
     (or reads a local `mainImage` GLSL file with `--from-file`);
  2. resolves every render pass (Common, Image, Buffer A-D) and each pass's
     input channels (textures, cubemaps, buffers, keyboard, ...);
  3. downloads the referenced media assets via `curl`, with per-channel
     overrides so you can substitute your own texture/cubemap files;
  4. reconstructs the wrapper Shadertoy injects implicitly (the uniform block,
     the `iChannel*` samplers, the bare-name aliases and the driving `main()`)
     so the raw pass code compiles;
  5. converts each pass GLSL -> SPIR-V with `glslangValidator`, then
     SPIR-V -> WGSL with Dawn's `tint` CLI when it is available;
  6. writes a normalized `manifest.json` plus per-pass `.glsl` / `.spv` / `.wgsl`
     and an `assets/` directory.

The GLSL->SPIR-V half needs only `glslangValidator` (Khronos, in the distro's
`glslang-tools`). The SPIR-V->WGSL half needs the `tint` CLI, which ships in the
dawn-exotic release tarball (built with TINT_BUILD_SPV_READER / CMD_TOOLS). If
`tint` is not found the tool still emits the wrapped GLSL and validated SPIR-V
and reports that the WGSL step was skipped, so it is useful before the Dawn
build lands.

Rendering the emitted WGSL inside yetty (channel textures + multipass buffers)
is a separate `yshadertoy` module extension; today `ycat`/`draw_shader` accept
only a single-pass, texture-free WGSL `mainImage`. The `manifest.json` carries
everything that extension needs.

Usage
-----
    yshadertoy.py <id|url|file> [-o OUT] [-k APPKEY]
                  [--channel0 SPEC ... --channel3 SPEC]
                  [--tint PATH] [--glslang PATH]
                  [--from-file] [--no-assets] [--no-convert] [--pass NAME] [-v]

`SPEC` for a channel override is a local path or an http(s) URL. The Shadertoy
app key comes from `--api-key` or `$SHADERTOY_APPKEY`; get one (free) at
https://www.shadertoy.com/howto#q2.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SHADERTOY_HOST = "https://www.shadertoy.com"
API_URL = SHADERTOY_HOST + "/api/v1/shaders/{shader_id}?key={app_key}"

# Shadertoy channel input types -> GLSL sampler type for the wrapper.
SAMPLER_FOR_CTYPE = {
    "texture": "sampler2D",
    "buffer": "sampler2D",
    "keyboard": "sampler2D",
    "music": "sampler2D",
    "musicstream": "sampler2D",
    "mic": "sampler2D",
    "webcam": "sampler2D",
    "video": "sampler2D",
    "cubemap": "samplerCube",
    "volume": "sampler3D",
}


# --------------------------------------------------------------------------
# curl helpers — the tool shells out to curl for every network fetch.
# --------------------------------------------------------------------------
def curl_to_file(url: str, dest: Path, verbose: bool = False) -> None:
    """Download `url` into `dest` with curl, failing loudly on error."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["curl", "-fsSL", "--retry", "2", "-o", str(dest), url]
    if verbose:
        print(f"    curl {url} -> {dest}", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"curl failed for {url}: {result.stderr.strip()}")


def curl_json(url: str, verbose: bool = False) -> dict:
    """Fetch `url` with curl and parse the body as JSON."""
    cmd = ["curl", "-fsSL", "--retry", "2", url]
    if verbose:
        # Do not print the URL: it carries the private app key.
        print("    curl <shadertoy api> (key redacted)", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"curl failed: {result.stderr.strip()}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"Shadertoy API did not return JSON (bad id or key?): {exc}"
        ) from exc


# --------------------------------------------------------------------------
# Shader source resolution — id / url / local file.
# --------------------------------------------------------------------------
def extract_shader_id(argument: str) -> str:
    """Turn a Shadertoy id or a /view/<id> URL into the bare id."""
    match = re.search(r"/view/([A-Za-z0-9]+)", argument)
    if match:
        return match.group(1)
    return argument


def fetch_shader(shader_id: str, app_key: str, verbose: bool) -> dict:
    """Fetch and unwrap the Shadertoy API `Shader` object."""
    url = API_URL.format(shader_id=shader_id, app_key=app_key)
    payload = curl_json(url, verbose=verbose)
    if "Error" in payload:
        raise RuntimeError(f"Shadertoy API error: {payload['Error']}")
    shader = payload.get("Shader")
    if not shader:
        raise RuntimeError(f"no 'Shader' object in API response for {shader_id}")
    return shader


def fetch_shader_via_browser(shader_id: str, verbose: bool) -> dict:
    """Fetch a public shader with no API key by driving a real browser.

    Shadertoy sits behind Cloudflare's bot challenge, so plain HTTP is blocked.
    cdp-fetch.js navigates headless Chrome to /view/<id> — clearing the challenge
    like any visitor — and captures the JSON the page's own XHR to /shadertoy
    returns (a list of shader objects). Needs `node` and `google-chrome`. Only
    public shaders are reachable this way.
    """
    fetcher = Path(__file__).with_name("cdp-fetch.js")
    if not shutil.which("node"):
        raise RuntimeError("browser fetch needs Node.js (install node) — or pass --api-key")
    if not shutil.which("google-chrome"):
        raise RuntimeError("browser fetch needs google-chrome — or pass --api-key")
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as handle:
        out_path = handle.name
    cmd = ["node", str(fetcher), shader_id, out_path]
    if verbose:
        print(f"    {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"browser fetch failed: {result.stderr.strip() or result.stdout.strip()}")
    data = json.loads(Path(out_path).read_text())
    shaders = data if isinstance(data, list) else [data]
    if not shaders or "renderpass" not in shaders[0]:
        raise RuntimeError(f"no shader data for {shader_id} (private/unlisted, or Cloudflare not cleared)")
    return shaders[0]


def shader_from_file(path: Path) -> dict:
    """Build a minimal single-pass `Shader` object from a local GLSL file."""
    code = path.read_text()
    return {
        "info": {"id": path.stem, "name": path.stem},
        "renderpass": [
            {"name": "Image", "type": "image", "code": code, "inputs": [], "outputs": []}
        ],
    }


# --------------------------------------------------------------------------
# GLSL wrapper reconstruction.
# --------------------------------------------------------------------------
UNIFORM_BLOCK = """\
layout(set = 0, binding = 0, std140) uniform ShadertoyUniforms {
    vec3  iResolution;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    int   iFrame;
    float iSampleRate;
    vec4  iMouse;
    vec4  iDate;
    vec3  iChannelResolution[4];
    float iChannelTime[4];
} shadertoy;
"""

# Bare-name aliases so the untouched Shadertoy body keeps compiling. Declared
# AFTER the uniform block so the member names inside the block are not rewritten.
UNIFORM_ALIASES = """\
#define iResolution        (shadertoy.iResolution)
#define iTime              (shadertoy.iTime)
#define iTimeDelta         (shadertoy.iTimeDelta)
#define iFrameRate         (shadertoy.iFrameRate)
#define iFrame             (shadertoy.iFrame)
#define iSampleRate        (shadertoy.iSampleRate)
#define iMouse             (shadertoy.iMouse)
#define iDate              (shadertoy.iDate)
#define iChannelResolution (shadertoy.iChannelResolution)
#define iChannelTime       (shadertoy.iChannelTime)
#define iGlobalTime        (shadertoy.iTime)
#define texture2D          texture
#define textureCube        texture
"""

MAIN_BODY = """\
layout(location = 0) out vec4 shadertoy_out_color;

void main()
{
    vec4 fragment_color = vec4(0.0);
    /* Shadertoy fragCoord has a bottom-left, pixel-centered origin.
       iResolution is the #define alias for shadertoy.iResolution. */
    vec2 fragCoord = gl_FragCoord.xy;
    fragCoord.y = iResolution.y - fragCoord.y;
    mainImage(fragment_color, fragCoord);
    shadertoy_out_color = fragment_color;
}
"""

PRECISION_RE = re.compile(r"^\s*precision\s+\w+\s+\w+\s*;\s*$", re.MULTILINE)


def sampler_declarations(inputs: list[dict]) -> str:
    """Declare iChannel0..3 with the sampler type each bound input needs.

    Unbound channels default to sampler2D so a shader that references a channel
    the API did not list still compiles.
    """
    sampler_types = {0: "sampler2D", 1: "sampler2D", 2: "sampler2D", 3: "sampler2D"}
    for entry in inputs:
        channel = entry.get("channel")
        if channel is None or not 0 <= int(channel) <= 3:
            continue
        ctype = entry.get("ctype", "texture")
        sampler_types[int(channel)] = SAMPLER_FOR_CTYPE.get(ctype, "sampler2D")
    lines = [
        f"layout(set = 0, binding = {channel + 1}) uniform {sampler} iChannel{channel};"
        for channel, sampler in sorted(sampler_types.items())
    ]
    return "\n".join(lines) + "\n"


def build_wrapper(pass_code: str, common_code: str, inputs: list[dict]) -> str:
    """Assemble compilable Vulkan GLSL around a raw Shadertoy pass body."""
    common_code = PRECISION_RE.sub("", common_code)
    pass_code = PRECISION_RE.sub("", pass_code)
    parts = [
        "#version 450",
        "",
        sampler_declarations(inputs).rstrip(),
        "",
        UNIFORM_BLOCK.rstrip(),
        "",
        UNIFORM_ALIASES.rstrip(),
        "",
    ]
    if common_code.strip():
        parts += ["/* ---- shared Common pass ---- */", common_code.strip(), ""]
    parts += [
        "/* ---- render pass body ---- */",
        pass_code.strip(),
        "",
        MAIN_BODY.rstrip(),
        "",
    ]
    return "\n".join(parts)


# --------------------------------------------------------------------------
# Conversion — glslang and tint.
# --------------------------------------------------------------------------
def find_tint(explicit: str | None) -> str | None:
    """Locate the tint CLI: --tint, $TINT, PATH, then a few cheap known paths.

    Auto-detection deliberately stats a small curated candidate list instead of
    walking the (very large) Dawn/yetty build trees. Once yetty's build extracts
    the dawn-exotic tarball to a fixed location, add that path here or pass
    --tint / $TINT.
    """
    if explicit:
        return explicit if Path(explicit).is_file() else None
    from_env = os.environ.get("TINT")
    if from_env and Path(from_env).is_file():
        return from_env
    on_path = shutil.which("tint")
    if on_path:
        return on_path
    home = Path.home()
    repo_root = Path(__file__).resolve().parents[2]
    candidates = [
        repo_root / "third_party" / "dawn" / "bin" / "tint",
        home / "work" / "my" / "dawn-exotic" / "build-x86_64-release" / "tint",
        home / "work" / "my" / "dawn-exotic" / "build-aarch64-release" / "tint",
    ]
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def glsl_to_spirv(glsl_path: Path, spv_path: Path, glslang: str, verbose: bool) -> None:
    cmd = [glslang, "-V", "--target-env", "vulkan1.1", "-S", "frag",
           str(glsl_path), "-o", str(spv_path)]
    if verbose:
        print(f"    {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"glslang failed:\n{result.stdout}\n{result.stderr}")


def spirv_to_wgsl(spv_path: Path, wgsl_path: Path, tint: str, verbose: bool) -> None:
    cmd = [tint, "--format", "wgsl", "-o", str(wgsl_path), str(spv_path)]
    if verbose:
        print(f"    {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"tint failed:\n{result.stdout}\n{result.stderr}")


# --------------------------------------------------------------------------
# yetty-native conversion — a self-contained `mainImage` yetty ingests directly.
# --------------------------------------------------------------------------
# yetty's yshadertoy prim concatenates its own vertex + fragment shaders around a
# user `fn mainImage(fragCoord, iResolution, iTime, iMouse) -> vec4<f32>` that
# must carry NO bindings or entry points. To get that from Shadertoy GLSL we
# compile with the uniforms as plain globals (tint lowers them to var<private>,
# not bindings), drop tint's generated @fragment entry, and append an adapter
# that feeds yetty's parameters into those globals via tint's stable main_inner.
# A shader that samples iChannel* forces real texture bindings and is not
# supported on this path (that is the M5 full-module route).
YETTY_GLSL_GLOBALS = """\
vec3  iResolution;
float iTime;
float iTimeDelta;
float iFrameRate;
int   iFrame;
float iSampleRate;
vec4  iMouse;
vec4  iDate;
vec3  iChannelResolution[4];
float iChannelTime[4];
"""


def build_globals_glsl(pass_code: str, common_code: str) -> str:
    common_code = PRECISION_RE.sub("", common_code)
    pass_code = PRECISION_RE.sub("", pass_code)
    parts = ["#version 450", YETTY_GLSL_GLOBALS.rstrip(), ""]
    if common_code.strip():
        parts += [common_code.strip(), ""]
    parts += [
        pass_code.strip(),
        "",
        "layout(location = 0) out vec4 yetty_out;",
        "void main() {",
        "    vec4 color = vec4(0.0);",
        "    mainImage(color, gl_FragCoord.xy);",
        "    yetty_out = color;",
        "}",
        "",
    ]
    return "\n".join(parts)


def strip_fragment_entry(wgsl: str) -> str:
    """Remove tint's generated `@fragment fn main(...) {...}` via brace matching."""
    marker = wgsl.find("@fragment")
    if marker < 0:
        return wgsl
    brace = wgsl.find("{", marker)
    if brace < 0:
        return wgsl
    depth = 0
    end = brace
    for index in range(brace, len(wgsl)):
        if wgsl[index] == "{":
            depth += 1
        elif wgsl[index] == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                break
    return (wgsl[:marker] + wgsl[end:]).rstrip() + "\n"


def build_yetty_adapter(module_wgsl: str) -> str:
    present = set(re.findall(r"var<private>\s+(\w+)\s*:", module_wgsl))
    assigns = []
    if "iResolution" in present:
        assigns.append("    iResolution = res;")
    if "iTime" in present:
        assigns.append("    iTime = tm;")
    if "iMouse" in present:
        assigns.append("    iMouse = ms;")
    joined = ("\n".join(assigns) + "\n") if assigns else ""
    return (
        "\nfn mainImage(fc: vec2<f32>, res: vec3<f32>, tm: f32, ms: vec4<f32>) -> vec4<f32> {\n"
        + joined
        + "    main_inner(vec4<f32>(fc, 0.0, 1.0));\n"
        "    return yetty_out;\n"
        "}\n"
    )


def convert_pass_to_yetty(pass_code: str, common_code: str, passes_dir: Path,
                          pass_slug: str, glslang: str, tint: str, verbose: bool):
    """Return the yetty-native WGSL path, or None if this pass can't use it."""
    glsl_path = passes_dir / f"{pass_slug}.yetty.frag"
    spv_path = passes_dir / f"{pass_slug}.yetty.spv"
    mod_path = passes_dir / f"{pass_slug}.yetty.mod.wgsl"
    out_path = passes_dir / f"{pass_slug}.yetty.wgsl"
    glsl_path.write_text(build_globals_glsl(pass_code, common_code))
    try:
        glsl_to_spirv(glsl_path, spv_path, glslang, verbose)
        spirv_to_wgsl(spv_path, mod_path, tint, verbose)
    except RuntimeError:
        return None  # references iChannel* / unsupported → needs the module path
    module = strip_fragment_entry(mod_path.read_text())
    if "fn main_inner" not in module or re.search(r"@group\(", module):
        return None
    out_path.write_text(module.rstrip() + "\n" + build_yetty_adapter(module))
    return out_path


def find_ycat(explicit: str | None) -> str | None:
    if explicit:
        return explicit if Path(explicit).is_file() else None
    on_path = shutil.which("ycat")
    if on_path:
        return on_path
    repo_root = Path(__file__).resolve().parents[2]
    for build_dir in sorted(repo_root.glob("build-desktop-*")):
        candidate = build_dir / "tools" / "ycat" / "ycat"
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def render_in_yetty(wgsl_path: Path, ycat: str, verbose: bool) -> bool:
    """Draw the shader into the current terminal (a no-op unless run in yetty)."""
    if verbose:
        print(f"    {ycat} {wgsl_path}", file=sys.stderr)
    return subprocess.run([ycat, str(wgsl_path)]).returncode == 0


# --------------------------------------------------------------------------
# Assets.
# --------------------------------------------------------------------------
def resolve_channel_overrides(args: argparse.Namespace) -> dict[int, str]:
    overrides: dict[int, str] = {}
    for channel in range(4):
        spec = getattr(args, f"channel{channel}")
        if spec:
            overrides[channel] = spec
    return overrides


def fetch_input_asset(entry: dict, assets_dir: Path, override: str | None,
                      no_assets: bool, verbose: bool) -> dict:
    """Download (or copy) one channel asset; return its manifest record."""
    channel = entry.get("channel")
    ctype = entry.get("ctype", "texture")
    record = {"channel": channel, "ctype": ctype, "src": entry.get("src")}

    if override is not None:
        record["source"] = "override"
        if re.match(r"^https?://", override):
            dest = assets_dir / f"channel{channel}{Path(override).suffix or '.dat'}"
            if not no_assets:
                curl_to_file(override, dest, verbose=verbose)
            record["asset"] = str(dest)
        else:
            src_path = Path(override)
            if not src_path.is_file():
                raise RuntimeError(f"--channel{channel}: no such file {override}")
            dest = assets_dir / f"channel{channel}{src_path.suffix}"
            if not no_assets:
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src_path, dest)
            record["asset"] = str(dest)
        return record

    if ctype in ("buffer", "keyboard", "mic", "webcam", "musicstream"):
        # Not a static media file — generated at render time. Recorded only.
        record["source"] = ctype
        record["asset"] = None
        return record

    src = entry.get("src")
    if not src:
        record["source"] = "none"
        record["asset"] = None
        return record
    url = src if re.match(r"^https?://", src) else SHADERTOY_HOST + src
    dest = assets_dir / Path(src).name
    if not no_assets:
        curl_to_file(url, dest, verbose=verbose)
    record["source"] = "shadertoy"
    record["asset"] = str(dest)
    return record


# --------------------------------------------------------------------------
# Driver.
# --------------------------------------------------------------------------
def slug(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9]", "", name) or "Pass"


def convert(shader: dict, out_dir: Path, args: argparse.Namespace,
            tint: str | None) -> dict:
    passes = shader.get("renderpass", [])
    common_code = "".join(
        rp.get("code", "") for rp in passes if rp.get("type") == "common"
    )
    overrides = resolve_channel_overrides(args)
    assets_dir = out_dir / "assets"
    passes_dir = out_dir / "passes"
    passes_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "id": shader.get("info", {}).get("id"),
        "name": shader.get("info", {}).get("name"),
        "uniforms": ["iResolution", "iTime", "iTimeDelta", "iFrameRate", "iFrame",
                     "iSampleRate", "iMouse", "iDate", "iChannelResolution",
                     "iChannelTime"],
        "tint": tint,
        "passes": [],
    }

    for render_pass in passes:
        ptype = render_pass.get("type")
        name = render_pass.get("name") or ptype or "Image"
        if ptype == "common":
            continue
        if ptype in ("sound", "cubemap"):
            print(f"==> skipping unsupported pass '{name}' (type={ptype})",
                  file=sys.stderr)
            manifest["passes"].append({"name": name, "type": ptype, "skipped": True})
            continue
        if args.pass_name and slug(name) != slug(args.pass_name):
            continue

        inputs = render_pass.get("inputs", [])
        pass_slug = slug(name)
        print(f"==> pass '{name}' ({ptype}) — {len(inputs)} channel input(s)")

        asset_records = []
        for entry in inputs:
            channel = entry.get("channel")
            override = overrides.get(int(channel)) if channel is not None else None
            asset_records.append(
                fetch_input_asset(entry, assets_dir, override, args.no_assets, args.verbose)
            )

        wrapped = build_wrapper(render_pass.get("code", ""), common_code, inputs)
        glsl_path = passes_dir / f"{pass_slug}.glsl"
        glsl_path.write_text(wrapped)

        pass_record = {
            "name": name,
            "type": ptype,
            "glsl": str(glsl_path),
            "channels": asset_records,
            "spirv": None,
            "wgsl": None,
            "yetty_wgsl": None,
        }

        if not args.no_convert:
            spv_path = passes_dir / f"{pass_slug}.spv"
            glsl_to_spirv(glsl_path, spv_path, args.glslang, args.verbose)
            pass_record["spirv"] = str(spv_path)
            print(f"    glslang -> {spv_path.name} ({spv_path.stat().st_size} bytes)")
            if tint:
                wgsl_path = passes_dir / f"{pass_slug}.wgsl"
                spirv_to_wgsl(spv_path, wgsl_path, tint, args.verbose)
                pass_record["wgsl"] = str(wgsl_path)
                print(f"    tint    -> {wgsl_path.name} ({wgsl_path.stat().st_size} bytes)")
                # yetty-native form — only for texture-free single passes.
                if not inputs:
                    yetty_path = convert_pass_to_yetty(
                        render_pass.get("code", ""), common_code, passes_dir,
                        pass_slug, args.glslang, tint, args.verbose)
                    if yetty_path:
                        pass_record["yetty_wgsl"] = str(yetty_path)
                        print(f"    yetty   -> {yetty_path.name}")
                    else:
                        print("    yetty   -> (skipped: needs textures/multipass — M5)")
                else:
                    print("    yetty   -> (skipped: channel inputs need textures — M5)")

        manifest["passes"].append(pass_record)

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="yshadertoy.py",
        description="Import a Shadertoy shader (via curl) into yetty's WGSL pipeline.",
    )
    parser.add_argument("shader",
                        help="Shadertoy id, /view/<id> URL, or a local GLSL file with --from-file")
    parser.add_argument("-o", "--out", type=Path, default=None,
                        help="output directory (default: ./<shader-id>)")
    parser.add_argument("-k", "--api-key", default=os.environ.get("SHADERTOY_APPKEY"),
                        help="Shadertoy app key (or $SHADERTOY_APPKEY)")
    parser.add_argument("--from-file", action="store_true",
                        help="treat <shader> as a local GLSL mainImage file, no download")
    for channel in range(4):
        parser.add_argument(f"--channel{channel}", default=None, metavar="FILE|URL",
                            help=f"override iChannel{channel} with a local file or URL")
    parser.add_argument("--tint", default=None, help="path to the tint CLI")
    parser.add_argument("--glslang", default=shutil.which("glslangValidator"),
                        help="path to glslangValidator")
    parser.add_argument("--pass", dest="pass_name", default=None,
                        help="convert only this pass (e.g. Image, 'Buffer A')")
    parser.add_argument("--no-assets", action="store_true", help="do not download media assets")
    parser.add_argument("--no-convert", action="store_true",
                        help="fetch + assets only; skip glslang/tint")
    parser.add_argument("--ycat", default=None, help="path to the ycat binary (for rendering)")
    parser.add_argument("--no-render", action="store_true",
                        help="do not draw the Image pass into the current yetty terminal")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    if not args.no_convert and not args.glslang:
        parser.error("glslangValidator not found; install glslang-tools or pass --glslang")

    # 1. Resolve the shader source.
    if args.from_file or Path(args.shader).is_file():
        source_path = Path(args.shader)
        if not source_path.is_file():
            parser.error(f"--from-file given but {args.shader} is not a file")
        shader = shader_from_file(source_path)
        shader_id = source_path.stem
        raw_json = None
    else:
        shader_id = extract_shader_id(args.shader)
        if args.api_key:
            shader = fetch_shader(shader_id, args.api_key, args.verbose)
        else:
            print("==> no API key — fetching via headless browser (public shaders only)",
                  file=sys.stderr)
            shader = fetch_shader_via_browser(shader_id, args.verbose)
        raw_json = shader

    out_dir = args.out or Path(shader_id)
    out_dir.mkdir(parents=True, exist_ok=True)
    if raw_json is not None:
        (out_dir / "shader.json").write_text(json.dumps({"Shader": raw_json}, indent=2) + "\n")

    # 2. Locate tint (best-effort — the WGSL step is skipped if absent).
    tint = find_tint(args.tint)
    if not args.no_convert and not tint:
        print("==> note: tint CLI not found — emitting GLSL + SPIR-V only, "
              "WGSL step skipped.\n    Provide it with --tint or $TINT once the "
              "dawn-exotic release with bin/tint is installed.", file=sys.stderr)

    # 3. Convert.
    manifest = convert(shader, out_dir, args, tint)

    converted = sum(1 for p in manifest["passes"] if p.get("wgsl"))
    compiled = sum(1 for p in manifest["passes"] if p.get("spirv"))
    print(f"\n==> {out_dir}/  —  {compiled} pass(es) to SPIR-V, {converted} to WGSL")
    print(f"    manifest: {out_dir / 'manifest.json'}")

    # 4. Draw the Image pass into the current yetty terminal.
    if not args.no_render and not args.no_convert:
        image = next((p for p in manifest["passes"]
                      if p.get("yetty_wgsl") and p.get("type") == "image"), None)
        if image:
            ycat = find_ycat(args.ycat)
            if ycat:
                if render_in_yetty(Path(image["yetty_wgsl"]), ycat, args.verbose):
                    print(f"==> drew '{image['name']}' into the current terminal (yetty)")
            else:
                print("==> ycat not found — pass --ycat or run from a yetty build tree to render",
                      file=sys.stderr)
        else:
            print("==> nothing renderable in yetty yet (texture/multipass shader needs M5)",
                  file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
