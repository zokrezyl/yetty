#ifndef YETTY_YVTERM_OSC_ARGS_H
#define YETTY_YVTERM_OSC_ARGS_H

#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_OSC_ARGS_MAX 32

struct yetty_yvterm_osc_arg {
    const char *key;   /* Points into original string (not owned) */
    const char *value; /* Points into original string, NULL for flags */
    size_t key_len;
    size_t value_len;
};

struct yetty_yvterm_osc_args {
    struct yetty_yvterm_osc_arg items[YETTY_OSC_ARGS_MAX];
    size_t count;
    char *buf;           /* Working buffer (owned, null-terminated copy) */
    const char *payload; /* Pointer to payload after args (in original data) */
    size_t payload_len;
};

/**
 * Parse OSC args from "args;payload" format.
 * Args format: "--flag --key=value key2=value2"
 *
 * @param args Output struct (caller allocates)
 * @param data Input data (args;payload)
 * @param len Input length
 * @return 0 on success, -1 on error
 */
int yetty_yvterm_osc_args_parse(struct yetty_yvterm_osc_args *args, const char *data, size_t len);

/**
 * Free internal buffer.
 */
void yetty_yvterm_osc_args_free(struct yetty_yvterm_osc_args *args);

/**
 * Check if flag exists (e.g., "--yaml", "--clear").
 */
int yetty_yvterm_osc_args_has(const struct yetty_yvterm_osc_args *args, const char *key);

/**
 * Get value for key. Returns NULL if not found.
 * Returned pointer valid until args_free().
 */
const char *yetty_yvterm_osc_args_get(const struct yetty_yvterm_osc_args *args, const char *key);

/**
 * Get value as int. Returns default_val if not found or not a number.
 */
int yetty_yvterm_osc_args_get_int(const struct yetty_yvterm_osc_args *args, const char *key,
                                  int default_val);

/**
 * Get payload as a yetty_ycore_buffer_result.
 */
struct yetty_ycore_buffer_result yetty_yvterm_osc_args_get_payload_buffer(
    const struct yetty_yvterm_osc_args *args);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_OSC_ARGS_H */
