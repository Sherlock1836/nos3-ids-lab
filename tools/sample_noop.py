"""Send documented NOOP commands on the isolated NOS3 container's loopback."""

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
    parser.add_argument("--target", choices=["sample", "ci-lab", "both"], default="sample")
    args = parser.parse_args()
    if not 1 <= args.count <= 500:
        parser.error("count must be in 1-500")
    if not 0 <= args.sequence < 16384:
        parser.error("sequence must be in 0-16383")
    rng = Random(args.seed)
    mids = {"sample": [0x18FA], "ci-lab": [0x18E0], "both": [0x18FA, 0x18E0]}[args.target]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        for index in range(args.count):
            sequence = 0xC000 | ((args.sequence + index) & 0x3FFF)
            for mid in mids:
                header = struct.pack(">HHHB", mid, sequence, 1, 0)
                checksum = reduce(xor, header, 0xFF)
                endpoint.sendto(header + bytes([checksum]), ("127.0.0.1", 5012))
            if index + 1 < args.count:
                time.sleep(rng.uniform(0.225, 0.950))
    print(f"Sent {args.count} NOOPs per target ({args.target}) on container loopback")


if __name__ == "__main__":
    main()
