"""Send the documented sample NOOP on the isolated NOS3 container's loopback."""

import argparse
from functools import reduce
from operator import xor
from random import Random
import socket
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--sequence", type=int, default=0)
    parser.add_argument("--seed", type=int, default=42, help="Seed the nominal pacing jitter")
    args = parser.parse_args()
    if not 1 <= args.count <= 500:
        parser.error("count must be in 1-500")
    if not 0 <= args.sequence < 16384:
        parser.error("sequence must be in 0-16383")
    rng = Random(args.seed)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        for index in range(args.count):
            sequence = 0xC000 | ((args.sequence + index) & 0x3FFF)
            header = struct.pack(">HHHB", 0x18FA, sequence, 1, 0)
            checksum = reduce(xor, header, 0xFF)
            endpoint.sendto(header + bytes([checksum]), ("127.0.0.1", 5012))
            if index + 1 < args.count:
                time.sleep(rng.uniform(0.225, 0.950))
    print(f"Sent {args.count} Sample NOOPs on container loopback")


if __name__ == "__main__":
    main()
