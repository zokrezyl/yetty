#!/usr/bin/env python3
"""
Compare ybrowser's rendered anchor geometry against a Chrome-generated
reference (ref.json from make-fixture.py).

Design (why this is not a naive rect diff):

  * PARENT-RELATIVE positions. Each anchor's x/y is compared relative to its
    nearest anchored ancestor, on both sides. One wrong height near the top of
    the page then stays ONE red anchor instead of shifting every absolute
    coordinate below it red.

  * STRUCTURE before coordinates. For every container with >= 2 anchored
    children, the children are clustered into rows on both sides and the
    (row count, max items per row) shape is compared. "3-column grid became a
    6-row stack" is reported as one structure finding, which is the class of
    huge visual divergence this suite exists to catch.

  * FIRST DIVERGENCE. Findings are reported in document order and a finding
    whose ancestor already has one is suppressed into a count — the actionable
    output is the topmost element where the two engines start to disagree.

Tolerances are deliberately loose (this suite hunts layout-class bugs, not
±1px drift — the hand WPT suite owns exactness):
    position:  x ±8px, y ±16px   (relative to the parent anchor)
    width:     ±max(8px, 3%)
    height:    ±max(16px, 10%)   (text-metric slack: fonts differ)
    doc height ratio outside [0.8, 1.25] is a global finding

  * CLUSTERS. After the finding list, root-cause findings are grouped by
    signature (kind + tag + display + element class) and printed by
    count x magnitude — one line per *mechanism* instead of one line per
    anchor, which is what makes a 700-finding page triageable.

Usage:
    compare.py <ref.json> <dump.tsv>     # dump.tsv from ybrowser --dump-boxes
    compare.py <ref.json> -              # dump on stdin
Options: --limit N (top findings shown, default 15), --json <path>,
         --fixture <fixture.html> (element classes in findings/clusters),
         --clusters N (cluster lines shown, default 10; 0 disables).
Exit: 0 clean, 1 findings, 2 usage/input error.
"""
import argparse
import json
import os
import re
import sys

TOL_X = 8.0
TOL_Y = 16.0
TOL_W_PX, TOL_W_REL = 8.0, 0.03
TOL_H_PX, TOL_H_REL = 16.0, 0.10
DOC_HEIGHT_RATIO_LOW, DOC_HEIGHT_RATIO_HIGH = 0.8, 1.25
ROW_CLUSTER_MIN_GAP = 8.0

# Anchors whose reference box is essentially zero-area carry no signal
# (empty spacer divs, display:contents wrappers) — ignored entirely.
ZERO_AREA_LIMIT = 1.0


ANCHOR_TAG_RE = re.compile(r'<[a-zA-Z][^>]*?data-test="(a\d+)"[^>]*>')
CLASS_ATTR_RE = re.compile(r'class="([^"]*)"')


def load_classes(fixture_path):
    """One-pass anchor_id -> class-attribute map from a stamped fixture.
    Lets findings and clusters name the element (`<div .EctEBd>`) without
    re-capturing — refs recorded before `cls` was added stay usable."""
    classes = {}
    try:
        html = open(fixture_path, encoding="utf-8", errors="replace").read()
    except OSError:
        return classes
    for match in ANCHOR_TAG_RE.finditer(html):
        cls_match = CLASS_ATTR_RE.search(match.group(0))
        if cls_match:
            classes[match.group(1)] = cls_match.group(1).strip()
    return classes


def parse_dump(text):
    """Parse `ybrowser --dump-boxes` TSV into
    ({anchor_id: (x, y, w, h)}, {anchor_id: (width_source, height_source)}).

    Later boxes win on a duplicate name, matching the hand-WPT runner's
    behaviour (an element contributes one principal box; fragments carry no
    data-test). The sources are the engine's size-provenance names
    (`ws=`/`hs=` columns) — which layout decision produced the number.
    """
    boxes = {}
    sources = {}
    for line in text.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        cols = line.split("\t")
        if len(cols) < 7:
            continue
        name = cols[2]
        if not name or name == "-":
            continue
        try:
            boxes[name] = (float(cols[3]), float(cols[4]),
                           float(cols[5]), float(cols[6]))
        except ValueError:
            continue
        width_source = height_source = "?"
        for token in cols[7:]:
            if token.startswith("ws="):
                width_source = token[3:]
            elif token.startswith("hs="):
                height_source = token[3:]
        sources[name] = (width_source, height_source)
    return boxes, sources


