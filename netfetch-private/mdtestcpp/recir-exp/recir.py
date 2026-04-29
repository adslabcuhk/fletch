#!/usr/bin/env python3
import random
import socket
import sys
import struct
import hashlib
from time import sleep

from scapy.all import IP, UDP, Ether, get_if_hwaddr, get_if_list, sendp, Raw

# server165
dst_ip = "192.168.10.121"
dst_mac = "b8:ce:f6:99:fe:06"

_iface ="ens6np0"
def get_if():
    ifs = get_if_list()
    iface = None  # "h1-eth0"
    for i in get_if_list():
        if _iface in i:
            iface = i
            break
    if not iface:
        print("Cannot find ens6np0 interface")
        exit(1)
    return iface


def string_to_md5_bytes(input_string):
    hash_object = hashlib.md5()
    hash_object.update(input_string.encode())
    md5_bytes = hash_object.digest()
    # print(type(md5_bytes))
    # print(md5_bytes)
    return md5_bytes[0:8]


def int_to_hex_2bytes(number):
    if 0 <= number <= 0xFFFF:
        return format(number, '04x')
    else:
        return "Error: Number out of range (0-65535)"

def depth_to_hex(depth):
    return f'{depth:04x}'


def prepare_for_GET_token(path, path_depth, recir_counts):
    # split path
    if not path.startswith('/'):
        path = '/' + path
    path_segments = path.strip('/').split('/')
    # compute hash values
    hashes_hex = []
    hash_bytes = string_to_md5_bytes('/')
    hashes_hex.append(''.join(f'{byte:02x}' for byte in hash_bytes))
    # if path_depth>1:
    for segment in path_segments:
        segment_path = '/' + '/'.join(path_segments[:path_segments.index(segment) + 1])
        hash_bytes = string_to_md5_bytes(segment_path)
        hashes_hex.append(''.join(f'{byte:02x}' for byte in hash_bytes))
    # represent the real path
    print(hashes_hex)
    # represent the real path
    path_length_hex_representation = int_to_hex_2bytes(len(path))
    path_hex_representation = ''.join(f"{ord(c):02x}" for c in path)
    recir_test_depth_hex = f"{recir_counts:02x}"
    real_path_depth_hex = f"{path_depth:02x}"
    hashes_hex = hashes_hex[-path_depth:]
    # assemble the packet
    GETpayload = bytes.fromhex(
        "0030"  # operation type
        + recir_test_depth_hex
        + real_path_depth_hex
        + ''.join(hashes_hex)  # key1, key2, key3, ...
        + '01' * path_depth
        + path_length_hex_representation
        + path_hex_representation
    )
    return GETpayload


def prepare_for_WARMUP(path):
    # hash the key, key is path
    key = string_to_md5_bytes(path)
    key_hex_representation = ''.join(f'{byte:02x}' for byte in key)
    path_length_hex_representation = int_to_hex_2bytes(len(path))
    path_hex_representation = ''.join(f"{ord(c):02x}" for c in path)
    WARMUPpayload = bytes.fromhex(
        "0000"
        + "0001"
        + key_hex_representation
        + '01'
        + path_length_hex_representation
        + path_hex_representation
    )
    return WARMUPpayload


### WARMUP REQUESTS

def warmup_test(path):
    WARMUPpayload = prepare_for_WARMUP(path)
    addr = socket.gethostbyname(dst_ip)
    iface = get_if()
    print("sending on interface %s to %s" % (iface, str(addr)))
    pkt = Ether(src=get_if_hwaddr(iface), dst=dst_mac)
    pkt = (
        pkt
        / IP(dst=addr)
        / UDP(dport=1152, sport=random.randint(49152, 65535))
        / Raw(load=WARMUPpayload)
    )
    sendp(pkt, iface=iface, verbose=False)
    

### GET REQUESTS

def send_get_request_token(path, depth, recir_counts):
    GETpayload = prepare_for_GET_token(path, depth, recir_counts)
    addr = socket.gethostbyname(dst_ip)
    iface = get_if()
    print("sending on interface %s to %s" % (iface, str(addr)))
    pkt = Ether(src=get_if_hwaddr(iface), dst=dst_mac)
    pkt = (
        pkt
        / IP(dst=addr)
        / UDP(dport=1152, sport=random.randint(49152, 65535))
        / Raw(load=GETpayload)
    )
    sendp(pkt, iface=iface, verbose=False)



    
def main():
    warmup_test('/')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5/mdtest_tree.21')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5/mdtest_tree.21/mdtest_tree.85')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5/mdtest_tree.21/mdtest_tree.85/mdtest_tree.341')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5/mdtest_tree.21/mdtest_tree.85/mdtest_tree.341/mdtest_tree.1365')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5/mdtest_tree.21/mdtest_tree.85/mdtest_tree.341/mdtest_tree.1365/mdtest_tree.5461')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5/mdtest_tree.21/mdtest_tree.85/mdtest_tree.341/mdtest_tree.1365/mdtest_tree.5461/mdtest_tree.21845')
    warmup_test('/#test-dir.0.d.8.n.31981446.b.4/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5/mdtest_tree.21/mdtest_tree.85/mdtest_tree.341/mdtest_tree.1365/mdtest_tree.5461/mdtest_tree.21845/file.mdtest.shared.7995271')


if __name__ == "__main__":
    main()
