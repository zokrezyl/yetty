#!/usr/bin/env python3
"""Regression reference corpus runner (issue #454).

Records and checks deterministic regression references: each scenario runs
ycat on a repo sample input, captures the emitted OSC wire stream, and
decodes it with decode-ydraw into a stable, diffable text artifact. The
set of decoded artifacts plus a manifest forms the *corpus*.

The canonical accepted corpus lives as a GitHub Release asset
(`yetty-regression-corpus-current.zip` on the `regression-corpus` release)
and is only ever replaced by the manual `promote-regression-reference`
GitHub Actions workflow. Normal CI downloads the corpus, compares, and on
mismatch uploads only compact evidence. See test/regression/README.md.

Subcommands:
  record    run scenarios, write a reference corpus directory
  check     run scenarios, compare against a corpus (zip or dir)
  validate  validate a corpus before promotion
  merge     overlay a freshly recorded partial corpus onto a base corpus
  pack      zip a corpus directory and write a .sha256 sidecar
  fetch     download the active corpus release asset (retry + checksum)
  list      print scenario ids

Exit codes:
  0  success
  1  regression mismatch / validation failure / execution error
  3  fetch: no corpus published yet (bootstrap case)
  4  fetch: corpus unavailable after retries (possibly mid-promotion)

Only the Python standard library is used so the script runs unchanged on
GitHub-hosted runners, the Woodpecker build image, and developer machines.
"""

import argparse
import difflib
import fnmatch
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path

EXIT_OK = 0
EXIT_FAILURE = 1
EXIT_NOT_PUBLISHED = 3
EXIT_UNAVAILABLE = 4

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
SCENARIOS_FILE = SCRIPT_DIR / "scenarios.json"

DEFAULT_BUILD_DIR = "build-desktop-ytrace-release"
DEFAULT_GITHUB_REPO = "zokrezyl/yetty"
DEFAULT_RELEASE_TAG = "regression-corpus"
DEFAULT_ASSET_NAME = "yetty-regression-corpus-current.zip"

MANIFEST_NAME = "manifest.json"
MANIFEST_VERSION = 1
ARTIFACT_NAME = "decoded.txt"

ALLOWED_PROMOTION_REASONS = (
    "intentional-change",
    "bugfix-new-correct-output",
    "oracle-fix",
    "normalization-change",
)


class ScenarioError(Exception):
    """A scenario failed to execute (tool missing, nonzero exit, empty output)."""


def utc_now_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_scenarios(filter_glob):
    with open(SCENARIOS_FILE, "r", encoding="utf-8") as handle:
        definition = json.load(handle)
    if definition.get("version") != 1:
        raise SystemExit(f"unsupported scenarios.json version: {definition.get('version')}")
    defaults = definition.get("defaults", {})
    scenarios = []
    seen_ids = set()
    for entry in definition["scenarios"]:
        scenario_id = entry["id"]
        if scenario_id in seen_ids:
            raise SystemExit(f"duplicate scenario id: {scenario_id}")
        seen_ids.add(scenario_id)
        if filter_glob and not fnmatch.fnmatch(scenario_id, filter_glob):
            continue
        scenarios.append({
            "id": scenario_id,
            "description": entry.get("description", ""),
            "input": entry["input"],
            "ycat_args": entry.get("ycat_args", defaults.get("ycat_args", [])),
            "timeout_seconds": entry.get("timeout_seconds", defaults.get("timeout_seconds", 120)),
        })
    return scenarios


def tool_path(build_dir, relative):
    path = REPO_ROOT / build_dir / relative
    if not path.is_file():
        raise SystemExit(f"missing tool binary: {path} — build {build_dir} first")
    return path


def scenario_environment():
    environment = dict(os.environ)
    # Deterministic execution: force the in-yetty OSC dispatch path and pin
    # locale-dependent number formatting.
    environment["TERM_PROGRAM"] = "yetty"
    environment["LC_ALL"] = "C"
    environment["LANG"] = "C"
    return environment