def cluster_rows(rects):
    """Group child rects into visual rows by top edge; returns list of rows
    (each a list of rects), sorted top to bottom."""
    ordered = sorted(rects, key=lambda rect: (rect[1], rect[0]))
    heights = sorted(rect[3] for rect in ordered)
    median_height = heights[len(heights) // 2] if heights else 0.0
    threshold = max(ROW_CLUSTER_MIN_GAP, 0.4 * median_height)
    rows = []
    row_top = None
    for rect in ordered:
        if row_top is None or rect[1] - row_top > threshold:
            rows.append([rect])
            row_top = rect[1]
        else:
            rows[-1].append(rect)
    return rows


def shape_of(rects):
    rows = cluster_rows(rects)
    return len(rows), max(len(row) for row in rows)


class Comparison:
    def __init__(self, reference, boxes, classes=None, sources=None):
        self.reference = reference
        self.anchors = reference["anchors"]
        self.boxes = boxes
        # anchor_id -> (width_source, height_source): the engine's size
        # provenance, i.e. WHICH layout decision produced the number.
        self.sources = sources or {}
        # anchor_id -> class attribute. Reference entries recorded with
        # `cls` win; a fixture-derived map (load_classes) fills the rest.
        self.classes = dict(classes or {})
        for anchor_id, entry in self.anchors.items():
            if entry.get("cls"):
                self.classes[anchor_id] = entry["cls"]
        # Anchors worth comparing at all: non-zero reference area, and not
        # parked offscreen (screen-reader-only nodes at x:-9999 — their
        # geometry is meaningless and their placement order differs
        # between engines, producing phantom structure flips).
        self.active = {
            anchor_id: entry for anchor_id, entry in self.anchors.items()
            if (entry["rect"][2] >= ZERO_AREA_LIMIT
                or entry["rect"][3] >= ZERO_AREA_LIMIT)
            and entry["rect"][0] + entry["rect"][2] > 0
            and entry["rect"][1] + entry["rect"][3] > 0
        }
        self.findings = {}   # anchor_id -> [message, ...]
        self.stats = {"anchors": len(self.active), "missing": 0,
                      "structure": 0, "geometry": 0}

    def add(self, anchor_id, kind, message, delta=0.0):
        self.findings.setdefault(anchor_id, []).append((kind, message, delta))
        self.stats["missing" if kind == "missing"
                   else "structure" if kind == "structure"
                   else "geometry"] += 1

    def kinds_of(self, anchor_id):
        return {kind for kind, message, delta in self.findings.get(anchor_id, ())}

    def element_label(self, anchor_id):
        """`<tag .firstClass>` — how findings/clusters name an element."""
        entry = self.anchors.get(anchor_id, {})
        tag = entry.get("tag", "?")
        cls = self.classes.get(anchor_id, "")
        first_cls = cls.split()[0] if cls.split() else ""
        return "<%s%s>" % (tag, " ." + first_cls if first_cls else "")

    def anchored_ancestor_present_in_both(self, anchor_id):
        """Nearest ancestor anchor that produced a box on both sides."""
        parent_id = self.anchors[anchor_id].get("parent")
        while parent_id is not None:
            if parent_id in self.boxes and parent_id in self.active:
                return parent_id
            parent_id = self.anchors[parent_id].get("parent")
        return None

    def run(self):
        self.check_presence_and_geometry()
        self.check_structure()
        self.check_doc_height()
        return self.findings

    def check_presence_and_geometry(self):
        for anchor_id in sorted(self.active):
            entry = self.active[anchor_id]
            got = self.boxes.get(anchor_id)
            if got is None:
                self.add(anchor_id, "missing",
                         "no box in ybrowser (ref %s %s display:%s)"
                         % (fmt_rect(entry["rect"]),
                            self.element_label(anchor_id),
                            entry["display"]))
                continue
            ref_rect = entry["rect"]

            ancestor_id = self.anchored_ancestor_present_in_both(anchor_id)
            if ancestor_id is not None:
                ref_base = self.anchors[ancestor_id]["rect"]
                got_base = self.boxes[ancestor_id]
            else:
                ref_base = (0.0, 0.0, 0.0, 0.0)
                got_base = (0.0, 0.0, 0.0, 0.0)

            delta_x = abs((got[0] - got_base[0]) - (ref_rect[0] - ref_base[0]))
            delta_y = abs((got[1] - got_base[1]) - (ref_rect[1] - ref_base[1]))
            delta_w = abs(got[2] - ref_rect[2])
            delta_h = abs(got[3] - ref_rect[3])

            relative_to = ("relative to %s" % ancestor_id
                           if ancestor_id else "absolute")
            label = self.element_label(anchor_id)
            width_source, height_source = self.sources.get(anchor_id,
                                                           ("?", "?"))
            if delta_w > max(TOL_W_PX, TOL_W_REL * ref_rect[2]):
                self.add(anchor_id, "width",
                         "width %g vs ref %g (Δ%.0f) %s display:%s src=%s"
                         % (got[2], ref_rect[2], delta_w,
                            label, entry["display"], width_source), delta_w)
            if delta_h > max(TOL_H_PX, TOL_H_REL * ref_rect[3]):
                self.add(anchor_id, "height",
                         "height %g vs ref %g (Δ%.0f) %s display:%s src=%s"
                         % (got[3], ref_rect[3], delta_h,
                            label, entry["display"], height_source), delta_h)
            if delta_x > TOL_X:
                self.add(anchor_id, "pos",
                         "x offset Δ%.0f %s %s"
                         % (delta_x, relative_to, label), delta_x)
            if delta_y > TOL_Y:
                self.add(anchor_id, "pos",
                         "y offset Δ%.0f %s %s"
                         % (delta_y, relative_to, label), delta_y)

    def check_structure(self):
        children_of = {}
        for anchor_id, entry in self.active.items():
            parent_id = entry.get("parent")
            if parent_id is not None:
                children_of.setdefault(parent_id, []).append(anchor_id)
        for parent_id in sorted(children_of):
            child_ids = [child_id for child_id in children_of[parent_id]
                         if child_id in self.boxes]
            if len(child_ids) < 2:
                continue
            ref_shape = shape_of(
                [self.anchors[child_id]["rect"] for child_id in child_ids])
            got_shape = shape_of(
                [self.boxes[child_id] for child_id in child_ids])
            if ref_shape != got_shape:
                parent_entry = self.anchors.get(parent_id, {})
                self.add(parent_id, "structure",
                         "children shape ref %d rows x %d cols vs got "
                         "%d rows x %d cols (%d children) %s display:%s"
                         % (ref_shape[0], ref_shape[1], got_shape[0],
                            got_shape[1], len(child_ids),
                            self.element_label(parent_id),
                            parent_entry.get("display", "?")))

    def check_doc_height(self):
        # Content height from the anchors themselves on BOTH sides —
        # scrollHeight floors at the viewport height on short pages, so the
        # recorded meta value cannot be compared against box bottoms.
        if not self.boxes or not self.active:
            return
        ref_height = max(entry["rect"][1] + entry["rect"][3]
                         for entry in self.active.values())
        got_height = max(rect[1] + rect[3] for rect in self.boxes.values())
        if ref_height <= 0:
            return
        ratio = got_height / ref_height
        if ratio < DOC_HEIGHT_RATIO_LOW or ratio > DOC_HEIGHT_RATIO_HIGH:
            self.add("doc", "structure",
                     "content height %.0f vs ref %.0f (x%.2f)"
                     % (got_height, ref_height, ratio))


def fmt_rect(rect):
    return "(%g,%g %gx%g)" % tuple(rect)


def root_cause_findings(comparison):
    """Fold derived findings by how each defect propagates through layout:

      * a STRUCTURE or MISSING ancestor makes descendant geometry
        meaningless -> fold everything beneath it;
      * WIDTH flows parent -> child: fold a width finding when an ancestor
        already has one (the topmost wrong width is the cause);
      * HEIGHT flows child -> parent: fold a height finding when a
        DESCENDANT also has one (the deepest wrong height is the cause);
      * POS findings are parent-relative already and never folded.

    Returns (kept, folded_count) with kept entries
    (anchor_id, kind, message, delta) in document order."""
    findings = comparison.findings
    anchors = comparison.anchors
    if not findings:
        return [], 0

    children_of = {}
    for anchor_id, entry in anchors.items():
        parent_id = entry.get("parent")
        if parent_id is not None:
            children_of.setdefault(parent_id, []).append(anchor_id)

    def ancestors_of(anchor_id):
        parent_id = anchors.get(anchor_id, {}).get("parent")
        while parent_id is not None:
            yield parent_id
            parent_id = anchors[parent_id].get("parent")

    def subtree_has_height_finding(anchor_id):
        pending = list(children_of.get(anchor_id, ()))
        while pending:
            current = pending.pop()
            if "height" in comparison.kinds_of(current):
                return True
            pending.extend(children_of.get(current, ()))
        return False

    kept = []
    folded = 0
    for anchor_id in sorted(findings):
        ancestor_kinds = set()
        if anchor_id != "doc":
            for ancestor_id in ancestors_of(anchor_id):
                ancestor_kinds |= comparison.kinds_of(ancestor_id)
        if ancestor_kinds & {"structure", "missing"}:
            folded += len(findings[anchor_id])
            continue
        for kind, message, delta in findings[anchor_id]:
            if kind == "width" and "width" in ancestor_kinds:
                folded += 1
            elif kind == "height" and anchor_id != "doc" \
                    and subtree_has_height_finding(anchor_id):
                folded += 1
            else:
                kept.append((anchor_id, kind, message, delta))
    return kept, folded


def cluster_findings(comparison, kept):
    """Group root-cause findings by mechanism signature
    (kind, tag, display, first class, size provenance). Returns cluster
    dicts sorted by count desc — each line is one *divergence mechanism*
    with exemplar anchors to start reducing from, and `src` names the
    engine decision that produced the wrong number."""
    groups = {}
    for anchor_id, kind, message, delta in kept:
        if anchor_id == "doc":
            continue
        entry = comparison.anchors.get(anchor_id, {})
        cls = comparison.classes.get(anchor_id, "")
        first_cls = cls.split()[0] if cls.split() else ""
        width_source, height_source = comparison.sources.get(anchor_id,
                                                             ("?", "?"))
        source = (width_source if kind == "width"
                  else height_source if kind == "height" else "")
        key = (kind, entry.get("tag", "?"), entry.get("display", "?"),
               first_cls, source)
        group = groups.setdefault(key, {"count": 0, "total_delta": 0.0,
                                        "anchors": []})
        group["count"] += 1
        group["total_delta"] += delta
        if len(group["anchors"]) < 3:
            group["anchors"].append(anchor_id)
    clusters = []
    for (kind, tag, display, first_cls, source), group in groups.items():
        clusters.append({
            "kind": kind, "tag": tag, "display": display, "cls": first_cls,
            "source": source,
            "count": group["count"], "total_delta": group["total_delta"],
            "anchors": group["anchors"],
        })
    clusters.sort(key=lambda c: (-c["count"], -c["total_delta"]))
    return clusters


def report(comparison, limit, cluster_limit=10):
    """Print root-cause findings in document order, then the cluster
    summary. Returns the number of findings kept."""
    kept, folded = root_cause_findings(comparison)
    for index, (anchor_id, kind, message, delta) in enumerate(kept):
        if index >= limit:
            print("    ... %d more findings" % (len(kept) - index))
            break
        print("    %-6s [%-9s] %s" % (anchor_id, kind, message))
    if folded:
        print("    (%d derived finding(s) folded into the above)" % folded)
    if cluster_limit > 0 and kept:
        clusters = cluster_findings(comparison, kept)
        print("    -- clusters (root causes by mechanism) --")
        for cluster in clusters[:cluster_limit]:
            element = "<%s%s>" % (cluster["tag"],
                                  " ." + cluster["cls"] if cluster["cls"]
                                  else "")
            source = ("src=%s" % cluster["source"]) if cluster["source"] \
                else ""
            print("    %4dx %-9s %-28s display:%-12s %-15s ΣΔ%-7.0f e.g. %s"
                  % (cluster["count"], cluster["kind"], element,
                     cluster["display"], source, cluster["total_delta"],
                     " ".join(cluster["anchors"])))
        if len(clusters) > cluster_limit:
            print("    ... %d more clusters" % (len(clusters) - cluster_limit))
    return len(kept)


def compare_texts(reference, dump_text, classes=None):
    """Library entry for run.py / corpus.py: returns the Comparison."""
    boxes, sources = parse_dump(dump_text)
    comparison = Comparison(reference, boxes, classes, sources)
    comparison.run()
    return comparison


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("ref", help="ref.json from make-fixture.py")
    parser.add_argument("dump", help="ybrowser --dump-boxes output, or -")
    parser.add_argument("--limit", type=int, default=15)
    parser.add_argument("--clusters", type=int, default=10,
                        help="cluster-summary lines (0 disables)")
    parser.add_argument("--fixture",
                        help="stamped fixture.html — element classes in "
                             "findings and clusters (default: fixture.html "
                             "next to the ref when present)")
    parser.add_argument("--json", dest="json_out",
                        help="also write findings as JSON to this path")
    args = parser.parse_args()

    try:
        reference = json.load(open(args.ref, encoding="utf-8"))
    except (OSError, ValueError) as error:
        print("error reading %s: %s" % (args.ref, error), file=sys.stderr)
        return 2
    # The dump's trailing snippet column may cut a multibyte sequence in
    # half — decode tolerantly, the geometry columns are pure ASCII.
    dump_text = sys.stdin.buffer.read().decode("utf-8", errors="replace") \
        if args.dump == "-" \
        else open(args.dump, encoding="utf-8", errors="replace").read()

    fixture_path = args.fixture
    if fixture_path is None:
        sibling = os.path.join(os.path.dirname(os.path.abspath(args.ref)),
                               "fixture.html")
        fixture_path = sibling if os.path.exists(sibling) else None
    classes = load_classes(fixture_path) if fixture_path else None

    comparison = compare_texts(reference, dump_text, classes)
    stats = comparison.stats
    print("%d anchors: %d missing, %d structure, %d geometry findings"
          % (stats["anchors"], stats["missing"], stats["structure"],
             stats["geometry"]))
    report(comparison, args.limit, args.clusters)

    if args.json_out:
        kept, folded = root_cause_findings(comparison)
        with open(args.json_out, "w", encoding="utf-8") as handle:
            json.dump({"stats": stats,
                       "clusters": cluster_findings(comparison, kept),
                       "findings": {anchor_id: [
                           {"kind": kind, "message": message, "delta": delta}
                           for kind, message, delta in messages]
                           for anchor_id, messages in
                           comparison.findings.items()}},
                      handle, indent=1, sort_keys=True)
            handle.write("\n")
    return 1 if comparison.findings else 0


if __name__ == "__main__":
    sys.exit(main())
