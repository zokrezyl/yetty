/*
 * capture.c — yclass class `ynet:capture`: a loaded packet capture.
 *
 * A `capture` owns the packets read from a source (a pcap / pcapng file in M1;
 * a live libpcap interface in M2) plus the flow (conversation) aggregation
 * built while loading. It is the central object of the ynet module: the packet
 * table, protocol tree, hex view, and the coordinated graph / map / sequence
 * views are all projections of one capture.
 *
 * Being a yclass class, `make codegen` emits the public header (capture.h), the
 * method dispatch, model.yaml, and the FFI / host-language binding surface. The
 * only hand-written file is this annotated .c; capture.gen.c is #included at the
 * foot. Every method slot is `local@` — a capture is loaded and queried
 * in-process; the model still records the methods so bindings emit them.
 *
 * Packet bytes are copied into one growable arena; per-packet we cache only the
 * summary columns + the 5-tuple (cheap). The full protocol tree for a selected
 * packet is re-dissected on demand from the stored bytes — captures can be huge,
 * so caching a full dissection per packet is deliberately avoided.
 */
/* This TU deliberately does NOT include its own generated public header
 * (yetty/ynet/capture.h) — that header is a downstream artifact for other
 * modules. The foundational types this TU and the appended capture.gen.c need
 * are pulled in directly; the class handle Result wrapper plus the generated
 * accessor / downcast are declared here so the foot include and the slots have
 * them in scope. */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterminal/dcs-codes.h>

#include "dissect.h"

#include <math.h>
#include <pcap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-packet cached summary (the table columns). The full protocol tree is
 * re-dissected on demand, not stored here. */
struct ynet_packet_summary {
    char protocol[YNET_PROTO_MAX];
    char source[YNET_ENDPOINT_MAX];
    char destination[YNET_ENDPOINT_MAX];
    char info[YNET_INFO_MAX];
};

struct ynet_packet_record {
    uint64_t timestamp_seconds;
    uint32_t timestamp_microseconds;
    uint32_t captured_length;
    uint32_t original_length;
    size_t byte_offset; /* into capture->bytes */
    struct ynet_packet_summary summary;
};

/* A conversation between two endpoints, direction-independent. */
struct ynet_flow {
    enum ynet_addr_family family;
    uint8_t ip_protocol;
    int has_ports;
    uint8_t endpoint_a_addr[YNET_ADDR_MAX];
    uint8_t endpoint_b_addr[YNET_ADDR_MAX];
    uint16_t endpoint_a_port;
    uint16_t endpoint_b_port;
    uint64_t packet_count;
    uint64_t byte_count;
    char summary[YNET_INFO_MAX];
};

struct YETTY_ANNOTATE("class@ynet:capture")
    YETTY_ANNOTATE("include@yetty/ydraw-core/drawable-list.h") yetty_ynet_capture {
    char *path;
    int link_type; /* libpcap DLT_* */

    uint8_t *bytes; /* arena of all packet bytes */
    size_t bytes_length;
    size_t bytes_capacity;

    struct ynet_packet_record *records;
    uint32_t record_count;
    uint32_t record_capacity;

    struct ynet_flow *flows;
    uint32_t flow_count;
    uint32_t flow_capacity;

    uint64_t first_timestamp_seconds;
    uint32_t first_timestamp_microseconds;
    int have_first_timestamp;
};

/* Result wrapper for the capture handle. Declared here (not pulled from
 * capture.h, which this TU does not include) so the appended capture.gen.c —
 * which defines yetty_ynet_capture_from() returning it — has the type in scope.
 * The public capture.h publishes the identical declaration for consumers. */
YETTY_YRESULT_DECLARE(yetty_ynet_capture_ptr, struct yetty_ynet_capture *);

/* Defined in the appended capture.gen.c; forward-declared because this TU does
 * not include its own generated header. */
struct yetty_yclass_ptr_result yetty_ynet_capture_class_get(void);
struct yetty_ynet_capture_ptr_result yetty_ynet_capture_from(struct yetty_yclass_object *obj);

/*=============================================================================
 * Growable storage
 *===========================================================================*/