def run_scenario(ycat_binary, decode_binary, scenario, work_dir):
    """Run one scenario; return (wire_sha256, decoded_bytes)."""
    input_path = REPO_ROOT / scenario["input"]
    if not input_path.is_file():
        raise ScenarioError(f"input file missing: {scenario['input']}")

    environment = scenario_environment()
    command = [str(ycat_binary)] + list(scenario["ycat_args"]) + [str(input_path)]
    try:
        ycat_run = subprocess.run(
            command, cwd=REPO_ROOT, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=scenario["timeout_seconds"])
    except subprocess.TimeoutExpired:
        raise ScenarioError(f"ycat timed out after {scenario['timeout_seconds']}s")
    if ycat_run.returncode != 0:
        stderr_tail = ycat_run.stderr.decode("utf-8", errors="replace")[-500:]
        raise ScenarioError(f"ycat exited {ycat_run.returncode}: {stderr_tail}")
    wire_bytes = ycat_run.stdout
    if not wire_bytes:
        raise ScenarioError("ycat produced no output")

    # decode-ydraw prints the wire file name in its trailer line, so the
    # file name must be stable: always "wire.osc", decoded with the work
    # directory as cwd.
    wire_path = Path(work_dir) / "wire.osc"
    wire_path.write_bytes(wire_bytes)
    try:
        decode_run = subprocess.run(
            [str(decode_binary), "wire.osc"], cwd=work_dir, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=scenario["timeout_seconds"])
    except subprocess.TimeoutExpired:
        raise ScenarioError(f"decode-ydraw timed out after {scenario['timeout_seconds']}s")
    if decode_run.returncode != 0:
        output_tail = decode_run.stdout.decode("utf-8", errors="replace")[-500:]
        raise ScenarioError(f"decode-ydraw exited {decode_run.returncode}: {output_tail}")
    decoded_bytes = decode_run.stdout
    if not decoded_bytes:
        raise ScenarioError("decode-ydraw produced no output")
    if b", 0 error(s)" not in decoded_bytes:
        raise ScenarioError("decode-ydraw reported envelope errors")
    return sha256_bytes(wire_bytes), decoded_bytes


def load_manifest(corpus_dir):
    manifest_path = Path(corpus_dir) / MANIFEST_NAME
    if not manifest_path.is_file():
        raise SystemExit(f"corpus has no {MANIFEST_NAME}: {corpus_dir}")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("version") != MANIFEST_VERSION:
        raise SystemExit(f"unsupported corpus manifest version: {manifest.get('version')}")
    return manifest


def write_manifest(corpus_dir, manifest):
    manifest_path = Path(corpus_dir) / MANIFEST_NAME
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)
        handle.write("\n")


def materialize_corpus(corpus_argument, temp_stack):
    """Return a corpus directory; extract first when given a zip file."""
    corpus_path = Path(corpus_argument)
    if corpus_path.is_dir():
        return corpus_path
    if corpus_path.is_file():
        extract_dir = tempfile.mkdtemp(prefix="yetty-regression-corpus-")
        temp_stack.append(extract_dir)
        with zipfile.ZipFile(corpus_path) as archive:
            archive.extractall(extract_dir)
        return Path(extract_dir)
    raise SystemExit(f"corpus not found: {corpus_argument}")


def resolve_commit(explicit_commit):
    if explicit_commit:
        return explicit_commit
    try:
        git_run = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=10)
        if git_run.returncode == 0:
            return git_run.stdout.decode("ascii").strip()
    except OSError:
        pass
    return "unknown"


# ---------------------------------------------------------------------------
# record
# ---------------------------------------------------------------------------

