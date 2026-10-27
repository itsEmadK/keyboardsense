#!/usr/bin/env python3
"""Validate the shared renderer against the embedded loader's actual contract."""
import os
import pathlib
import struct
import sys
from check_elf import check


def sections(data):
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count = struct.unpack_from('<HH', data, 58)
    if size != 64 or offset + size * count > len(data):
        raise ValueError('invalid section table')
    return [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * size) for i in range(count)]


def symbols(data):
    table = sections(data)
    result = []
    for section in table:
        if section[1] != 11:
            continue
        if section[6] >= len(table) or section[9] != 24:
            raise ValueError('invalid dynamic symbol table')
        strings = table[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], 24):
            name, info, _, index, value, _ = struct.unpack_from('<IBBHQQ', data, offset)
            if name >= len(names):
                raise ValueError('invalid symbol name')
            text = names[name:].split(b'\0', 1)[0].decode()
            if text:
                result.append((text, index, value))
    return result


def validate(path, sdk):
    data = path.read_bytes()
    check(data)
    if struct.unpack_from('<H', data, 16)[0] != 3:
        raise ValueError('renderer must be shared ET_DYN')
    for section in sections(data):
        if section[1] == 4:  # RELA: only relocation types handled by stable_elfldr_bridge.c
            for offset in range(section[4], section[4] + section[5], 24):
                typ = struct.unpack_from('<Q', data, offset + 8)[0] & 0xffffffff
                if typ not in (0, 1, 6, 8):
                    raise ValueError(f'unsupported renderer relocation {typ}')
        if section[1] == 6:
            for offset in range(section[4], section[4] + section[5], 16):
                tag, value = struct.unpack_from('<qQ', data, offset)
                if tag == 0x6ffffffb and value & 0x08000000:
                    raise ValueError('shared renderer must not carry DF_1_PIE')
                if tag in (27, 28, 33) and value:
                    raise ValueError('renderer requires unsupported init/fini array execution')
    entries = symbols(data)
    if not any(name == 'elf_main' and index and value == struct.unpack_from('<Q', data, 24)[0]
               for name, index, value in entries):
        raise ValueError('entry does not match exported elf_main')
    mono = {'mono_get_root_domain', 'mono_domain_get', 'mono_thread_attach', 'mono_domain_assembly_open',
            'mono_assembly_get_image', 'mono_class_from_name', 'mono_class_get_method_from_name',
            'mono_class_get_property_from_name', 'mono_property_get_get_method',
            'mono_property_get_set_method', 'mono_runtime_invoke', 'mono_string_new',
            'mono_object_new', 'mono_runtime_object_init', 'mono_object_unbox',
            'mono_compile_method', 'mono_gchandle_new', 'mono_gchandle_get_target', 'mono_gchandle_free'}
    exports = set(mono)
    for lib in ['kernel_sys', 'kernel', 'SceLibcInternal', 'SceNet', 'SceSystemService', 'SceSysmodule', 'SceUserService', 'ScePad']:
        stub = sdk / 'target/lib' / f'lib{lib}.so'
        exports.update(name for name, index, _ in symbols(stub.read_bytes()) if index)
    missing = [name for name, index, _ in entries if not index and name not in exports]
    if missing:
        raise ValueError('imports absent from loader resolver libraries: ' + ', '.join(missing))
    print(f'{path}: shared renderer entry, relocations, imports, and initializer contract verified')


if __name__ == '__main__':
    validate(pathlib.Path(sys.argv[1]), pathlib.Path(os.environ['PS5_PAYLOAD_SDK']))
