#!/usr/bin/env python3
"""Send an ELF to an already-running PS5 ELF loader. Does not jailbreak."""
import argparse
import pathlib
import socket
from check_elf import check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='console LAN IP or hostname')
    parser.add_argument('payload', type=pathlib.Path)
    parser.add_argument('--port', type=int, default=9021)
    args = parser.parse_args()
    data = args.payload.read_bytes()
    check(data)
    with socket.create_connection((args.host, args.port), timeout=10) as connection:
        connection.settimeout(30)
        connection.sendall(data)
        connection.shutdown(socket.SHUT_WR)
    print(f'Sent {len(data)} bytes to {args.host}:{args.port}; check the console notification/klog.')


if __name__ == '__main__':
    main()