static int arena_append(struct yetty_ynet_capture *capture, const uint8_t *data, uint32_t length,
                        size_t *out_offset)
{
    if (capture->bytes_length + length > capture->bytes_capacity) {
        size_t new_capacity = capture->bytes_capacity ? capture->bytes_capacity : 65536;
        while (new_capacity < capture->bytes_length + length) {
            new_capacity *= 2;
        }
        uint8_t *grown = realloc(capture->bytes, new_capacity);
        if (!grown) {
            return 0;
        }
        capture->bytes = grown;
        capture->bytes_capacity = new_capacity;
    }
    *out_offset = capture->bytes_length;
    memcpy(capture->bytes + capture->bytes_length, data, length);
    capture->bytes_length += length;
    return 1;
}

static struct ynet_packet_record *records_push(struct yetty_ynet_capture *capture)
{
    if (capture->record_count >= capture->record_capacity) {
        uint32_t new_capacity = capture->record_capacity ? capture->record_capacity * 2 : 256;
        struct ynet_packet_record *grown =
            realloc(capture->records, (size_t)new_capacity * sizeof(*grown));
        if (!grown) {
            return NULL;
        }
        capture->records = grown;
        capture->record_capacity = new_capacity;
    }
    struct ynet_packet_record *record = &capture->records[capture->record_count++];
    memset(record, 0, sizeof(*record));
    return record;
}

/*=============================================================================
 * Flow aggregation
 *===========================================================================*/

static const char *proto_label(uint8_t ip_protocol)
{
    switch (ip_protocol) {
    case 6:
        return "TCP";
    case 17:
        return "UDP";
    case 1:
        return "ICMP";
    case 58:
        return "ICMPv6";
    default:
        return "IP";
    }
}

static void flow_render_summary(struct ynet_flow *flow)
{
    char address_a[YNET_ENDPOINT_MAX];
    char address_b[YNET_ENDPOINT_MAX];
    ynet_format_address(flow->family, flow->endpoint_a_addr, address_a, sizeof(address_a));
    ynet_format_address(flow->family, flow->endpoint_b_addr, address_b, sizeof(address_b));
    if (flow->has_ports) {
        /* Bracket IPv6 so the port colon is unambiguous ([addr]:port). */
        const char *open = flow->family == YNET_AF_INET6 ? "[" : "";
        const char *close = flow->family == YNET_AF_INET6 ? "]" : "";
        snprintf(flow->summary, sizeof(flow->summary),
                 "%-5s %s%s%s:%u \xE2\x86\x94 %s%s%s:%u  %llu pkts, %llu bytes",
                 proto_label(flow->ip_protocol), open, address_a, close, flow->endpoint_a_port,
                 open, address_b, close, flow->endpoint_b_port,
                 (unsigned long long)flow->packet_count, (unsigned long long)flow->byte_count);
    } else {
        snprintf(flow->summary, sizeof(flow->summary),
                 "%-5s %s \xE2\x86\x94 %s  %llu pkts, %llu bytes", proto_label(flow->ip_protocol),
                 address_a, address_b, (unsigned long long)flow->packet_count,
                 (unsigned long long)flow->byte_count);
    }
}

/* Order the two endpoints so a→b and b→a collapse to one flow. Returns 1 when
 * the dissection's src is endpoint "a", 0 when dst is "a". */
static int endpoint_order(const struct ynet_dissection *diss)
{
    int address_compare = memcmp(diss->src_addr, diss->dst_addr, YNET_ADDR_MAX);
    if (address_compare != 0) {
        return address_compare < 0;
    }
    return diss->src_port <= diss->dst_port;
}

