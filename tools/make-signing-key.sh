#!/usr/bin/env bash
# Turn on release signing for GhostNet-Setup.exe (optional, recommended).
#
# Run this on YOUR machine (never in CI, never commit the private key):
#   1. bash tools/make-signing-key.sh
#   2. gh secret set GHOSTNET_SIGNING_KEY < ghostnet-signing-key.pem
#   3. Paste the printed public key into .github/signing-pubkey.txt and commit it.
#   4. Store ghostnet-signing-key.pem somewhere safe (password manager) and delete the local copy.
# From the next release on, the installer refuses any manifest that isn't signed by this key.
set -euo pipefail
openssl ecparam -name prime256v1 -genkey -noout -out ghostnet-signing-key.pem
chmod 600 ghostnet-signing-key.pem
pub=$(openssl ec -in ghostnet-signing-key.pem -pubout -outform DER 2>/dev/null | tail -c 64 | od -An -tx1 -v | tr -d ' \n')
echo
echo "Public key (X||Y) - put this in .github/signing-pubkey.txt:"
echo "$pub"
