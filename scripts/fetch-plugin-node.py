# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage only Node and its license from the archive pinned by this repository.

The result has the official archive layout expected by PluginRuntime.cmake.
No extracted executable, package hook, or npm installer is run by this tool.
"""
import argparse
import hashlib
import json
import shutil
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ARCHIVE_LIMIT = 160 * 1024 * 1024
BINARY_LIMIT = 256 * 1024 * 1024
LICENSE_LIMIT = 16 * 1024 * 1024


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


class NodeRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, target):
        if not target.startswith("https://nodejs.org/"):
            raise ValueError("Node archive redirect left the official HTTPS origin")
        return super().redirect_request(request, fp, code, message, headers, target)


def download(url, path):
    opener = urllib.request.build_opener(NodeRedirects())
    with opener.open(url, timeout=30) as source, path.open("xb") as target:
        total = 0
        while block := source.read(1024 * 1024):
            total += len(block)
            if total > ARCHIVE_LIMIT:
                raise ValueError("Node archive exceeds the download budget")
            target.write(block)


def stage(platform, arch, output, archive=None, lock=None):
    lock = lock or json.loads((REPO / "packages/plugin-host/runtime-lock.json").read_text())
    version = lock["version"]
    prefix = f"node-{version}-{platform}-{arch}"
    filename = prefix + (".zip" if platform == "win" else ".tar.xz")
    expected = lock["archives"].get(filename)
    if not expected:
        raise ValueError("No pinned Node archive for this platform and architecture")
    output = output.resolve()
    if output.exists():
        raise ValueError("Runtime output must be a new directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".nnp-node-", dir=output.parent) as temporary:
        work = Path(temporary)
        stored = work / filename
        if archive:
            if archive.stat().st_size > ARCHIVE_LIMIT:
                raise ValueError("Node archive exceeds the archive budget")
            shutil.copyfile(archive, stored)
        else:
            download(f"https://nodejs.org/dist/{version}/{filename}", stored)
        if digest(stored) != expected:
            raise ValueError("Node archive SHA-256 does not match runtime-lock.json")
        binary = "node.exe" if platform == "win" else "bin/node"
        required = {f"{prefix}/{binary}": BINARY_LIMIT, f"{prefix}/LICENSE": LICENSE_LIMIT}
        package = zipfile.ZipFile(stored) if platform == "win" else tarfile.open(stored, "r:xz")
        with package:
            for name, limit in required.items():
                members = [m for m in (package.infolist() if platform == "win" else package.getmembers())
                           if (m.filename if platform == "win" else m.name) == name]
                if len(members) != 1:
                    raise ValueError("Pinned archive must contain each required member exactly once")
                member = members[0]
                size = member.file_size if platform == "win" else member.size
                if not 0 < size <= limit:
                    raise ValueError("Node archive member exceeds its size budget")
                if platform == "win":
                    mode = member.external_attr >> 16
                    if member.is_dir() or (mode & 0o170000) not in (0, 0o100000):
                        raise ValueError("Node archive member is not a regular file")
                    stream = package.open(member)
                else:
                    if not member.isfile():
                        raise ValueError("Node archive member is not a regular file")
                    stream = package.extractfile(member)
                destination = work / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                with stream, destination.open("xb") as target:
                    shutil.copyfileobj(stream, target)
                if destination.stat().st_size != size:
                    raise ValueError("Node archive member size mismatch")
        (work / prefix / binary).chmod(0o755)
        report = {"version": version, "platform": platform, "arch": arch,
                  "node": str(output / prefix / binary), "archive": str(output / filename),
                  "archiveSha256": expected, "nodeSha256": digest(work / prefix / binary)}
        (work / "runtime.json").write_text(json.dumps(report, indent=2) + "\n")
        # TemporaryDirectory tolerates its directory having been moved.
        work.rename(output)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=["darwin", "linux", "win"], required=True)
    parser.add_argument("--arch", choices=["arm64", "x64"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--archive", type=Path, help="Verify an already downloaded archive offline")
    args = parser.parse_args()
    try:
        print(json.dumps(stage(args.platform, args.arch, args.output, args.archive), indent=2))
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as failure:
        parser.exit(1, f"{failure}\n")


if __name__ == "__main__":
    main()
