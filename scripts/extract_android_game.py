#!/usr/bin/env python3
"""Extract a user's USA Xbox 360 disc after verifying default.xex.

Reads XDVDFS directly; offsets/layout follow the SDK's DiscImageDevice.
Never downloads game files or modifies the input image.
"""
import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import struct

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
USA_SHA1 = "aec598f88cf51181fc377b148e0b1ad30db4485c"
USA_SIZE = 7983104


class Disc:
    def __init__(self, source):
        self.source = source
        self.size = os.fstat(source.fileno()).st_size
        self.base = None
        for base in (0, 0xFB20, 0x20600, 0x2080000, 0xFD90000):
            if base + 32 * SECTOR + 28 <= self.size:
                source.seek(base + 32 * SECTOR)
                if source.read(20) == MAGIC:
                    self.base = base
                    root_sector, root_size = struct.unpack("<II", source.read(8))
                    break
        if self.base is None:
            raise ValueError("No Xbox XDVDFS partition found. A finished Xbox 360 ISO is required.")
        self.files = []
        self.directories = set()
        self.names = set()
        self.directory(root_sector, root_size, PurePosixPath(), 0)

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.size:
            raise ValueError("Disc entry points outside the ISO (incomplete or corrupt download).")
        self.source.seek(offset)
        data = self.source.read(size)
        if len(data) != size:
            raise ValueError("Short ISO read.")
        return data

    def directory(self, sector, size, parent, depth):
        if depth > 64 or not 14 <= size <= 32 * 1024 * 1024:
            raise ValueError("Invalid directory table.")
        key = (sector, size)
        if key in self.directories:
            raise ValueError("Cyclic or duplicate directory table.")
        self.directories.add(key)
        table = self.read(self.base + sector * SECTOR, size)
        pending, visited = [0], set()
        while pending:
            offset = pending.pop() * 4
            if offset in visited or offset + 14 > len(table):
                raise ValueError("Invalid XDVDFS directory tree.")
            visited.add(offset)
            left, right, child_sector, length, attributes, name_length = struct.unpack_from("<HHIIBB", table, offset)
            if not name_length or offset + 14 + name_length > len(table):
                raise ValueError("Invalid directory entry name length.")
            name = table[offset + 14:offset + 14 + name_length].decode("ascii")
            if name in (".", "..") or any(c in name for c in "/\\\0:"):
                raise ValueError("Unsafe ISO filename.")
            path = parent / name
            if path.as_posix().casefold() in self.names:
                raise ValueError("Duplicate ISO filename.")
            self.names.add(path.as_posix().casefold())
            if left:
                pending.append(left)
            if right:
                pending.append(right)
            if name.casefold() == "$systemupdate" and not parent.parts:
                continue
            if attributes & 0x10:
                if length:
                    self.directory(child_sector, length, path, depth + 1)
            else:
                start = self.base + child_sector * SECTOR
                if start + length > self.size:
                    raise ValueError("File outside ISO; the download may be incomplete.")
                self.files.append((path, start, length))

    def verify_usa(self):
        xex = next((entry for entry in self.files if entry[0].as_posix().casefold() == "default.xex"), None)
        if xex is None or xex[2] != USA_SIZE:
            raise ValueError("This disc does not contain the supported USA default.xex.")
        digest = hashlib.sha1(self.read(xex[1], xex[2])).hexdigest()
        if digest != USA_SHA1:
            raise ValueError(f"Unsupported executable SHA-1: {digest}; expected {USA_SHA1}.")
        required = {"long2data/long2data_us.cpk", "long2data/stream_us.cpk", "long2data/stream_jp.cpk"}
        if not required.issubset(self.names):
            raise ValueError("Disc is missing required Burst Limit archives.")
        return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "game_data_root")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    if args.iso.suffix.lower() != ".iso":
        parser.error("Use a finished .iso file, not a browser's .crdownload/.part.")
    with args.iso.open("rb") as source:
        disc = Disc(source)
        digest = disc.verify_usa()
        total = sum(size for _, _, size in disc.files)
        print(f"USA executable verified: {digest}; {len(disc.files)} files, {total / 1024**3:.2f} GiB", flush=True)
        if args.verify_only:
            return
        destination = args.output.resolve()
        if destination.exists() and any(destination.iterdir()):
            parser.error("Output is not empty; choose a new folder to preserve existing game files.")
        destination.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(destination).free < total + 64 * 1024**2:
            parser.error("Insufficient disk space for extraction.")
        # default.xex is last; its presence marks a completed install.
        files = sorted(disc.files, key=lambda item: (item[0].as_posix().casefold() == "default.xex", item[1]))
        for index, (relative, offset, size) in enumerate(files, 1):
            target = destination.joinpath(*relative.parts)
            if not target.resolve().is_relative_to(destination):
                raise ValueError("Output path escapes destination.")
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + ".extracting")
            try:
                source.seek(offset)
                with temporary.open("xb") as output:
                    remaining = size
                    while remaining:
                        chunk = source.read(min(remaining, 8 * 1024**2))
                        if not chunk:
                            raise ValueError("ISO was truncated while extracting.")
                        output.write(chunk)
                        remaining -= len(chunk)
                temporary.replace(target)
            finally:
                temporary.unlink(missing_ok=True)
            print(f"[{index}/{len(files)}] {relative}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from None
