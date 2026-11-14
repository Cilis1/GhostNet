#!/usr/bin/env python3
"""GhostNet automated playtester.

Drives the real game binary through stdin/stdout (with --fast --no-color),
solving locks from the feedback the game prints - no cheats, no hidden hooks.
It plays the campaign, manages the network, robs, steals, and arms bait.

usage: python3 tools/playtest_bot.py path/to/ghostnet [seed] [runs]
"""
import itertools
import random
import os
import re
import select
import subprocess
import sys
import tempfile
import time

WORDS = """ghost cache proxy relay trace cable laser shell admin patch crypt token guard vault noise pulse virus drone
flash debug logic stack queue array pixel modem input error quiet black night steel glass rogue agent cyber nexus spike
sigil rivet bytes route cloak smoke ember flare frost storm tower orbit radar sonar lumen prism quark rumor vapor wired
pivot crane forge grime haunt index joint knife lodge metal nerve omega plaza quota raven scope thorn ultra vigil wrath
xenon yield zebra audit blade chord delta drift fable gamma hydra irony jewel karma lunar mirth north onion piano relic
salvo tango umbra venom whisk alarm badge boost cargo codec depot fiber gauge hatch intel latch mains""".split()
_src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "game", "locks.cpp")
if os.path.exists(_src):  # keep the bot's dictionary in sync with the game's
    _body = open(_src).read().split("kWords = {", 1)[1].split("};", 1)[0]
    WORDS = re.findall(r'"([a-z]+)"', _body)
CHAOS = False


