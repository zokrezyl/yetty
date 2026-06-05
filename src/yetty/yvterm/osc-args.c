/* osc-args.c - OSC argument parser */

#include <yetty/yvterm/osc-args.h>
#include <stdlib.h>
#include <string.h>

int yetty_yvterm_osc_args_parse(struct yetty_yvterm_osc_args *args, const char *data, size_t len)
{
    const char *sep;
    size_t args_len;
    char *p, *end;

    if (!args || !data) {
        return -1;
    }

    memset(args, 0, sizeof(*args));

    /* Find semicolon separating args from payload */
    sep = memchr(data, ';', len);
    if (sep) {
        args_len = sep - data;
        args->payload = sep + 1;
        args->payload_len = len - args_len - 1;
    } else {
        args_len = len;
        args->payload = NULL;
        args->payload_len = 0;
    }

    if (args_len == 0) {
        return 0;
    }

    /* Copy args portion for tokenization */
    args->buf = malloc(args_len + 1);
    if (!args->buf) {
        return -1;
    }
    memcpy(args->buf, data, args_len);
    args->buf[args_len] = '\0';

    /* Tokenize by whitespace */
    p = args->buf;
    end = args->buf + args_len;

    while (p < end && args->count < YETTY_OSC_ARGS_MAX) {
        char *token_start, *token_end, *eq;

        /* Skip whitespace */
        while (p < end && (*p == ' ' || *p == '\t')) {
            p++;
        }
        if (p >= end) {
            break;
        }

        token_start = p;

        /* Find end of token */
        while (p < end && *p != ' ' && *p != '\t') {
            p++;
        }
        token_end = p;

        if (token_start == token_end) {
            continue;
        }

        /* Null-terminate token */
        if (p < end) {
            *p++ = '\0';
        }

        /* Parse token: --key=value, --flag, or key=value */
        struct yetty_yvterm_osc_arg *arg = &args->items[args->count];

        if (token_start[0] == '-' && token_start[1] == '-') {
            /* --key or --key=value */
            arg->key = token_start + 2;
            eq = strchr(arg->key, '=');
            if (eq) {
                arg->key_len = eq - arg->key;
                *eq = '\0';
                arg->value = eq + 1;
                arg->value_len = strlen(arg->value);
            } else {
                arg->key_len = strlen(arg->key);
                arg->value = NULL;
                arg->value_len = 0;
            }
        } else {
            /* key=value */
            eq = strchr(token_start, '=');
            if (eq) {
                arg->key = token_start;
                arg->key_len = eq - token_start;
                *eq = '\0';
                arg->value = eq + 1;
                arg->value_len = strlen(arg->value);
            } else {
                /* Bare word - treat as flag */
                arg->key = token_start;
                arg->key_len = strlen(token_start);
                arg->value = NULL;
                arg->value_len = 0;
            }
        }

        args->count++;
    }

    return 0;
}

void yetty_yvterm_osc_args_free(struct yetty_yvterm_osc_args *args)
{
    if (args) {
        free(args->buf);
        args->buf = NULL;
        args->count = 0;
    }
}

int yetty_yvterm_osc_args_has(const struct yetty_yvterm_osc_args *args, const char *key)
{
    size_t i, key_len;

    if (!args || !key) {
        return 0;
    }

    key_len = strlen(key);
    for (i = 0; i < args->count; i++) {
        if (args->items[i].key_len == key_len && memcmp(args->items[i].key, key, key_len) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *yetty_yvterm_osc_args_get(const struct yetty_yvterm_osc_args *args, const char *key)
{
    size_t i, key_len;

    if (!args || !key) {
        return NULL;
    }

    key_len = strlen(key);
    for (i = 0; i < args->count; i++) {
        if (args->items[i].key_len == key_len && memcmp(args->items[i].key, key, key_len) == 0) {
            return args->items[i].value;
        }
    }
    return NULL;
}

int yetty_yvterm_osc_args_get_int(const struct yetty_yvterm_osc_args *args, const char *key,
                                 int default_val)
{
    const char *val = yetty_yvterm_osc_args_get(args, key);
    if (!val) {
        return default_val;
    }

    char *endptr;
    long v = strtol(val, &endptr, 10);
    if (*endptr != '\0') {
        return default_val;
    }

    return (int)v;
}

struct yetty_ycore_buffer_result yetty_yvterm_osc_args_get_payload_buffer(
    const struct yetty_yvterm_osc_args *args)
{
    if (!args || !args->payload || args->payload_len == 0) {
        return YETTY_ERR(yetty_ycore_buffer, "no payload");
    }

    struct yetty_ycore_buffer buf = {
        .data = (uint8_t *)args->payload, .size = args->payload_len, .capacity = args->payload_len};
    return YETTY_OK(yetty_ycore_buffer, buf);
}
