# GhostNet — Design Panel Review

Before and during the build, the GhostNet design document (*"a plain-language
description of everything you can do, own, risk, and lose"*) went through a
review by a **panel of five AI reviewers**. Each reviewer took one lens and was
told to attack the design from it. Their proposals were then checked against
the design doc's own hard rules (Part Eleven, *"Things that never happen"*, and
the four identity pillars in Part Thirteen: **upkeep, bait, the cost of
stealing, and no reconfiguring mid-run**).

> **How this panel was run:** the five reviewers are separate critique roles
> played by the AI agent that built the game. They are not five independent
> models. To ground them in something real, the panel also got data from
> `tools/playtest_bot.py`, an automated player that plays the real binary
> end-to-end. Its numbers are quoted below.

Status key: ✅ adopted and built · 🕓 deferred (fits the design, not built yet) · ❌ rejected (it breaks a design wall)

---

## 1. SYSTEMS — "Does the loop close?"

| # | Proposal | Status |
|---|---|---|
| S1 | Build Part Thirteen's minimum loop first (run → lock → rack → bill) and tune the numbers in **one** file before adding anything else. | ✅ `src/game/balance.h` holds every number |
| S2 | Make time **action-based** (every command costs connection seconds) and have only downloads and anchors play out in real time. Then a console game stays fair, the trace still feels urgent, and runs can be played and tested reliably. | ✅ |
| S3 | Settle the bill against the machines that were running **when the run began**. A machine stolen mid-run is not billed, and one lost as bait drops off. | ✅ exactly as written in the doc |
| S4 | Burn a target for the rest of its map cycle once you have hit it, so the three runs per map are three separate decisions. | ✅ |
| S5 | Pick the two stealable targets, the trail, and passphrase placement from the world seed. Roll lock *answers* fresh on every connection, so a failed contract can't be memorised and replayed. | ✅ |

## 2. ECONOMY — "Does the upkeep actually hurt?"

| # | Proposal | Status |
|---|---|---|
| E1 | Clean T2 machine upkeep = 5cr. Trapped corporate T3 = 25cr. That is the doc's "five times as much" and "twenty-five credits a run" in actual numbers. | ✅ `TIER_UPKEEP + (noise-1)*3` |
| E2 | **Playtest finding:** the careful bot finished the whole campaign in **13 runs** with a three-machine network, which means the network never had to matter. Fix: Mags offers **one new contract per city refresh**. | ✅ the bot now needs about 15 runs, and contracts land around runs 1/4/7/10/13 |
| E3 | Add a small, permanent **deck upgrade** track (Breaker / Uplink / Shroud). That gives the doc's middle-game question something real to weigh: *"am I a person who maintains a network, or a person who buys better tools?"* | ✅ |
| E4 | An unpaid bill puts every external machine to sleep and disarms the bait. No debt, no partial payment. | ✅ seen in the reckless-bot runs |
| E5 | Repair spends "as much as you can afford and no more". | ✅ |
| E6 | Interest or loans for players who are broke. | ❌ the doc says *no debt, ever* |

## 3. PLAYER EXPERIENCE — "Will a human feel it?"

| # | Proposal | Status |
|---|---|---|
| X1 | **The Panel in-game:** three advisor AIs (VESPER for risk, LEDGER for money, MOTH for intel) read your meters out loud before a run and vote GO / CAUTION / ABORT. They never touch a lock. It answers *"never hide the numbers"* without adding a menu of numbers. | ✅ `src/game/panel.cpp` |
| X2 | Make the bait death a moment: the run stops, the name is typed out slowly, an unpleasant sound plays, and the count of burned machines goes up and never comes down. | ✅ |
| X3 | Arming a machine you **bought** as bait asks twice and spells out what happens. Arming a captured one happens at once. Scrapping requires typing the machine's name. | ✅ |
| X4 | Draw the rack as a physical object: a green `*` light when running, a dim `o` when stored, a red `X` fault light when dead, a `BAIT` tag on the bait, condition shown as scorch marks (`===~~~`), and noise as `)))`. | ✅ |
| X5 | A practice range for locks that costs nothing and where emergency attempts don't work. | ✅ |
| X6 | A per-machine "hum" that gets louder with noise. | 🕓 consoles only `Beep`. A future graphical build should do this |
| X7 | A tutorial pop-up for every system. | ❌ the doc wants the network to be discovered. The Panel and the first contract teach instead |

## 4. PUZZLE DESIGN — "Is the extra guess worth having?"

| # | Proposal | Status |
|---|---|---|
| P1 | Three lock types: **Keypad** (hit/near), **Rotor** (per wheel, where `~` means *one notch away*, not misplaced) and **Cipher** (word feedback). The Rotor is the doc's *"near-miss feedback works differently"* lock, and reports and MOTH warn about it. | ✅ |
| P2 | Running out of guesses **seals** the lock and sends a trace spike, but an unused emergency attempt or a passphrase can still get you in. That is exactly the "one unlucky moment" the compute job is meant to insure against. | ✅ |
| P3 | Allow one emergency attempt per lock, ever. Each machine gives it once per run, the home server gives it for free, and the machine that helped is named. | ✅ |
| P4 | Put passphrases in memos (shrines collect them), so a careful reader can steal hardware without ever solving a lock. | ✅ |
| P5 | Have the compute job show a hint (which digits are wrong). | ❌ the doc says compute *"does not give you a hint"* |

## 5. SECURITY & DISTRIBUTION — "Can a stranger trust the .exe?"

| # | Proposal | Status |
|---|---|---|
| D1 | The downloader never accepts a URL. The repo is fixed at compile time, and it talks only to an allowlist of GitHub hosts over HTTPS with TLS 1.2 or newer, with Windows validating the certificate. | ✅ |
| D2 | Follow redirects by hand so **every hop** is checked again (HTTPS, allowlisted host, port 443). | ✅ |
| D3 | Use a manifest of SHA-256 + size for every file. Hash while streaming, enforce size caps, and stage files so **nothing replaces anything until everything has verified**. | ✅ |
| D4 | Cross-check with GitHub's own `digest` for each asset, and require that assets were uploaded by `github-actions[bot]`, so a hand-uploaded file is refused. | ✅ |
| D5 | Optional ECDSA P-256 signing of the manifest, required once a public key is compiled in. | ✅ code + tooling. The repo owner turns it on (see `SECURITY.md`) |
| D6 | Per-user install with an `asInvoker` manifest, so it never asks for admin. Static runtime, so no DLLs need to be found. | ✅ |
| D7 | Authenticode code signing, to stop the SmartScreen warning. | 🕓 needs a paid certificate held by the owner |
| D8 | A full source download that compiles on the user's machine. | ❌ much bigger attack surface and needs a compiler. Prebuilt, hash-pinned binaries are safer |

---

## Ideas the panel rejected because they break a design wall

These came up in review and were dropped because Part Eleven forbids them:
daily login rewards, offline income, raids on your rack, crafting or merging
machines, levelling up machines, a premium currency, loot-box machine rolls,
and machines that run jobs for you while you're away. *"Every one of them would
replace a decision with waiting."*

## Deferred backlog (in the doc's own priority order)

1. A graphical client (SDL/raylib) with the rack as real chunky boxes, plus hum and burn sounds.
2. More lock families. Each one needs a clearly stated rule for near-miss feedback.
3. Several world seeds or "cities", so the two stealable targets keep moving between saves.
4. A seeded challenge mode that you start yourself (never on a timer).
