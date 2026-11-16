#!/usr/bin/env python3
"""Write ghostnet-manifest.txt for a release.

usage: make_manifest.py <repo> <version> <out-manifest> <file> [<file> ...]
Every payload file is listed with its SHA-256 and size; the installer refuses
anything that doesn't match byte-for-byte.
"""
import hashlib
import os
import re
import sys

SAFE = re.compile(r"^[A-Za-z0-9_-][A-Za-z0-9._-]{0,63}$")


def main():
    repo, version, out, files = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    if not re.fullmatch(r"[A-Za-z0-9.+-]{1,32}", version):
        sys.exit(f"bad version {version!r}")
    lines = ["GHOSTNET-MANIFEST 1", f"repo {repo}", f"version {version}"]
    for path in files:
        name = os.path.basename(path)
        if not SAFE.match(name) or ".." in name or name.endswith("."):
            sys.exit(f"unsafe file name {name!r}")
        data = open(path, "rb").read()
        lines.append(f"file {hashlib.sha256(data).hexdigest()} {len(data)} {name}")
    with open(out, "w", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(open(out).read())


if __name__ == "__main__":
    main()
