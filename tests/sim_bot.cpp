// GhostNet - automated playtester for the game core.
//
// Plays the real rules (the same code the windowed game runs) end to end:
// solves locks from their feedback only, reads memos for passphrases, builds a
// network, captures hardware, arms decoys, tries the deep job early (to check
// the shortfall readout) and again once its network is strong. After every run
// it checks the game's invariants and round-trips the save file.
//
// usage: ghostnet_sim [seed] [runs] [careful|chaos|deep]
//   deep  exit 1 unless the deep job is beaten within [runs]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "balance.h"
#include "intel.h"
#include "locks.h"
#include "paths.h"
#include "rack.h"
#include "rng.h"
#include "rules.h"
#include "run.h"
#include "save.h"
#include "world.h"

namespace {

bool CHAOS = false;
int problems = 0;
void fail(const std::string& s) {
    std::cerr << "PROBLEM: " << s << "\n";
    ++problems;
}

struct Solver {
    LockType kind;
    std::vector<std::string> cands;
    std::vector<std::set<int>> wheels;
    std::string last;
    explicit Solver(LockType k) : kind(k) {
        if (k == LockType::Keypad) {
            for (int a = 0; a < 10; ++a)
                for (int b = 0; b < 10; ++b)
                    for (int c = 0; c < 10; ++c)
                        for (int d = 0; d < 10; ++d)
                            if (a != b && a != c && a != d && b != c && b != d && c != d)
                                cands.push_back({char('0' + a), char('0' + b), char('0' + c), char('0' + d)});
        } else if (k == LockType::Rotor) {
            for (int i = 0; i < 4; ++i) {
                std::set<int> s;
                for (int v = 0; v < 10; ++v) s.insert(v);
                wheels.push_back(s);
            }
        } else {
            cands = cipherWords();
        }
    }
    std::string next() {
        if (kind == LockType::Rotor) {
            std::string g;
            for (auto& w : wheels) {
                int best = *w.begin(), bestScore = 99;
                for (int v : w) {
                    int same = 0, adj = 0, far = 0;
                    for (int o : w) {
                        if (o == v) ++same;
                        else if (std::abs(o - v) == 1) ++adj;
                        else ++far;
                    }
                    int sc = std::max({same, adj, far});
                    if (sc < bestScore) { bestScore = sc; best = v; }
                }
                g += char('0' + best);
            }
            return last = g;
        }
        if (cands.empty()) return last = kind == LockType::Keypad ? "0123" : "crane";
        return last = cands.size() < 3 ? cands[0] : cands[cands.size() / 2];
    }
    void feed(const Lock::Row& row) {
        const std::string& g = row.guess;
        if (kind == LockType::Keypad) {
            std::vector<std::string> keep;
            for (auto& c : cands) {
                bool ok = true;
                for (int i = 0; i < 4 && ok; ++i) {
                    int mk = g[i] == c[i] ? 2 : c.find(g[i]) != std::string::npos ? 1 : 0;
                    ok = mk == row.marks[static_cast<size_t>(i)];
                }
                if (ok) keep.push_back(c);
            }
            cands.swap(keep);
        } else if (kind == LockType::Rotor) {
            for (int i = 0; i < 4; ++i) {
                int v = g[i] - '0';
                std::set<int> keep;
                for (int o : wheels[i]) {
                    int d = std::abs(o - v);
                    int m = d == 0 ? 2 : d == 1 ? 1 : 0;
                    if (m == row.marks[static_cast<size_t>(i)]) keep.insert(o);
                }
                wheels[i] = keep;
            }
        } else {
            std::vector<std::string> keep;
            for (auto& c : cands) {
                std::vector<int> res(5, 0);
                int counts[26] = {0};
                for (int i = 0; i < 5; ++i) {
                    if (g[i] == c[i]) res[i] = 2;
                    else counts[c[i] - 'a']++;
                }
                for (int i = 0; i < 5; ++i)
                    if (res[i] != 2 && counts[g[i] - 'a'] > 0) { res[i] = 1; counts[g[i] - 'a']--; }
                if (res == row.marks) keep.push_back(c);
            }
            cands.swap(keep);
        }
    }
};

struct SimStats {
    int runs = 0, captures = 0, steals = 0, traceouts = 0, sacrifices = 0, phrases = 0, extra = 0, honeypots = 0;
    int bought = 0, decoys = 0, deepAttempts = 0, shortfalls = 0, deepWon = 0, annex = 0, contracts = 0, unpaid = 0;
} st;

void checkInvariants(const GameState& g) {
    if (g.credits < 0) fail("negative credits");
    if (g.net.activeCount() > g.net.slots) fail("more machines running than slots");
    if (g.net.externalCount() > bal::MAX_EXTERNAL) fail("more than 10 external machines");
    if (g.net.decoyId >= 0 && !g.net.decoy()) fail("decoy id points at an invalid machine");
    for (auto& m : g.net.machines) {
        if (m.condition < 0 || m.condition > 100) fail("condition out of range");
        if (m.dark() && m.active) fail(m.name + " is dark but active");
        if (m.home && (m.condition != 100 || !m.active)) fail("N0 damaged or stored");
    }
    if (g.netUnlocked && !g.net.home()) fail("network unlocked without N0");
    if (g.act < 1 || g.act > 3) fail("act out of range");
}

void roundTrip(const GameState& g) {
    std::string err;
    if (!saveGame(g, &err)) { fail("save failed: " + err); return; }
    GameState h;
    if (!loadGame(h, &err)) { fail("load failed: " + err); return; }
    if (h.credits != g.credits || h.net.machines.size() != g.net.machines.size() || h.story != g.story || h.act != g.act ||
        h.deepDone != g.deepDone || h.net.decoyId != g.net.decoyId || h.deck.lens != g.deck.lens || h.knownPhrases != g.knownPhrases ||
        h.wall.size() != g.wall.size() || h.programs[0] != g.programs[0] || h.programs[1] != g.programs[1])
        fail("save/load round trip changed the game");
    for (size_t i = 0; i < h.net.machines.size() && i < g.net.machines.size(); ++i)
        if (h.net.machines[i].trait != g.net.machines[i].trait || h.net.machines[i].job != g.net.machines[i].job)
            fail("save/load lost a machine's job or trait");
}

void solveDoor(RunSession& r) {
    if (r.doorOpen || r.over()) return;
    if (r.g.knownPhrases.count(r.t.id) && !CHAOS) {
        ++st.phrases;
        if (r.phrase(r.t.passphrase)) return;
        if (r.over()) return;
    }
    Solver s(r.lock.type);
    for (int i = 0; i < 16 && !r.doorOpen && !r.over(); ++i) {
        if (r.lock.sealed()) {
            std::string err;
            if (r.extra(err)) { ++st.extra; continue; }
            r.disconnect();
            return;
        }
        std::string guess = s.next();
        if (CHAOS && r.lock.left() > 1 && std::rand() % 10 < 6) {
            guess = r.lock.type == LockType::Keypad ? "0123" : r.lock.type == LockType::Rotor ? "0000" : "zzzzz";
            s.last = guess;
        }
        std::string err;
        if (!r.guess(guess, err)) { fail("lock rejected a well-formed guess: " + err); r.disconnect(); return; }
        if (!r.lock.history.empty()) s.feed(r.lock.history.back());
    }
}

void runTimed(RunSession& r) {
    int guard = 0;
    while (r.busy() && !r.over() && ++guard < 1000) r.step();
}

void playInside(RunSession& r, bool wantSteal, bool deep, const ReportView& rep) {
    if (r.over() || !r.doorOpen) return;
    if (wantSteal) {
        CaptureQuote q = r.quote();
        if (q.possible) {
            std::string err;
            ++st.steals;
            if (r.startHold(q.def, err)) {
                runTimed(r);
                return;
            }
        }
    }
    if (!deep)
        for (size_t i = 0; i < r.t.files.size() && !r.over(); ++i)
            if (r.t.files[i].readable() && r.secondsLeft() > 12) r.read(static_cast<int>(i));
    std::vector<int> order;
    for (size_t i = 0; i < r.t.files.size(); ++i) if (r.t.files[i].kind == FileKind::Objective) order.push_back(static_cast<int>(i));
    std::vector<int> pay;
    for (size_t i = 0; i < r.t.files.size(); ++i) {
        const File& f = r.t.files[i];
        if (f.kind != FileKind::Paydata && f.kind != FileKind::Honeypot) continue;
        bool confirmed = false;
        for (auto& w : rep.warnings) if (w.find(f.dir + "/" + f.name) != std::string::npos && w.find("Confirmed") != std::string::npos) confirmed = true;
        if (!CHAOS && confirmed) continue;
        pay.push_back(static_cast<int>(i));
    }
    bool suspected = false;
    for (auto& w : rep.warnings) if (w.find("suspected") != std::string::npos) suspected = true;
    if (!CHAOS && suspected && !pay.empty()) {
        auto worst = std::max_element(pay.begin(), pay.end(), [&](int a, int b) { return r.t.files[size_t(a)].credits < r.t.files[size_t(b)].credits; });
        pay.erase(worst);
    }
    order.insert(order.end(), pay.begin(), pay.end());
    for (int i : order) {
        if (r.over()) return;
        const File& f = r.t.files[static_cast<size_t>(i)];
        if (f.taken) continue;
        int secs = secondsToDownload(r.g, f.size);
        if (f.kind != FileKind::Objective && !CHAOS && r.secondsLeft() < secs * 1.3 + 6) continue;
        std::string err;
        if (!r.startDownload(i, err)) { fail("download refused: " + err); continue; }
        bool bait = f.kind == FileKind::Honeypot;
        runTimed(r);
        if (bait) ++st.honeypots;
    }
    r.disconnect();
}

void armAnyDecoy(GameState& g, bool boughtToo) {
    if (g.net.decoy()) return;
    for (auto& m : g.net.machines)
        if (m.working() && m.job == Job::Proxy && !m.home && (m.captured || boughtToo)) {
            if (armDecoy(g, m, false).msg == "confirm" && !armDecoy(g, m, true).ok) continue;
            if (g.net.decoy()) { ++st.decoys; return; }
        }
}

void manage(GameState& g) {
    if (!g.netUnlocked) {
        if (g.deck.uplink == 0 && g.credits >= 400 + 150) buyTool(g, Tool::Uplink);
        return;
    }
    repairAll(g);
    for (auto& m : g.net.machines) if (!m.active && !m.dark()) activate(g, m);
    auto hasJob = [&](Job j) {
        for (auto& m : g.net.machines) if (m.active && m.job == j) return true;
        return false;
    };
    auto free = [&] { return g.net.activeCount() < g.net.slots; };
    for (int k = 0; k < 4; ++k) {
        int reserve = 2 * g.net.upkeepTotal() + 150;
        Machine* h = g.net.home();
        std::vector<std::pair<int, std::pair<int, bool>>> steps = {
            {0, {static_cast<int>(Tool::Uplink), g.deck.uplink == 0}},
            {1, {static_cast<int>(Job::Proxy), !hasJob(Job::Proxy) && free()}},
            {0, {static_cast<int>(Tool::Breaker), g.deck.breaker == 0}},
            {1, {static_cast<int>(Job::Scout), !hasJob(Job::Scout) && free()}},
            {2, {0, h && h->tier == 1}},
            {0, {static_cast<int>(Tool::Shroud), g.deck.shroud == 0}},
            {3, {0, g.net.slots == 3}},
            {1, {static_cast<int>(Job::Relay), g.net.relayBonus() < bal::RELAY_CAP && free()}},
            {0, {static_cast<int>(Tool::Uplink), g.deck.uplink == 1}},
            {2, {0, h && h->tier == 2}},
            {3, {0, g.net.slots == 4}},
            {1, {static_cast<int>(Job::Proxy), g.net.proxyBonus() < bal::PROXY_CAP && free()}},
            {0, {static_cast<int>(Tool::Shroud), g.deck.shroud == 1}},
            {3, {0, g.net.slots == 5}},
            {1, {static_cast<int>(Job::Relay), g.net.relayBonus() < bal::RELAY_CAP && free()}},
            {1, {static_cast<int>(Job::Proxy), g.net.proxyBonus() < bal::PROXY_CAP && free()}},
            {0, {static_cast<int>(Tool::Uplink), g.deck.uplink == 2}},
            {0, {static_cast<int>(Tool::Shroud), g.deck.shroud == 2}},
        };
        auto it = std::find_if(steps.begin(), steps.end(), [](auto& s) { return s.second.second; });
        if (it == steps.end()) break;
        int cost = 0;
        int kind = it->first, arg = it->second.first;
        if (kind == 0) cost = toolInfo(g, static_cast<Tool>(arg)).nextCost;
        if (kind == 1) cost = bal::BUY_PRICE[arg];
        if (kind == 2) cost = bal::HOME_UPGRADE[h->tier - 1];
        if (kind == 3) cost = bal::SLOT_COST[g.net.slots - bal::START_SLOTS];
        if (g.credits - cost < reserve) break;
        if (kind == 1 && g.net.externalCount() >= bal::MAX_EXTERNAL) break;
        Result res = kind == 0 ? buyTool(g, static_cast<Tool>(arg)) : kind == 1 ? buyMachine(g, static_cast<Job>(arg))
                   : kind == 2 ? upgradeHome(g) : expandRack(g);
        if (!res.ok) { fail("purchase refused unexpectedly: " + res.msg); break; }
        if (kind == 1) ++st.bought;
    }
    // Re-job spare captured scouts/compute boxes into speed or cover once the deck is built.
    if (!CHAOS && g.deck.uplink + g.deck.shroud >= 3) {
        bool keptScout = false;
        for (auto& m : g.net.machines) {
            if (!m.working() || m.home || g.net.decoyId == m.id) continue;
            if (m.job == Job::Scout && !keptScout) { keptScout = true; continue; }
            if (m.job != Job::Scout && m.job != Job::Compute) continue;
            if (g.credits < bal::REIMAGINE_FEE + 2 * g.net.upkeepTotal() + 150) break;
            Job to = g.net.relayBonus() < bal::RELAY_CAP ? Job::Relay : Job::Proxy;
            if (to == Job::Proxy && g.net.proxyBonus() >= bal::PROXY_CAP) break;
            Result res = rejob(g, m, to);
            if (!res.ok) fail("re-job refused: " + res.msg);
        }
    }
    armAnyDecoy(g, CHAOS);
}

}  // namespace