def command_record(args):
    scenarios = load_scenarios(args.filter)
    if not scenarios:
        print(f"no scenarios match filter: {args.filter}")
        return EXIT_FAILURE
    ycat_binary = tool_path(args.build_dir, "tools/ycat/ycat")
    decode_binary = tool_path(args.build_dir, "tools/decode-ydraw/decode-ydraw")
    commit = resolve_commit(args.commit)

    output_dir = Path(args.out)
    if output_dir.exists():
        shutil.rmtree(output_dir)
    (output_dir / "scenarios").mkdir(parents=True)

    manifest = {
        "version": MANIFEST_VERSION,
        "promotion": {
            "commit": commit,
            "reason": args.reason,
            "issue": args.issue,
            "promoted_at": utc_now_iso(),
            "scenario_filter": args.filter or "*",
        },
        "scenarios": {},
    }

    for scenario in scenarios:
        runs = []
        for run_index in range(max(1, args.stability_runs)):
            with tempfile.TemporaryDirectory(prefix="yetty-regression-run-") as work_dir:
                runs.append(run_scenario(ycat_binary, decode_binary, scenario, work_dir))
        wire_sha256, decoded_bytes = runs[0]
        for extra_wire_sha256, extra_decoded_bytes in runs[1:]:
            if extra_decoded_bytes != decoded_bytes or extra_wire_sha256 != wire_sha256:
                print(f"UNSTABLE  {scenario['id']}: output differs between repeat runs")
                return EXIT_FAILURE

        scenario_dir = output_dir / "scenarios" / scenario["id"]
        scenario_dir.mkdir(parents=True)
        artifact_relative = f"scenarios/{scenario['id']}/{ARTIFACT_NAME}"
        (output_dir / artifact_relative).write_bytes(decoded_bytes)
        manifest["scenarios"][scenario["id"]] = {
            "description": scenario["description"],
            "input": scenario["input"],
            "input_sha256": sha256_file(REPO_ROOT / scenario["input"]),
            "artifact": artifact_relative,
            "artifact_sha256": sha256_bytes(decoded_bytes),
            "wire_sha256": wire_sha256,
            "commit": commit,
            "reason": args.reason,
            "issue": args.issue,
            "recorded_at": utc_now_iso(),
        }
        print(f"recorded  {scenario['id']}  ({len(decoded_bytes)} B decoded)")

    write_manifest(output_dir, manifest)
    print(f"corpus recorded: {output_dir}  ({len(scenarios)} scenario(s), commit {commit[:12]})")
    return EXIT_OK


# ---------------------------------------------------------------------------
# check
# ---------------------------------------------------------------------------

def truncate_diff_lines(diff_lines, max_lines):
    if len(diff_lines) <= max_lines:
        return diff_lines
    kept = diff_lines[:max_lines]
    kept.append(f"... diff truncated ({len(diff_lines) - max_lines} more lines)\n")
    return kept


def command_check(args):
    temp_stack = []
    try:
        return run_check(args, temp_stack)
    finally:
        for temp_dir in temp_stack:
            shutil.rmtree(temp_dir, ignore_errors=True)


