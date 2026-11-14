# GhostNet

> *A game about a runner who stops renting and starts owning.*

**GHOSTNET** is a terminal hacking game written in C++17. In this city, server
space is real estate. You rent a room called THE RACK, take jobs off the board,
and break into servers before HELIOS (the maintenance corp whose trace meter is
"how long you've been an open ticket") closes your ticket. Then you find what the
dead courier SAINT left behind: a salvaged 3-slot rack and a home server, **N0**.
From there you build a network you own, and every machine in it bills you, wears
out, and can die in your place. The goal is to survive, then pull off **the deep
job**.

It is built from the concept doc *"GHOSTNET: A game about a runner who stops
renting and starts owning"*. A [design panel of AI reviewers](DESIGN_PANEL.md)
proposed improvements, and each one was checked against the doc's own rules.

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
THE RACK -> JOB BOARD -> pick a listing -> scan it / ask the Panel -> CONNECT
  door>   break the lock in limited guesses   (or: phrase <word>)
  inside> ls / cat <n> / get <n> / steal / disconnect
THE RECKONING -> haul, fines, the bill, wear, machines going dark
```

* **Three acts.** *Renting* ends at your first trace-out. *Owning* starts with
  your first captured machine. *The Deep Job* starts when a decoy first dies for you.
* **Trace-out:** your accounts are seized, so the night's haul is lost. You pay a fine
  (never more than you have), the bill still comes due, and your machines wear hard.
* **Downloading** takes time and makes you easier to find: the trace runs ×1.25 while bytes move.
* **Locks:** Keypad (hit/near), Rotor (`~` = one notch away, *not* misplaced) and
  Cipher (word feedback). A passphrase opens a door without breaking it.
* **The deck:** tools you buy for yourself (Breaker, Uplink, Shroud, Lens). They are
  permanent, never billed, and separate from the network.
* **GhostNet** (unlocks after your first contract): N0 plus up to 10 machines.
  * **Relay** speeds downloads.
  * **Proxy** slows the trace.
  * **Scout** gives target reports and reveals `[HW]` listings.
  * **Compute** holds one emergency extra guess.
  * Bought machines are clean tier 2: Relay 400, Scout 450, Proxy 500, Compute 550.
  * Tier 3 comes only from upgrading N0 or stealing corporate iron.
* **The bill:** running machines bill you after a run that does something. If you can't
  pay, nothing is taken and everything except N0 goes dark. There is never debt.
* **Decoy:** arm a Proxy. If the trace fills, HELIOS traces it instead of you. The decoy
  is destroyed, the fine is halved, and you keep the night.
* **The deep job** is tuned so that no deck alone can finish it. You can try it as soon
  as it appears. If you fall short, the game tells you by how much.
  * **Owner ending:** finish the deep job and retire as an owner.
  * **Renter ending:** walk away and be filed as a runner who kept renting.
  * Neither ending is a loss.
* **The Panel:** VESPER (risk), LEDGER (money) and MOTH (intel) are advisor AIs. They read
  your meters and vote before each run. They never solve a lock.
* **Never in this game:** idle income, offline progress, build timers, raids, a second currency, gacha or levelling.

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
* `python3 tools/playtest_bot.py build/GhostNet 7 15`: an automated player that plays the real game. It solves locks from the on-screen feedback and learns passphrases only by reading memos in-game.
  * Add `chaos` at the end for a reckless player. It exercises lockouts, trace-outs, decoy sacrifices and unpaid bills.
  * Add `deep` (e.g. `... 7 70 deep`) and the run fails unless the bot builds a network and beats the deep job. It also makes one doomed early attempt, to check the shortfall readout.
* `GhostNet-Setup.exe --selftest`: the downloader's offline security self-test.

## Releasing

Push a tag like `v0.2.1`. `.github/workflows/build.yml` then:

1. builds and tests on Linux (including the careful, reckless and deep-job playtest bots) and on Windows (MSVC, static runtime);
2. publishes a release with `GhostNet.exe`, `README.txt`, `ghostnet-manifest.txt` (SHA-256 of every file, signed if a key is configured) and `GhostNet-Setup.exe`;
3. downloads that published installer on a fresh Windows runner and does a real install → run → verify → uninstall.

## Layout

```
src/game/        the game (portable C++17)
  balance.h      every tunable number
  network.cpp    owning machines        netscreen.cpp  the rack screen
  run.cpp        connecting, locks, downloads, capture, the reckoning, acts
  world.cpp      job board, contracts, the deep job, side routes, shrines
  report.cpp     scouting reports       panel.cpp      the advisor AIs
src/installer/   GhostNet-Setup.exe (WinHTTP + CNG, no third-party code)
tools/           playtest bot, manifest + signing tools
packaging/       README.txt shipped with the game
res/             icon, version info, asInvoker manifest
```
