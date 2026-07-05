#!/usr/bin/env python3
"""
Drill into anchors of a captured/rendered page: per-anchor ref-vs-got
geometry, the element's opening tag, and child listings — the step after
`corpus.py` (or `compare.py`) points at an anchor id.

    inspect.py <page-dir> a0042 a0043        # anchor detail
    inspect.py <page-dir> --children a0042   # anchored children, both sides
    inspect.py <page-dir> --parents a0042    # anchored ancestor chain

<page-dir> holds fixture.html + ref.json (+ dump.tsv, as corpus.py leaves
behind; pass --dump for a different box dump). Attribute noise (jslog,
jsaction, ...) is stripped from printed tags.
"""
import argparse
import json
import os
import re
import sys

NOISE_ATTRS_RE = re.compile(
    r'\s(?:jslog|jsdata|jsaction|jscontroller|jsmodel|jsname|jsrenderer|'
    r'jsshadow|data-p|ved|data-ved|srcset)="[^"]*"')


def load_boxes(dump_path):
    """anchor_id -> (x, y, w, h, "ws/hs" size-provenance suffix)."""
    boxes = {}
    for line in open(dump_path, encoding="utf-8", errors="replace"):
        columns = line.split("\t")
        if len(columns) < 7 or columns[2] in ("-", ""):
            continue
        try:
            rect = tuple(float(value) for value in columns[3:7])
        except ValueError:
            continue
        width_source = height_source = ""
        for token in columns[7:]:
            if token.startswith("ws="):
                width_source = token[3:]
            elif token.startswith("hs="):
                height_source = token[3:]
        provenance = ("w:%s h:%s" % (width_source, height_source)
                      if width_source or height_source else "")
        boxes[columns[2]] = rect + (provenance,)
    return boxes


def opening_tag(html, anchor_id, max_len=300):
    position = html.find('data-test="%s"' % anchor_id)
    if position < 0:
        return "(not in fixture)"
    start = html.rfind("<", 0, position)
    end = html.find(">", position)
    if start < 0 or end < 0:
        return "(unparsable)"
    tag = NOISE_ATTRS_RE.sub("", html[start:end + 1])
    return tag if len(tag) <= max_len else tag[:max_len] + "..."


def fmt(rect):
    if rect is None:
        return "MISSING"
    if len(rect) == 5:
        return "(%.1f,%.1f %.1fx%.1f) %s" % rect
    return "(%.1f,%.1f %.1fx%.1f)" % tuple(rect)


def print_anchor(anchor_id, anchors, boxes, html):
    entry = anchors.get(anchor_id)
    if entry is None:
        print("%s: no such anchor" % anchor_id)
        return
    print("== %s <%s> display:%s parent:%s" %
          (anchor_id, entry["tag"], entry["display"], entry.get("parent")))
    print("   ref %s" % fmt(tuple(entry["rect"])))
    print("   got %s" % fmt(boxes.get(anchor_id)))
    print("   %s" % opening_tag(html, anchor_id))


def print_children(parent_id, anchors, boxes, html):
    print("== children of %s  %s" %
          (parent_id, opening_tag(html, parent_id, 120)))
    for anchor_id in sorted(anchors):
        entry = anchors[anchor_id]
        if entry.get("parent") != parent_id:
            continue
        print("   %-6s <%s> %-14s ref %-28s got %s" %
              (anchor_id, entry["tag"], "disp:" + entry["display"],
               fmt(tuple(entry["rect"])), fmt(boxes.get(anchor_id))))


def print_parents(anchor_id, anchors, boxes, html):
    chain = []
    current = anchor_id
    while current is not None:
        chain.append(current)
        current = anchors.get(current, {}).get("parent")
    print("== ancestor chain of %s (deepest first)" % anchor_id)
    for link_id in chain:
        entry = anchors.get(link_id, {})
        print("   %-6s <%s> %-14s ref %-28s got %s" %
              (link_id, entry.get("tag", "?"),
               "disp:" + entry.get("display", "?"),
               fmt(tuple(entry["rect"])) if "rect" in entry else "?",
               fmt(boxes.get(link_id))))
        print("          %s" % opening_tag(html, link_id, 160))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("page_dir",
                        help="directory with fixture.html + ref.json "
                             "(+ dump.tsv)")
    parser.add_argument("anchors", nargs="*", help="anchor ids (aNNNN)")
    parser.add_argument("--children", metavar="ID",
                        help="list anchored children of ID")
    parser.add_argument("--parents", metavar="ID",
                        help="list the anchored ancestor chain of ID")
    parser.add_argument("--dump",
                        help="box dump to compare against "
                             "(default <page-dir>/dump.tsv)")
    args = parser.parse_args()

    ref_path = os.path.join(args.page_dir, "ref.json")
    fixture_path = os.path.join(args.page_dir, "fixture.html")
    dump_path = args.dump or os.path.join(args.page_dir, "dump.tsv")
    try:
        anchors = json.load(open(ref_path, encoding="utf-8"))["anchors"]
        html = open(fixture_path, encoding="utf-8", errors="replace").read()
    except OSError as error:
        print("error: %s" % error, file=sys.stderr)
        return 2
    boxes = load_boxes(dump_path) if os.path.exists(dump_path) else {}
    if not boxes:
        print("note: no box dump at %s (run corpus.py first, or --dump)"
              % dump_path)

    if args.children:
        print_children(args.children, anchors, boxes, html)
    if args.parents:
        print_parents(args.parents, anchors, boxes, html)
    for anchor_id in args.anchors:
        print_anchor(anchor_id, anchors, boxes, html)
    if not (args.children or args.parents or args.anchors):
        print("nothing to do — pass anchor ids, --children or --parents")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