def run_check(args, temp_stack):
    scenarios = load_scenarios(args.filter)
    if not scenarios:
        print(f"no scenarios match filter: {args.filter}")
        return EXIT_FAILURE
    ycat_binary = tool_path(args.build_dir, "tools/ycat/ycat")
    decode_binary = tool_path(args.build_dir, "tools/decode-ydraw/decode-ydraw")
    corpus_dir = materialize_corpus(args.corpus, temp_stack)
    manifest = load_manifest(corpus_dir)
    manifest_scenarios = manifest["scenarios"]

    evidence_dir = Path(args.evidence)
    if evidence_dir.exists():
        shutil.rmtree(evidence_dir)
    evidence_dir.mkdir(parents=True)

    results = []
    for scenario in scenarios:
        scenario_id = scenario["id"]
        reference = manifest_scenarios.get(scenario_id)
        if reference is None:
            results.append({"id": scenario_id, "status": "NEW",
                            "detail": "no reference in corpus yet — promote to admit"})
            continue

        input_path = REPO_ROOT / scenario["input"]
        if not input_path.is_file():
            results.append({"id": scenario_id, "status": "ERROR",
                            "detail": f"input file missing: {scenario['input']}"})
            continue
        current_input_sha256 = sha256_file(input_path)
        if current_input_sha256 != reference["input_sha256"]:
            results.append({
                "id": scenario_id, "status": "INPUT-CHANGED",
                "detail": ("scenario input changed since the reference was recorded "
                           "— review and promote if intentional")})
            continue

        try:
            with tempfile.TemporaryDirectory(prefix="yetty-regression-run-") as work_dir:
                wire_sha256, decoded_bytes = run_scenario(
                    ycat_binary, decode_binary, scenario, work_dir)
        except ScenarioError as error:
            results.append({"id": scenario_id, "status": "ERROR", "detail": str(error)})
            continue

        reference_path = corpus_dir / reference["artifact"]
        if not reference_path.is_file():
            results.append({"id": scenario_id, "status": "ERROR",
                            "detail": f"corpus is missing artifact {reference['artifact']}"})
            continue
        reference_bytes = reference_path.read_bytes()
        if decoded_bytes == reference_bytes:
            results.append({"id": scenario_id, "status": "PASS", "detail": ""})
            continue

        reference_text = reference_bytes.decode("utf-8", errors="replace").splitlines(keepends=True)
        current_text = decoded_bytes.decode("utf-8", errors="replace").splitlines(keepends=True)
        diff_lines = list(difflib.unified_diff(
            reference_text, current_text,
            fromfile=f"reference/{scenario_id}/{ARTIFACT_NAME}",
            tofile=f"current/{scenario_id}/{ARTIFACT_NAME}"))
        diff_lines = truncate_diff_lines(diff_lines, args.max_diff_lines)
        diff_path = evidence_dir / f"{scenario_id}.diff"
        diff_path.write_text("".join(diff_lines), encoding="utf-8")
        results.append({
            "id": scenario_id, "status": "FAIL",
            "detail": (f"decoded output differs "
                       f"(reference sha {reference['artifact_sha256'][:12]}, "
                       f"current sha {sha256_bytes(decoded_bytes)[:12]}); "
                       f"see {diff_path.name}"),
            "reference_meta": {
                "commit": reference.get("commit", ""),
                "reason": reference.get("reason", ""),
                "issue": reference.get("issue", ""),
                "recorded_at": reference.get("recorded_at", ""),
            },
            "current_wire_sha256": wire_sha256,
        })

    defined_ids = {scenario["id"] for scenario in scenarios}
    if not args.filter or args.filter == "*":
        for stale_id in sorted(set(manifest_scenarios) - defined_ids):
            results.append({"id": stale_id, "status": "STALE",
                            "detail": "reference exists but scenario is no longer defined "
                                      "— next promotion drops it"})

    counts = {}
    for result in results:
        counts[result["status"]] = counts.get(result["status"], 0) + 1
    summary = {
        "checked_at": utc_now_iso(),
        "commit": resolve_commit(None),
        "corpus_promotion": manifest.get("promotion", {}),
        "counts": counts,
        "results": results,
    }
    with open(evidence_dir / "summary.json", "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)
        handle.write("\n")

    failing_statuses = {"FAIL", "INPUT-CHANGED", "ERROR"}
    for result in results:
        marker = "  " if result["status"] == "PASS" else "! "
        line = f"{marker}{result['status']:<14} {result['id']}"
        if result["detail"]:
            line += f"  — {result['detail']}"
        print(line)
    print(f"summary: {json.dumps(counts, sort_keys=True)}")

    if any(result["status"] in failing_statuses for result in results):
        print("regression check FAILED — evidence in", evidence_dir)
        print("if the new output is intentional, run the "
              "promote-regression-reference workflow (see test/regression/README.md)")
        return EXIT_FAILURE
    print("regression check passed")
    return EXIT_OK


# ---------------------------------------------------------------------------
# validate
# ---------------------------------------------------------------------------

def command_validate(args):
    temp_stack = []
    try:
        return run_validate(args, temp_stack)
    finally:
        for temp_dir in temp_stack:
            shutil.rmtree(temp_dir, ignore_errors=True)


def run_validate(args, temp_stack):
    scenarios = load_scenarios(None)
    corpus_dir = materialize_corpus(args.corpus, temp_stack)
    manifest = load_manifest(corpus_dir)
    manifest_scenarios = manifest["scenarios"]
    problems = []

    for scenario in scenarios:
        scenario_id = scenario["id"]
        reference = manifest_scenarios.get(scenario_id)
        if reference is None:
            problems.append(f"missing scenario: {scenario_id}")
            continue
        artifact_path = corpus_dir / reference["artifact"]
        if not artifact_path.is_file():
            problems.append(f"{scenario_id}: artifact file missing: {reference['artifact']}")
            continue
        artifact_bytes = artifact_path.read_bytes()
        if not artifact_bytes:
            problems.append(f"{scenario_id}: artifact is empty")
        if sha256_bytes(artifact_bytes) != reference["artifact_sha256"]:
            problems.append(f"{scenario_id}: artifact checksum mismatch")
        input_path = REPO_ROOT / reference["input"]
        if not input_path.is_file():
            problems.append(f"{scenario_id}: input file missing: {reference['input']}")
        elif sha256_file(input_path) != reference["input_sha256"]:
            problems.append(f"{scenario_id}: input checksum does not match the checked-out tree")
        for field in ("commit", "reason", "recorded_at"):
            if not reference.get(field):
                problems.append(f"{scenario_id}: metadata field '{field}' is empty")

    defined_ids = {scenario["id"] for scenario in scenarios}
    for stale_id in sorted(set(manifest_scenarios) - defined_ids):
        problems.append(f"stale reference for undefined scenario: {stale_id} "
                        "(merge drops these — do not promote a stale pack)")

    if args.require_promotion_meta:
        promotion = manifest.get("promotion", {})
        if promotion.get("reason") not in ALLOWED_PROMOTION_REASONS:
            problems.append(
                f"promotion reason '{promotion.get('reason')}' not in "
                f"{ALLOWED_PROMOTION_REASONS}")
        if not promotion.get("issue"):
            problems.append("promotion metadata has no issue/PR reference")
        if not promotion.get("commit") or promotion.get("commit") == "unknown":
            problems.append("promotion metadata has no commit")
        for scenario_id, reference in sorted(manifest_scenarios.items()):
            if reference.get("reason") not in ALLOWED_PROMOTION_REASONS:
                problems.append(f"{scenario_id}: reason '{reference.get('reason')}' not in "
                                f"{ALLOWED_PROMOTION_REASONS}")
            if not reference.get("issue"):
                problems.append(f"{scenario_id}: no issue/PR reference")

    if problems:
        for problem in problems:
            print(f"INVALID: {problem}")
        return EXIT_FAILURE
    print(f"corpus valid: {len(manifest_scenarios)} scenario(s)")
    return EXIT_OK


# ---------------------------------------------------------------------------
# merge
# ---------------------------------------------------------------------------

def command_merge(args):
    temp_stack = []
    try:
        return run_merge(args, temp_stack)
    finally:
        for temp_dir in temp_stack:
            shutil.rmtree(temp_dir, ignore_errors=True)


def run_merge(args, temp_stack):
    scenarios = load_scenarios(None)
    base_dir = materialize_corpus(args.base, temp_stack)
    overlay_dir = materialize_corpus(args.overlay, temp_stack)
    base_manifest = load_manifest(base_dir)
    overlay_manifest = load_manifest(overlay_dir)

    output_dir = Path(args.out)
    if output_dir.exists():
        shutil.rmtree(output_dir)
    (output_dir / "scenarios").mkdir(parents=True)

    merged_manifest = {
        "version": MANIFEST_VERSION,
        # The promotion block always describes the newest promotion event.
        "promotion": overlay_manifest.get("promotion", {}),
        "scenarios": {},
    }

    taken_from_overlay = 0
    taken_from_base = 0
    dropped_stale = sorted(
        (set(base_manifest["scenarios"]) | set(overlay_manifest["scenarios"]))
        - {scenario["id"] for scenario in scenarios})
    for scenario in scenarios:
        scenario_id = scenario["id"]
        if scenario_id in overlay_manifest["scenarios"]:
            source_dir, source_manifest = overlay_dir, overlay_manifest
            taken_from_overlay += 1
        elif scenario_id in base_manifest["scenarios"]:
            source_dir, source_manifest = base_dir, base_manifest
            taken_from_base += 1
        else:
            # Neither side has it; validate reports the gap.
            continue
        reference = source_manifest["scenarios"][scenario_id]
        artifact_relative = reference["artifact"]
        destination = output_dir / artifact_relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_dir / artifact_relative, destination)
        merged_manifest["scenarios"][scenario_id] = reference

    write_manifest(output_dir, merged_manifest)
    print(f"merged corpus: {output_dir}  "
          f"({taken_from_overlay} regenerated, {taken_from_base} kept from base"
          + (f", dropped stale: {', '.join(dropped_stale)}" if dropped_stale else "")
          + ")")
    return EXIT_OK


