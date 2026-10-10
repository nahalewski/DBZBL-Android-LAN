#!/usr/bin/env python3
"""Exercise the Mac native or Android Java importer with a supplied USA XEX.

Build the production-source probe with AddressSanitizer/UndefinedBehaviorSanitizer:
clang++ -std=c++23 -fsanitize=address,undefined -Isrc -I/opt/homebrew/opt/libarchive/include tests/game_import_probe.cpp \
  src/burstlimit_game_import.cpp -larchive -o out/game-import-probe
python3 scripts/test_mac_game_import.py out/game-import-probe game_data_root/default.xex
For Android, compile GameDataImporter.java with tests/GameDataImportProbe.java and
pass an executable Java launcher as probe with --android-java.
No game executable or ISO is distributed with these tests.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile


def entry(name, sector, size, attributes=0, right=0):
    encoded = name.encode('ascii')
    return struct.pack('<HHIIBB', 0, right, sector, size, attributes, len(encoded)) + encoded


def table(entries):
    records = [entry(*item) for item in entries]
    offsets, offset = [], 0
    for record in records:
        offsets.append(offset)
        offset += (len(record) + 3) & ~3
    result = bytearray(offset)
    for i, record in enumerate(records):
        result[offsets[i]:offsets[i] + len(record)] = record
        if i + 1 < len(records):
            struct.pack_into('<H', result, offsets[i] + 2, offsets[i + 1] // 4)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('xex', type=Path)
    parser.add_argument('--android-java', action='store_true', help='Test the Android Java probe instead of the native Mac probe')
    args = parser.parse_args()
    probe = args.probe.resolve()
    xex = args.xex.read_bytes()
    assert hashlib.sha1(xex).hexdigest() == 'aec598f88cf51181fc377b148e0b1ad30db4485c'
    count = 0
    with tempfile.TemporaryDirectory(prefix='burstlimit-import-tests-') as tmp:
        root = Path(tmp)
        archives = {'LONG2DATA/LONG2DATA_US.CPK': b'asset-a',
                    'LONG2DATA/STREAM_US.CPK': b'asset-b',
                    'LONG2DATA/STREAM_JP.CPK': b'asset-c'}

        def run(source, name, error=None, cancel=None):
            nonlocal count
            target = root / name
            command = [str(probe), str(source), str(target)]
            if cancel is not None:
                command.append(str(cancel))
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            assert 'Sanitizer' not in result.stderr, result.stderr
            if error:
                assert result.returncode == 1 and error in result.stderr, result.stderr
                assert not target.exists(), f'Failed import published {target}'
            else:
                assert result.returncode == 0, result.stderr
                assert subprocess.run([str(probe), 'verify', str(target)]).returncode == 0
                assert (target / 'default.xex').read_bytes() == xex
                for path, payload in archives.items():
                    assert (target / path).read_bytes() == payload
            assert not list(root.glob(name + '.installing-*')), 'Partial staging folder retained'
            count += 1
            return target

        def iso(name, base=0, root_table=None):
            path = root / name
            payload_sector = 40
            sectors = []
            for payload in [xex, *archives.values()]:
                sectors.append(payload_sector)
                payload_sector += (len(payload) + 2047) // 2048
            archive_table = table([(Path(n).name, s, len(p))
                                   for (n, p), s in zip(archives.items(), sectors[1:])])
            valid_root = table([('default.xex', sectors[0], len(xex)),
                                ('LONG2DATA', 37, len(archive_table), 0x10)])
            directory = valid_root if root_table is None else root_table
            with path.open('wb') as out:
                out.truncate(base + payload_sector * 2048)
                out.seek(base + 32 * 2048)
                out.write(b'MICROSOFT*XBOX*MEDIA' + struct.pack('<II', 36, len(directory)))
                out.seek(base + 36 * 2048)
                out.write(directory)
                out.seek(base + 37 * 2048)
                out.write(archive_table)
                for sector, payload in zip(sectors, [xex, *archives.values()]):
                    out.seek(base + sector * 2048)
                    out.write(payload)
            return path

        for i, base in enumerate([0, 0xFB20, 0x20600, 0x2080000, 0xFD90000]):
            run(iso(f'valid-{i}.iso', base), f'iso-{i}')
        run(root / 'valid-0.iso', 'cancel-iso', 'canceled', cancel=1)
        bad = root / 'short.iso'
        bad.write_bytes(b'MICROSOFT*XBOX*MEDIA')
        run(bad, 'short', "isn't an Xbox")
        run(iso('bounds.iso', root_table=entry('default.xex', 0xFFFFFFFF, len(xex))),
            'bounds', 'invalid file entry')
        run(iso('tree.iso', root_table=entry('a', 40, 1, right=60000)), 'tree', 'directory tree')
        run(iso('cycle.iso', root_table=table([('a', 40, 1), ('b', 40, 1)])[:16] +
                entry('b', 40, 1, right=4)), 'cycle', 'directory tree')
        run(iso('dir-cycle.iso', root_table=entry('loop', 36, 18, 0x10)),
            'dir-cycle', 'directory table')
        run(iso('traversal.iso', root_table=entry('..', 40, 1)), 'traversal', 'unsafe filename')
        run(iso('duplicate.iso', root_table=table([('a', 40, 1), ('A', 40, 1)])),
            'duplicate', 'duplicate')
        wrong_version = iso('wrong-version.iso')
        with wrong_version.open('r+b') as out:
            out.seek(40 * 2048)
            out.write(b'NOPE')
        run(wrong_version, 'wrong-version', "isn't the supported")

        def zipped(name, prefix='', modify=None):
            path = root / name
            content = {'default.xex': xex, **archives}
            if modify:
                modify(content)
            with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as out:
                for filename, data in content.items():
                    out.writestr(prefix + filename, data)
            return path

        for i, prefix in enumerate(['', 'Dragon Ball Z - Burst Limit (USA)/']):
            run(zipped(f'valid-{i}.zip', prefix), f'zip-{i}')
        run(root / 'valid-0.zip', 'cancel-zip', 'canceled', cancel=1)
        run(zipped('bad-xex.zip', modify=lambda d: d.update({'default.xex': b'wrong'})),
            'bad-xex', "doesn't contain")
        run(zipped('missing.zip', modify=lambda d: d.pop('LONG2DATA/STREAM_US.CPK')),
            'missing', 'missing a required')
        run(zipped('unsafe.zip', modify=lambda d: d.update({'../outside': b'no'})),
            'unsafe', 'unsafe filename')
        run(zipped('zip-collision.zip', modify=lambda d: d.update({'DEFAULT.XEX': xex})),
            'zip-collision', 'duplicate')
        link = root / 'symlink.zip'
        with zipfile.ZipFile(link, 'w') as out:
            info = zipfile.ZipInfo('link')
            info.create_system = 3
            info.external_attr = 0o120777 << 16
            out.writestr(info, '/tmp/outside')
        # Java's ZIP reader extracts entries as regular files and never follows ZIP
        # symlink metadata; this link-only fixture is rejected for missing XEX.
        run(link, 'symlink', 'ZIP has no default.xex' if args.android_java else 'unsupported entries')
        truncated = root / 'truncated.zip'
        truncated.write_bytes((root / 'valid-0.zip').read_bytes()[:100])
        run(truncated, 'truncated', 'ZIP')

        occupied = root / 'occupied'
        occupied.mkdir()
        (occupied / 'keep.txt').write_text('keep')
        result = subprocess.run([str(probe), str(root / 'valid-0.zip'), str(occupied)],
                                capture_output=True, text=True)
        assert result.returncode == 1 and 'already contains' in result.stderr
        assert (occupied / 'keep.txt').read_text() == 'keep'
        count += 1
    print(f'PASS: {count} {"Android Java" if args.android_java else "native"} importer checks (ISO offsets, ZIP layouts, invalid/truncated input, '
          'cycles, traversal, collisions, cancellation cleanup, existing-file preservation).')


if __name__ == '__main__':
    main()