static void flow_account(struct yetty_ynet_capture *capture, const struct ynet_dissection *diss,
                         uint32_t byte_length)
{
    if (diss->family == YNET_AF_NONE) {
        return; /* no L3 5-tuple (e.g. ARP) — not a conversation in M1 */
    }
    int source_is_a = endpoint_order(diss);
    const uint8_t *addr_a = source_is_a ? diss->src_addr : diss->dst_addr;
    const uint8_t *addr_b = source_is_a ? diss->dst_addr : diss->src_addr;
    uint16_t port_a = source_is_a ? diss->src_port : diss->dst_port;
    uint16_t port_b = source_is_a ? diss->dst_port : diss->src_port;

    for (uint32_t index = 0; index < capture->flow_count; index++) {
        struct ynet_flow *flow = &capture->flows[index];
        if (flow->family == diss->family && flow->ip_protocol == diss->ip_protocol &&
            flow->has_ports == diss->has_ports && flow->endpoint_a_port == port_a &&
            flow->endpoint_b_port == port_b &&
            memcmp(flow->endpoint_a_addr, addr_a, YNET_ADDR_MAX) == 0 &&
            memcmp(flow->endpoint_b_addr, addr_b, YNET_ADDR_MAX) == 0) {
            flow->packet_count++;
            flow->byte_count += byte_length;
            flow_render_summary(flow);
            return;
        }
    }

    if (capture->flow_count >= capture->flow_capacity) {
        uint32_t new_capacity = capture->flow_capacity ? capture->flow_capacity * 2 : 64;
        struct ynet_flow *grown = realloc(capture->flows, (size_t)new_capacity * sizeof(*grown));
        if (!grown) {
            return;
        }
        capture->flows = grown;
        capture->flow_capacity = new_capacity;
    }
    struct ynet_flow *flow = &capture->flows[capture->flow_count++];
    memset(flow, 0, sizeof(*flow));
    flow->family = diss->family;
    flow->ip_protocol = diss->ip_protocol;
    flow->has_ports = diss->has_ports;
    memcpy(flow->endpoint_a_addr, addr_a, YNET_ADDR_MAX);
    memcpy(flow->endpoint_b_addr, addr_b, YNET_ADDR_MAX);
    flow->endpoint_a_port = port_a;
    flow->endpoint_b_port = port_b;
    flow->packet_count = 1;
    flow->byte_count = byte_length;
    flow_render_summary(flow);
}

/*=============================================================================
 * State reset
 *===========================================================================*/

static void capture_reset(struct yetty_ynet_capture *capture)
{
    free(capture->path);
    free(capture->bytes);
    free(capture->records);
    free(capture->flows);
    capture->path = NULL;
    capture->bytes = NULL;
    capture->bytes_length = 0;
    capture->bytes_capacity = 0;
    capture->records = NULL;
    capture->record_count = 0;
    capture->record_capacity = 0;
    capture->flows = NULL;
    capture->flow_count = 0;
    capture->flow_capacity = 0;
    capture->link_type = 0;
    capture->have_first_timestamp = 0;
}

/*=============================================================================
 * Methods
 *===========================================================================*/

/* Load a pcap / pcapng file. Reads every packet, dissects it for the summary
 * columns + 5-tuple, copies its bytes into the arena, and folds it into the
 * flow table. Uses libpcap offline reading — no root, no live interface. */
YETTY_ANNOTATE("virtual@ynet:capture:load_file")
YETTY_ANNOTATE("local@ynet:load_file")
static struct yetty_ycore_void_result capture_load_file(struct yetty_yclass_object *obj,
                                                        const char *path)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, capture_result, "load_file: data_get");
    struct yetty_ynet_capture *capture = capture_result.value;
    if (!path) {
        return YETTY_ERR(yetty_ycore_void, "load_file: NULL path");
    }

    capture_reset(capture);

    char error_buffer[PCAP_ERRBUF_SIZE];
    error_buffer[0] = '\0';
    pcap_t *handle = pcap_open_offline(path, error_buffer);
    if (!handle) {
        return YETTY_ERR(yetty_ycore_void, "load_file: cannot open capture file");
    }

    capture->link_type = pcap_datalink(handle);
    size_t path_size = strlen(path) + 1;
    capture->path = malloc(path_size);
    if (capture->path) {
        memcpy(capture->path, path, path_size);
    }

    struct pcap_pkthdr *header = NULL;
    const uint8_t *packet_data = NULL;
    int status;
    while ((status = pcap_next_ex(handle, &header, &packet_data)) == 1) {
        uint32_t captured = header->caplen;
        struct ynet_packet_record *record = records_push(capture);
        if (!record) {
            pcap_close(handle);
            return YETTY_ERR(yetty_ycore_void, "load_file: out of memory (records)");
        }
        if (captured > 0) {
            if (!arena_append(capture, packet_data, captured, &record->byte_offset)) {
                pcap_close(handle);
                return YETTY_ERR(yetty_ycore_void, "load_file: out of memory (arena)");
            }
        }
        record->timestamp_seconds = (uint64_t)header->ts.tv_sec;
        record->timestamp_microseconds = (uint32_t)header->ts.tv_usec;
        record->captured_length = captured;
        record->original_length = header->len;

        if (!capture->have_first_timestamp) {
            capture->first_timestamp_seconds = record->timestamp_seconds;
            capture->first_timestamp_microseconds = record->timestamp_microseconds;
            capture->have_first_timestamp = 1;
        }

        struct ynet_dissection dissection;
        ynet_dissect(capture->bytes + record->byte_offset, captured, capture->link_type,
                     &dissection);
        snprintf(record->summary.protocol, sizeof(record->summary.protocol), "%s",
                 dissection.protocol);
        snprintf(record->summary.source, sizeof(record->summary.source), "%s", dissection.source);
        snprintf(record->summary.destination, sizeof(record->summary.destination), "%s",
                 dissection.destination);
        snprintf(record->summary.info, sizeof(record->summary.info), "%s", dissection.info);

        flow_account(capture, &dissection, header->len);
    }
    pcap_close(handle);

    if (status == -1) {
        return YETTY_ERR(yetty_ycore_void, "load_file: error reading capture");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_count")
YETTY_ANNOTATE("local@ynet:packet_count")
static struct yetty_ycore_uint32_result capture_packet_count(struct yetty_yclass_object *obj)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, capture_result, "packet_count: data_get");
    return YETTY_OK(yetty_ycore_uint32, capture_result.value->record_count);
}