// ---------------------------------------------------------------------------
// Rule checks for the 0.4 systems: keypad marks, programs, jammer/vault, traits.
// ---------------------------------------------------------------------------
#define CHECK(cond, msg) do { if (!(cond)) fail(std::string("selftest: ") + (msg)); } while (0)

Target testTarget(LockType lock) {
    Target t;
    t.id = 900;
    t.name = "Test Rig";
    t.host = "test.rig";
    t.kind = Kind::Guarded;
    t.security = 3;
    t.lock = lock;
    File pay;
    pay.name = "pay.db";
    pay.size = 40;
    pay.credits = 300;
    File small = pay;
    small.name = "small.db";
    small.credits = 100;
    File bait = pay;
    bait.name = "bait.db";
    bait.kind = FileKind::Honeypot;
    t.files = {pay, small, bait};
    return t;
}

void selfTest() {
    // Keypad: one mark per box, consistent with the hit/near counts.
    {
        Lock k;
        k.type = LockType::Keypad;
        k.secret = "1234";
        k.maxGuesses = 6;
        std::string err;
        CHECK(k.guess("1325", err), "keypad guess accepted");
        const auto& row = k.history.back();
        CHECK((row.marks == std::vector<int>{2, 1, 1, 0}), "keypad marks are per box (1325 vs 1234 -> 2,1,1,0)");
        CHECK(row.hit == 1 && row.nearc == 2, "keypad counts still match the marks");
        CHECK(k.peek() == 1, "PEEK skips a box already shown green");
        CHECK(k.known(0) && k.known(1) && !k.known(2), "known() tracks green boxes and peeks");
    }
    GameState g;
    g.worldSeed = 3;
    g.credits = 5000;
    unlockNetwork(g);
    // Programs: buy, refund, two slots only.
    CHECK(buyProgram(g, Program::Freeze).ok && buyProgram(g, Program::Peek).ok, "load two programs");
    CHECK(!buyProgram(g, Program::Burst).ok, "a third program is refused");
    int before = g.credits;
    CHECK(unloadProgram(g, 1).ok && g.credits == before + programCost(Program::Peek), "unloading refunds in full");
    buyProgram(g, Program::Peek);
    {
        RunSession r(g, testTarget(LockType::Keypad), 1);
        std::string err;
        double t0 = r.trace;
        CHECK(r.runProgram(0, err), "FREEZE runs at the door");
        CHECK(g.programs[0] == Program::None, "a program is consumed when it runs");
        CHECK(!r.programUsable(1), "only one program per lock");
        r.guess(r.lock.secret.substr(0, 3) + (r.lock.secret[3] == '9' ? "8" : "9"), err);   // 2s while frozen
        CHECK(r.trace == t0, "FREEZE holds the trace still");
        CHECK(r.programUsable(1) == false, "the limit holds after a guess");
    }
    {
        g.programs[0] = Program::Spoof;
        g.programs[1] = Program::Burst;
        Target t = testTarget(LockType::None);
        RunSession r(g, t, 2);
        std::string err;
        CHECK(r.doorOpen, "unlocked test door");
        CHECK(r.runProgram(0, err) && r.spoofArmed, "SPOOF arms");
        CHECK(r.startDownload(2, err), "take the bait");
        while (r.busy() && !r.over()) r.step();
        CHECK(!r.honeypot && !r.spoofArmed, "SPOOF defuses the honeypot once");
        CHECK(r.runProgram(1, err), "BURST runs inside");
        CHECK(r.startDownload(0, err), "download with BURST");
        int total = r.actTotal, steps = 0;
        while (r.busy() && !r.over()) { r.step(); ++steps; }
        CHECK(steps < total, "BURST finishes the download in fewer seconds");
    }
    // Jammer + vault.
    {
        GameState h;
        h.worldSeed = 4;
        h.credits = 5000;
        unlockNetwork(h);
        h.net.slots = bal::MAX_SLOTS;
        buyMachine(h, Job::Jammer);
        buyMachine(h, Job::Vault);
        CHECK(h.net.jamDelay() == bal::JAM_DELAY[2] && h.net.jamSpikeCut() == bal::JAM_SPIKE[2], "a T2 jammer counts");
        CHECK(h.net.bestVault() != nullptr, "a vault is found");
        Target t = testTarget(LockType::None);
        t.watchdog = true;
        RunSession r(h, t, 3);
        CHECK(r.jamDelay > 0 && r.vaultPct == bal::VAULT_KEEP[2], "the run picks up jammer and vault");
        std::string err;
        r.startDownload(0, err);
        while (r.busy() && !r.over()) r.step();
        int credits = h.credits;
        r.trace = 99.9;
        r.startDownload(1, err);
        while (!r.over()) { if (!r.busy()) r.startDownload(1, err); r.step(); }
        CHECK(r.end == End::TraceOut, "traced with no decoy");
        Reckoning rk = r.settle();
        (void)rk;
        int kept = 300 * bal::VAULT_KEEP[2] / 100;
        CHECK(h.credits >= credits - 10000 && h.stats.earned >= kept, "the vault keeps part of the best file");
    }
    // Traits.
    {
        Machine m;
        m.captured = true;
        m.tier = 2;
        m.noise = 2;
        m.condition = 50;
        Machine s = m;
        s.trait = Trait::Salvaged;
        CHECK(conditionAfterRun(s, false) == conditionAfterRun(m, false) - bal::SALVAGED_EXTRA_WEAR, "SALVAGED wears faster");
        CHECK(repairCost(s) == 50 * bal::SALVAGED_REPAIR, "SALVAGED repairs cheap");
        Machine o = m;
        o.trait = Trait::OldNet;
        o.condition = 31;
        CHECK(conditionAfterRun(o, false) == 30, "OLD NET has a wear floor");
        CHECK(conditionAfterRun(o, true) < 30, "...but not on a trace-out");
        CHECK(rejobFee(o) == 2 * bal::REIMAGINE_FEE, "OLD NET re-jobs at double");
        Machine c = m;
        c.trait = Trait::CorpBuild;
        CHECK(upkeepOf(c) > upkeepOf(m) && conditionAfterRun(c, false) > conditionAfterRun(m, false), "CORPORATE bills more, wears less");
        CHECK(traitFor(Kind::Guarded) == Trait::HotIron && traitFor(Kind::Shrine) == Trait::None, "traits come from the kind");
    }
    // A burned HOT IRON decoy: memorial nameplate, no fine.
    {
        GameState h;
        h.worldSeed = 5;
        h.credits = 3000;
        unlockNetwork(h);
        Machine d;
        d.id = h.net.nextId++;
        d.name = "hot-1";
        d.job = Job::Proxy;
        d.tier = 2;
        d.captured = true;
        d.active = true;
        d.trait = Trait::HotIron;
        h.net.machines.push_back(d);
        CHECK(armDecoy(h, h.net.machines.back(), true).ok, "arm the hot decoy");
        RunSession r(h, testTarget(LockType::None), 4);
        r.trace = 99.95;
        std::string err;
        r.startDownload(0, err);
        while (!r.over()) r.step();
        CHECK(r.end == End::Sacrifice, "the decoy burns");
        long long fines = h.stats.finesPaid;
        r.settle();
        CHECK(h.wall.size() == 1 && h.wall[0].name == "hot-1", "the decoy's nameplate goes on the wall");
        CHECK(h.stats.finesPaid == fines, "HOT IRON decoys waive the fine");
        roundTrip(h);
    }
    if (!problems) std::printf("selftest: keypad marks, programs, jammer/vault, traits, memorial - OK\n");
}

