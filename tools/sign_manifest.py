#!/usr/bin/env python3
"""Sign ghostnet-manifest.txt with an ECDSA P-256 key (optional hardening).

usage: sign_manifest.py <private-key.pem> <manifest> <out.sig>
Writes the signature as hex of raw r||s (64 bytes), which is what Windows CNG
(BCryptVerifySignature) expects. Uses only the openssl CLI + the stdlib.
"""
import subprocess
import sys


def der_to_raw(der: bytes) -> bytes:
    # SEQUENCE { INTEGER r, INTEGER s }
    assert der[0] == 0x30, "not a DER sequence"
    i = 2 if der[1] < 0x80 else 2 + (der[1] & 0x7F)
    out = b""
    for _ in range(2):
        assert der[i] == 0x02, "expected INTEGER"
        n = der[i + 1]
        v = der[i + 2:i + 2 + n].lstrip(b"\x00")
        assert len(v) <= 32
        out += v.rjust(32, b"\x00")
        i += 2 + n
    return out


def main():
    key, manifest, out = sys.argv[1:4]
    der = subprocess.run(["openssl", "dgst", "-sha256", "-sign", key, manifest], check=True, capture_output=True).stdout
    raw = der_to_raw(der)
    with open(out, "w", newline="\n") as fh:
        fh.write(raw.hex() + "\n")
    print("signed", manifest, "->", out)


if __name__ == "__main__":
    main()