class Game:
    def __init__(self, exe, seed, home):
        env = dict(os.environ, HOME=home, APPDATA=home)
        self.p = subprocess.Popen([exe, "--fast", "--no-color", "--seed", str(seed)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        self.buf = ""
        self.transcript = []

    def read(self, quiet=0.15, limit=5.0):
        out = ""
        end = time.time() + limit
        while time.time() < end:
            r, _, _ = select.select([self.p.stdout], [], [], quiet)
            if not r:
                if out:
                    break
                continue
            chunk = os.read(self.p.stdout.fileno(), 65536).decode("utf-8", "replace")
            if not chunk:
                break
            out += chunk
        self.transcript.append(out)
        self.buf = out
        return out

    def send(self, line):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()
        return self.read()


def bulls_cows(guess, secret):
    hit = sum(g == s for g, s in zip(guess, secret))
    near = sum(1 for i, g in enumerate(guess) if g != secret[i] and g in secret)
    return hit, near


def wordle(guess, secret):
    res = ["."] * 5
    counts = {}
    for i in range(5):
        if guess[i] == secret[i]:
            res[i] = "G"
        else:
            counts[secret[i]] = counts.get(secret[i], 0) + 1
    for i in range(5):
        if res[i] == "G":
            continue
        if counts.get(guess[i], 0) > 0:
            res[i] = "Y"
            counts[guess[i]] -= 1
    return "".join(res)


class Solver:
    def __init__(self, kind):
        self.kind = kind
        if kind == "KEYPAD":
            self.cands = ["".join(p) for p in itertools.permutations("0123456789", 4)]
        elif kind == "ROTOR":
            self.wheels = [set(range(10)) for _ in range(4)]
        else:
            self.cands = [w for w in WORDS if len(w) == 5]
        self.last = None

    def next(self):
        if self.kind == "ROTOR":
            g = ""
            for w in self.wheels:
                opts = sorted(w)
                # pick a value that splits the remaining set well: prefer middle of a cluster
                best = min(opts, key=lambda v: max(
                    len([o for o in opts if o == v]), len([o for o in opts if abs(o - v) == 1]),
                    len([o for o in opts if abs(o - v) > 1])))
                g += str(best)
            self.last = g
            return g
        if not self.cands:
            self.last = "0123" if self.kind == "KEYPAD" else "crane"
            return self.last
        self.last = self.cands[0] if len(self.cands) < 3 else self.cands[len(self.cands) // 2]
        return self.last

    def feed(self, line):
        g = self.last
        if self.kind == "KEYPAD":
            m = re.search(r"HIT (\d)\s+NEAR (\d)", line)
            if not m:
                return
            fb = (int(m.group(1)), int(m.group(2)))
            self.cands = [c for c in self.cands if bulls_cows(g, c) == fb]
        elif self.kind == "ROTOR":
            marks = re.findall(r" ([=~.]) ", line.split("->", 1)[1] + " ")
            if len(marks) < 4:
                return
            for i, mk in enumerate(marks[:4]):
                v = int(g[i])
                if mk == "=":
                    self.wheels[i] = {v}
                elif mk == "~":
                    self.wheels[i] &= {v - 1, v + 1}
                else:
                    self.wheels[i] = {o for o in self.wheels[i] if abs(o - v) > 1}
        else:
            seg = line.split("->", 1)[1]
            toks = re.findall(r"\[([A-Z])\]|\(([a-z])\)| ([a-z]) ", seg)
            if len(toks) < 5:
                return
            pat = "".join("G" if t[0] else "Y" if t[1] else "." for t in toks[:5])
            self.cands = [c for c in self.cands if wordle(g, c) == pat]


def solve_door(game, out, stats):
    m = re.search(r"(KEYPAD|ROTOR|CIPHER) LOCK", out)
    if not m:
        return out
    solver = Solver(m.group(1))
    for _ in range(14):
        if "ACCESS GRANTED" in out or "accepts the phrase" in out or "door>" not in out:
            return out
        if "sealed" in out.lower() or "LOCKOUT" in out:
            if "type 'extra'" in out:
                out = game.send("extra")
                stats["extra"] += 1
                if "spins up" not in out:
                    return game.send("disconnect")
            elif "lock is sealed" in out or "LOCKOUT" in out:
                return game.send("disconnect")
        g = solver.next()
        if CHAOS and stats.setdefault("_wasted", 0) < 99 and "guesses left: 1/" not in out and random.random() < 0.8:
            g = {"KEYPAD": "0123", "ROTOR": "0000", "CIPHER": "zzzzz"}[solver.kind]
            solver.last = g
        out = game.send(g)
        for line in out.splitlines():
            if "->" in line:
                solver.feed(line)
    return out


def parse_files(out):
    files = []
    for line in out.splitlines():
        m = re.match(r"\s+(\d+)\.\s+(\S+)\s+(\d+)MB\s+~(\d+)s\s+(OBJECTIVE|\d+cr)(.*)", line)
        if m:
            files.append(dict(n=int(m.group(1)), path=m.group(2), size=int(m.group(3)), secs=int(m.group(4)),
                              kind=m.group(5), taken="[taken]" in m.group(6)))
        t = re.match(r"\s+(\d+)\.\s+(\S+)\s+\[text", line)
        if t:
            files.append(dict(n=int(t.group(1)), path=t.group(2), kind="TEXT", taken=False, secs=2))
    return files


def trace_pct(out):
    vals = re.findall(r"TRACE \[[#-]+\] (\d+)%", out)
    return int(vals[-1]) if vals else 0


def secs_left(out):
    vals = re.findall(r"~(\d+)s left", out)
    return int(vals[-1]) if vals else 99


def finish_prompts(game, out):
    for _ in range(12):
        if "Name your home server" in out:
            out = game.send("Biscuit")
            continue
        if "(press Enter)" in out:
            out = game.send("")
            continue
        break
    return out


def do_run(game, out, stats, want_steal=False):
    out = solve_door(game, out, stats)
    if "> " in out and ("ls" in out or "help" in out) and "door>" not in out:
        pass
    files = parse_files(out)
    if want_steal and "steal" and "detachable" in "".join(game.transcript[-6:]):
        out = game.send("steal")
        m = re.search(r"Type '(\w+)' or '(\w+)'", out)
        if m:
            out = game.send(m.group(1))
            stats["steal_attempts"] += 1
            if "ANCHOR HOLDS" in out:
                stats["steals"] += 1
            return finish_prompts(game, out)
    # read text, grab objectives first, then paydata while the margin holds
    for f in files:
        if f["kind"] == "TEXT" and secs_left(out) > 12:
            out = game.send(f"cat {f['n']}")
            if "TRACED" in out or "TRACE COMPLETE" in out:
                return finish_prompts(game, out)
    order = [f for f in files if f["kind"] == "OBJECTIVE"] + [f for f in files if f["kind"].endswith("cr")]
    for f in order:
        if f["taken"]:
            continue
        objective = f["kind"] == "OBJECTIVE"
        if not objective and not CHAOS and secs_left(out) < f["secs"] + 6:
            continue
        out = game.send(f"get {f['n']}")
        if "BAIT FILE" in out:
            stats["honeypots"] += 1
        if "TRACED" in out or "TRACE COMPLETE" in out or "THE BILL" in out:
            return finish_prompts(game, out)
    out = game.send("disconnect")
    return finish_prompts(game, out)


def board_pick(out, prefer):
    lines = out.splitlines()
    for want in prefer:
        for line in lines:
            m = re.match(r"\s+(\d+)\.\s+(.*)", line)
            if m and want(line) and "already hit" not in line:
                return m.group(1)
    return None


def main():
    exe = sys.argv[1]
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 7
    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    global CHAOS
    CHAOS = len(sys.argv) > 4 and sys.argv[4] == "chaos"
    random.seed(seed)
    home = tempfile.mkdtemp(prefix="ghostnet-bot-")
    game = Game(exe, seed, home)
    stats = dict(runs=0, extra=0, steals=0, steal_attempts=0, honeypots=0, sacrifices=0, traceouts=0)
    out = game.read(limit=3)
    out = game.send("2")  # new game
    bought = set()
    for i in range(runs):
        out = game.send("1")  # job board
        is_contract = lambda l: "CONTRACT" in l
        hw = lambda l: "[HW]" in l
        anyt = lambda l: "OPEN" in l or "SHRINE" in l or "HIDDEN" in l or "GUARDED" in l
        net_ok = "GhostNet:" in "".join(game.transcript[-3:])
        prefer = [is_contract] if i % 3 == 0 or not net_ok else ([hw, is_contract, anyt] if i % 4 == 1 else [anyt, is_contract, hw])
        pick = board_pick(out, prefer)
        if not pick:
            out = game.send("0")
            break
        out = game.send(pick)
        steal = "[HW]" in [l for l in game.transcript[-2].splitlines() if l.strip().startswith(pick + ".")][0] \
            if [l for l in game.transcript[-2].splitlines() if l.strip().startswith(pick + ".")] else False
        if "Scan target" in out and "GhostNet" in "".join(game.transcript):
            out = game.send("1")
            out = game.send("")
        out = game.send("2")  # panel
        out = game.send("")
        out = game.send("3")  # connect
        out = game.send("y")
        stats["runs"] += 1
        out = do_run(game, out, stats, want_steal=steal and i % 2 == 1)
        joined = "".join(game.transcript[-8:])
        if "TRACE COMPLETE" in joined:
            stats["sacrifices"] += 1
        elif "TRACED" in joined:
            stats["traceouts"] += 1
        out = finish_prompts(game, out)
        # back to hub
        for _ in range(4):
            if "Pick a target number" in out:
                out = game.send("0")
            if "YOUR DECK" in out and "1) Job board" in out:
                break
        # manage network between runs
        if "GhostNet:" in out:
            cr = int(re.search(r"Credits (\d+)cr", out).group(1))
            out = game.send("2")
            cmds = []
            if cr > 160 and "proxy" not in bought:
                cmds += ["buy proxy"]; bought.add("proxy")
            elif cr > 250 and "compute" not in bought:
                cmds += ["buy compute"]; bought.add("compute")
            elif cr > 180 and "scout" not in bought:
                cmds += ["buy scout"]; bought.add("scout")
            cmds += ["repair all"] + [f"on {k}" for k in range(1, 8)]
            for c in cmds:
                out = game.send(c)
            m = re.search(r"\[(\d+)\]\s+cap-\S+\s+PROXY", out) or re.search(r"\[(\d+)\]\s+clean-proxy-\d+", "".join(game.transcript[-4:]))
            if m and "bait none" in "".join(game.transcript[-12:]):
                out = game.send(f"bait {m.group(1)}")
                if "[y/n]" in out:
                    out = game.send("y")
                    out = game.send("y")
            out = game.send("rename 0 Biscuit")
            out = game.send("back")
    out = game.send("5")  # records
    records = out
    out = game.send("")
    out = game.send("0")
    out = game.send("0")
    game.p.stdin.close()
    game.p.wait(timeout=10)
    full = "".join(game.transcript)
    with open(os.path.join(home, "transcript.txt"), "w") as fh:
        fh.write(full)
    print(records)
    print("BOT STATS:", stats)
    print("transcript:", os.path.join(home, "transcript.txt"))
    bad = [w for w in ("Segmentation", "terminate called", "Unknown command") if w in full]
    print("problems:", bad or "none")
    return 0 if game.p.returncode == 0 and not [b for b in bad if b != "Unknown command"] else 1


if __name__ == "__main__":
    sys.exit(main())