/* Helper shared by the per-index accessors. */
static struct ynet_packet_record *record_at(struct yetty_ynet_capture *capture, uint32_t index)
{
    if (index >= capture->record_count) {
        return NULL;
    }
    return &capture->records[index];
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_time")
YETTY_ANNOTATE("local@ynet:packet_time")
static struct yetty_ycore_float_result capture_packet_time(struct yetty_yclass_object *obj,
                                                           uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, capture_result, "packet_time: data_get");
    struct yetty_ynet_capture *capture = capture_result.value;
    struct ynet_packet_record *record = record_at(capture, index);
    if (!record) {
        return YETTY_ERR(yetty_ycore_float, "packet_time: index out of range");
    }
    double seconds = (double)record->timestamp_seconds - (double)capture->first_timestamp_seconds;
    double micros =
        (double)record->timestamp_microseconds - (double)capture->first_timestamp_microseconds;
    return YETTY_OK(yetty_ycore_float, (float)(seconds + micros / 1000000.0));
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_length")
YETTY_ANNOTATE("local@ynet:packet_length")
static struct yetty_ycore_uint32_result capture_packet_length(struct yetty_yclass_object *obj,
                                                              uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, capture_result, "packet_length: data_get");
    struct ynet_packet_record *record = record_at(capture_result.value, index);
    if (!record) {
        return YETTY_ERR(yetty_ycore_uint32, "packet_length: index out of range");
    }
    return YETTY_OK(yetty_ycore_uint32, record->original_length);
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_protocol")
YETTY_ANNOTATE("local@ynet:packet_protocol")
static struct yetty_ycore_const_char_ptr_result capture_packet_protocol(
    struct yetty_yclass_object *obj, uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, capture_result, "packet_protocol: data_get");
    struct ynet_packet_record *record = record_at(capture_result.value, index);
    if (!record) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "packet_protocol: index out of range");
    }
    return YETTY_OK(yetty_ycore_const_char_ptr, record->summary.protocol);
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_source")
YETTY_ANNOTATE("local@ynet:packet_source")
static struct yetty_ycore_const_char_ptr_result capture_packet_source(
    struct yetty_yclass_object *obj, uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, capture_result, "packet_source: data_get");
    struct ynet_packet_record *record = record_at(capture_result.value, index);
    if (!record) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "packet_source: index out of range");
    }
    return YETTY_OK(yetty_ycore_const_char_ptr, record->summary.source);
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_destination")
YETTY_ANNOTATE("local@ynet:packet_destination")
static struct yetty_ycore_const_char_ptr_result capture_packet_destination(
    struct yetty_yclass_object *obj, uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, capture_result, "packet_destination: data_get");
    struct ynet_packet_record *record = record_at(capture_result.value, index);
    if (!record) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "packet_destination: index out of range");
    }
    return YETTY_OK(yetty_ycore_const_char_ptr, record->summary.destination);
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_info")
YETTY_ANNOTATE("local@ynet:packet_info")
static struct yetty_ycore_const_char_ptr_result capture_packet_info(struct yetty_yclass_object *obj,
                                                                    uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, capture_result, "packet_info: data_get");
    struct ynet_packet_record *record = record_at(capture_result.value, index);
    if (!record) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "packet_info: index out of range");
    }
    return YETTY_OK(yetty_ycore_const_char_ptr, record->summary.info);
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_bytes")
YETTY_ANNOTATE("local@ynet:packet_bytes")
static struct yetty_ycore_const_uint8_ptr_result capture_packet_bytes(
    struct yetty_yclass_object *obj, uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_uint8_ptr, capture_result, "packet_bytes: data_get");
    struct yetty_ynet_capture *capture = capture_result.value;
    struct ynet_packet_record *record = record_at(capture, index);
    if (!record) {
        return YETTY_ERR(yetty_ycore_const_uint8_ptr, "packet_bytes: index out of range");
    }
    return YETTY_OK(yetty_ycore_const_uint8_ptr, capture->bytes + record->byte_offset);
}