# ---------------------------------------------------------------------------
# pack
# ---------------------------------------------------------------------------

def command_pack(args):
    corpus_dir = Path(args.corpus)
    if not corpus_dir.is_dir():
        raise SystemExit(f"corpus directory not found: {corpus_dir}")
    output_path = Path(args.out)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    member_paths = sorted(
        path for path in corpus_dir.rglob("*") if path.is_file())
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for member_path in member_paths:
            archive.write(member_path, member_path.relative_to(corpus_dir))

    pack_sha256 = sha256_file(output_path)
    sidecar_path = Path(str(output_path) + ".sha256")
    sidecar_path.write_text(f"{pack_sha256}  {output_path.name}\n", encoding="utf-8")
    print(f"packed: {output_path}  ({output_path.stat().st_size} B, sha256 {pack_sha256[:12]}…)")
    return EXIT_OK


# ---------------------------------------------------------------------------
# fetch
# ---------------------------------------------------------------------------

def github_request(url, accept):
    headers = {
        "User-Agent": "yetty-regression-corpus",
        "Accept": accept,
        "X-GitHub-Api-Version": "2022-11-28",
    }
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return urllib.request.Request(url, headers=headers)


def download_url(url, destination):
    request = github_request(url, "application/octet-stream")
    with urllib.request.urlopen(request, timeout=120) as response, \
            open(destination, "wb") as handle:
        shutil.copyfileobj(response, handle)


