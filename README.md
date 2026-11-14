# GhostNet

> *own the network. pay the bill. lose what you name.*

**GHOSTNET** is a terminal hacking game written in C++17. You break into
servers before the trace finds you. Along the way you build a private mesh of
machines you own, and every one of them bills you, wears out, and can die in
your place.

It is built from the *GHOSTNET* design document (the system extracted from
*NEON//FALL*). A [design panel of AI reviewers](DESIGN_PANEL.md) proposed
improvements, and each was checked against the document's own rules.

## Download & play (Windows)

1. Go to **[Releases](https://github.com/Cilis1/GhostNet/releases/latest)** and download **`GhostNet-Setup.exe`**.
2. Run it. It downloads the game from this repository's release over HTTPS,
   checks every file's SHA-256, installs to `%LOCALAPPDATA%\Programs\GhostNet`
   (no admin needed) and adds a Start Menu shortcut.
3. Run it again any time to update or repair. Use `GhostNet-Setup.exe --uninstall` to remove the game (your saves are kept).

The exe isn't code-signed yet, so SmartScreen may warn you. Check the hash in
the release notes, then click *More info → Run anyway*. See
[SECURITY.md](SECURITY.md) for exactly what the downloader checks.

## How it plays

```
JOB BOARD -> pick a target -> scan it / ask the Panel -> CONNECT
  door>   break the lock in limited guesses   (or: phrase <word>)
  inside> ls / cat <n> / get <n> / steal / disconnect
THE BILL -> haul, fines, upkeep, wear, machines going dark
```

* **Locks:** Keypad (hit/near), Rotor (`~` = one notch away, *not* misplaced) and Cipher (word feedback).
* **GhostNet** (unlocks after your first contract): **Relay** speeds downloads,
  **Proxy** slows the trace, **Scout** gives target reports, and **Compute**
  holds one emergency extra guess per run.
* **The bill:** running machines bill you after every run and wear down. If you can't pay, everything goes dark (there is never debt).
* **Bait:** arm a Proxy. If you're traced, it is destroyed in your place and the fine is halved.
* **Stealing:** targets marked `[HW]` carry detachable hardware. Rob first and the machine arrives damaged. Steal first and you leave the money behind.
* **The Panel:** VESPER (risk), LEDGER (money) and MOTH (intel) are advisor AIs who read your meters and vote before each run. They never solve a lock.
* There is no idle income, no timers and no raids. A five-contract campaign then continues as an endless city.

## Build from source

Any C++17 compiler and CMake 3.15+:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/GhostNet          # build\Release\GhostNet.exe on Windows
```

On Windows this also builds `GhostNet-Setup.exe`. The game itself also runs on Linux and macOS.

Useful flags: `--seed N` (world seed), `--fast` (no animations), `--no-color`.

## Tests

* `ctest --test-dir build`: tests for the installer's JSON/manifest/file-name validation.
* `python3 tools/playtest_bot.py build/GhostNet 7 15`: an automated player that plays the real game, solving locks from the on-screen feedback.
  Add `chaos` at the end for a reckless player that exercises lockouts, emergency attempts, trace-outs and unpaid bills.
* `GhostNet-Setup.exe --selftest`: the downloader's offline security self-test.

## Releasing

Push a tag like `v0.1.1`. `.github/workflows/build.yml` then:

1. builds and tests on Linux (including both playtest bots) and on Windows (MSVC, static runtime);
2. publishes a release with `GhostNet.exe`, `README.txt`, `ghostnet-manifest.txt` (SHA-256 of every file, signed if a key is configured) and `GhostNet-Setup.exe`;
3. downloads that published installer on a fresh Windows runner and does a real install → run → verify → uninstall.

## Layout

```
src/game/        the game (portable C++17)
  balance.h      every tunable number
  network.cpp    owning machines        netscreen.cpp  the rack screen
  run.cpp        connecting, locks, downloads, stealing, the bill
  world.cpp      job board, contracts, side routes, shrines
  report.cpp     scouting reports       panel.cpp      the advisor AIs
src/installer/   GhostNet-Setup.exe (WinHTTP + CNG, no third-party code)
tools/           playtest bot, manifest + signing tools
packaging/       README.txt shipped with the game
res/             icon, version info, asInvoker manifest
```
