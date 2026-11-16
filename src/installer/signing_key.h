// Optional release-signing public key (ECDSA P-256), as 128 hex chars: X (32 bytes) || Y (32 bytes).
//
// EMPTY  -> the installer relies on: pinned repo + HTTPS (TLS 1.2+, certificate validated by Windows)
//           + host allowlist + uploader check + GitHub's asset digests + per-file SHA-256 from the manifest.
// SET    -> additionally REQUIRES ghostnet-manifest.sig to be a valid signature by the matching private key.
//           A release without a valid signature is refused.
//
// To turn signing on, run tools/make-signing-key.sh and follow its instructions (see SECURITY.md).
#pragma once

#ifndef GHOSTNET_SIGNING_PUBKEY_HEX
#define GHOSTNET_SIGNING_PUBKEY_HEX ""
#endif