def command_fetch(args):
    release_url = f"https://api.github.com/repos/{args.repo}/releases/tags/{args.tag}"
    sidecar_name = args.asset + ".sha256"
    destination = Path(args.dest)
    destination.parent.mkdir(parents=True, exist_ok=True)

    last_problem = ""
    for attempt in range(1, args.attempts + 1):
        if attempt > 1:
            print(f"retrying in {args.retry_delay_seconds}s "
                  f"(attempt {attempt}/{args.attempts}; last problem: {last_problem})")
            time.sleep(args.retry_delay_seconds)
        try:
            with urllib.request.urlopen(
                    github_request(release_url, "application/vnd.github+json"),
                    timeout=60) as response:
                release = json.load(response)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                print(f"no '{args.tag}' release published on {args.repo} — "
                      "run the promote-regression-reference workflow to bootstrap")
                return EXIT_NOT_PUBLISHED
            last_problem = f"release lookup HTTP {error.code}"
            continue
        except (urllib.error.URLError, OSError, json.JSONDecodeError) as error:
            last_problem = f"release lookup failed: {error}"
            continue

        assets = {asset["name"]: asset for asset in release.get("assets", [])}
        if args.asset not in assets or sidecar_name not in assets:
            # The release exists but the active pack (or its checksum) is
            # absent — the usual cause is an in-flight promotion swap.
            last_problem = f"asset '{args.asset}' (or its .sha256) not on the release"
            continue

        try:
            download_dir = destination.parent
            temporary_pack = download_dir / (destination.name + ".downloading")
            download_url(assets[args.asset]["browser_download_url"], temporary_pack)
            sidecar_path = Path(str(destination) + ".sha256")
            download_url(assets[sidecar_name]["browser_download_url"], sidecar_path)
        except (urllib.error.URLError, OSError) as error:
            last_problem = f"download failed: {error}"
            continue

        expected_sha256 = sidecar_path.read_text(encoding="utf-8").split()[0]
        actual_sha256 = sha256_file(temporary_pack)
        if actual_sha256 != expected_sha256:
            last_problem = "checksum mismatch (pack and sidecar out of sync)"
            temporary_pack.unlink(missing_ok=True)
            continue
        temporary_pack.replace(destination)
        print(f"fetched corpus: {destination}  (sha256 {actual_sha256[:12]}…, "
              f"release '{args.tag}', asset '{args.asset}')")
        return EXIT_OK

    print(f"corpus unavailable after {args.attempts} attempts: {last_problem}")
    print("this can happen while a promotion is replacing the asset — "
          "restart this job once the promotion workflow has finished")
    return EXIT_UNAVAILABLE