YETTY_ANNOTATE("virtual@ynet:capture:packet_caplen")
YETTY_ANNOTATE("local@ynet:packet_caplen")
static struct yetty_ycore_uint32_result capture_packet_caplen(struct yetty_yclass_object *obj,
                                                              uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, capture_result, "packet_caplen: data_get");
    struct ynet_packet_record *record = record_at(capture_result.value, index);
    if (!record) {
        return YETTY_ERR(yetty_ycore_uint32, "packet_caplen: index out of range");
    }
    return YETTY_OK(yetty_ycore_uint32, record->captured_length);
}

YETTY_ANNOTATE("virtual@ynet:capture:flow_count")
YETTY_ANNOTATE("local@ynet:flow_count")
static struct yetty_ycore_uint32_result capture_flow_count(struct yetty_yclass_object *obj)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, capture_result, "flow_count: data_get");
    return YETTY_OK(yetty_ycore_uint32, capture_result.value->flow_count);
}

YETTY_ANNOTATE("virtual@ynet:capture:flow_summary")
YETTY_ANNOTATE("local@ynet:flow_summary")
static struct yetty_ycore_const_char_ptr_result capture_flow_summary(
    struct yetty_yclass_object *obj, uint32_t index)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, capture_result, "flow_summary: data_get");
    struct yetty_ynet_capture *capture = capture_result.value;
    if (index >= capture->flow_count) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "flow_summary: index out of range");
    }
    return YETTY_OK(yetty_ycore_const_char_ptr, capture->flows[index].summary);
}

/*=============================================================================
 * Topology figure — the scrollback-figure consumption mode.
 *
 * Renders the capture's conversations as a host/edge graph: hosts on a ring,
 * each conversation an edge coloured by transport protocol and thickened by
 * traffic, node size by degree. Produced as a ydraw drawable list that the tool
 * ships to the pane as a YDRAW_BIN DCS figure (the same path ycat / yflame use).
 * This is the visual payoff a plain packet table cannot express.
 *===========================================================================*/

enum {
    YNET_TOPO_MAX_HOSTS = 128,
};

struct ynet_topo_host {
    enum ynet_addr_family family;
    uint8_t addr[YNET_ADDR_MAX];
    uint32_t degree;
    float x;
    float y;
    float radius;
};

/* Packed RGBA with R in the low byte — the encoding the ydraw pipeline reads. */
static uint32_t pack_rgba(uint8_t red, uint8_t green, uint8_t blue)
{
    return 0xFF000000u | ((uint32_t)blue << 16) | ((uint32_t)green << 8) | (uint32_t)red;
}

static uint32_t proto_edge_color(uint8_t ip_protocol)
{
    switch (ip_protocol) {
    case 6:
        return pack_rgba(107, 168, 146); /* TCP  — brand mint */
    case 17:
        return pack_rgba(107, 150, 197); /* UDP  — cool blue  */
    case 1:
    case 58:
        return pack_rgba(206, 160, 90); /* ICMP — amber */
    default:
        return pack_rgba(128, 134, 134);
    }
}

static int topo_find_or_add(struct ynet_topo_host *hosts, uint32_t *count,
                            enum ynet_addr_family family, const uint8_t *addr)
{
    for (uint32_t index = 0; index < *count; index++) {
        if (hosts[index].family == family && memcmp(hosts[index].addr, addr, YNET_ADDR_MAX) == 0) {
            return (int)index;
        }
    }
    if (*count >= YNET_TOPO_MAX_HOSTS) {
        return -1;
    }
    struct ynet_topo_host *host = &hosts[(*count)++];
    memset(host, 0, sizeof(*host));
    host->family = family;
    memcpy(host->addr, addr, YNET_ADDR_MAX);
    return (int)(*count - 1);
}

