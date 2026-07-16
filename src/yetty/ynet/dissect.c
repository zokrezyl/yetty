/*
 * dissect.c — packet dissectors for ynet (Ethernet / Linux-SLL / raw-IP link
 * layers; IPv4, IPv6, TCP, UDP, ICMP/ICMPv6, ARP, DNS).
 *
 * Deliberately dependency-free (no libpcap, no yclass) so it is trivially
 * unit-testable and reusable. Every multi-byte read is bounds-checked against
 * the captured length before it happens; a short packet stops dissection early
 * rather than reading past the buffer.
 *
 * First release scope: enough to make a capture legible, not Wireshark-level
 * coverage. Unknown next-protocols surface as a raw "Data" layer.
 */
#include "dissect.h"

#include <stdio.h>
#include <string.h>

/* libpcap DLT_* link types we understand. Defined locally so this TU needs no
 * pcap.h; the values are the stable libpcap constants. */
enum {
    YNET_DLT_NULL = 0,       /* BSD loopback: 4-byte host-order address family */
    YNET_DLT_EN10MB = 1,     /* Ethernet II */
    YNET_DLT_RAW_BSD = 12,   /* raw IP (BSD numbering) */
    YNET_DLT_RAW_LINUX = 101, /* raw IP (Linux numbering) */
    YNET_DLT_LINUX_SLL = 113, /* Linux "cooked" capture (tcpdump -i any) */
};

enum {
    YNET_ETHERTYPE_IPV4 = 0x0800,
    YNET_ETHERTYPE_ARP = 0x0806,
    YNET_ETHERTYPE_VLAN = 0x8100,
    YNET_ETHERTYPE_IPV6 = 0x86DD,
};

enum {
    YNET_IP_PROTO_ICMP = 1,
    YNET_IP_PROTO_TCP = 6,
    YNET_IP_PROTO_UDP = 17,
    YNET_IP_PROTO_ICMPV6 = 58,
};

