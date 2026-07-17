/*
 * yrich keymap — semantic commands + remappable modal bindings.
 *
 * Pins: default + vi bindings resolve; the same key means different commands
 * in different modes; bindings are remappable and removable at runtime; and
 * command<->name round-trips (for config / scripting).
 */

#include <yetty/yrich/yrich-keymap.h>

#include "ytest.h"

static void test_default_bindings(struct ytest *test)
{
    struct yetty_yrich_keymap keymap;
    yetty_yrich_keymap_init(&keymap);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_keymap_load_defaults(&keymap)));

    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                 YETTY_YRICH_KEY_S, YETTY_YRICH_MOD_CTRL),
                       YETTY_YRICH_CMD_SAVE);
    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                 YETTY_YRICH_KEY_B, YETTY_YRICH_MOD_CTRL),
                       YETTY_YRICH_CMD_TOGGLE_BOLD);
    /* An unbound chord resolves to NONE. */
    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                 YETTY_YRICH_KEY_Q, YETTY_YRICH_MOD_CTRL),
                       YETTY_YRICH_CMD_NONE);

    yetty_yrich_keymap_clear(&keymap);
}

static void test_modal_isolation(struct ytest *test)
{
    struct yetty_yrich_keymap keymap;
    yetty_yrich_keymap_init(&keymap);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_keymap_load_defaults(&keymap)));

    /* Bare 'h' is a caret motion in vi-normal, but unbound in default mode. */
    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_VI_NORMAL,
                                                 YETTY_YRICH_KEY_H, 0),
                       YETTY_YRICH_CMD_CARET_LEFT);
    YTEST_CHECK_EQ_INT(
        test, yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_H, 0),
        YETTY_YRICH_CMD_NONE);
    /* 'i' enters insert mode from normal; Escape leaves insert mode. */
    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_VI_NORMAL,
                                                 YETTY_YRICH_KEY_I, 0),
                       YETTY_YRICH_CMD_MODE_VI_INSERT);
    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_VI_INSERT,
                                                 YETTY_YRICH_KEY_ESCAPE, 0),
                       YETTY_YRICH_CMD_MODE_VI_NORMAL);

    yetty_yrich_keymap_clear(&keymap);
}

static void test_remap_and_unbind(struct ytest *test)
{
    struct yetty_yrich_keymap keymap;
    yetty_yrich_keymap_init(&keymap);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_keymap_load_defaults(&keymap)));

    /* Rebind Ctrl+S from SAVE to REDO — remappable to whatever key. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_keymap_bind(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                            YETTY_YRICH_KEY_S, YETTY_YRICH_MOD_CTRL,
                                                            YETTY_YRICH_CMD_REDO)));
    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                 YETTY_YRICH_KEY_S, YETTY_YRICH_MOD_CTRL),
                       YETTY_YRICH_CMD_REDO);

    /* Bind a brand-new chord, then remove it with CMD_NONE. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_keymap_bind(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                            YETTY_YRICH_KEY_P, YETTY_YRICH_MOD_CTRL,
                                                            YETTY_YRICH_CMD_SAVE)));
    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                 YETTY_YRICH_KEY_P, YETTY_YRICH_MOD_CTRL),
                       YETTY_YRICH_CMD_SAVE);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_keymap_bind(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                            YETTY_YRICH_KEY_P, YETTY_YRICH_MOD_CTRL,
                                                            YETTY_YRICH_CMD_NONE)));
    YTEST_CHECK_EQ_INT(test,
                       yetty_yrich_keymap_lookup(&keymap, YETTY_YRICH_MODE_DEFAULT,
                                                 YETTY_YRICH_KEY_P, YETTY_YRICH_MOD_CTRL),
                       YETTY_YRICH_CMD_NONE);

    yetty_yrich_keymap_clear(&keymap);
}

static void test_command_name_roundtrip(struct ytest *test)
{
    YTEST_CHECK_STR_EQ(test, yetty_yrich_command_name(YETTY_YRICH_CMD_SAVE), "doc.save");
    YTEST_CHECK_STR_EQ(test, yetty_yrich_command_name(YETTY_YRICH_CMD_TOGGLE_BOLD), "format.bold");
    YTEST_CHECK_STR_EQ(test, yetty_yrich_command_name(YETTY_YRICH_CMD_NONE), "none");
    YTEST_CHECK_EQ_INT(test, yetty_yrich_command_from_name("doc.save"), YETTY_YRICH_CMD_SAVE);
    YTEST_CHECK_EQ_INT(test, yetty_yrich_command_from_name("mode.vi_insert"),
                       YETTY_YRICH_CMD_MODE_VI_INSERT);
    YTEST_CHECK_EQ_INT(test, yetty_yrich_command_from_name("nope.nope"), YETTY_YRICH_CMD_NONE);
}

int main(void)
{
    struct ytest test = ytest_begin("yrich_keymap");
    YTEST_RUN(&test, test_default_bindings);
    YTEST_RUN(&test, test_modal_isolation);
    YTEST_RUN(&test, test_remap_and_unbind);
    YTEST_RUN(&test, test_command_name_roundtrip);
    return ytest_end(&test);
}