static struct yetty_ycore_void_result topo_add_label(struct yetty_ydraw_drawable_list *list,
                                                     float x, float y, uint32_t z, float size,
                                                     uint32_t color, const char *text)
{
    struct yetty_ycore_buffer buffer = {
        .data = (uint8_t *)text,
        .capacity = strlen(text),
        .size = strlen(text),
    };
    return yetty_ydraw_drawable_list_add_text(list, x, y, &buffer, size, color, z, -1, 0.0f);
}

static struct yetty_ycore_void_result topo_build(struct yetty_ydraw_drawable_list *list,
                                                 struct yetty_ynet_capture *capture, float canvas_w,
                                                 float canvas_h)
{
    uint32_t text_primary = pack_rgba(224, 229, 228);
    uint32_t text_muted = pack_rgba(159, 167, 168);

    /* Background panel. */
    struct yetty_ysdf_box background = {canvas_w * 0.5f, canvas_h * 0.5f, canvas_w * 0.5f,
                                        canvas_h * 0.5f, 12.0f};
    struct yetty_ycore_void_result step = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, 0, 0, pack_rgba(11, 16, 20), pack_rgba(54, 74, 71), 1.0f, &background);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, step, "topo: background");

    /* Aggregate the unique hosts from the flow table. */
    struct ynet_topo_host hosts[YNET_TOPO_MAX_HOSTS];
    uint32_t host_count = 0;
    for (uint32_t index = 0; index < capture->flow_count; index++) {
        struct ynet_flow *flow = &capture->flows[index];
        int endpoint_a = topo_find_or_add(hosts, &host_count, flow->family, flow->endpoint_a_addr);
        int endpoint_b = topo_find_or_add(hosts, &host_count, flow->family, flow->endpoint_b_addr);
        if (endpoint_a >= 0) {
            hosts[endpoint_a].degree++;
        }
        if (endpoint_b >= 0) {
            hosts[endpoint_b].degree++;
        }
    }

    /* Circular layout. */
    float center_x = canvas_w * 0.5f;
    float center_y = canvas_h * 0.5f + 16.0f;
    float layout_radius = (canvas_w < canvas_h ? canvas_w : canvas_h) * 0.34f;
    for (uint32_t index = 0; index < host_count; index++) {
        float angle = host_count > 1 ? 6.2831853f * (float)index / (float)host_count : 0.0f;
        hosts[index].x = center_x + layout_radius * cosf(angle);
        hosts[index].y = center_y + layout_radius * sinf(angle);
        float radius = 7.0f + (float)hosts[index].degree * 1.5f;
        hosts[index].radius = radius > 22.0f ? 22.0f : radius;
    }

    /* Edges (z=1), coloured by protocol, thickened by packet volume. */
    for (uint32_t index = 0; index < capture->flow_count; index++) {
        struct ynet_flow *flow = &capture->flows[index];
        int endpoint_a = topo_find_or_add(hosts, &host_count, flow->family, flow->endpoint_a_addr);
        int endpoint_b = topo_find_or_add(hosts, &host_count, flow->family, flow->endpoint_b_addr);
        if (endpoint_a < 0 || endpoint_b < 0) {
            continue;
        }
        float stroke = 1.5f + (float)flow->packet_count * 0.6f;
        if (stroke > 7.0f) {
            stroke = 7.0f;
        }
        struct yetty_ysdf_segment edge = {hosts[endpoint_a].x, hosts[endpoint_a].y,
                                          hosts[endpoint_b].x, hosts[endpoint_b].y};
        step = yetty_ydraw_drawable_list_add_cmd_add_segment(
            list, 0, 1, 0u, proto_edge_color(flow->ip_protocol), stroke, &edge);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, step, "topo: edge");
    }

    /* Nodes (z=2) + labels (z=3). */
    for (uint32_t index = 0; index < host_count; index++) {
        struct yetty_ysdf_circle node = {hosts[index].x, hosts[index].y, hosts[index].radius};
        step = yetty_ydraw_drawable_list_add_cmd_add_circle(list, 0, 2, pack_rgba(30, 38, 44),
                                                            pack_rgba(116, 197, 165), 2.0f, &node);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, step, "topo: node");

        char label[YNET_ENDPOINT_MAX];
        ynet_format_address(hosts[index].family, hosts[index].addr, label, sizeof(label));
        float dir_x = hosts[index].x - center_x;
        float dir_y = hosts[index].y - center_y;
        float length = sqrtf(dir_x * dir_x + dir_y * dir_y);
        if (length < 1.0f) {
            length = 1.0f;
        }
        float label_x = hosts[index].x + dir_x / length * (hosts[index].radius + 6.0f);
        float label_y = hosts[index].y + dir_y / length * (hosts[index].radius + 6.0f) + 4.0f;
        if (dir_x < 0.0f) {
            /* Left half: pull the label left so it does not overrun the edge. */
            label_x -= (float)strlen(label) * 6.5f;
        }
        step = topo_add_label(list, label_x, label_y, 3, 12.0f, text_primary, label);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, step, "topo: label");
    }

    /* Title (z=4). */
    char title[160];
    snprintf(title, sizeof(title), "ynet topology  \xE2\x80\x94  %u hosts, %u flows, %u packets",
             host_count, capture->flow_count, capture->record_count);
    step = topo_add_label(list, 18.0f, 26.0f, 4, 17.0f, text_primary, title);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, step, "topo: title");

    /* Legend (z=4). */
    struct {
        const char *name;
        uint8_t protocol;
    } legend[] = {{"TCP", 6}, {"UDP", 17}, {"ICMP", 1}};
    float legend_x = 18.0f;
    float legend_y = canvas_h - 18.0f;
    for (int index = 0; index < 3; index++) {
        struct yetty_ysdf_segment swatch = {legend_x, legend_y, legend_x + 18.0f, legend_y};
        step = yetty_ydraw_drawable_list_add_cmd_add_segment(
            list, 0, 4, 0u, proto_edge_color(legend[index].protocol), 3.0f, &swatch);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, step, "topo: legend swatch");
        step = topo_add_label(list, legend_x + 24.0f, legend_y + 4.0f, 4, 12.0f, text_muted,
                              legend[index].name);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, step, "topo: legend label");
        legend_x += 84.0f;
    }
    return YETTY_OK_VOID();
}

