#!/usr/bin/env python3
"""Reject host binaries and malformed payload headers before packaging."""
import pathlib
import struct
import sys


def check(data: bytes) -> None:
    if len(data) < 64 or data[:4] != b'\x7fELF' or data[4:6] != b'\x02\x01':
        raise ValueError('expected a little-endian ELF64 payload')
    elf_type, machine = struct.unpack_from('<HH', data, 16)
    if elf_type not in (2, 3) or machine != 62:
        raise ValueError('expected an x86-64 executable or PIE')
    entry, phoff = struct.unpack_from('<QQ', data, 24)
    phsize, phnum = struct.unpack_from('<HH', data, 54)
    # The PS5 SDK deliberately places _start at virtual address zero.
    if phsize != 56 or not phnum or phoff < 64 or phoff + phsize * phnum > len(data):
        raise ValueError('invalid entry point or program header table')
    executable_entry = False
    for i in range(phnum):
        typ, flags, offset, addr, _, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize)
        if typ != 1:
            continue
        if filesz > memsz or offset + filesz > len(data):
            raise ValueError('invalid load segment')
        if align < 0x4000 or align & (align - 1) or (addr - offset) % align:
            raise ValueError('load segment must respect PS5 16-KiB alignment')
        executable_entry |= bool(flags & 1 and addr <= entry < addr + filesz)
    if not executable_entry:
        raise ValueError('entry point is outside executable file data')


if __name__ == '__main__':
    path = pathlib.Path(sys.argv[1])
    check(path.read_bytes())
    print(f'{path}: ELF64 x86-64, valid executable entry and 16-KiB load alignment')