int main(int argc, char** argv) {
    uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 7;
    int runs = argc > 2 ? std::atoi(argv[2]) : 25;
    std::string mode = argc > 3 ? argv[3] : "careful";
    CHAOS = mode == "chaos";
    std::srand(static_cast<unsigned>(seed));
    auto dir = std::filesystem::temp_directory_path() / ("ghostnet-sim-" + std::to_string(seed) + "-" + mode);
    paths::overrideSaveDir(dir.string());
    selfTest();

    GameState g;
    g.worldSeed = seed;
    g.credits = bal::START_CREDITS;
    g.mapRunsLeft = bal::RUNS_PER_MAP;

    for (int i = 0; i < runs && !g.deepDone; ++i) {
        manage(g);
        std::vector<Target> board = buildBoard(g);
        auto usable = [&](const Target& t) { return !g.burned.count(t.id); };
        bool deepReady = g.netUnlocked && (g.knownPhrases.count(kDeepId) || CHAOS) &&
                         g.net.relayBonus() + g.net.proxyBonus() >= 45 && g.deck.uplink + g.deck.shroud >= 3;
        bool early = st.deepAttempts == 0;
        const Target* pick = nullptr;
        auto choose = [&](auto pred) {
            if (pick) return;
            for (auto& t : board) if (usable(t) && pred(t)) { pick = &t; return; }
        };
        if (early || deepReady) choose([](const Target& t) { return t.deep; });
        if (!g.knownPhrases.count(kDeepId)) choose([](const Target& t) { return t.kind == Kind::Side && t.sideIndex == kAnnexSide; });
        if (!CHAOS || i % 2 == 0) choose([](const Target& t) { return t.kind == Kind::Story && !t.deep; });
        if (i % 3 == 1) choose([&](const Target& t) { return t.stealable && g.net.bestScoutTier() > 0; });
        bool strong = g.netUnlocked && g.net.proxyBonus() >= 9 && (g.deck.breaker >= 1 || CHAOS);
        auto rich = [](const Target& t) { return t.kind == Kind::Guarded || t.kind == Kind::Corporate || (t.kind == Kind::Side && t.sideIndex != kAnnexSide); };
        auto easy = [](const Target& t) { return t.kind == Kind::Open || t.kind == Kind::Shrine || t.kind == Kind::Hidden; };
        if (strong) { choose(rich); choose(easy); }
        else { choose(easy); choose(rich); }
        if (!pick) break;
        Target t = *pick;
        if (t.deep && !early) armAnyDecoy(g, true);
        if (g.netUnlocked && !t.deep) scanTarget(g, t);
        ReportView rep = reportView(g, t);
        (void)panelBriefing(g, t);
        bool wantSteal = t.stealable && g.net.bestScoutTier() > 0 && i % 3 == 1 && !t.deep &&
                         g.net.externalCount() < bal::MAX_EXTERNAL - 4;

        RunSession r(g, t, seed * 7919 + static_cast<uint64_t>(i));
        ++st.runs;
        if (t.deep) ++st.deepAttempts;
        if (t.kind == Kind::Side && t.sideIndex == kAnnexSide) ++st.annex;
        solveDoor(r);
        playInside(r, wantSteal, t.deep, rep);
        if (!r.over()) r.disconnect();
        Reckoning rk = r.settle();
        if (rk.end == End::TraceOut) ++st.traceouts;
        if (rk.end == End::Sacrifice) ++st.sacrifices;
        if (rk.end == End::Steal) ++st.captures;
        if (rk.deep && !rk.deepWon) {
            ++st.shortfalls;
            if (rk.shortfall.total != 3) fail("deep shortfall did not count the archives");
        }
        if (rk.deepWon) ++st.deepWon;
        if (!rk.storyOutro.empty() && !rk.deep) ++st.contracts;
        for (auto& l : rk.lines) if (l.text.find("UNPAID") != std::string::npos) ++st.unpaid;
        if (rk.end == End::Sacrifice && rk.sacrificed.empty()) fail("sacrifice without a name");
        checkInvariants(g);
        roundTrip(g);
        std::printf("run %2d  %-34s %-10s credits %6d  relay %2d proxy %2d  deck %d/%d/%d  act %d\n", st.runs, t.name.c_str(),
                    rk.end == End::TraceOut ? "TRACED" : rk.end == End::Sacrifice ? "SACRIFICE" : rk.end == End::Steal ? "CAPTURE"
                    : rk.deepWon ? "DEEP WON" : "clear",
                    g.credits, g.net.relayBonus(), g.net.proxyBonus(), g.deck.breaker, g.deck.uplink, g.deck.shroud, g.act);
    }
    std::printf("\nSIM STATS seed=%llu mode=%s: runs %d contracts %d captures %d (tries %d) traceouts %d sacrifices %d phrases %d "
                "extra %d honeypots %d bought %d decoys %d unpaid %d annex %d deep %d/%d won, shortfall screens %d\n",
                static_cast<unsigned long long>(seed), mode.c_str(), st.runs, st.contracts, st.captures, st.steals, st.traceouts,
                st.sacrifices, st.phrases, st.extra, st.honeypots, st.bought, st.decoys, st.unpaid, st.annex, st.deepWon, st.deepAttempts,
                st.shortfalls);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (problems) { std::printf("problems: %d\n", problems); return 1; }
    if (mode == "deep" && !st.deepWon) { std::printf("FAIL: the deep job was not beaten\n"); return 1; }
    std::printf("problems: none\n");
    return 0;
}
