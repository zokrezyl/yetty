#ifndef YETTY_YSSH_OPENSSH_CONFIG_PARSER_H
#define YETTY_YSSH_OPENSSH_CONFIG_PARSER_H

#define YETTY_YSSH_OPENSSH_CONFIG_MAX_IDENTITY_FILES 8

/* Parsed result of ~/.ssh/config for one host_arg.
 *
 * Behaviour mirrors openssh: all matching Host blocks apply, first
 * occurrence of a keyword wins (except IdentityFile which accumulates).
 *
 * Unset string fields are NULL.
 * Unset integer fields:  port == 0,  all others == -1.
 *
 * Caller must free with yetty_yssh_openssh_config_free(). */
struct yetty_yssh_openssh_config {
    char *hostname;     /* HostName — real host to connect to */
    int   port;         /* Port (0 = not set) */
    char *user;         /* User */

    char *identity_files[YETTY_YSSH_OPENSSH_CONFIG_MAX_IDENTITY_FILES];
    int   num_identity_files;

    char *proxy_jump;               /* ProxyJump raw string */
    int   server_alive_interval;    /* ServerAliveInterval seconds (-1 = unset) */
    int   server_alive_count_max;   /* ServerAliveCountMax (-1 = unset) */
    int   connect_timeout;          /* ConnectTimeout seconds (-1 = unset) */
    char *known_hosts_file;         /* UserKnownHostsFile */
    int   strict_host_key_checking; /* -1=unset  0=no  1=yes  2=ask */
    int   forward_agent;            /* -1=unset  0=no  1=yes */
    int   batch_mode;               /* -1=unset  0=no  1=yes */
};

/* Resolve ~/.ssh/config then /etc/ssh/ssh_config for host_arg.
 * Returns NULL only on allocation failure. An all-unset config is valid.
 * Caller owns the result. */
struct yetty_yssh_openssh_config *yetty_yssh_openssh_config_resolve(const char *host_arg);

void yetty_yssh_openssh_config_free(struct yetty_yssh_openssh_config *cfg);

#endif /* YETTY_YSSH_OPENSSH_CONFIG_PARSER_H */
