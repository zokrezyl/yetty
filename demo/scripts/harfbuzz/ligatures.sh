#!/usr/bin/env bash
# HarfBuzz programming-ligature demo.
#
# The terminal grid font cannot shape, so a run of operator characters that
# forms a programming ligature (=>, !=, ===, ...) is suppressed on the grid
# and re-drawn as a single shaped glyph through the SDF free-position path
# against the bundled Fira Code face. Ordinary text stays on the crisp MSDF
# grid; only the ligature spans are shaped.
#
# Needs a build with YETTY_ENABLE_LIB_HARFBUZZ=ON. Without it the operators
# render as individual grid glyphs (no ligature) — still correct, just plain.

echo "=== HarfBuzz · programming ligatures (Fira Code) ==="
echo

echo "arrows        ->  <-  =>  -->  <--  ->>  <<-  <->  <==>  <-->"
echo "comparison    ==  !=  ===  !==  >=  <=  <>  =/="
echo "logic / bits  &&  ||  |>  <|  <<  >>  |||"
echo "comments      //  ///  /*  */  ##  ###"
echo "misc          ::  :::  ..  ...  ++  --  **  :=  =~  !~  ://  </>"
echo

echo "--- in context ---"
echo 'let f = (x) => x != 0 && x >= 1;'
echo 'if a == b || c /= d { ptr->next; }'
echo 'x |> map |> filter   // pipeline'
echo 'range = 0 ..< n       /* half-open */'
echo
echo "=== end ==="
