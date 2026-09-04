#!/usr/bin/env bash
#
# Derive the per-variant installer bootstrap scripts from install.sh and
# install.ps1: install-min.{sh,ps1} and install-max.{sh,ps1} are the same
# scripts with the default variant pinned, so there is exactly one bootstrap
# to maintain. The site build runs this to populate yetty.dev; run it by hand
# to inspect the results.
#
# Usage:
#   build-tools/install/make-variants.sh <outdir>
#
# Writes into <outdir>: install.sh, install.ps1 (verbatim copies) and
# install-min.sh, install-max.sh, install-min.ps1, install-max.ps1.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
outdir="${1:-}"
[ -n "$outdir" ] || { echo "usage: $0 <outdir>" >&2; exit 2; }
mkdir -p "$outdir"

# The single line each script reads its pinned variant from. Both scripts
# carry it in exactly this shape; a script that drifts fails loudly here.
readonly sh_marker='^variant_default="default"$'
readonly ps1_marker="^\\\$variantDefault = 'default'\$"

grep -q "$sh_marker" "$here/install.sh" || {
    echo "make-variants: install.sh lost its 'variant_default=\"default\"' line" >&2
    exit 1
}
grep -q "$ps1_marker" "$here/install.ps1" || {
    echo "make-variants: install.ps1 lost its \"\$variantDefault = 'default'\" line" >&2
    exit 1
}

cp "$here/install.sh" "$outdir/install.sh"
cp "$here/install.ps1" "$outdir/install.ps1"

for variant in min max; do
    sed "s/$sh_marker/variant_default=\"$variant\"/" "$here/install.sh" > "$outdir/install-$variant.sh"
    grep -q "^variant_default=\"$variant\"\$" "$outdir/install-$variant.sh" || {
        echo "make-variants: failed to pin variant '$variant' in install-$variant.sh" >&2
        exit 1
    }
    chmod +x "$outdir/install-$variant.sh"

    sed "s/$ps1_marker/\$variantDefault = '$variant'/" "$here/install.ps1" > "$outdir/install-$variant.ps1"
    grep -q "^\\\$variantDefault = '$variant'\$" "$outdir/install-$variant.ps1" || {
        echo "make-variants: failed to pin variant '$variant' in install-$variant.ps1" >&2
        exit 1
    }
done

echo "make-variants: wrote install{,-min,-max}.{sh,ps1} to $outdir"
