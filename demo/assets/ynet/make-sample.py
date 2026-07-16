#!/usr/bin/env python3
"""Generate demo/assets/ynet/sample.pcap — a small but varied capture that
showcases the ynet dissectors and flow aggregation: two TCP conversations, a
DNS query + response, an ICMP echo request/reply, and one IPv6 UDP packet.

The pcap format is trivial, so this writes it directly — no libpcap/tshark
needed. Re-run to regenerate: `python3 demo/assets/ynet/make-sample.py`.
"""
import struct
import sys

MAC_A = bytes.fromhex("525400aaaaaa")
MAC_B = bytes.fromhex("525400bbbbbb")
CLIENT = bytes([192, 168, 1, 20])
WEB = bytes([93, 184, 216, 34])
RESOLVER = bytes([192, 168, 1, 1])
PING_TARGET = bytes([1, 1, 1, 1])
CLIENT6 = bytes.fromhex("20010db8000000000000000000000001")
SERVER6 = bytes.fromhex("20010db8000000000000000000000002")


def ethernet(dst, src, ethertype, payload):
    return dst + src + struct.pack(">H", ethertype) + payload


def ipv4(protocol, src, dst, payload, ident=0x4000):
    header = bytearray(
        struct.pack(">BBHHHBBH", 0x45, 0x00, 20 + len(payload), ident, 0x4000, 64, protocol, 0)
    )
    return bytes(header) + src + dst + payload


def ipv6(next_header, src, dst, payload):
    header = struct.pack(">IHBB", 0x60000000, len(payload), next_header, 64)
    return header + src + dst + payload


def tcp(src_port, dst_port, seq, ack, flags, window):
    return struct.pack(">HHIIBBHHH", src_port, dst_port, seq, ack, 0x50, flags, window, 0, 0)


def udp(src_port, dst_port, payload):
    return struct.pack(">HHHH", src_port, dst_port, 8 + len(payload), 0) + payload


def dns(transaction_id, name, is_response, qtype=1):
    flags = 0x8180 if is_response else 0x0100
    answers = 1 if is_response else 0
    header = struct.pack(">HHHHHH", transaction_id, flags, 1, answers, 0, 0)
    question = b"".join(bytes([len(part)]) + part.encode() for part in name.split(".")) + b"\x00"
    body = header + question + struct.pack(">HH", qtype, 1)
    if is_response:
        # One A record pointing back at the question name via a compression ptr.
        body += struct.pack(">HHHIH", 0xC00C, 1, 1, 300, 4) + WEB
    return body


def icmp(kind, identifier, sequence):
    return struct.pack(">BBHHH", kind, 0, 0, identifier, sequence)


def build_packets():
    packets = []
    # Conversation 1: TCP handshake to a web server.
    packets.append(ethernet(MAC_B, MAC_A, 0x0800, ipv4(6, CLIENT, WEB, tcp(51000, 443, 0, 0, 0x02, 64240))))
    packets.append(ethernet(MAC_A, MAC_B, 0x0800, ipv4(6, WEB, CLIENT, tcp(443, 51000, 0, 1, 0x12, 65160))))
    packets.append(ethernet(MAC_B, MAC_A, 0x0800, ipv4(6, CLIENT, WEB, tcp(51000, 443, 1, 1, 0x10, 64240))))
    # Conversation 2: a second TCP flow (different source port) — plain SYN.
    packets.append(ethernet(MAC_B, MAC_A, 0x0800, ipv4(6, CLIENT, WEB, tcp(51002, 80, 0, 0, 0x02, 64240))))
    # DNS query + response.
    packets.append(ethernet(MAC_B, MAC_A, 0x0800, ipv4(17, CLIENT, RESOLVER, udp(53001, 53, dns(0x1234, "example.com", False)))))
    packets.append(ethernet(MAC_A, MAC_B, 0x0800, ipv4(17, RESOLVER, CLIENT, udp(53, 53001, dns(0x1234, "example.com", True)))))
    # ICMP echo request + reply.
    packets.append(ethernet(MAC_B, MAC_A, 0x0800, ipv4(1, CLIENT, PING_TARGET, icmp(8, 0x0abc, 1))))
    packets.append(ethernet(MAC_A, MAC_B, 0x0800, ipv4(1, PING_TARGET, CLIENT, icmp(0, 0x0abc, 1))))
    # IPv6 UDP (e.g. QUIC-style) — exercises the IPv6 dissector.
    packets.append(ethernet(MAC_B, MAC_A, 0x86DD, ipv6(17, CLIENT6, SERVER6, udp(52000, 443, b"\x00" * 16))))
    return packets


def write_pcap(path, packets):
    with open(path, "wb") as handle:
        handle.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
        base = 1_600_000_000
        for index, packet in enumerate(packets):
            handle.write(struct.pack("<IIII", base, index * 1500, len(packet), len(packet)))
            handle.write(packet)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "demo/assets/ynet/sample.pcap"
    write_pcap(out, build_packets())
    print(f"wrote {out}")
