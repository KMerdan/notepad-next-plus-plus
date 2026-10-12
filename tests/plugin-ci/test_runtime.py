# SPDX-License-Identifier: GPL-3.0-or-later
"""Check archive trust and layouts without executing a downloaded runtime."""
import hashlib
import importlib.util
import io
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("runtime", ROOT / "scripts/fetch-plugin-node.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def archive(self, platform="darwin", duplicate=False, symlink=False):
        prefix = f"node-v24.21.0-{platform}-arm64"
        binary = "node.exe" if platform == "win" else "bin/node"
        path = self.root / ("fixture.zip" if platform == "win" else "fixture.tar.xz")
        entries = [(f"{prefix}/{binary}", b"fixture-runtime"), (f"{prefix}/LICENSE", b"fixture-license")]
        if duplicate:
            entries.append(entries[0])
        if platform == "win":
            with zipfile.ZipFile(path, "w") as archive:
                for name, content in entries:
                    archive.writestr(name, content)
        else:
            with tarfile.open(path, "w:xz") as archive:
                for name, content in entries:
                    entry = tarfile.TarInfo(name)
                    entry.size = len(content)
                    if symlink and name.endswith(binary):
                        entry.type = tarfile.SYMTYPE
                        entry.linkname = "/outside"
                    archive.addfile(entry, io.BytesIO(content))
        filename = prefix + (".zip" if platform == "win" else ".tar.xz")
        lock = {"version": "v24.21.0", "archives": {filename: hashlib.sha256(path.read_bytes()).hexdigest()}}
        return path, lock

    def test_posix_and_windows_offline_layouts(self):
        for platform in ["darwin", "linux", "win"]:
            with self.subTest(platform=platform):
                archive, lock = self.archive(platform)
                report = runtime.stage(platform, "arm64", self.root / platform, archive, lock)
                node = Path(report["node"])
                self.assertEqual(node.read_bytes(), b"fixture-runtime")
                license = node.parent / "LICENSE" if platform == "win" else node.parent.parent / "LICENSE"
                self.assertEqual(license.read_bytes(), b"fixture-license")
                self.assertEqual(Path(report["archive"]).read_bytes(), archive.read_bytes())

    def test_digest_failure_does_not_publish_output(self):
        archive, lock = self.archive()
        lock["archives"] = {name: "0" * 64 for name in lock["archives"]}
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            runtime.stage("darwin", "arm64", self.root / "output", archive, lock)
        self.assertFalse((self.root / "output").exists())
        self.assertFalse(list(self.root.glob(".nnp-node-*")))

    def test_duplicate_and_symbolic_runtime_members_are_rejected(self):
        for options in [{"duplicate": True}, {"symlink": True}]:
            with self.subTest(options=options):
                archive, lock = self.archive(**options)
                with self.assertRaises(ValueError):
                    runtime.stage("darwin", "arm64", self.root / "output", archive, lock)
                self.assertFalse((self.root / "output").exists())

    def test_existing_output_is_preserved(self):
        archive, lock = self.archive()
        output = self.root / "output"
        output.mkdir()
        (output / "sentinel").write_text("retain")
        with self.assertRaisesRegex(ValueError, "new directory"):
            runtime.stage("darwin", "arm64", output, archive, lock)
        self.assertEqual((output / "sentinel").read_text(), "retain")

    def test_unpinned_architecture_is_rejected(self):
        archive, lock = self.archive()
        with self.assertRaisesRegex(ValueError, "No pinned"):
            runtime.stage("darwin", "x64", self.root / "output", archive, lock)


if __name__ == "__main__":
    unittest.main()
