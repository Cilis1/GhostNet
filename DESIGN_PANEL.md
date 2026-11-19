# GhostNet — Design Panel Review

The game was built from the concept doc *"GHOSTNET: A game about a runner who
stops renting and starts owning"*, which replaced an earlier spec. Before and
during the build it went through a review by a **panel of five AI reviewers**.
Each reviewer took one lens and was told to attack the design from it. Their
proposals were then checked against the doc's own hard rules:

* its **never-include** list: no idle income, offline progress, build timers,
  raids, territory, sending machines on jobs, merging or crafting, a second
  currency, gacha, levelling, machines improving on their own, or permanent loss
  other than by choice or sacrifice;
* its pillars: **the network is optional and never replaces your tools**, **the
  bill**, **the decoy**, and **the deep job is out of reach of any deck alone**.

> **How this panel was run:** the five reviewers are separate critique roles
> played by the AI agent that built the game. They are not five independent
> models. To ground them in something real, the panel also got data from
> `tests/sim_bot.cpp`, an automated player that plays the real game rules
> end to end (it solves locks only from their feedback), and from a small simulation of the deep job. Their numbers are
> quoted below.

Status key: ✅ adopted and built · 🕓 deferred (fits the design, not built yet) · ❌ rejected (it breaks a design wall)

---

## 1. SYSTEMS — "Does the loop close?"

| # | Proposal | Status |
|---|---|---|
| S1 | Keep every number from the doc's price list in **one** file and tune nothing anywhere else. | ✅ `src/core/balance.h` (doc numbers are marked `[doc]`) |
| S2 | Make time **action-based**: every command costs connection seconds, and only downloads and captures play out in real time. Downloads multiply the trace (×1.25), because the doc says downloading *makes you easier to find*. | ✅ |
| S3 | Fix the bill **when the run starts**. A machine captured mid-run isn't billed that run, and a dead decoy drops off. Bill only after a completion, a repeat clear, a trace-out, a decoy loss or a capture. Never after practice or an empty walk-away. | ✅ |
| S4 | Track the acts from what the player actually does. *Renting* ends at the first trace-out, *Owning* starts at the first capture, and *The Deep Job* starts at the first decoy sacrifice. Act state is saved, and old 0.1.x saves derive it from their stats. | ✅ |
| S5 | When the board refreshes, the player forgets old listings. Jobs in progress (contracts, side routes, the deep job) survive. | ✅ |

## 2. ECONOMY — "Does the bill actually hurt, and is the deep job really out of a deck's reach?"

| # | Proposal | Status |
|---|---|---|
| E1 | Upkeep is the tier cost plus a surcharge for each signal step above minimum. Clean T2 costs 20cr, an open T1 capture 25cr, a watched T2 50cr and a corporate T3 90cr. Captured hardware is always louder than anything you can buy. | ✅ |
| E2 | **Deep-job simulation.** Trace 4.4%/s with a watchdog ramp, three archives of 34/36/38 MB, and a rotor lock. A *maxed deck with the passphrase* still fails on archive 3/3. A mid network (relay ~25%, proxy ~18%) with the phrase gets out at ~87% trace. | ✅ `DEEP_TRACE_RATE = 4.4` |
| E3 | **Playtest finding:** across seeds 7/11/23/42 the careful bot beats the deep job in **18–21 runs**, at 97% trace on seed 7, with N0 at T3, maxed relays, one proxy and the phrase. Its early attempt (deck + 1 machine) falls **~43% short**, and the shortfall readout says so. | ✅ enforced in CI (`ghostnet_sim … deep`, 17 seeds) |
| E4 | An unpaid bill takes nothing. Every machine except N0 goes dark, the decoy is disarmed, and the game says *no debt; earn credits, switch them back on*. | ✅ |
| E5 | A trace-out seizes the night's haul and the contract. The fine is capped at the player's credits. | ✅ |
| E6 | Interest or loans for players who are broke. | ❌ the doc says there is never debt |

## 3. PLAYER EXPERIENCE — "Will a human feel it?"

| # | Proposal | Status |
|---|---|---|
| X1 | **The Panel in-game:** three advisor AIs (VESPER for risk, LEDGER for money, MOTH for intel) read your meters before a run and vote GO / CAUTION / ABORT. MOTH tracks how close your network is to the deep job. | ✅ `src/core/intel.cpp`, drawn as THE PANEL tab |
| X2 | Make the decoy's death a moment. It is named, gets its own red flash and sound, and the line reads: *"HELIOS traced it instead of you."* The run is recorded as *escape bought with hardware*, and the lifetime count of burned machines never goes down. | ✅ |
| X3 | Arming a machine you **bought** as a decoy needs a confirmation dialog that spells out the consequence. A captured proxy arms at once. Scrapping shows the exact value and asks for confirmation. | ✅ |
| X4 | The rack screen (a live mesh graph of your machines) answers the doc's four questions at a glance: what's running, what it's worth, what the next run costs, and who is armed to die. | ✅ |
| X5 | A failed deep job ends with a **"how far short"** readout: archives secured, MB left, seconds still needed, and the percent you were short. | ✅ |
| X6 | Neither ending is a loss. Finish the deep job and *"The rack goes quiet. The machines stay on."* Walk away and you are *"filed: a runner who kept renting."* | ✅ |
| X7 | A tutorial pop-up for every system. | ❌ the doc wants the network to be discovered. The Panel and the first contract teach instead |

## 4. PUZZLE DESIGN — "Is information worth as much as money?"

