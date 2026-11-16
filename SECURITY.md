# Security: how GhostNet-Setup.exe downloads the game safely

`GhostNet-Setup.exe` is a small Windows program (`src/installer/main.cpp`). It
uses only built-in Windows APIs: **WinHTTP** for networking and **CNG/BCrypt**
for SHA-256 and ECDSA. It uses no third-party code and no external libraries,
and it is built with the static MSVC runtime.

## What it checks, in order

1. **Pinned source.** The repository (`Cilis1/GhostNet`) is compiled in. There
   is no option to point it anywhere else.
2. **HTTPS only, TLS 1.2+.** TLS 1.0/1.1 are switched off for the session.
   Certificate validation is done by Windows and is never disabled. A bad or
   intercepted certificate aborts the install.
3. **Host allowlist + manual redirects.** Every request and every redirect hop
   must be `https://` on port 443 to one of: `api.github.com`, `github.com`,
   `objects.githubusercontent.com`, `release-assets.githubusercontent.com`,
   `github-releases.githubusercontent.com`. Look-alike hosts are rejected.
4. **Release sanity.** It uses the latest *published* release only (no drafts,
   no prereleases). Every asset must be fully uploaded, must have been
   uploaded by **`github-actions[bot]`** (the CI pipeline in
   `.github/workflows/build.yml`), and must have a download URL inside
   `github.com/Cilis1/GhostNet/releases/download/`.
5. **Manifest.** `ghostnet-manifest.txt` lists the repo, the version and, for
   each file, its SHA-256 and exact size. The installer then checks that:
   - the manifest's repo matches the pinned repo, and its version matches the release tag;
   - file names are plain names only (no paths, no `..`, no hidden files, no `CON`/`NUL`-style device names);
   - every listed file exists in the release with the same size, and **GitHub's own recorded `sha256` digest** matches the manifest.
6. **Streaming verification.** Each file is downloaded with a hard size cap and
   hashed as it streams. The hash and size must match the manifest exactly. If
   anything fails, every staged file is deleted.
7. **Atomic install.** Files go to `.staging\` first. Only after **every** file
   has verified are they moved into place. Each one is then hashed again on disk.
8. **Least privilege.** It installs per-user to
   `%LOCALAPPDATA%\Programs\GhostNet`. The embedded `asInvoker` manifest means
   it never asks for admin rights.

## Optional: signed manifests (recommended)

The checks above defend against network attackers, CDN corruption and
hand-uploaded files. To also defend against a stolen GitHub token being used to
publish a fake release through CI, turn on signing:

```bash
bash tools/make-signing-key.sh                     # on your own machine
gh secret set GHOSTNET_SIGNING_KEY < ghostnet-signing-key.pem
# paste the printed public key into .github/signing-pubkey.txt, then commit it
# keep ghostnet-signing-key.pem offline (password manager), delete the local copy
```

From the next tagged release on, CI signs the manifest
(`ghostnet-manifest.sig`, ECDSA P-256 over SHA-256). The new installer
**refuses** any release that doesn't carry a valid signature from that key.
CI also refuses to publish if the public key is set but the secret is missing.

## What it does not do (yet)

- **Authenticode code signing.** The `.exe` files are not signed with a paid
  certificate, so SmartScreen may show *"Windows protected your PC"*. Check the
  SHA-256 in the release notes, then choose *More info → Run anyway*.
- It does not run anything it downloaded, unless you answer **Y** to *"Launch GhostNet now?"*

## Self-test

`GhostNet-Setup.exe --selftest` runs offline checks: SHA-256 known answers,
JSON edge cases, file-name attacks, the URL allowlist, and rejection of a
garbage signature. CI runs it on every build. Every tagged release is then
installed end-to-end from GitHub on a fresh Windows runner.

## Reporting a problem

Please open a GitHub issue, or contact the repository owner privately for anything sensitive.
