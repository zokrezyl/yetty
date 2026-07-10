/* impl/libc/kqueue.c — slice-router for FreeBSD kqueue / kevent.
 *
 * The two host backends share nothing at all (different syscalls,
 * different state, different shape conversion), so this translation
 * unit is intentionally empty:
 *
 *   kqueue-darwin.c   native kqueue passthrough — macos / ios / tvos /
 *                     ios-sim / freebsd (selected by meson when
 *                     host_machine.system() == 'darwin' or 'freebsd').
 *   kqueue-linux.c    epoll-backed emulation — linux hosts.
 *
 * Both slices export the same `yos_kqueue*`, `yos_kevent*` and
 * `yos_proc_watches_init / _notify_exit` symbol set; main.c sees only
 * one of them at link time depending on the platform meson chose.
 */