| # | Proposal | Status |
|---|---|---|
| P1 | Three lock types: **Keypad** (hit/near), **Rotor** (per wheel, where amber/`+-1` means *one notch away*, not misplaced) and **Cipher** (word feedback). | ✅ |
| P2 | One contract pays mostly in **information** (QUIET FLOOR). It reveals the deep archive and the HELIOS Facilities Annex. | ✅ |
| P3 | Keep the deep job's passphrase in **one** place, the Facilities Annex. The annex stays on the board until you have read it, so the cleanest way into the deep job is to go and look. | ✅ |
| P4 | `[HW]` markers and the hardware flag in reports appear only while a scouting machine is running. | ✅ |
| P5 | Have the compute job show a hint (which digits are wrong). | ❌ compute is an emergency attempt, not a hint |

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

## 6. ART & FEEL (0.3.0) — "It has to look like the city, not a terminal."

| # | Proposal | Status |
|---|---|---|
| A1 | Leave the terminal behind: a real window (raylib), a 1280×720 virtual canvas scaled to any size, F11 for borderless fullscreen. | ✅ `src/gui/` |
| A2 | The city is always there: a parallax skyline with lit windows, neon signs, the HELIOS tower and rain over a synthwave grid, with a CRT scanline pass on top. | ✅ `gx::city`, `gx::crt` |
| A3 | Locks are tiles you can read at a glance: green exact, amber near (rotor marks `+-1`), dark miss, with a staggered reveal; keypad shows HIT/NEAR pips. Clickable on-screen keys as well as the keyboard. | ✅ |
| A4 | The trace is a ring gauge that turns red and counts down the seconds near the end; lockouts and alarms flash and shake the screen. A trace-out gets a full-screen **TRACED** slam; a clean exit gets **JACKED OUT**. | ✅ |
| A5 | The route is drawn: YOU → your machines → the target, with packets moving along the links, and the decoy circled in red. | ✅ |
| A6 | Every sound is synthesised at start-up (no audio files to ship): key clicks, lock hits, alarms, the decoy burn, the hum of the rack. M mutes. | ✅ `src/gui/audio.cpp` |
| A7 | CI drives every screen of the real front end on a software renderer and uploads the screenshots, so the art can't silently break. | ✅ `GhostNet --shots DIR` |
| A8 | Animated 3D rack room. | 🕓 the 2D mesh says the same thing faster |
| A9 | Cosmetic shop for neon colours. | ❌ that's a second currency in disguise |

---

## Ideas the panel rejected because they break a design wall

These came up in review and were dropped because the never-include list forbids
them: daily login rewards, offline income, raids on your rack, sending machines
out on jobs, crafting or merging machines, levelling up machines, a premium
currency, loot-box machine rolls, and machines that work while you're away.

## 0.5.0 — Subnets and above-tier hardware: how they stay inside the walls

* **Clusters are not merging.** Three matching tier 3+ machines in a subnet *act* one tier
  higher only while all three are plugged in together. They stay three machines, each with
  its own condition, trait, bill and wear. Pull one out and the bonus ends at once. Nothing
  is consumed and nothing is permanent.
* **Prototypes are not levelling.** Tier 4 can't be grown into. It only arrives by capturing
  a HELIOS black-site prototype, and it comes with a lifelong UNSTABLE drawback.
* **Subnets don't work while you're away.** A subnet acts only during runs you play. It bills
  and wears with the rest, and its once-per-run strike is a choice you make at the lock.
* **Subnets are optional.** The deep job is still beatable without a gateway (sims: careful
  mode is unchanged at ~20 runs).

## 0.6.0 — Act IV, the Ledger: how it stays inside the walls

* **Towers never act on their own.** No timers, no raids, no decay. A tower waits on your
  board until you start a climb, and a floor you've taken stays taken.
* **The only reaction is to a trace.** A trace-out re-keys that tower's doors, so phrases you
  held go stale. Nothing you own is lost; floor plans and taken floors are kept.
* **Doctrines are told, not sprung.** The board and the briefing name each tower's doctrine
  and what beats it, before you spend anything.
* **Prep is bought once with credits.** Nothing new to grind: no second currency, no
  timers on intel.
* **The deep-job pillar still holds.** The bot's kit trials require that the matched kit
  takes every floor and that one build sent against the wrong doctrine fails.

## 0.7.0 — Kessler codes persist: patience counts, the wall moves to the first visit

* **The problem.** A Kessler door re-rolled every connection, so a deck without the matched
  kit faced the same lottery every time: about 0.2% to open with 4 guesses. Patience taught
  you nothing.
* **The fix: the code stays.** It changes only when a player action re-keys it: a trace (the
  whole tower) or a full alarm log (that floor). Nothing is timed, and nothing changes
  while you're away. Your rows are kept as guesses, never as marks, and replayed as LOGGED
  rows for free.
* **The alarm log keeps it honest.** 6 misses fill it; the 7th seals the door and re-keys
  that floor. Brute force still fails; careful play needs 5 misses or fewer (4 with a
  survey).
* **More information, not more guesses.** Directional rotor arrows, a CODES LEFT counter
  (MOTH counts, never names), and a TUMBLER SURVEY bought with credits. No new currency,
  no timers.
* **The wall, reworded.** The matched kit takes every floor on its first visit, and the
  wrong kit can't. Patience gets the wrong kit there later (bot: BREAKER 3 plus surveys
  climbs all three floors within 9 connections, with no alarm re-keys). BREAKER 0 without
  compute never opens a door, because its one guess goes on the keypad every time.

## Deferred backlog

1. ~~A graphical client~~ built in 0.3.0 (see section 6).
2. More lock families. Each one needs a clearly stated rule for near-miss feedback.
3. ~~A post-ending "owner" mode~~ built in 0.6.0 as Act IV, the Ledger (see below).
4. Authenticode code signing, to stop the SmartScreen warning (needs a paid certificate held by the owner).