/* Render the capture's conversations as a topology figure. `width`/`height` are
 * the figure's pixel size (0 → defaults). Returns a ydraw drawable list the
 * caller emits and then destroys. */
YETTY_ANNOTATE("virtual@ynet:capture:render")
YETTY_ANNOTATE("local@ynet:render")
static struct yetty_ydraw_drawable_list_result capture_render(struct yetty_yclass_object *obj,
                                                              uint32_t width, uint32_t height)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, capture_result, "render: data_get");
    struct yetty_ynet_capture *capture = capture_result.value;

    float canvas_w = width > 0 ? (float)width : 960.0f;
    float canvas_h = height > 0 ? (float)height : 620.0f;

    struct yetty_ydraw_drawable_list_config config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = canvas_w,
        .scene_max_y = canvas_h,
    };
    struct yetty_ydraw_drawable_list_result list_result =
        yetty_ydraw_drawable_list_config_buffer_create(&config);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_result, "render: list create");

    struct yetty_ycore_void_result built =
        topo_build(list_result.value, capture, canvas_w, canvas_h);
    if (YETTY_IS_ERR(built)) {
        yetty_ydraw_drawable_list_destroy(list_result.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "render: build topology", built);
    }
    return list_result;
}

/* Serialize a rendered drawable list as a YDRAW_BIN DCS envelope on `fd` — the
 * scrolling-layer figure path (mirrors yflame's emit_osc). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ynet_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                   int fd)
{
    if (!list) {
        return YETTY_ERR(yetty_ycore_void, "ynet emit_osc: NULL list");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)list, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "ynet emit_osc: empty serialize");
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_void_result emit_result = yetty_yface_emit_to_fd(
        fd, YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_result, "ynet emit_osc: yface_emit_to_fd");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ynet:capture:destroy")
YETTY_ANNOTATE("local@ynet:destroy")
static struct yetty_ycore_void_result capture_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_ynet_capture_ptr_result capture_result = yetty_ynet_capture_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, capture_result, "destroy: data_get");
    capture_reset(capture_result.value);
    return yetty_yclass_object_free(obj);
}

#include "yetty/gen/impl/ynet/capture.c"
