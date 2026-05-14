/*
 * Windows tty abstraction — Windows yetty doesn't have a `yetty -e`
 * style PTY-share scenario, so the stderr-rerouting probe is a
 * no-op. The function still exists with the same signature so client
 * tools (ygreeter, yplot, …) can call it unconditionally.
 *
 * See include/yetty/yplatform/tty.h for the cross-platform contract.
 */

#include <yetty/yplatform/tty.h>

struct yetty_yplatform_tty_redirected_result
yetty_yplatform_tty_redirect_stderr_if_shared_with_stdout(const char *basename)
{
    (void)basename;
    return YETTY_OK(yetty_yplatform_tty_redirected, 0);
}
