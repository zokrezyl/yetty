#include "ssh-binary-locator.h"

#include <yetty/ytrace/ytrace.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static char s_ssh_path[PATH_MAX];

/* Writes the directory of the running executable into buf (no trailing slash).
 * Returns 0 on success, -1 if the path cannot be determined. */
static int resolve_exe_dir(char *buf, size_t size)
{
    char exe[PATH_MAX];

#ifdef __linux__
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0 || (size_t)n >= sizeof(exe)) {
        return -1;
    }
    exe[n] = '\0';
#elif defined(__APPLE__)
    uint32_t exe_size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &exe_size) != 0) {
        return -1;
    }
#else
    (void)buf;
    (void)size;
    return -1;
#endif

    char *slash = strrchr(exe, '/');
    if (!slash || slash == exe) {
        return -1;
    }
    *slash = '\0';

    size_t dir_len = (size_t)(slash - exe);
    if (dir_len + 1 > size) {
        return -1;
    }
    memcpy(buf, exe, dir_len + 1);
    return 0;
}

const char *yssh_resolve_ssh_binary(void)
{
    if (s_ssh_path[0]) {
        return s_ssh_path;
    }

    const char *env = getenv("YETTY_SSH_BINARY");
    if (env && env[0]) {
        snprintf(s_ssh_path, sizeof(s_ssh_path), "%s", env);
        return s_ssh_path;
    }

    char exe_dir[PATH_MAX];
    if (resolve_exe_dir(exe_dir, sizeof(exe_dir)) == 0) {
        snprintf(s_ssh_path, sizeof(s_ssh_path),
                 "%s/../libexec/yetty/openssh/ssh", exe_dir);
        if (access(s_ssh_path, X_OK) == 0) {
            return s_ssh_path;
        }
        s_ssh_path[0] = '\0';
    }

    ywarn("ssh: vendored ssh binary not found; falling back to /usr/bin/ssh");
    snprintf(s_ssh_path, sizeof(s_ssh_path), "/usr/bin/ssh");
    return s_ssh_path;
}
