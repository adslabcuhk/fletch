#!/usr/bin/env python3
"""
Warmup script that reads paths from active_path.log and sends one WARMUP
packet per path to the switch.

Usage:
    sudo python3 warmup_from_log.py [max_lines]
    sudo python3 warmup_from_log.py                # default: all lines
    sudo python3 warmup_from_log.py 1000           # read first 1000 lines

All packets are pre-built before sending to maximize send rate.
"""

import argparse
import hashlib
import random
import sys
import time

from scapy.all import IP, UDP, Ether, get_if_hwaddr, get_if_list, sendp, Raw

# ----- config -----
DEFAULT_LOG_PATH = "active_path.log"
DEFAULT_DST_IP = "192.168.10.121"
DEFAULT_DST_MAC = "b8:ce:f6:99:fe:06"
DEFAULT_IFACE = "ens6np0"
DEFAULT_DPORT = 1152


# ----- helpers -----
def get_if(iface_hint):
    for i in get_if_list():
        if iface_hint in i:
            return i
    print(f"Cannot find {iface_hint} interface")
    sys.exit(1)


def string_to_md5_bytes(s):
    return hashlib.md5(s.encode()).digest()[0:8]


def int_to_hex_2bytes(n):
    assert 0 <= n <= 0xFFFF
    return format(n, '04x')


def build_warmup_payload(path):
    key = string_to_md5_bytes(path)
    key_hex = key.hex()
    path_len_hex = int_to_hex_2bytes(len(path))
    path_hex = path.encode().hex()
    payload = bytes.fromhex(
        "0000"
        + "0001"
        + key_hex
        + "01"
        + path_len_hex
        + path_hex
    )
    return payload


def build_pkt(src_mac, dst_mac, dst_ip, dport, payload):
    return (
        Ether(src=src_mac, dst=dst_mac)
        / IP(dst=dst_ip)
        / UDP(dport=dport, sport=random.randint(49152, 65535))
        / Raw(load=payload)
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("max_lines", nargs="?", type=int, default=None,
                    help="read at most this many lines (default: all)")
    ap.add_argument("--log", default=DEFAULT_LOG_PATH)
    ap.add_argument("--dst-ip", default=DEFAULT_DST_IP)
    ap.add_argument("--dst-mac", default=DEFAULT_DST_MAC)
    ap.add_argument("--iface", default=DEFAULT_IFACE)
    ap.add_argument("--dport", type=int, default=DEFAULT_DPORT)
    args = ap.parse_args()

    # ---- read paths ----
    paths = []
    with open(args.log, "r") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            paths.append(line)
            if args.max_lines is not None and len(paths) >= args.max_lines:
                break
    print(f"[warmup] loaded {len(paths)} paths from {args.log}")

    # ---- pre-build packets ----
    iface = get_if(args.iface)
    src_mac = get_if_hwaddr(iface)
    print(f"[warmup] iface={iface} src_mac={src_mac} "
          f"dst_ip={args.dst_ip} dst_mac={args.dst_mac}")

    print(f"[warmup] building {len(paths)} packets...")
    build_start = time.time()
    all_pkts = [
        build_pkt(src_mac, args.dst_mac, args.dst_ip, args.dport,
                  build_warmup_payload(p))
        for p in paths
    ]
    build_elapsed = time.time() - build_start
    print(f"[warmup] built {len(all_pkts)} packets in {build_elapsed:.2f}s")

    # ---- send ----
    print(f"[warmup] sending {len(all_pkts)} packets on {iface}...")
    send_start = time.time()
    sendp(all_pkts, iface=iface, verbose=False)
    send_elapsed = time.time() - send_start
    pps = len(all_pkts) / send_elapsed if send_elapsed > 0 else 0
    print(f"[warmup] sent {len(all_pkts)} packets in {send_elapsed:.2f}s "
          f"({pps:.0f} pps)")


if __name__ == "__main__":
    main()