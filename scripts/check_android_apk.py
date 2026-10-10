#!/usr/bin/env python3
"""Check all APK native libraries for ARM64 and 16 KiB ELF/ZIP alignment."""
import argparse
import struct
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    args = parser.parse_args()
    with zipfile.ZipFile(args.apk) as archive, args.apk.open("rb") as raw:
        libraries = [info for info in archive.infolist() if info.filename.startswith("lib/") and info.filename.endswith(".so")]
        if not libraries:
            raise ValueError("No native libraries found")
        for info in libraries:
            data = archive.read(info)
            if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 183:
                raise ValueError(f"Not ARM64 little-endian ELF64: {info.filename}")
            offset = struct.unpack_from("<Q", data, 32)[0]
            entry_size, count = struct.unpack_from("<HH", data, 54)
            loads = 0
            for index in range(count):
                kind, _, file_offset, address, _, _, _, alignment = struct.unpack_from("<IIQQQQQQ", data, offset + index * entry_size)
                if kind == 1:
                    loads += 1
                    if alignment < 16384 or (address - file_offset) % 16384:
                        raise ValueError(f"PT_LOAD is not 16 KiB aligned: {info.filename}")
            if not loads:
                raise ValueError(f"No loadable segments: {info.filename}")
            zip_result = "compressed (extracted at install)"
            if info.compress_type == zipfile.ZIP_STORED:
                raw.seek(info.header_offset + 26)
                name_length, extra_length = struct.unpack("<HH", raw.read(4))
                start = info.header_offset + 30 + name_length + extra_length
                if start % 16384:
                    raise ValueError(f"Uncompressed .so ZIP offset is not 16 KiB aligned: {info.filename}")
                zip_result = "16 KiB ZIP alignment"
            print(f"PASS {info.filename}: 16 KiB ELF segments; {zip_result}")
    print("APK alignment verified. This is not a 16 KiB-device runtime test.")


if __name__ == "__main__":
    main()
