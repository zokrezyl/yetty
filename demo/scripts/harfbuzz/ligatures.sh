#!/usr/bin/env bash
# HarfBuzz programming-ligature demo.
#
# A programming ligature (=>, !=, ===, ...) is shaped through Fira Code's calt
# tables, which keep one glyph per cell — each operator character is substituted
# with a ligature-piece glyph that tiles with its neighbours. Each piece is
# drawn directly by the grid shader as an ordinary one-cell glyph from the
# bundled Fira Code raster face. Ordinary text stays on the crisp MSDF grid;
# only the ligature spans use the Fira Code face.
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
