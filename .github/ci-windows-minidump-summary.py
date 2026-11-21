#!/usr/bin/env python3
"""Report the exception's module and offset from a local Windows minidump."""
from pathlib import Path, PureWindowsPath
import struct
import sys


def fault_location(data):
    if data[:4] != b"MDMP":
        raise ValueError("unrecognized Windows minidump")
    count, directory = struct.unpack_from("<II", data, 8)
    streams = {}
    for index in range(count):
        kind, size, start = struct.unpack_from("<III", data, directory + index * 12)
        if start + size > len(data):
            raise ValueError("stream exceeds the minidump")
        streams[kind] = (start, size)
    exception, size = streams[6]
    if size < 32:
        raise ValueError("incomplete exception stream")
    code, = struct.unpack_from("<I", data, exception + 8)
    address, = struct.unpack_from("<Q", data, exception + 24)
    module_list, size = streams[4]
    count, = struct.unpack_from("<I", data, module_list)
    if size < 4 + count * 108:
        raise ValueError("incomplete module list")
    for index in range(count):
        entry = module_list + 4 + index * 108
        base, length = struct.unpack_from("<QI", data, entry)
        if base <= address < base + length:
            name, = struct.unpack_from("<I", data, entry + 20)
            length, = struct.unpack_from("<I", data, name)
            if name + 4 + length > len(data):
                raise ValueError("incomplete module name")
            module = PureWindowsPath(data[name + 4:name + 4 + length].decode("utf-16-le")).name
            return f"Windows exception {code:#010x} in {module}+{address-base:#x}"
    return f"Windows exception {code:#010x} outside the recorded modules"


if __name__ == "__main__":
    print(fault_location(Path(sys.argv[1]).read_bytes()))
