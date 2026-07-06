# Vendored fzy matching core

The fuzzy-matching algorithm from [fzy](https://github.com/jhawthorn/fzy)
(MIT, see `LICENSE`): `match.c`, `match.h`, `bonus.h`, taken from the
upstream `master` branch's `src/`. fzy ships as a standalone TTY program,
not a library, so the matcher is vendored the same way `picohttpparser/`
is.

Local changes, kept minimal so upstream refreshes stay a copy:

- `match.c` / `bonus.h`: `#include "../config.h"` → `#include "config.h"`.
- `config.h` here is the scoring-constant subset of upstream's
  `src/config.def.h` (the rest of that file configures the fzy TTY
  program, which is not vendored).

API (see `match.h`): `has_match(needle, haystack)` for filtering,
`match(needle, haystack)` for a ranking score (`score_t`, higher is
better), `match_positions()` additionally reports which haystack
characters matched (useful for highlighting later).

Used by yai for the slash-command completion menu and history browsing;
candidates for later: transcript/message search, /resume session pickers.
