#!/usr/bin/env python3
"""GhostNet automated playtester.

Drives the real game binary through stdin/stdout (with --fast --no-color),
solving locks from the feedback the game prints - no cheats, no hidden hooks.
It plays the campaign, reads passphrases, builds a network, captures hardware,
arms decoys, attempts the deep job early (to check the shortfall readout) and
again once its network is strong, and retires.

usage: python3 tools/playtest_bot.py path/to/ghostnet [seed] [runs] [chaos|deep]
  chaos  wastes guesses, grabs everything, arms bought proxies as decoys
  deep   fails (exit 1) unless the bot beats the deep job within [runs]
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

    def read(self, quiet=0.06, limit=5.0):
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


PHRASES = {}   # host -> passphrase, learned only by reading memos in-game


def learn_phrases(text):
    for host, ph in re.findall(r"door at (\S+) still answers to '([A-Z]+)'", text):
        PHRASES[host] = ph


def solve_door(game, out, stats, host=None):
    m = re.search(r"(KEYPAD|ROTOR|CIPHER) LOCK", out)
    if not m:
        return out
    if host and host in PHRASES and not CHAOS:
        out = game.send("phrase " + PHRASES[host])
        stats["phrases"] += 1
        if "accepts the phrase" in out:
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
        if CHAOS and "guesses left: 1/" not in out and random.random() < 0.8:
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


def secs_left(out):
    vals = re.findall(r"~(\d+)s left", out)
    return int(vals[-1]) if vals else 99


def ended(out):
    return "TRACED" in out or "TRACE COMPLETE" in out or "THE RECKONING" in out


def finish_prompts(game, out):
    for _ in range(16):
        learn_phrases(out)
        if "Name your home server" in out:
            out = game.send("")          # keep the default name, N0
            continue
        if "(press Enter)" in out:
            out = game.send("")
            continue
        break
    return out


def do_run(game, out, stats, want_steal=False, deep=False, host=None, report=""):
    out = solve_door(game, out, stats, host)
    if ended(out):
        return finish_prompts(game, out)
    files = parse_files(out)
    if want_steal and "detachable" in "".join(game.transcript[-8:]):
        out = game.send("steal")
        m = re.search(r"Type '(\w+)' or '(\w+)'", out)
        if m:
            out = game.send(m.group(1))
            stats["steal_attempts"] += 1
            if "CAPTURED." in out:
                stats["captures"] += 1
            return finish_prompts(game, out)
    if not deep:
        for f in files:
            if f["kind"] == "TEXT" and secs_left(out) > 12:
                out = game.send(f"cat {f['n']}")
                learn_phrases(out)
                if ended(out):
                    return finish_prompts(game, out)
    pay = [f for f in files if f["kind"].endswith("cr")]
    if not CHAOS:   # a careful runner believes the scouting report about bait files
        confirmed = re.findall(r"Confirmed bait file: (\S+)", report)
        if confirmed:
            pay = [f for f in pay if f["path"] not in confirmed]
        elif "suspected" in report and pay:
            worst = max(pay, key=lambda f: int(f["kind"][:-2]))
            pay = [f for f in pay if f is not worst]
    order = [f for f in files if f["kind"] == "OBJECTIVE"] + pay
    for f in order:
        if f["taken"]:
            continue
        objective = f["kind"] == "OBJECTIVE"
        if not objective and not CHAOS and secs_left(out) < f["secs"] * 1.3 + 6:
            continue
        out = game.send(f"get {f['n']}")
        if "BAIT FILE" in out:
            stats["honeypots"] += 1
        if ended(out):
            return finish_prompts(game, out)
    out = game.send("disconnect")
    return finish_prompts(game, out)


def board_rows(out):
    rows = []
    for line in out.splitlines():
        m = re.match(r"\s+(\d+)\.\s+(.*)", line)
        if m and "already hit" not in line:
            rows.append((m.group(1), line))
    return rows


def pick_row(rows, preds):
    for p in preds:
        for n, line in rows:
            if p(line):
                return n, line
    return None, None


class Net:
    def __init__(self, text):
        self.text = text
        self.relay = int((re.findall(r"Download bonus \+(\d+)%", text) or ["0"])[-1])
        self.proxy = int((re.findall(r"Trace reduction -(\d+)%", text) or ["0"])[-1])
        m = re.findall(r"Running (\d+)/(\d+) slots", text)
        self.running, self.slots = (int(m[-1][0]), int(m[-1][1])) if m else (0, 3)
        self.bill = int((re.findall(r"Next run costs (\d+)cr", text) or ["0"])[-1])
        self.armed = (re.findall(r"Armed to die: (\S+)", text) or ["nobody"])[-1]
        last = text.rsplit("THE RACK - GhostNet", 1)[-1]
        self.machines = re.findall(
            r"\[(\d+)\]\s+(.{1,16}?)\s+(RELAY|PROXY|SCOUT|COMPUTE)\s+T(\d)\s+(home|captured|bought)\s+\S*\s+(\d+)?", last)
        rack, _, storage = last.partition("COLD STORAGE")
        self.stored = re.findall(r"\[(\d+)\]", storage)
        self.racked = [m for m in self.machines if "[" + m[0] + "]" in rack]
        self.home_tier = next((int(m[3]) for m in self.machines if m[4] == "home"), 1)


def manage(game, out, stats, credits, deck):
    """Spend between runs: deck tools and owned machines toward the deep job."""
    out = game.send("2")
    net = Net(out)
    reserve = 2 * net.bill + 150
    # Switch on anything sitting in storage while there's room; repair what's worn.
    out = game.send("repair all")
    out = game.send("ls")
    net = Net(out)
    for mid in net.stored:
        if net.running < net.slots:
            out = game.send(f"on {mid}")
            if "hums to life" in out:
                net.running += 1
    out = game.send("ls")
    net = Net(out)
    credits = int((re.findall(r"Credits (\d+)cr", out) or [credits])[-1])
    jobs = [m[2] for m in net.racked]
    want_scout = "SCOUT" not in jobs
    steps = [
        ("deck", "2", 400, lambda: deck["uplink"] == 0),
        ("buy", "proxy", 500, lambda: "PROXY" not in jobs and net.running < net.slots),
        ("deck", "1", 450, lambda: deck["breaker"] == 0),
        ("buy", "scout", 450, lambda: want_scout and net.running < net.slots),
        ("home", "", 600, lambda: net.home_tier == 1),
        ("deck", "3", 500, lambda: deck["shroud"] == 0),
        ("expand", "", 350, lambda: net.slots == 3),
        ("buy", "relay", 400, lambda: net.relay < 42 and net.running < net.slots),
        ("deck", "2", 850, lambda: deck["uplink"] == 1),
        ("home", "", 1000, lambda: net.home_tier == 2),
        ("expand", "", 650, lambda: net.slots == 4),
        ("buy", "proxy", 500, lambda: net.proxy < 30 and net.running < net.slots),
        ("deck", "3", 1000, lambda: deck["shroud"] == 1),
        ("expand", "", 1000, lambda: net.slots == 5),
        ("buy", "relay", 400, lambda: net.relay < 42 and net.running < net.slots),
        ("buy", "proxy", 500, lambda: net.proxy < 30 and net.running < net.slots),
        ("deck", "2", 1500, lambda: deck["uplink"] == 2),
        ("deck", "3", 1800, lambda: deck["shroud"] == 2),
    ]
    for _ in range(4):   # up to four purchases per visit, always keeping the reserve
        step = next((st for st in steps if st[3]()), None)
        if step is None or credits - step[2] < reserve:
            break
        kind, arg, cost, _cond = step
        if kind == "buy":
            out = game.send(f"buy {arg}")
            if "racked and running" in out:
                credits -= cost
                stats["bought"] += 1
        elif kind == "home":
            out = game.send("home")
            if "is now tier" in out:
                credits -= cost
        elif kind == "expand":
            out = game.send("expand")
            if "bolted in" in out:
                credits -= cost
        elif kind == "deck":
            game.send("back")
            game.send("3")
            o = game.send(arg)
            if "upgraded to level" in o:
                credits -= cost
                deck[{"1": "breaker", "2": "uplink", "3": "shroud"}[arg]] += 1
            game.send("0")
            out = game.send("2")
        out = game.send("ls")
        net = Net(out)
        jobs = [m[2] for m in net.racked]
        want_scout = "SCOUT" not in jobs
    # A captured proxy is a free decoy. Chaos mode also sacrifices bought ones.
    if net.armed == "nobody":
        for mid, name, job, tier, origin, cond in net.racked:
            if job == "PROXY" and (origin == "captured" or CHAOS):
                out = game.send(f"decoy {mid}")
                if "Type 'sacrifice'" in out:
                    out = game.send("sacrifice")
                if "DECOY ARMED" in out:
                    stats["decoys_armed"] += 1
                break
    out = game.send("back")
    return out


def arm_any_decoy(game, stats):
    out = game.send("2")
    net = Net(out)
    if net.armed == "nobody":
        for mid, name, job, tier, origin, cond in net.racked:
            if job == "PROXY":
                out = game.send(f"decoy {mid}")
                if "Type 'sacrifice'" in out:
                    out = game.send("sacrifice")
                if "DECOY ARMED" in out:
                    stats["decoys_armed"] += 1
                break
    return game.send("back")


def main():
    exe = sys.argv[1]
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 7
    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    global CHAOS
    mode = sys.argv[4] if len(sys.argv) > 4 else ""
    CHAOS = mode == "chaos"
    random.seed(seed)
    home = tempfile.mkdtemp(prefix="ghostnet-bot-")
    game = Game(exe, seed, home)
    stats = dict(runs=0, extra=0, captures=0, steal_attempts=0, honeypots=0, sacrifices=0, traceouts=0, phrases=0,
                 bought=0, decoys_armed=0, deep_attempts=0, deep_shortfalls=0, deep_won=0, annex=0)
    deck = dict(breaker=0, uplink=0, shroud=0)
    out = game.read(limit=3)
    out = game.send("2")  # new game
    out = finish_prompts(game, out)
    credits = 150
    for i in range(runs):
        hub = out
        net_on = "GhostNet:" in hub
        if net_on and i > 0:
            hub = manage(game, hub, stats, credits, deck)
        if net_on:
            # Read the rack once more so the deep-job decision sees current numbers.
            o = game.send("2")
            net = Net(o)
            game.send("back")
        else:
            net = None
        out = game.send("1")  # job board
        rows = board_rows(out)
        deep_ready = net is not None and ("deep.archive.helios.int" in PHRASES or CHAOS) and \
            net.relay + net.proxy >= 50 and deck["uplink"] + deck["shroud"] >= 3
        early = stats["deep_attempts"] == 0
        is_deep = lambda l: "[THE DEEP JOB]" in l
        annex = lambda l: "Facilities Annex" in l
        contract = lambda l: "[CONTRACT" in l
        hw = lambda l: "[HW]" in l
        rich = lambda l: "WATCHED" in l or "CORPORATE" in l or ("SIDE ROUTE" in l and "Annex" not in l)
        easy = lambda l: "OPEN" in l or "SHRINE" in l or "HIDDEN" in l
        preds = []
        if any(is_deep(l) for _, l in rows) and (early or deep_ready):
            preds.append(is_deep)
        if "deep.archive.helios.int" not in PHRASES:
            preds.append(annex)
        preds.append(contract)
        if i % 3 == 1:
            preds.append(hw)
        strong = net is not None and net.proxy >= 9 and (deck["breaker"] >= 1 or CHAOS)
        preds += [rich, easy] if strong else [easy, rich]
        pick, line = pick_row(rows, preds)
        if not pick:
            out = game.send("0")
            break
        deep = is_deep(line)
        if deep and not early:
            game.send("0")
            arm_any_decoy(game, stats)
            out = game.send("1")
        out = game.send(pick)
        hosts = re.findall(r"\n  (\S+\.\S+)  -  ", out)
        host = hosts[-1] if hosts else None
        report = ""
        if ("Scan target" in out or "Read report" in out) and net_on and not deep:
            out = game.send("1")
            report = out
            out = game.send("")
        out = game.send("2")  # panel
        out = game.send("")
        out = game.send("3")  # connect
        out = game.send("y")
        stats["runs"] += 1
        if deep:
            stats["deep_attempts"] += 1
        if annex(line):
            stats["annex"] += 1
        out = do_run(game, out, stats, want_steal=hw(line) and i % 3 == 1 and not deep, deep=deep, host=host, report=report)
        joined = "".join(game.transcript[-12:])
        if "TRACE COMPLETE" in joined:
            stats["sacrifices"] += 1
        elif "TRACED" in joined:
            stats["traceouts"] += 1
        if "HOW FAR SHORT" in joined:
            stats["deep_shortfalls"] += 1
        out = finish_prompts(game, out)
        if "The rack goes quiet. The machines stay on." in "".join(game.transcript[-12:]):
            stats["deep_won"] += 1
            out = finish_prompts(game, out)
        for _ in range(4):
            if "Pick a target number" in out:
                out = game.send("0")
            if "1) Job board" in out:
                break
        credits = int((re.findall(r"Credits (\d+)cr", out) or [credits])[-1])
        if stats["deep_won"]:
            break
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
    ok = game.p.returncode == 0 and not [b for b in bad if b != "Unknown command"]
    if mode == "deep" and not stats["deep_won"]:
        print("FAIL: the deep job was not beaten")
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
