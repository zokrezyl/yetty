# ymux engine contract test (#695 phase 1) — headless, no fork, no GPU, no
# yvterm. The link line doubles as the phase-1 closure proof: ymux + shared
# libvterm + core libs only; linking fails if the module ever grows a Dawn,
# renderer, yfigure, or yvterm dependency.
yetty_add_c_test(
    NAME    ymux_engine
    SOURCES engine-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)
target_compile_definitions(ymux_engine-test PRIVATE
    YMUX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

# ymux vtsink contract (#699.2): the client-side ordered terminal-byte sink —
# feed routes to the wired emit, tracks the applied generation, empty/no-emit
# feeds stay safe, and the generated feed slot dispatches BOTH locally and over
# a real RPC loopback (socketpair + server thread: proxy feed → wire → skel →
# impl → emit, reply = applied ACK, ordered).
yetty_add_c_test(
    NAME    ymux_vtsink
    SOURCES vtsink-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm Threads::Threads
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless rpc
    TIMEOUT 60)

# ymux tiered-history contract (#695 phase 2): hot/warm/cold round-trips
# with identity intact, row-cap floor advancement, reject semantics.
yetty_add_c_test(
    NAME    ymux_history
    SOURCES history-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless scrollback
    TIMEOUT 60)

# ymux pane contract (#695 phase 2): one timeline over history + live rows;
# logical ids survive the REAL scroll path into warm segments verbatim.
yetty_add_c_test(
    NAME    ymux_pane
    SOURCES pane-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# ymux attachment contract (#695 phase 2): follow vs stable anchoring,
# floor clamp, independent multi-attachment views/selections/generations.
yetty_add_c_test(
    NAME    ymux_attachment
    SOURCES attachment-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# ymux rich-store contract (#695 phase 2): stable ids, creation records,
# tagged anchors, lossless journal replay, stream mapping incl DELETE.
yetty_add_c_test(
    NAME    ymux_rich
    SOURCES rich-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 30)

# ymux projection contract (#695 phases 3-4 CPU): projector⇄surface loop —
# reconstruction parity after every chunk, FULL idempotence/staleness, delta
# base rules, quiescent no-op, anchored viewports, resync.
yetty_add_c_test(
    NAME    ymux_projection
    SOURCES projection-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# ymux session contract (#695 phase 5 model): pane/attachment ownership,
# first-eligible controller + canonical geometry, token resume, takeover,
# permission-gated input/resize, detach-with-output + reconnect.
yetty_add_c_test(
    NAME    ymux_session
    SOURCES session-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# ymux rich wire contract (#695 v1 rich): vendor DCS 600001 through the
# REAL pane byte path → store mint with stable cursor anchor → projector
# rich body (visible set, replay payloads, quiescence, tombstones/CLEAR).
yetty_add_c_test(
    NAME    ymux_rich_wire
    SOURCES rich-wire-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# #699 byte-parity: the ymux TTY emitter (tty-render.c) reproduces tmux's
# terminfo capability MODEL (review cycle 21): the tty-term.c/tty-features.c
# analog — compiled-terminfo loader, tparm-style expander, feature tokens,
# terminal-overrides (cap=value / cap@). Proves the model consumes real
# capability inputs and produces the same operations tmux would.
yetty_add_c_test(
    NAME    ymux_tty_term
    SOURCES tty-term-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# Figure-key envelope → terminal bytes: the ONE shared encoder both
# attach-bridge paths use (review cycle 21) — control chords (Ctrl-B!),
# Alt/meta prefixing, xterm modifier parameters, non-BMP UTF-8,
# exactly-once printable delivery.
yetty_add_c_test(
    NAME    ymux_key_encode
    SOURCES key-encode-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# tty_cursor output byte-for-byte for the fixed xterm-256color profile.
yetty_add_c_test(
    NAME    ymux_tty_render
    SOURCES tty-render-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# #699 cdx-2 item 2 — DECSLRM scoping proof. The daemon pane engine swallows an
# app's DEC ?69/DECSLRM (tmux-pane semantics; engine.c is the ONLY setter of the
# ignore flag), while the yscene RECEIVER libvterm keeps DECSLRM enabled so the
# compositor can address a partial-width client rectangle. Asserts the EFFECTIVE
# SCROLLREGION_LEFT/RIGHT the scroll/IL/DL/ICH paths consume, not a stored bit.
yetty_add_c_test(
    NAME    ymux_margin_scope
    SOURCES margin-scope-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# #695 slow-client TX policy (tx-queue.c): fully-sent leading-frame reclaim,
# selective terminal-redraw compaction that keeps a mid-flight leading frame and
# RPC/effect frames, and the strike predicate that never disconnects a draining
# (progressing) client. Pure buffer logic — no socket, no GPU.
yetty_add_c_test(
    NAME    ymux_tx_queue
    SOURCES tx-queue-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# #695 content-addressed resource store (resource.c): dedup heavy payloads by
# 64-bit content hash + full byte compare; refcount eviction.
yetty_add_c_test(
    NAME    ymux_resource
    SOURCES resource-test.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# ymux daemon⇄client contract (#695 phase 5): the full tmux loop over a
# REAL unix socket — attach/spawn, paint parity, input round-trip, hard
# client death + token reconnect, two-client controller policy, shutdown.
# The pane PTY is a memory pair (the test holds the application end).
yetty_add_c_test(
    NAME    ymux_daemon
    SOURCES daemon-test.c ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# REAL-ycat integration (#699 review cycle 19): the built ycat executable in a
# real forkpty pane of an in-process ymux session — envelope intake, row
# reservation, rich publication, and headless yscene application, end to end
# on committed fixtures. Pins the seam every synthetic-byte test misses (the
# tall-figure daemon SEGV and the zero-scene_max_y markdown bounds both hid
# there).
yetty_add_c_test(
    NAME    ymux_ycat_real
    SOURCES ycat-real-test.c ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yscene yetty_yfigure yetty_ydraw_list yetty_ysdf
            yetty_yclass yetty_ycore vterm lz4_static util
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux integration
    TIMEOUT 180)
set_tests_properties(ymux_ycat_real PROPERTIES ENVIRONMENT
    "YCAT_BINARY=$<TARGET_FILE:ycat>;YMUX_FIXTURE_DIR=${YETTY_ROOT}/test/fixtures/ymux")
add_dependencies(ymux_ycat_real-test ycat)

# #699 differential harness (YMUX half): emits the projector's VT redraw for
# stdin app-output. The tmux-parity-harness.py drives pinned tmux with the SAME
# input and compares — so the real ymux emitter is run and its complete stream
# compared, not substring-matched. As a bare ctest it reads empty stdin (no-op).
yetty_add_c_test(
    NAME    ymux_vtdiff_driver
    SOURCES vtdiff-driver.c
    LIBS    yetty_ymux yetty_api_ymux yetty_yclass yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract ymux headless
    TIMEOUT 60)

# #699 closure gate: FAIL if the ymux implementation includes or links yvterm.
# The independent libvterm+yscene endpoint is the only terminal engine ymux may
# use (review cycle 21) — this pins the architectural boundary in CI.
add_test(NAME ymux_no_yvterm_gate
    COMMAND ${YETTY_ROOT}/test/ut/ymux/no-yvterm-gate.sh)
set_tests_properties(ymux_no_yvterm_gate PROPERTIES
    ENVIRONMENT "YMUX_BINARY=$<TARGET_FILE:ymux>;YETTY_ROOT=${YETTY_ROOT}"
    LABELS "contract;ymux;headless"
    TIMEOUT 30)

# #699 MANDATORY byte-differential gate (review cycle 23): the FULL 77-case
# incremental matrix — the projector's real VT stream vs pinned tmux (d5afb67),
# byte-for-byte. This is the authoritative parity gate; res-nocsr-il and every
# no-CSR / cancellation case live here, not in the smaller smoke matrix.
# MANDATORY gate: incremental mode compares against the offline oracle. A
# missing oracle is a hard FAIL (return 1), so this test can NEVER pass as a
# skip without comparing a byte — deliberately NO SKIP_RETURN_CODE here.
add_test(NAME ymux_tmux_diff_incremental
    COMMAND ${Python3_EXECUTABLE} ${YETTY_ROOT}/test/ut/ymux/tmux-diff.py --incremental)
set_tests_properties(ymux_tmux_diff_incremental PROPERTIES
    ENVIRONMENT "YETTY_BUILD_DIR=${CMAKE_BINARY_DIR}"
    LABELS "contract;ymux;differential"
    TIMEOUT 300)

# MANDATORY attach-preamble gate: the projector's first full projection (attach
# preamble + blank redraw) vs the pinned tmux attach dump, byte-for-byte after
# stripping the documented outer-terminal segments. Offline oracle only.
add_test(NAME ymux_tmux_diff_preamble
    COMMAND ${Python3_EXECUTABLE} ${YETTY_ROOT}/test/ut/ymux/tmux-diff.py --preamble)
set_tests_properties(ymux_tmux_diff_preamble PROPERTIES
    ENVIRONMENT "YETTY_BUILD_DIR=${CMAKE_BINARY_DIR}"
    LABELS "contract;ymux;differential"
    TIMEOUT 300)

# MANDATORY synchronized-output gate: a sync-capable client's full-redraw
# preamble must wrap in DEC ?2026 h/l exactly where tmux's tty_sync_start/end do.
add_test(NAME ymux_tmux_diff_preamble_sync
    COMMAND ${Python3_EXECUTABLE} ${YETTY_ROOT}/test/ut/ymux/tmux-diff.py --preamble-sync)
set_tests_properties(ymux_tmux_diff_preamble_sync PROPERTIES
    ENVIRONMENT "YETTY_BUILD_DIR=${CMAKE_BINARY_DIR}"
    LABELS "contract;ymux;differential"
    TIMEOUT 300)

# MANDATORY horizontal-margins gate: a margin-capable client's preamble must
# enable DECLRMM (?69h) and establish full-screen left/right margins as tmux does.
add_test(NAME ymux_tmux_diff_preamble_margins
    COMMAND ${Python3_EXECUTABLE} ${YETTY_ROOT}/test/ut/ymux/tmux-diff.py --preamble-margins)
set_tests_properties(ymux_tmux_diff_preamble_margins PROPERTIES
    ENVIRONMENT "YETTY_BUILD_DIR=${CMAKE_BINARY_DIR}"
    LABELS "contract;ymux;differential"
    TIMEOUT 300)

# Non-mandatory developer-convenience LIVE differential: drives the pinned tmux
# binary and SELF-SKIPS (exit 77) when it is not built. This is where the skip
# lives now — never on the mandatory oracle gate above.
add_test(NAME ymux_tmux_diff_live
    COMMAND ${Python3_EXECUTABLE} ${YETTY_ROOT}/test/ut/ymux/tmux-diff.py)
set_tests_properties(ymux_tmux_diff_live PROPERTIES
    ENVIRONMENT "YETTY_BUILD_DIR=${CMAKE_BINARY_DIR}"
    LABELS "contract;ymux;differential"
    TIMEOUT 300
    SKIP_RETURN_CODE 77)
