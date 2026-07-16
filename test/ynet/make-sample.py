#!/usr/bin/env python3
"""Generate test/ynet/sample.pcap — a tiny deterministic capture that exercises
every M1 dissector: Ethernet + IPv4 with TCP (SYN / SYN-ACK, one bidirectional
flow), UDP/DNS (a query), and ICMP (echo request).

The pcap file format is trivial, so this writes it directly — no libpcap / tshark
needed. Re-run to regenerate: `python3 test/ynet/make-sample.py`.
"""
import struct
import sys

MAC_CLIENT = bytes.fromhex("525400654321")
MAC_SERVER = bytes.fromhex("525400123456")
IP_CLIENT = bytes([192, 168, 0, 2])
IP_RESOLVER = bytes([192, 168, 0, 1])
IP_WEB = bytes([93, 184, 216, 34])
IP_PING = bytes([8, 8, 8, 8])


def ethernet(dst, src, ethertype, payload):
    return dst + src + struct.pack(">H", ethertype) + payload


def ipv4(protocol, src, dst, payload):
    total_len = 20 + len(payload)
    header = bytearray(
        struct.pack(
            ">BBHHHBBH", 0x45, 0x00, total_len, 0x1c46, 0x4000, 64, protocol, 0x0000
        )
    )
    header += src + dst
    return bytes(header) + payload


def tcp(src_port, dst_port, seq, ack, flags, window):
    # data offset 5 (20 bytes), no options
    return struct.pack(
        ">HHIIBBHHH", src_port, dst_port, seq, ack, 0x50, flags, window, 0x0000, 0x0000
    )


def udp(src_port, dst_port, payload):
    length = 8 + len(payload)
    return struct.pack(">HHHH", src_port, dst_port, length, 0x0000) + payload


def dns_query(transaction_id, name, qtype=1):
    header = struct.pack(">HHHHHH", transaction_id, 0x0100, 1, 0, 0, 0)
    encoded = b"".join(
        bytes([len(label)]) + label.encode() for label in name.split(".")
    ) + b"\x00"
    return header + encoded + struct.pack(">HH", qtype, 1)


def icmp_echo_request(identifier, sequence):
    return struct.pack(">BBHHH", 8, 0, 0x0000, identifier, sequence)


def build_packets():
    tcp_syn = ethernet(
        MAC_SERVER, MAC_CLIENT, 0x0800,
        ipv4(6, IP_CLIENT, IP_WEB, tcp(52134, 443, 0, 0, 0x02, 64240)),
    )
    tcp_syn_ack = ethernet(
        MAC_CLIENT, MAC_SERVER, 0x0800,
        ipv4(6, IP_WEB, IP_CLIENT, tcp(443, 52134, 1, 1, 0x12, 65160)),
    )
    dns = ethernet(
        MAC_SERVER, MAC_CLIENT, 0x0800,
        ipv4(17, IP_CLIENT, IP_RESOLVER, udp(50000, 53, dns_query(0xabcd, "example.com"))),
    )
    ping = ethernet(
        MAC_SERVER, MAC_CLIENT, 0x0800,
        ipv4(1, IP_CLIENT, IP_PING, icmp_echo_request(1, 1)),
    )
    return [tcp_syn, tcp_syn_ack, dns, ping]


def write_pcap(path, packets):
    with open(path, "wb") as handle:
        # Global header: magic, ver 2.4, tz 0, sigfigs 0, snaplen 65535, DLT_EN10MB.
        handle.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
        base_seconds = 1_600_000_000
        for index, packet in enumerate(packets):
            handle.write(
                struct.pack("<IIII", base_seconds, index * 1000, len(packet), len(packet))
            )
            handle.write(packet)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "test/ynet/sample.pcap"
    write_pcap(out, build_packets())
    print(f"wrote {out}")
