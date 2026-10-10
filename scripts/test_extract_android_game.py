"""Synthetic disc tests: no copyrighted game files required."""
import hashlib
import struct
import tempfile
import unittest
from pathlib import PurePosixPath
from unittest.mock import patch
import extract_android_game as extractor


def entry(name, sector=40, size=4, attributes=0, left=0, right=0):
    name = name.encode("ascii")
    return struct.pack("<HHIIBB", left, right, sector, size, attributes, len(name)) + name


class DiscTests(unittest.TestCase):
    def disc(self, table, payload=b"test", base=0):
        source = tempfile.TemporaryFile()
        self.addCleanup(source.close)
        source.truncate(base + 44 * extractor.SECTOR)
        source.seek(base + 32 * extractor.SECTOR)
        source.write(extractor.MAGIC + struct.pack("<II", 36, len(table)))
        source.seek(base + 36 * extractor.SECTOR)
        source.write(table)
        source.seek(base + 40 * extractor.SECTOR)
        source.write(payload)
        source.flush()
        return extractor.Disc(source)

    def test_partition_offsets_and_payload(self):
        for base in (0, 0xFB20, 0x20600, 0x2080000, 0xFD90000):
            disc = self.disc(entry("default.xex"), base=base)
            self.assertEqual(disc.files[0][0], PurePosixPath("default.xex"))
            self.assertEqual(disc.read(disc.files[0][1], 4), b"test")

    def test_children_and_case_collision(self):
        root = entry("a", right=4)
        disc = self.disc(root + b"\0" + entry("b"))
        self.assertEqual([p.as_posix() for p, _, _ in disc.files], ["a", "b"])
        with self.assertRaisesRegex(ValueError, "Duplicate ISO"):
            self.disc(root + b"\0" + entry("A"))

    def test_unsafe_names(self):
        for name in ("..", ".", "a/b", "a\\b", "a:b", "a\0b"):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "Unsafe"):
                self.disc(entry(name))

    def test_cyclic_tree_and_bad_pointer(self):
        with self.assertRaisesRegex(ValueError, "directory tree"):
            self.disc(entry("a", right=4) + b"\0" + entry("b", right=4))
        with self.assertRaisesRegex(ValueError, "directory tree"):
            self.disc(entry("a", right=60000))

    def test_directory_cycle(self):
        table = entry("dir", sector=36, size=17, attributes=0x10)
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            self.disc(table)

    def test_file_bounds(self):
        with self.assertRaisesRegex(ValueError, "File outside"):
            self.disc(entry("default.xex", size=1000000))

    def test_executable_hash_and_required_archives(self):
        disc = self.disc(entry("default.xex"))
        with patch.object(extractor, "USA_SIZE", 4):
            with self.assertRaisesRegex(ValueError, "Unsupported executable"):
                disc.verify_usa()
            digest = hashlib.sha1(b"test").hexdigest()
            with patch.object(extractor, "USA_SHA1", digest):
                with self.assertRaisesRegex(ValueError, "missing required"):
                    disc.verify_usa()
                disc.names.update({"long2data/long2data_us.cpk", "long2data/stream_us.cpk",
                                   "long2data/stream_jp.cpk"})
                self.assertEqual(disc.verify_usa(), digest)


if __name__ == "__main__":
    unittest.main()