# ---------------------------------------------------------------------------
# list
# ---------------------------------------------------------------------------

def command_list(args):
    for scenario in load_scenarios(args.filter):
        print(f"{scenario['id']:<28} {scenario['input']}")
    return EXIT_OK


# ---------------------------------------------------------------------------
# argument parsing
# ---------------------------------------------------------------------------

def build_parser():
    parser = argparse.ArgumentParser(
        prog="regression.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="command", required=True)

    record_parser = subparsers.add_parser("record", help="record a reference corpus")
    record_parser.add_argument("--out", required=True, help="output corpus directory")
    record_parser.add_argument("--build-dir", default=DEFAULT_BUILD_DIR)
    record_parser.add_argument("--filter", default=None, help="glob over scenario ids")
    record_parser.add_argument("--stability-runs", type=int, default=1,
                               help="run each scenario N times and require identical output")
    record_parser.add_argument("--commit", default=None,
                               help="commit sha to record (default: git rev-parse HEAD)")
    record_parser.add_argument("--reason", default="local-record",
                               help="promotion reason (one of %s for promotion)"
                               % ", ".join(ALLOWED_PROMOTION_REASONS))
    record_parser.add_argument("--issue", default="",
                               help="issue/PR reference justifying the new references")
    record_parser.set_defaults(handler=command_record)

    check_parser = subparsers.add_parser("check", help="check against a corpus")
    check_parser.add_argument("--corpus", required=True, help="corpus zip or directory")
    check_parser.add_argument("--evidence", required=True, help="evidence output directory")
    check_parser.add_argument("--build-dir", default=DEFAULT_BUILD_DIR)
    check_parser.add_argument("--filter", default=None, help="glob over scenario ids")
    check_parser.add_argument("--max-diff-lines", type=int, default=400)
    check_parser.set_defaults(handler=command_check)

    validate_parser = subparsers.add_parser("validate", help="validate a corpus")
    validate_parser.add_argument("--corpus", required=True, help="corpus zip or directory")
    validate_parser.add_argument("--require-promotion-meta", action="store_true",
                                 help="require a valid promotion reason and issue reference")
    validate_parser.set_defaults(handler=command_validate)

    merge_parser = subparsers.add_parser(
        "merge", help="overlay a partial recording onto a base corpus")
    merge_parser.add_argument("--base", required=True, help="base corpus zip or directory")
    merge_parser.add_argument("--overlay", required=True,
                              help="freshly recorded corpus zip or directory")
    merge_parser.add_argument("--out", required=True, help="merged corpus directory")
    merge_parser.set_defaults(handler=command_merge)

    pack_parser = subparsers.add_parser("pack", help="zip a corpus + .sha256 sidecar")
    pack_parser.add_argument("--corpus", required=True, help="corpus directory")
    pack_parser.add_argument("--out", required=True, help="output zip path")
    pack_parser.set_defaults(handler=command_pack)

    fetch_parser = subparsers.add_parser(
        "fetch", help="download the active corpus release asset")
    fetch_parser.add_argument("--dest", required=True, help="destination zip path")
    fetch_parser.add_argument("--repo", default=DEFAULT_GITHUB_REPO)
    fetch_parser.add_argument("--tag", default=DEFAULT_RELEASE_TAG)
    fetch_parser.add_argument("--asset", default=DEFAULT_ASSET_NAME)
    fetch_parser.add_argument("--attempts", type=int, default=4)
    fetch_parser.add_argument("--retry-delay-seconds", type=int, default=20)
    fetch_parser.set_defaults(handler=command_fetch)

    list_parser = subparsers.add_parser("list", help="print scenario ids")
    list_parser.add_argument("--filter", default=None, help="glob over scenario ids")
    list_parser.set_defaults(handler=command_list)

    return parser


def main():
    args = build_parser().parse_args()
    try:
        return args.handler(args)
    except ScenarioError as error:
        print(f"scenario execution failed: {error}")
        return EXIT_FAILURE


if __name__ == "__main__":
    sys.exit(main())
