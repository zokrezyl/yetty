/*
 * dissect.h — protocol dissection model for ynet.
 *
 * A dissection turns one raw packet (link-layer bytes + a libpcap DLT_* link
 * type) into a stack of layers, each carrying named fields with byte
 * offset/length so the same model drives BOTH the protocol-detail tree and the
 * hex-view highlight. It also yields Wireshark-style row-summary columns
 * (protocol / source / destination / info) and a machine-readable 5-tuple used
 * for flow aggregation.
 *
 * The dissector is pure (no libpcap, no yclass): easy to unit-test and reuse.
 */
#ifndef YETTY_YNET_DISSECT_H
#define YETTY_YNET_DISSECT_H

#include <stddef.h>
#include <stdint.h>

enum {
    YNET_MAX_LAYERS = 8,
    YNET_FIELDS_PER_LAYER = 24,
    YNET_FIELD_NAME_MAX = 48,
    YNET_FIELD_VALUE_MAX = 96,
    YNET_LAYER_TITLE_MAX = 160,
    YNET_PROTO_MAX = 16,
    YNET_ENDPOINT_MAX = 72,
    YNET_INFO_MAX = 200,
    YNET_ADDR_MAX = 16,
};

/* Address family for the L3 endpoints. Own enum so the dissector needs no
 * <sys/socket.h> — the values are arbitrary, not AF_INET/AF_INET6. */
enum ynet_addr_family {
    YNET_AF_NONE = 0,
    YNET_AF_INET = 4,
    YNET_AF_INET6 = 6,
};

struct ynet_field {
    char name[YNET_FIELD_NAME_MAX];
    char value[YNET_FIELD_VALUE_MAX];
    uint32_t offset; /* byte offset into the packet buffer */
    uint32_t length; /* byte length of the field */
};

struct ynet_layer {
    char title[YNET_LAYER_TITLE_MAX];
    struct ynet_field fields[YNET_FIELDS_PER_LAYER];
    uint32_t field_count;
    uint32_t offset; /* start of this layer within the packet */
    uint32_t length; /* bytes this layer spans */
};

struct ynet_dissection {
    struct ynet_layer layers[YNET_MAX_LAYERS];
    uint32_t layer_count;

    /* Row-summary columns. */
    char protocol[YNET_PROTO_MAX];
    char source[YNET_ENDPOINT_MAX];
    char destination[YNET_ENDPOINT_MAX];
    char info[YNET_INFO_MAX];

    /* Machine-readable 5-tuple for flow aggregation (best-effort: `family`
     * stays YNET_AF_NONE when no L3 was recognised). */
    enum ynet_addr_family family;
    uint8_t src_addr[YNET_ADDR_MAX];
    uint8_t dst_addr[YNET_ADDR_MAX];
    int has_ports;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t ip_protocol;
};

/* Dissect one packet. `linktype` is a libpcap DLT_* value. Never fails: on
 * malformed / truncated input it records what it can and stops (a trailing raw
 * "Data" layer covers any undissected remainder). `out` is fully overwritten. */
void ynet_dissect(const uint8_t *data, uint32_t len, int linktype, struct ynet_dissection *out);

/* Format a raw L3 address into `buf` (IPv4 dotted-quad / IPv6 colon-hex). */
void ynet_format_address(enum ynet_addr_family family, const uint8_t *addr, char *buf,
                         size_t buf_size);

#endif /* YETTY_YNET_DISSECT_H */