/* Network-byte-order reads. Caller guarantees the bytes are in range. */
static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)((bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

/*=============================================================================
 * Layer / field builders
 *===========================================================================*/

static struct ynet_layer *layer_begin(struct ynet_dissection *out, uint32_t offset,
                                       const char *title)
{
    if (out->layer_count >= YNET_MAX_LAYERS) {
        return NULL;
    }
    struct ynet_layer *layer = &out->layers[out->layer_count++];
    memset(layer, 0, sizeof(*layer));
    layer->offset = offset;
    snprintf(layer->title, sizeof(layer->title), "%s", title);
    return layer;
}

static void layer_set_title(struct ynet_layer *layer, const char *title)
{
    if (layer) {
        snprintf(layer->title, sizeof(layer->title), "%s", title);
    }
}

/* Add a field. `value` is a preformatted string. offset/length point into the
 * packet for the hex-view highlight. */
static void field_add(struct ynet_layer *layer, const char *name, uint32_t offset, uint32_t length,
                      const char *value)
{
    if (!layer || layer->field_count >= YNET_FIELDS_PER_LAYER) {
        return;
    }
    struct ynet_field *field = &layer->fields[layer->field_count++];
    snprintf(field->name, sizeof(field->name), "%s", name);
    snprintf(field->value, sizeof(field->value), "%s", value ? value : "");
    field->offset = offset;
    field->length = length;
}

static void field_add_uint(struct ynet_layer *layer, const char *name, uint32_t offset,
                           uint32_t length, uint64_t value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    field_add(layer, name, offset, length, buffer);
}

/*=============================================================================
 * Address formatting
 *===========================================================================*/

void ynet_format_address(enum ynet_addr_family family, const uint8_t *addr, char *buf,
                         size_t buf_size)
{
    if (buf_size == 0) {
        return;
    }
    if (family == YNET_AF_INET) {
        snprintf(buf, buf_size, "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
    } else if (family == YNET_AF_INET6) {
        /* Full (non-compressed) form — unambiguous and enough for M1. */
        snprintf(buf, buf_size, "%x:%x:%x:%x:%x:%x:%x:%x", read_be16(addr + 0), read_be16(addr + 2),
                 read_be16(addr + 4), read_be16(addr + 6), read_be16(addr + 8), read_be16(addr + 10),
                 read_be16(addr + 12), read_be16(addr + 14));
    } else {
        buf[0] = '\0';
    }
}

static void format_mac(const uint8_t *mac, char *buf, size_t buf_size)
{
    snprintf(buf, buf_size, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
}

/*=============================================================================
 * Transport & application layers
 *===========================================================================*/

static void set_endpoints_with_ports(struct ynet_dissection *out, uint16_t src_port,
                                      uint16_t dst_port)
{
    char address[YNET_ENDPOINT_MAX];
    /* IPv6 colons collide with the port separator — bracket the address, as
     * Wireshark does: [2001:db8::1]:443. */
    const char *open = out->family == YNET_AF_INET6 ? "[" : "";
    const char *close = out->family == YNET_AF_INET6 ? "]" : "";
    ynet_format_address(out->family, out->src_addr, address, sizeof(address));
    snprintf(out->source, sizeof(out->source), "%s%s%s:%u", open, address, close, src_port);
    ynet_format_address(out->family, out->dst_addr, address, sizeof(address));
    snprintf(out->destination, sizeof(out->destination), "%s%s%s:%u", open, address, close,
             dst_port);
    out->has_ports = 1;
    out->src_port = src_port;
    out->dst_port = dst_port;
}

static void dissect_dns(const uint8_t *data, uint32_t len, uint32_t offset, uint32_t avail,
                        struct ynet_dissection *out);

static void dissect_udp(const uint8_t *data, uint32_t len, uint32_t offset,
                        struct ynet_dissection *out)
{
    if (offset + 8 > len) {
        return;
    }
    uint16_t src_port = read_be16(data + offset);
    uint16_t dst_port = read_be16(data + offset + 2);
    uint16_t udp_len = read_be16(data + offset + 4);

    struct ynet_layer *layer = layer_begin(out, offset, "User Datagram Protocol");
    field_add_uint(layer, "Source Port", offset, 2, src_port);
    field_add_uint(layer, "Destination Port", offset + 2, 2, dst_port);
    field_add_uint(layer, "Length", offset + 4, 2, udp_len);
    field_add_uint(layer, "Checksum", offset + 6, 2, read_be16(data + offset + 6));
    if (layer) {
        layer->length = 8;
    }

    snprintf(out->protocol, sizeof(out->protocol), "UDP");
    set_endpoints_with_ports(out, src_port, dst_port);
    snprintf(out->info, sizeof(out->info), "%u \xE2\x86\x92 %u  Len=%u", src_port, dst_port,
             (unsigned)(udp_len > 8 ? udp_len - 8 : 0));

    uint32_t payload = offset + 8;
    if ((src_port == 53 || dst_port == 53) && payload < len) {
        dissect_dns(data, len, payload, len - payload, out);
    }
}

static void tcp_flags_string(uint8_t flags, char *buf, size_t buf_size)
{
    /* Order matches Wireshark's [ ] summary. */
    static const struct {
        uint8_t bit;
        const char *name;
    } table[] = {
        {0x01, "FIN"}, {0x02, "SYN"}, {0x04, "RST"},
        {0x08, "PSH"}, {0x10, "ACK"}, {0x20, "URG"},
    };
    buf[0] = '\0';
    size_t used = 0;
    for (size_t index = 0; index < sizeof(table) / sizeof(table[0]); index++) {
        if (flags & table[index].bit) {
            int written = snprintf(buf + used, buf_size - used, "%s%s", used ? ", " : "",
                                   table[index].name);
            if (written > 0) {
                used += (size_t)written;
            }
        }
    }
}

static void dissect_tcp(const uint8_t *data, uint32_t len, uint32_t offset,
                        struct ynet_dissection *out)
{
    if (offset + 20 > len) {
        return;
    }
    uint16_t src_port = read_be16(data + offset);
    uint16_t dst_port = read_be16(data + offset + 2);
    uint32_t seq = read_be32(data + offset + 4);
    uint32_t ack = read_be32(data + offset + 8);
    uint8_t data_offset_words = (uint8_t)(data[offset + 12] >> 4);
    uint8_t flags = (uint8_t)(data[offset + 13] & 0x3F);
    uint16_t window = read_be16(data + offset + 14);
    uint32_t header_len = (uint32_t)data_offset_words * 4;
    if (header_len < 20) {
        header_len = 20;
    }

    char flags_str[64];
    tcp_flags_string(flags, flags_str, sizeof(flags_str));

    struct ynet_layer *layer = layer_begin(out, offset, "Transmission Control Protocol");
    field_add_uint(layer, "Source Port", offset, 2, src_port);
    field_add_uint(layer, "Destination Port", offset + 2, 2, dst_port);
    field_add_uint(layer, "Sequence Number", offset + 4, 4, seq);
    field_add_uint(layer, "Acknowledgment Number", offset + 8, 4, ack);
    field_add_uint(layer, "Header Length", offset + 12, 1, header_len);
    field_add(layer, "Flags", offset + 13, 1, flags_str[0] ? flags_str : "none");
    field_add_uint(layer, "Window", offset + 14, 2, window);
    field_add_uint(layer, "Checksum", offset + 16, 2, read_be16(data + offset + 16));
    if (layer) {
        layer->length = header_len;
    }

    uint32_t payload_len = (offset + header_len <= len) ? len - offset - header_len : 0;
    snprintf(out->protocol, sizeof(out->protocol), "TCP");
    set_endpoints_with_ports(out, src_port, dst_port);
    if (flags & 0x10) { /* ACK present → show Ack= */
        snprintf(out->info, sizeof(out->info), "%u \xE2\x86\x92 %u [%s] Seq=%u Ack=%u Win=%u Len=%u",
                 src_port, dst_port, flags_str[0] ? flags_str : "none", seq, ack, window,
                 payload_len);
    } else {
        snprintf(out->info, sizeof(out->info), "%u \xE2\x86\x92 %u [%s] Seq=%u Win=%u Len=%u",
                 src_port, dst_port, flags_str[0] ? flags_str : "none", seq, window, payload_len);
    }

    uint32_t payload = offset + header_len;
    if ((src_port == 53 || dst_port == 53) && payload + 2 < len) {
        /* DNS-over-TCP is length-prefixed by 2 bytes. */
        dissect_dns(data, len, payload + 2, len - payload - 2, out);
    }
}

static void dissect_icmp(const uint8_t *data, uint32_t len, uint32_t offset, int is_v6,
                         struct ynet_dissection *out)
{
    if (offset + 4 > len) {
        return;
    }
    uint8_t type = data[offset];
    uint8_t code = data[offset + 1];

    const char *description = "";
    if (!is_v6) {
        switch (type) {
        case 0: description = "Echo (ping) reply"; break;
        case 3: description = "Destination unreachable"; break;
        case 8: description = "Echo (ping) request"; break;
        case 11: description = "Time-to-live exceeded"; break;
        default: description = "ICMP"; break;
        }
    } else {
        switch (type) {
        case 128: description = "Echo (ping) request"; break;
        case 129: description = "Echo (ping) reply"; break;
        case 133: description = "Router solicitation"; break;
        case 134: description = "Router advertisement"; break;
        case 135: description = "Neighbor solicitation"; break;
        case 136: description = "Neighbor advertisement"; break;
        default: description = "ICMPv6"; break;
        }
    }

    struct ynet_layer *layer =
        layer_begin(out, offset, is_v6 ? "Internet Control Message Protocol v6"
                                       : "Internet Control Message Protocol");
    field_add_uint(layer, "Type", offset, 1, type);
    field_add_uint(layer, "Code", offset + 1, 1, code);
    field_add_uint(layer, "Checksum", offset + 2, 2, read_be16(data + offset + 2));
    if (layer) {
        layer->length = 4;
    }

    snprintf(out->protocol, sizeof(out->protocol), is_v6 ? "ICMPv6" : "ICMP");
    snprintf(out->info, sizeof(out->info), "%s (type=%u code=%u)", description, type, code);
}

/* Parse a DNS name starting at `pos`; follows a single level of 0xC0
 * compression. Writes a dotted name into `buf`. Returns 0 on malformed input. */
static int dns_read_name(const uint8_t *data, uint32_t len, uint32_t pos, char *buf,
                         size_t buf_size)
{
    size_t used = 0;
    int jumps = 0;
    buf[0] = '\0';
    while (pos < len) {
        uint8_t label_len = data[pos];
        if (label_len == 0) {
            return 1;
        }
        if ((label_len & 0xC0) == 0xC0) {
            if (pos + 1 >= len || ++jumps > 8) {
                return 0;
            }
            pos = (uint32_t)(((label_len & 0x3F) << 8) | data[pos + 1]);
            continue;
        }
        pos++;
        if (pos + label_len > len) {
            return 0;
        }
        for (uint8_t index = 0; index < label_len; index++) {
            if (used + 2 < buf_size) {
                buf[used++] = (char)data[pos + index];
            }
        }
        pos += label_len;
        if (data[pos] != 0 && used + 1 < buf_size) {
            buf[used++] = '.';
        }
    }
    buf[used < buf_size ? used : buf_size - 1] = '\0';
    return 1;
}

static const char *dns_type_name(uint16_t type)
{
    switch (type) {
    case 1: return "A";
    case 2: return "NS";
    case 5: return "CNAME";
    case 6: return "SOA";
    case 12: return "PTR";
    case 15: return "MX";
    case 16: return "TXT";
    case 28: return "AAAA";
    case 33: return "SRV";
    case 255: return "ANY";
    default: return "?";
    }
}

static void dissect_dns(const uint8_t *data, uint32_t len, uint32_t offset, uint32_t avail,
                        struct ynet_dissection *out)
{
    if (avail < 12) {
        return;
    }
    uint16_t transaction_id = read_be16(data + offset);
    uint16_t flags = read_be16(data + offset + 2);
    uint16_t questions = read_be16(data + offset + 4);
    uint16_t answers = read_be16(data + offset + 6);
    int is_response = (flags & 0x8000) != 0;

    struct ynet_layer *layer = layer_begin(out, offset, "Domain Name System");
    char hex_id[16];
    snprintf(hex_id, sizeof(hex_id), "0x%04x", transaction_id);
    field_add(layer, "Transaction ID", offset, 2, hex_id);
    field_add(layer, "Type", offset + 2, 2, is_response ? "response" : "query");
    field_add_uint(layer, "Questions", offset + 4, 2, questions);
    field_add_uint(layer, "Answer RRs", offset + 6, 2, answers);

    char name[128] = "";
    uint16_t qtype = 0;
    if (questions > 0) {
        uint32_t question_pos = offset + 12;
        if (dns_read_name(data, len, question_pos, name, sizeof(name))) {
            /* Advance past the name to read QTYPE. */
            uint32_t walk = question_pos;
            while (walk < len && data[walk] != 0) {
                if ((data[walk] & 0xC0) == 0xC0) {
                    walk += 2;
                    goto have_qtype;
                }
                walk += (uint32_t)data[walk] + 1;
            }
            walk += 1; /* the zero label */
        have_qtype:
            if (walk + 2 <= len) {
                qtype = read_be16(data + walk);
            }
            field_add(layer, "Query Name", question_pos, 0, name);
            field_add(layer, "Query Type", walk, 2, dns_type_name(qtype));
        }
    }

    snprintf(out->protocol, sizeof(out->protocol), "DNS");
    if (is_response) {
        snprintf(out->info, sizeof(out->info), "Standard query response %s %s %s", hex_id,
                 dns_type_name(qtype), name);
    } else {
        snprintf(out->info, sizeof(out->info), "Standard query %s %s %s", hex_id,
                 dns_type_name(qtype), name);
    }
    (void)avail;
}

/*=============================================================================
 * Network layer
 *===========================================================================*/

static void dispatch_l4(const uint8_t *data, uint32_t len, uint32_t offset, uint8_t protocol,
                        struct ynet_dissection *out)
{
    out->ip_protocol = protocol;
    switch (protocol) {
    case YNET_IP_PROTO_TCP: dissect_tcp(data, len, offset, out); break;
    case YNET_IP_PROTO_UDP: dissect_udp(data, len, offset, out); break;
    case YNET_IP_PROTO_ICMP: dissect_icmp(data, len, offset, 0, out); break;
    case YNET_IP_PROTO_ICMPV6: dissect_icmp(data, len, offset, 1, out); break;
    default: break;
    }
}

static void dissect_ipv4(const uint8_t *data, uint32_t len, uint32_t offset,
                         struct ynet_dissection *out)
{
    if (offset + 20 > len) {
        return;
    }
    uint8_t version_ihl = data[offset];
    uint32_t header_len = (uint32_t)(version_ihl & 0x0F) * 4;
    if (header_len < 20 || offset + header_len > len) {
        return;
    }
    uint16_t total_len = read_be16(data + offset + 2);
    uint8_t ttl = data[offset + 8];
    uint8_t protocol = data[offset + 9];

    out->family = YNET_AF_INET;
    memcpy(out->src_addr, data + offset + 12, 4);
    memcpy(out->dst_addr, data + offset + 16, 4);

    char src[YNET_ENDPOINT_MAX];
    char dst[YNET_ENDPOINT_MAX];
    ynet_format_address(YNET_AF_INET, out->src_addr, src, sizeof(src));
    ynet_format_address(YNET_AF_INET, out->dst_addr, dst, sizeof(dst));

    char title[YNET_LAYER_TITLE_MAX];
    snprintf(title, sizeof(title), "Internet Protocol Version 4, Src: %s, Dst: %s", src, dst);
    struct ynet_layer *layer = layer_begin(out, offset, title);
    field_add_uint(layer, "Version", offset, 1, 4);
    field_add_uint(layer, "Header Length", offset, 1, header_len);
    field_add_uint(layer, "Total Length", offset + 2, 2, total_len);
    field_add_uint(layer, "Time to Live", offset + 8, 1, ttl);
    field_add_uint(layer, "Protocol", offset + 9, 1, protocol);
    field_add(layer, "Source", offset + 12, 4, src);
    field_add(layer, "Destination", offset + 16, 4, dst);
    if (layer) {
        layer->length = header_len;
    }

    snprintf(out->protocol, sizeof(out->protocol), "IPv4");
    snprintf(out->source, sizeof(out->source), "%s", src);
    snprintf(out->destination, sizeof(out->destination), "%s", dst);

    dispatch_l4(data, len, offset + header_len, protocol, out);
}

static void dissect_ipv6(const uint8_t *data, uint32_t len, uint32_t offset,
                         struct ynet_dissection *out)
{
    if (offset + 40 > len) {
        return;
    }
    uint16_t payload_len = read_be16(data + offset + 4);
    uint8_t next_header = data[offset + 6];
    uint8_t hop_limit = data[offset + 7];

    out->family = YNET_AF_INET6;
    memcpy(out->src_addr, data + offset + 8, 16);
    memcpy(out->dst_addr, data + offset + 24, 16);

    char src[YNET_ENDPOINT_MAX];
    char dst[YNET_ENDPOINT_MAX];
    ynet_format_address(YNET_AF_INET6, out->src_addr, src, sizeof(src));
    ynet_format_address(YNET_AF_INET6, out->dst_addr, dst, sizeof(dst));

    char title[YNET_LAYER_TITLE_MAX];
    snprintf(title, sizeof(title), "Internet Protocol Version 6, Src: %s, Dst: %s", src, dst);
    struct ynet_layer *layer = layer_begin(out, offset, title);
    field_add_uint(layer, "Version", offset, 1, 6);
    field_add_uint(layer, "Payload Length", offset + 4, 2, payload_len);
    field_add_uint(layer, "Next Header", offset + 6, 1, next_header);
    field_add_uint(layer, "Hop Limit", offset + 7, 1, hop_limit);
    field_add(layer, "Source", offset + 8, 16, src);
    field_add(layer, "Destination", offset + 24, 16, dst);
    if (layer) {
        layer->length = 40;
    }

    snprintf(out->protocol, sizeof(out->protocol), "IPv6");
    snprintf(out->source, sizeof(out->source), "%s", src);
    snprintf(out->destination, sizeof(out->destination), "%s", dst);

    /* M1 does not walk IPv6 extension headers — a recognised upper protocol
     * dissects directly; anything else falls through to the summary only. */
    dispatch_l4(data, len, offset + 40, next_header, out);
}

static void dissect_arp(const uint8_t *data, uint32_t len, uint32_t offset,
                        struct ynet_dissection *out)
{
    if (offset + 8 > len) {
        return;
    }
    uint16_t operation = read_be16(data + offset + 6);
    struct ynet_layer *layer = layer_begin(out, offset, "Address Resolution Protocol");
    field_add(layer, "Opcode", offset + 6, 2, operation == 1 ? "request" : "reply");
    snprintf(out->protocol, sizeof(out->protocol), "ARP");
    snprintf(out->info, sizeof(out->info), "%s", operation == 1 ? "ARP request" : "ARP reply");
}

static void dissect_ethertype(const uint8_t *data, uint32_t len, uint32_t offset,
                              uint16_t ethertype, struct ynet_dissection *out)
{
    switch (ethertype) {
    case YNET_ETHERTYPE_IPV4: dissect_ipv4(data, len, offset, out); break;
    case YNET_ETHERTYPE_IPV6: dissect_ipv6(data, len, offset, out); break;
    case YNET_ETHERTYPE_ARP: dissect_arp(data, len, offset, out); break;
    default: break;
    }
}

/*=============================================================================
 * Link layer + entry point
 *===========================================================================*/

static void dissect_ethernet(const uint8_t *data, uint32_t len, struct ynet_dissection *out)
{
    if (len < 14) {
        return;
    }
    char src[24];
    char dst[24];
    format_mac(data + 0, dst, sizeof(dst));
    format_mac(data + 6, src, sizeof(src));
    uint16_t ethertype = read_be16(data + 12);
    uint32_t offset = 14;

    struct ynet_layer *layer = layer_begin(out, 0, "Ethernet II");
    field_add(layer, "Destination", 0, 6, dst);
    field_add(layer, "Source", 6, 6, src);

    /* One 802.1Q VLAN tag, if present. */
    if (ethertype == YNET_ETHERTYPE_VLAN && len >= 18) {
        field_add_uint(layer, "VLAN ID", 14, 2, read_be16(data + 14) & 0x0FFF);
        ethertype = read_be16(data + 16);
        offset = 18;
    }
    field_add_uint(layer, "Type", offset - 2, 2, ethertype);
    if (layer) {
        layer->length = offset;
    }
    dissect_ethertype(data, len, offset, ethertype, out);
}

static void dissect_linux_sll(const uint8_t *data, uint32_t len, struct ynet_dissection *out)
{
    if (len < 16) {
        return;
    }
    uint16_t protocol = read_be16(data + 14);
    struct ynet_layer *layer = layer_begin(out, 0, "Linux cooked capture");
    field_add_uint(layer, "Protocol", 14, 2, protocol);
    if (layer) {
        layer->length = 16;
    }
    dissect_ethertype(data, len, 16, protocol, out);
}

static void dissect_null_loopback(const uint8_t *data, uint32_t len, struct ynet_dissection *out)
{
    if (len < 4) {
        return;
    }
    /* BSD loopback family is written in host byte order; 2 (AF_INET) may appear
     * as 0x02000000. Accept either endianness. */
    uint32_t family = read_be32(data);
    struct ynet_layer *layer = layer_begin(out, 0, "Null/Loopback");
    if (layer) {
        layer->length = 4;
    }
    if (family == 2 || family == 0x02000000) {
        dissect_ipv4(data, len, 4, out);
    } else {
        dissect_ipv6(data, len, 4, out);
    }
}

static void dissect_raw_ip(const uint8_t *data, uint32_t len, struct ynet_dissection *out)
{
    if (len < 1) {
        return;
    }
    uint8_t version = (uint8_t)(data[0] >> 4);
    if (version == 4) {
        dissect_ipv4(data, len, 0, out);
    } else if (version == 6) {
        dissect_ipv6(data, len, 0, out);
    }
}

void ynet_dissect(const uint8_t *data, uint32_t len, int linktype, struct ynet_dissection *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->protocol, sizeof(out->protocol), "?");
    if (!data || len == 0) {
        return;
    }

    switch (linktype) {
    case YNET_DLT_EN10MB: dissect_ethernet(data, len, out); break;
    case YNET_DLT_LINUX_SLL: dissect_linux_sll(data, len, out); break;
    case YNET_DLT_NULL: dissect_null_loopback(data, len, out); break;
    case YNET_DLT_RAW_BSD:
    case YNET_DLT_RAW_LINUX: dissect_raw_ip(data, len, out); break;
    default:
        /* Unknown link type: still show the frame exists. */
        layer_begin(out, 0, "Unknown link-layer");
        break;
    }

    /* Fill any empty summary columns so the row is never blank. */
    if (out->info[0] == '\0') {
        snprintf(out->info, sizeof(out->info), "%u bytes", (unsigned)len);
    }
    if (out->layer_count == 0) {
        layer_begin(out, 0, "Data");
    }
    (void)layer_set_title;
}
