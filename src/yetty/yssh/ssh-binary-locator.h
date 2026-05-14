#ifndef YETTY_YSSH_SSH_BINARY_LOCATOR_H
#define YETTY_YSSH_SSH_BINARY_LOCATOR_H

/*
 * Resolves the path to the ssh binary to use.
 *
 * Resolution order:
 *   1. YETTY_SSH_BINARY env var (development / override)
 *   2. <exe_dir>/../libexec/yetty/openssh/ssh (installed tree)
 *   3. /usr/bin/ssh (system fallback — logs a warning)
 *
 * The returned pointer is valid for the lifetime of the process.
 * The first successful resolution is cached; subsequent calls are free.
 */
const char *yssh_resolve_ssh_binary(void);

#endif /* YETTY_YSSH_SSH_BINARY_LOCATOR_H */
