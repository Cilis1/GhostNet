// GhostNet - automated playtester for the game core.
//
// Plays the real rules (the same code the windowed game runs) end to end:
// solves locks from their feedback only, reads memos for passphrases, builds a
// network, captures hardware, arms decoys, tries the deep job early (to check
// the shortfall readout) and again once its network is strong. After every run
// it checks the game's invariants and round-trips the save file.
//
// usage: ghostnet_sim [seed] [runs] [careful|chaos|deep|subnet|tower]
//   deep   exit 1 unless the deep job is beaten within [runs]
//   tower  beat the deep job, open Act IV and climb the corp towers; then kit trials: a build matched to each
//          tower's doctrine must take all of it, and a single build facing the wrong doctrine must fail
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
bool SUBNET = false;   // "subnet" mode: build subnets early, hunt prototypes, never take the deep job
bool TOWER = false;    // "tower" mode: keep playing after the deep job, into Act IV
int KIT_TIER = 3;      // the tier of the late-game owner's iron in the kit trials
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
    // A composite's rotor stage: every wheel within 2 notches of the keypad digit in its box.
    void carry(const Lock& L) {
        kind = LockType::Rotor;
        wheels.clear();
        for (int i = 0; i < 4; ++i) {
            std::set<int> s;
            int k = L.carried() ? L.outerRows.back().guess[static_cast<size_t>(i)] - '0' : -1;
            for (int v = 0; v <= 9; ++v) if (k < 0 || std::abs(v - k) <= 2) s.insert(v);
            wheels.push_back(s);
        }
    }
    // What PEEK / CRACK showed on screen: box i is exactly c.
    void pin(int i, char c) {
        if (kind == LockType::Rotor) { wheels[static_cast<size_t>(i)] = {c - '0'}; return; }
        std::vector<std::string> keep;
        for (auto& w : cands) if (w[static_cast<size_t>(i)] == c) keep.push_back(w);
        cands.swap(keep);
    }
    explicit Solver(LockType k) : kind(k == LockType::Composite ? LockType::Keypad : k) {
        if (kind == LockType::Keypad) {
            for (int a = 0; a < 10; ++a)
                for (int b = 0; b < 10; ++b)
                    for (int c = 0; c < 10; ++c)
                        for (int d = 0; d < 10; ++d)
                            if (a != b && a != c && a != d && b != c && b != d && c != d)
                                cands.push_back({char('0' + a), char('0' + b), char('0' + c), char('0' + d)});
        } else if (kind == LockType::Rotor) {
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
        if (kind == LockType::Keypad && cands.size() > 2 && cands.size() <= 400) return last = bestKeypad();
        return last = cands.size() < 3 ? cands[0] : cands[cands.size() / 2];
    }
    // A careful player's keypad move once the field is small: any legal code (not just a candidate)
    // that leaves the fewest candidates in the worst case - e.g. parking untried digits in boxes already known.
    std::string bestKeypad() const {
        static std::vector<std::string> all = [] {
            std::vector<std::string> v;
            for (int a = 0; a < 10; ++a)
                for (int b = 0; b < 10; ++b)
                    for (int c = 0; c < 10; ++c)
                        for (int d = 0; d < 10; ++d)
                            if (a != b && a != c && a != d && b != c && b != d && c != d)
                                v.push_back({char('0' + a), char('0' + b), char('0' + c), char('0' + d)});
            return v;
        }();
        std::string best = cands[0];
        size_t bestWorst = cands.size() + 1;
        bool bestIsCand = true;
        for (auto& g : all) {
            int bucket[81] = {0};
            size_t worst = 0;
            for (auto& c : cands) {
                int code = 0;
                for (int i = 0; i < 4; ++i) code = code * 3 + (g[i] == c[i] ? 2 : c.find(g[i]) != std::string::npos ? 1 : 0);
                worst = std::max(worst, static_cast<size_t>(++bucket[code]));
            }
            bool isCand = std::find(cands.begin(), cands.end(), g) != cands.end();
            if (worst < bestWorst || (worst == bestWorst && isCand && !bestIsCand)) { best = g; bestWorst = worst; bestIsCand = isCand; }
        }
        return best;
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
    int gateways = 0, plugged = 0, strikes = 0, clusters = 0, protos = 0, protoSeen = 0, protoTries = 0;
    int towerRuns = 0, floors = 0, rekeys = 0, prepBought = 0, sweeps = 0, misses = 0, programs = 0;
} st;

// Fire any subnet strike that is usable right now (the bot is greedy with them).
void useStrikes(RunSession& r) {
    for (int id : r.strikeBays) {
        std::string err;
        // On a tower the bot saves BLACKOUT for when the trace is actually climbing.
        const Machine* gw = r.g.net.find(id);
        if (r.t.kind == Kind::Tower && gw && gw->role == BayRole::Stealth && r.trace < 35) continue;
        if (r.strikeUsable(id) && r.strike(id, err)) ++st.strikes;
    }
}
// Deck programs, on towers: PEEK at the door, FREEZE when the trace climbs, BURST on a big download.
void usePrograms(RunSession& r) {
    if (r.t.kind != Kind::Tower) return;
    for (int s = 0; s < kProgramSlots; ++s) {
        Program p = r.g.programs[s];
        bool want = (p == Program::Peek && !r.doorOpen) || (p == Program::Freeze && r.trace >= 45) ||
                    (p == Program::Burst && r.act == RunSession::Act::Download && r.actTotal - r.actDone >= 8) ||
                    (p == Program::Spoof && r.doorOpen);
        std::string err;
        if (want && r.programUsable(s) && r.runProgram(s, err)) ++st.programs;
    }
}

void checkInvariants(const GameState& g) {
    if (g.credits < 0) fail("negative credits");
    if (g.net.activeCount() > g.net.slots) fail("more machines running than slots");
    if (g.net.externalCount() > bal::MAX_EXTERNAL) fail("more external machines than MAX_EXTERNAL");
    if (g.net.decoyId >= 0 && !g.net.decoy()) fail("decoy id points at an invalid machine");
    for (auto& m : g.net.machines) {
        if (m.condition < 0 || m.condition > 100) fail("condition out of range");
        if (m.dark() && m.active) fail(m.name + " is dark but active");
        if (m.home && (m.condition != 100 || !m.active)) fail("N0 damaged or stored");
    }
    for (auto& m : g.net.machines) {
        if (m.bay < 0) continue;
        const Machine* gw = g.net.find(m.bay);
        if (!gw || gw->job != Job::Gateway || gw->bay >= 0) fail(m.name + " is plugged into something that isn't a gateway");
        if (!m.active || m.home || m.job == Job::Gateway) fail(m.name + " sits in a subnet it can't be in");
        if (gw && static_cast<int>(g.net.bayMembers(gw->id).size()) > bal::BAY_SLOTS) fail("a subnet holds more than its slots");
    }
    if (g.net.relayBonus() > bal::RELAY_HARD_CAP || g.net.proxyBonus() > bal::PROXY_HARD_CAP) fail("a bonus passed its hard cap");
    if (g.netUnlocked && !g.net.home()) fail("network unlocked without N0");
    if (g.act < 1 || g.act > kMaxAct) fail("act out of range");
    if (g.act == 4 && !g.deepDone) fail("Act IV without the deep job");
    for (int k = 0; k < kTowerCount; ++k) if (g.floorsCleared(k) < 0 || g.floorsCleared(k) > kFloors) fail("tower progress out of range");
}

void roundTrip(const GameState& g) {
    std::string err;
    if (!saveGame(g, &err)) { fail("save failed: " + err); return; }
    GameState h;
    if (!loadGame(h, &err)) { fail("load failed: " + err); return; }
    if (h.credits != g.credits || h.net.machines.size() != g.net.machines.size() || h.story != g.story || h.act != g.act ||
        h.deepDone != g.deepDone || h.net.decoyId != g.net.decoyId || h.deck.lens != g.deck.lens || h.knownPhrases != g.knownPhrases ||
        h.wall.size() != g.wall.size() || h.programs[0] != g.programs[0] || h.programs[1] != g.programs[1] ||
        [&] { for (int k = 0; k < kTowerCount; ++k) if (h.floorsCleared(k) != g.floorsCleared(k) || h.keyOf(k) != g.keyOf(k)) return true; return false; }())
        fail("save/load round trip changed the game");
    for (size_t i = 0; i < h.net.machines.size() && i < g.net.machines.size(); ++i)
        if (h.net.machines[i].trait != g.net.machines[i].trait || h.net.machines[i].job != g.net.machines[i].job)
            fail("save/load lost a machine's job or trait");
    for (auto& m : g.net.machines) {
        const Machine* k = h.net.find(m.id);
        if (!k || k->bay != m.bay || k->role != m.role || k->bayOn != m.bayOn || k->tier != m.tier || k->active != m.active)
            fail("save/load lost a machine's subnet, role or tier");
    }
    if (h.net.relayBonus() != g.net.relayBonus() || h.net.proxyBonus() != g.net.proxyBonus() || h.net.upkeepTotal() != g.net.upkeepTotal())
        fail("save/load changed the network's numbers");
}

void solveDoor(RunSession& r) {
    if (r.doorOpen || r.over()) return;
    if (r.g.knownPhrases.count(r.t.id) && !CHAOS) {
        ++st.phrases;
        r.phrase(r.t.passphrase);   // a composite's phrase opens only the outer keypad
        if (r.doorOpen || r.over()) return;
    }
    Solver s(r.lock.type);
    if (r.lock.composite() && r.lock.stage == 1) s.carry(r.lock);
    int stage = r.lock.stage;
    size_t pinned = 0;
    for (int i = 0; i < 24 && !r.doorOpen && !r.over(); ++i) {
        if (!CHAOS) { useStrikes(r); usePrograms(r); }
        if (r.over()) return;
        for (; pinned < r.lock.peeked.size(); ++pinned) {   // what PEEK / CRACK showed on screen
            int pos = r.lock.peeked[pinned];
            s.pin(pos, r.lock.secret[static_cast<size_t>(pos)]);
        }
        if (r.lock.sealed()) {
            std::string err;
            if (r.extra(err)) { ++st.extra; continue; }
            r.disconnect();
            return;
        }
        std::string guess = s.next();
        if (CHAOS && r.lock.left() > 1 && std::rand() % 10 < 6) {
            guess = r.lock.cur() == LockType::Keypad ? "0123" : r.lock.cur() == LockType::Rotor ? "0000" : "zzzzz";
            s.last = guess;
        }
        std::string err;
        if (!r.guess(guess, err)) { fail("lock rejected a well-formed guess: " + err); r.disconnect(); return; }
        if (r.lock.stage != stage) {   // the keypad opened: the rotor starts from its digits
            stage = r.lock.stage;
            s.carry(r.lock);
            pinned = 0;
        } else if (!r.lock.history.empty()) {
            s.feed(r.lock.history.back());
        }
    }
}

void runTimed(RunSession& r) {
    int guard = 0;
    while (r.busy() && !r.over() && ++guard < 1000) {
        if (!CHAOS) { useStrikes(r); usePrograms(r); }
        if (!r.busy() || r.over()) break;
        r.step();
    }
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
    if (!deep && r.t.kind != Kind::Tower)
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
    if (r.t.kind == Kind::Tower)   // the way up: the facilities memo, right after the objectives
        for (size_t i = 0; i < r.t.files.size(); ++i)
            if (r.t.files[i].phraseFor >= 0) order.insert(order.begin() + static_cast<long>(r.t.objectiveCount()), -1 - static_cast<int>(i));
    for (int i : order) {
        if (r.over()) return;
        if (i < 0) {   // a memo to read
            if (r.jobDone && r.secondsLeft() > 8) r.read(-1 - i);
            continue;
        }
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
        if (g.act >= 4) {   // the towers want a finished deck: BREAKER for the FORTRESS, cover for the clock
            std::vector<std::pair<int, std::pair<int, bool>>> late = {
                {0, {static_cast<int>(Tool::Breaker), g.deck.breaker < bal::DECK_MAX}},
                {0, {static_cast<int>(Tool::Shroud), g.deck.shroud < bal::DECK_MAX}},
                {1, {static_cast<int>(Job::Proxy), g.net.proxyBonus() < bal::PROXY_CAP && free()}},
                {0, {static_cast<int>(Tool::Uplink), g.deck.uplink < bal::DECK_MAX}},
            };
            steps.insert(steps.begin(), late.begin(), late.end());
        }
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
    if (!CHAOS && g.act < 4 && g.deck.uplink + g.deck.shroud >= 3) {   // Act IV keeps its kit
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
    // Subnets, once the main rack is maxed: buy a gateway and plug stored machines into it.
    if (g.net.slots >= (SUBNET ? 4 : bal::MAX_SLOTS) && g.deck.uplink + g.deck.shroud >= (SUBNET ? 1 : 3)) {
        // Only a running gateway opens a bay. A captured one sitting in storage counts for nothing
        // until it's switched on, so try that first (if the bill allows) before buying another.
        bool haveGw = false;
        for (auto& m : g.net.machines) if (m.job == Job::Gateway && m.active) haveGw = true;
        int reserve = 2 * g.net.upkeepTotal() + 300;
        if (!haveGw)
            for (auto& m : g.net.machines)
                if (m.job == Job::Gateway && !m.dark() && g.credits >= reserve + 2 * upkeepOf(m) && activate(g, m).ok) {
                    setRole(g, m, CHAOS ? BayRole::Siege : SUBNET ? BayRole::Speed : BayRole::Stealth);
                    haveGw = true;
                    break;
                }
        if (!haveGw && g.credits - bal::BUY_PRICE[static_cast<int>(Job::Gateway)] >= reserve && g.net.externalCount() < bal::MAX_EXTERNAL) {
            // The rack is full: store the weakest non-home machine to make room for the gateway.
            if (g.net.activeCount() >= g.net.slots)
                for (auto& m : g.net.machines)
                    if (!m.home && m.active && m.bay < 0 && g.net.decoyId != m.id) { deactivate(g, m); break; }
            Result res = buyMachine(g, Job::Gateway);
            if (res.ok) {
                ++st.gateways;
                ++st.bought;
                Machine& gw = g.net.machines.back();
                if (!gw.active) activate(g, gw);
                setRole(g, gw, CHAOS ? BayRole::Siege : SUBNET ? BayRole::Speed : BayRole::Stealth);
            }
        }
        for (auto* gw : g.net.openBays()) {
            Machine* G = g.net.find(gw->id);
            for (auto& m : g.net.machines)
                if (!m.active && !m.dark() && canPlug(g, m, *G) && g.credits > 2 * g.net.upkeepTotal() + upkeepOf(m) + 150 &&
                    plugIn(g, m, *G).ok)
                    ++st.plugged;
            if (g.net.clusterBonus(G->id)) ++st.clusters;
        }
    }
    armAnyDecoy(g, CHAOS);
}

// ---------------------------------------------------------------------------
// Act IV: the careful bot on a tower floor. Prep is bought with credits, the
// floor is played, and nothing happens unless the player starts it.
// ---------------------------------------------------------------------------
void towerPrep(GameState& g, const Target& t, bool buyPhrase) {
    int reserve = 2 * g.net.upkeepTotal() + 300;
    if (prepAvailable(g, t, Prep::Plan) && g.credits - prepCost(t, Prep::Plan) >= reserve && buyPrep(g, t, Prep::Plan).ok) ++st.prepBought;
    if (buyPhrase && prepAvailable(g, t, Prep::Phrase) && g.credits - prepCost(t, Prep::Phrase) >= reserve && buyPrep(g, t, Prep::Phrase).ok)
        ++st.prepBought;
}

// Programs for the doctrine, reloaded before every attempt (they're single use).
void loadPrograms(GameState& g, Doctrine d) {
    for (int s = 0; s < kProgramSlots; ++s) unloadProgram(g, s);
    if (d == Doctrine::Fortress) { buyProgram(g, Program::Peek); buyProgram(g, Program::Freeze); }
    else { buyProgram(g, Program::Freeze); buyProgram(g, Program::Burst); }
}

Reckoning playFloor(GameState& g, const Target& t, uint64_t entropy) {
    ReportView rep = reportView(g, t);
    (void)panelBriefing(g, t);
    RunSession r(g, t, entropy);
    ++st.towerRuns;
    solveDoor(r);
    playInside(r, false, false, rep);
    if (!r.over()) r.disconnect();
    st.sweeps += r.sweeps;
    st.misses += r.misses;
    int got = 0;
    for (auto& f : r.t.files) if (f.kind == FileKind::Objective && f.taken) ++got;
    if (std::getenv("GHOSTNET_SIM_VERBOSE"))
        std::printf("    floor %s: end %d door %d stage %d guesses %d/%d trace %.0f secs %d obj %d/3 sweeps %d misses %d jam %d/%d%% eff %.2f\n",
                    t.name.c_str(), static_cast<int>(r.end), r.doorOpen, r.lock.stage, r.lock.used, r.lock.maxGuesses, r.trace, r.secs, got,
                    r.sweeps, r.misses, r.jamDelay, r.jamCut, effectiveTraceRate(g, t));
    Reckoning rk = r.settle();
    if (rk.floorCleared >= 0) ++st.floors;
    if (rk.rekeyed) ++st.rekeys;
    if (rk.end == End::TraceOut) ++st.traceouts;
    return rk;
}

// The natural bot's retool for a doctrine, with real purchases only: a gateway in the role the
// tower calls for, and its subnet filled with compute boxes (FORTRESS) or jammers (BIOSENTINEL).
// Returns false if it can't afford the kit yet.
bool retoolFor(GameState& g, Doctrine d) {
    const Job want = d == Doctrine::Fortress ? Job::Compute : Job::Jammer;
    const BayRole role = d == Doctrine::Fortress ? BayRole::Siege : BayRole::Stealth;
    Machine* gw = nullptr;
    for (auto& m : g.net.machines) if (m.job == Job::Gateway && m.working()) { gw = &m; break; }
    auto reserve = [&] { return 2 * g.net.upkeepTotal() + 400; };
    if (!gw) {
        if (g.credits - bal::BUY_PRICE[static_cast<int>(Job::Gateway)] < reserve() || g.net.externalCount() >= bal::MAX_EXTERNAL) return false;
        if (g.net.activeCount() >= g.net.slots)
            for (auto& m : g.net.machines)
                if (!m.home && m.active && m.bay < 0 && m.job != Job::Proxy && m.job != Job::Relay) { deactivate(g, m); break; }
        if (g.net.activeCount() >= g.net.slots)
            for (auto& m : g.net.machines)
                if (!m.home && m.active && m.bay < 0) { deactivate(g, m); break; }
        if (!buyMachine(g, Job::Gateway).ok) return false;
        ++st.bought;
        gw = &g.net.machines.back();
        if (!gw->active && !activate(g, *gw).ok) return false;
    }
    int gwId = gw->id;
    if (gw->role != role) setRole(g, *g.net.find(gwId), role);
    // Make room: anything in the bay that isn't part of the kit goes back to storage.
    for (auto* m : g.net.bayMembers(gwId)) if (m->job != want) unplug(g, *g.net.find(m->id));
    auto count = [&] { int n = 0; for (auto* m : g.net.bayMembers(gwId)) if (m->job == want) ++n; return n; };
    for (auto& m : g.net.machines)   // stored kit machines first
        if (count() < 2 && m.job == want && !m.active && !m.dark() && canPlug(g, m, *g.net.find(gwId))) plugIn(g, m, *g.net.find(gwId));
    while (count() < 2) {
        if (g.credits - bal::BUY_PRICE[static_cast<int>(want)] < reserve() || g.net.externalCount() >= bal::MAX_EXTERNAL) return false;
        if (!buyMachine(g, want).ok) return false;
        ++st.bought;
        Machine& m = g.net.machines.back();
        if (m.active) deactivate(g, m);
        if (!canPlug(g, m, *g.net.find(gwId)) || !plugIn(g, m, *g.net.find(gwId)).ok) return false;
        ++st.plugged;
    }
    if (!g.net.bayOnline(*g.net.find(gwId))) toggleBay(g, *g.net.find(gwId));
    loadPrograms(g, d);
    return g.net.bayOnline(*g.net.find(gwId));
}

// A late-game owner: the deep job done, a maxed deck, a full main rack of tier-3 speed and cover.
Machine owned(GameState& g, Job j, int tier) {
    Machine m;
    m.id = g.net.nextId++;
    m.name = std::string(jobName(j)).substr(0, 4) + "-" + std::to_string(m.id);
    m.job = j;
    m.tier = tier;
    m.captured = true;
    m.noise = 1;
    m.condition = 100;
    m.active = false;
    g.net.machines.push_back(m);
    return m;
}

GameState ledgerOwner(uint64_t seed) {
    GameState g;
    g.worldSeed = seed;
    g.credits = 30000;
    g.mapRunsLeft = bal::RUNS_PER_MAP;
    g.story = storyCount();
    g.act = 3;
    g.deepDone = true;
    g.retired = true;
    unlockNetwork(g);
    g.net.slots = bal::MAX_SLOTS;
    g.net.home()->tier = 3;
    g.deck.breaker = g.deck.uplink = g.deck.shroud = bal::DECK_MAX;
    g.deck.lens = bal::LENS_MAX;
    for (Job j : {Job::Relay, Job::Relay, Job::Proxy, Job::Proxy}) {
        int id = owned(g, j, KIT_TIER).id;
        activate(g, *g.net.find(id));
    }
    if (!enterLedger(g)) fail("enterLedger refused an owner");
    return g;
}

// The doctrine slot of the main rack: a gateway whose subnet is built for one doctrine.
void fitKit(GameState& g, Doctrine d) {
    auto& v = g.net.machines;
    for (auto& m : v) if (m.bay >= 0) unplug(g, m);
    v.erase(std::remove_if(v.begin(), v.end(), [](const Machine& m) { return m.job == Job::Gateway || m.job == Job::Compute || m.job == Job::Jammer; }),
            v.end());
    int gw = owned(g, Job::Gateway, KIT_TIER).id;
    if (!activate(g, *g.net.find(gw)).ok) fail("kit: the gateway has no slot");
    std::vector<Job> bay = d == Doctrine::Fortress ? std::vector<Job>{Job::Compute, Job::Compute, Job::Relay}
                                                   : std::vector<Job>{Job::Jammer, Job::Jammer, Job::Proxy};
    for (Job j : bay) {
        int id = owned(g, j, KIT_TIER).id;
        if (!plugIn(g, *g.net.find(id), *g.net.find(gw)).ok) fail("kit: a machine wouldn't plug in");
    }
    setRole(g, *g.net.find(gw), d == Doctrine::Fortress ? BayRole::Siege : BayRole::Stealth);
    loadPrograms(g, d);
}

// Climb one tower with one kit. Returns floors taken.
int climb(GameState g, int tower, Doctrine kit, uint64_t entropy, int attempts, int* runsUsed) {
    int runs = 0;
    for (int a = 0; a < attempts && g.floorsCleared(tower) < kFloors; ++a) {
        repairAll(g);
        for (auto& m : g.net.machines) if (m.bay >= 0 || m.job == Job::Gateway) m.condition = 100;
        fitKit(g, kit);
        Target t = makeTowerFloor(g, tower, g.floorsCleared(tower));
        towerPrep(g, t, true);
        g.credits = std::max(g.credits, 30000);
        playFloor(g, t, entropy * 104729 + static_cast<uint64_t>(a));
        ++runs;
        checkInvariants(g);
    }
    if (runsUsed) *runsUsed = runs;
    return g.floorsCleared(tower);
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
    // Subnets: a gateway opens a bay; members don't use main slots; power budget; role; on/off.
    {
        GameState h;
        h.worldSeed = 6;
        h.credits = 20000;
        unlockNetwork(h);
        h.net.slots = bal::MAX_SLOTS;
        CHECK(buyMachine(h, Job::Gateway).ok, "buy a gateway");
        Machine& gw = h.net.machines.back();
        int gwId = gw.id;
        CHECK(h.net.bayOpen(gw) && h.net.openBays().size() == 1, "a running gateway opens a bay");
        int mainBefore = h.net.activeCount();
        const int base = h.net.relayBonus();   // N0 starts as a T1 relay
        auto addT3 = [&](Job j, const std::string& name) -> Machine& {
            Machine m;
            m.id = h.net.nextId++;
            m.name = name;
            m.job = j;
            m.tier = 3;
            m.captured = true;
            m.noise = 1;
            h.net.machines.push_back(m);
            return h.net.machines.back();
        };
        addT3(Job::Relay, "r1");
        addT3(Job::Relay, "r2");
        addT3(Job::Relay, "r3");
        auto* G = h.net.find(gwId);
        std::string why;
        // T2 gateway: 7 power, a T3 draws 3 -> only two fit
        CHECK(plugIn(h, *h.net.find(gwId + 1), *G).ok && plugIn(h, *h.net.find(gwId + 2), *G).ok, "plug two T3 relays in");
        CHECK(!canPlug(h, *h.net.find(gwId + 3), *G, &why) && why.find("power") != std::string::npos, "the power budget refuses a third T3");
        CHECK(h.net.activeCount() == mainBefore, "subnet members don't take main-rack slots");
        int bayRelay = std::min(2 * bal::RELAY_BONUS[3] + bal::SPEED_BONUS[2], bal::BAY_RELAY_CAP);
        CHECK(h.net.relayBonus() == base + bayRelay, "the subnet's relays pool under the subnet cap");
        CHECK(h.net.bayUpkeep(gwId) == 2 * upkeepOf(*h.net.find(gwId + 1)), "the subnet bills its members");
        CHECK(toggleBay(h, *G).ok && h.net.relayBonus() == base && h.net.bayUpkeep(gwId) == 0, "switched off: no bill, no effect");
        CHECK(toggleBay(h, *G).ok && h.net.relayBonus() == base + bayRelay, "switched back on");
        // Upgrade the gateway's power (a T3 gateway: 9) and form a cluster.
        G->tier = 3;
        CHECK(plugIn(h, *h.net.find(gwId + 3), *G).ok, "the third relay fits a T3 gateway");
        CHECK(h.net.clusterBonus(gwId) == 1 && h.net.effTier(*h.net.find(gwId + 1)) == 4, "three matching T3s act as T4");
        CHECK(h.net.relayBonus() == base + std::min(3 * bal::RELAY_BONUS[4] + bal::SPEED_BONUS[3], bal::BAY_RELAY_CAP), "the cluster uses T4 numbers");
        CHECK(setRole(h, *G, BayRole::Siege).ok && h.net.computeCapacity() == 1 && h.net.jamSpikeCut() == bal::SIEGE_SPIKE[3], "SIEGE role");
        roundTrip(h);
        CHECK(unplug(h, *h.net.find(gwId + 2)).ok && h.net.clusterBonus(gwId) == 0, "pulling one out ends the cluster - nothing merged");
        CHECK(!h.net.find(gwId + 2)->active && h.net.find(gwId + 2)->tier == 3, "the unplugged machine is unchanged, in storage");
        CHECK(!scrap(h, gwId).ok, "a gateway with a subnet can't be scrapped");
        CHECK(!rejob(h, *G, Job::Relay).ok, "or re-jobbed");
        // Over budget (e.g. a member got swapped for heavier iron): the bay browns out.
        h.net.find(gwId + 1)->tier = 4;
        h.net.find(gwId + 3)->tier = 4;
        CHECK(!h.net.bayOnline(*G) && h.net.relayBonus() == base, "over its power budget, a subnet browns out");
        h.net.find(gwId + 1)->tier = 3;
        h.net.find(gwId + 3)->tier = 3;
        CHECK(h.net.bayOnline(*G), "back under budget");
    }
    // Strikes: once per run, matched to the role, wear the whole bay; a gateway decoy burns the subnet.
    {
        GameState h;
        h.worldSeed = 7;
        h.credits = 20000;
        unlockNetwork(h);
        buyMachine(h, Job::Gateway);
        int gwId = h.net.machines.back().id;
        buyMachine(h, Job::Proxy);
        Machine& p = h.net.machines.back();
        int pId = p.id;
        CHECK(plugIn(h, p, *h.net.find(gwId)).ok, "plug a proxy into the bay");
        setRole(h, *h.net.find(gwId), BayRole::Stealth);
        {
            RunSession r(h, testTarget(LockType::Keypad), 9);
            std::string err;
            CHECK(r.strikeUsable(gwId), "a STEALTH strike is ready");
            double t0 = r.trace;
            CHECK(r.strike(gwId, err), "BLACKOUT");
            CHECK(r.freezeLeft == bal::STRIKE_BLACKOUT, "BLACKOUT sets the freeze");
            r.guess("1234", err);
            CHECK(r.trace == t0, "BLACKOUT holds the trace");
            CHECK(!r.strike(gwId, err), "once per run");
            r.disconnect();
            int c0 = h.net.find(pId)->condition, g0 = h.net.find(gwId)->condition;
            r.settle();
            CHECK(h.net.find(pId)->condition < c0 && h.net.find(gwId)->condition <= g0 - bal::STRIKE_WEAR,
                  "a strike wears every machine in the bay");
        }
        setRole(h, *h.net.find(gwId), BayRole::Siege);
        {
            RunSession r(h, testTarget(LockType::Keypad), 10);
            std::string err;
            CHECK(r.strike(gwId, err) && r.lock.known(0) && r.lock.known(1), "CRACK reveals two boxes");
            r.disconnect();
            r.settle();
        }
        setRole(h, *h.net.find(gwId), BayRole::Speed);
        {
            RunSession r(h, testTarget(LockType::None), 11);
            std::string err;
            CHECK(!r.strikeUsable(gwId), "MIRROR needs a download");
            r.startDownload(0, err);
            int left = r.actTotal - r.actDone;
            CHECK(r.strike(gwId, err) && (r.actTotal - r.actDone < left || !r.busy()), "MIRROR jumps the download");
            r.disconnect();
            r.settle();
        }
        for (auto& m : h.net.machines) m.condition = 100;
        CHECK(armDecoy(h, *h.net.find(gwId), true).ok, "arm a whole subnet as the decoy");
        size_t before = h.net.machines.size();
        RunSession r(h, testTarget(LockType::None), 12);
        r.trace = 99.95;
        std::string err;
        r.startDownload(0, err);
        while (!r.over()) r.step();
        CHECK(r.end == End::Sacrifice, "the subnet takes the ticket");
        CHECK(h.net.machines.size() == before - 2 && !h.net.find(pId) && !h.net.find(gwId), "the gateway's subnet is lost as a group");
        long long fines = h.stats.finesPaid;
        r.settle();
        CHECK(h.wall.size() == 2 && h.stats.finesPaid == fines, "two nameplates; the fine is waived");
        roundTrip(h);
    }
    // Prototypes: the only real tier 4.
    {
        Target t = testTarget(LockType::None);
        t.kind = Kind::Prototype;
        t.id = 1036;
        t.stealable = true;
        CHECK(hardwareFor(t).tier == 4 && traitFor(Kind::Prototype) == Trait::Unstable, "a prototype lab holds UNSTABLE T4");
        Machine m;
        m.captured = true;
        m.tier = 4;
        m.noise = 4;
        m.condition = 80;
        Machine u = m;
        u.trait = Trait::Unstable;
        CHECK(upkeepOf(u) > upkeepOf(m) && conditionAfterRun(u, false) < conditionAfterRun(m, false) && repairCost(u) > repairCost(m),
              "UNSTABLE: bills more, wears double, repairs dear");
        bool seen = false;
        for (int map = 0; map < 30 && !seen; ++map) {
            GameState h;
            h.worldSeed = 99;
            h.netUnlocked = true;
            h.act = 2;
            h.mapIndex = map;
            for (auto& b : buildBoard(h)) if (b.kind == Kind::Prototype) {
                seen = true;
                CHECK(b.stealable && b.watchdog && b.security == 4 && !b.files.empty(), "prototype labs are hard");
            }
        }
        CHECK(seen, "prototype listings appear on some boards");
        GameState h;
        h.worldSeed = 8;
        h.credits = 20000;
        unlockNetwork(h);
        h.net.slots = bal::MAX_SLOTS;
        RunSession r(h, t, 13);
        CaptureQuote q = r.quote();
        std::string err;
        CHECK(q.possible && r.startHold(q.def, err), "start holding the prototype");
        while (r.busy() && !r.over()) r.step();
        CHECK(r.end == End::Steal && h.net.machines.back().tier == 4 && h.net.machines.back().trait == Trait::Unstable,
              "capturing a prototype gives real T4 UNSTABLE hardware");
        r.settle();
        roundTrip(h);
        CHECK(upgradeHome(h).ok || h.net.home()->tier <= 3, "N0 never passes tier 3");
    }
    // 0.6 - the composite lock: keypad marks carry into the rotor's start; each stage judges itself.
    {
        Rng rr(42);
        for (int n = 0; n < 200; ++n) {
            Lock c = makeLock(LockType::Composite, rr, 5);
            std::string outer = c.secret;
            CHECK(c.stage == 0 && c.cur() == LockType::Keypad && c.inner.size() == 4, "a composite starts as a keypad");
            for (int i = 0; i < 4; ++i) CHECK(std::abs(c.inner[size_t(i)] - outer[size_t(i)]) <= 2, "every rotor wheel is within 2 of its keypad digit");
            std::string err;
            std::string wrong = outer.substr(1) + outer.substr(0, 1);
            CHECK(c.guess(wrong, err) && c.history.back().hit == 0 && c.stage == 0, "a keypad miss stays on stage 1");
            CHECK(c.guess(outer, err) && c.stage == 1 && !c.open && c.cur() == LockType::Rotor, "the right keypad code wakes the rotor");
            CHECK(c.history.empty() && c.outerRows.size() == 2 && c.carried() && c.secret == c.inner, "the rotor starts its own feedback");
            CHECK(c.used == 2, "one shared guess budget");
            CHECK(c.guess(c.inner, err) && c.open, "the rotor opens the door");
        }
        Lock k = makeLock(LockType::Composite, rr, 0);
        std::string err;
        CHECK(k.maxGuesses == bal::FORTRESS_GUESSES && k.peek() == 0 && k.known(0), "PEEK works on the keypad stage");
        k.guess(k.secret, err);
        CHECK(k.peeked.empty() && !k.known(0) && k.peek() == 0, "...and again on the rotor stage");
        CHECK(lockHasOddNearRule(LockType::Composite) && lockRuleText(LockType::Composite).find("2 notches") != std::string::npos,
              "the composite's near-miss rule is stated");
    }
    // 0.6 - Act IV: the Ledger, the towers, prep, re-keying, save v5.
    {
        GameState h;
        h.worldSeed = 11;
        CHECK(!enterLedger(h) && buildBoard(h).front().kind != Kind::Tower, "no Ledger before the deep job");
        h = ledgerOwner(11);
        CHECK(h.act == 4 && !enterLedger(h), "the Ledger opens once, as Act IV");
        std::vector<Target> b = buildBoard(h);
        CHECK(b.size() >= 2 && b[0].kind == Kind::Tower && b[1].kind == Kind::Tower, "both towers lead the board");
        CHECK(b[0].doctrine == Doctrine::Fortress && b[0].lock == LockType::Composite && b[1].doctrine == Doctrine::Biosentinel && b[1].watchdog,
              "Kessler-Maru is a FORTRESS, Tidewater a BIOSENTINEL");
        Target f0 = makeTowerFloor(h, 0, 0);
        bool memo = false;
        for (auto& f : makeTowerFloor(h, 1, 0).files)
            if (f.phraseFor == floorId(1, 1) && f.text.find(towerPassphrase(h, 1, 1)) != std::string::npos) memo = true;
        CHECK(memo, "each BIOSENTINEL floor leaks the next floor's phrase");
        int c0 = h.credits;
        CHECK(buyPrep(h, f0, Prep::Plan).ok && h.reports[f0.id] == 3 && h.credits == c0 - bal::PREP_PLAN_COST, "a floor plan is a full report");
        CHECK(reportView(h, f0).quality == 3, "the plan shows at full quality");
        CHECK(!f0.backdoor && !buyPrep(h, f0, Prep::Phrase).ok, "FORTRESS: no ports, no phrase for sale");
        Target b0 = makeTowerFloor(h, 1, 0);
        CHECK(buyPrep(h, b0, Prep::Phrase).ok && h.knownPhrases.count(b0.id) && !buyPrep(h, b0, Prep::Phrase).ok, "a phrase is bought once");
        {   // a composite behind a phrase: the phrase opens the keypad only, without showing its code
            Target c = b0;
            c.lock = LockType::Composite;
            RunSession r(h, c, 76);
            CHECK(r.phrase(c.passphrase) && !r.doorOpen && r.lock.stage == 1 && !r.lock.carried(), "a phrase opens a composite's keypad only, code unseen");
            double t0 = r.trace;
            CHECK(!r.phrase(c.passphrase) && r.trace == t0, "the rotor takes no phrases, and costs no time to learn it");
            r.disconnect();
            r.settle();
        }
        {   // FORTRESS: misses ping the trace
            RunSession r(h, f0, 77);
            double t0 = r.trace;
            std::string err;
            std::string miss = r.lock.secret.substr(1) + r.lock.secret.substr(0, 1);   // all amber, never the code
            CHECK(r.guess(miss, err), "a keypad-stage guess");
            CHECK(r.misses == 1 && r.trace > t0 + bal::COST_GUESS * r.rateNow(), "a FORTRESS miss is logged as a ping");
            r.trace = 99.9;
            r.guess(miss, err);
            CHECK(r.end == End::TraceOut, "traced on the tower");
            r.settle();
            CHECK(h.keyOf(0) == 1, "a trace re-keys the tower");
            CHECK(h.reports.count(f0.id), "the floor plan survives a re-key");
        }
        {   // a re-key stales the phrases you hold for that tower (and only on a real trace)
            RunSession r(h, b0, 79);
            r.trace = 99.95;
            std::string err;
            r.phrase("WRONGWORD");
            CHECK(r.end == End::TraceOut, "traced at Tidewater");
            r.settle();
            CHECK(h.keyOf(1) == 1 && !h.knownPhrases.count(b0.id) && makeTowerFloor(h, 1, 0).passphrase == towerPassphrase(h, 1, 0),
                  "the re-key stales the bought phrase");
            roundTrip(h);
        }
        {   // FORTRESS: every compute box answers once at the door
            GameState o = ledgerOwner(13);
            fitKit(o, Doctrine::Fortress);
            RunSession r(o, makeTowerFloor(o, 0, 0), 80);
            int cap = r.emergLeft, extras = 0;
            std::string err;
            while (r.extra(err)) ++extras;
            CHECK(cap == 3 && extras == 3 && r.lock.maxGuesses == bal::FORTRESS_GUESSES + o.deck.breaker + 3, "three answers on a FORTRESS door");
            r.disconnect();
            r.settle();
            roundTrip(h);
        }
        {   // BIOSENTINEL: sweeps on the beat after the jammers' hold; a frozen trace lets them pass
            Target t = makeTowerFloor(h, 1, 0);
            t.lock = LockType::None;
            RunSession r(h, t, 78);
            CHECK(r.nextSweep() == bal::BIO_SWEEP_EVERY - r.secs % bal::BIO_SWEEP_EVERY, "the next sweep is announced");
            r.freezeLeft = 1000;
            std::string err;
            r.startDownload(0, err);
            while (r.busy() && !r.over()) r.step();
            CHECK(r.sweeps == 0, "sweeps pass over a frozen trace");
            r.freezeLeft = 0;
            int s0 = r.secs;
            r.trace = 0;
            for (int i = 0; i < bal::BIO_SWEEP_EVERY + 1 && !r.over(); ++i) { r.startDownload(1, err); r.step(); r.abortAction(); }
            CHECK(r.sweeps >= 1 || r.secs - s0 < bal::BIO_SWEEP_EVERY, "a sweep pings an unfrozen trace");
            r.disconnect();
            r.settle();
        }
        {   // clearing a floor moves the tower up; the Boardroom owns it
            GameState o = ledgerOwner(12);
            for (int f = 0; f < kFloors; ++f) {
                Target t = makeTowerFloor(o, 1, f);
                t.lock = LockType::None;
                t.watchdog = false;
                t.doctrine = Doctrine::None;
                t.traceOverride = 0.1;
                RunSession r(o, t, 90 + static_cast<uint64_t>(f));
                std::string err;
                for (size_t i = 0; i < r.t.files.size(); ++i)
                    if (r.t.files[i].kind == FileKind::Objective) { r.startDownload(static_cast<int>(i), err); while (r.busy()) r.step(); }
                CHECK(r.jobDone, "all of a floor's objectives");
                r.disconnect();
                Reckoning rk = r.settle();
                CHECK(rk.floorCleared == f && o.floorsCleared(1) == f + 1, "the floor counts");
                if (f + 1 == kFloors) CHECK(rk.towerOwned && !rk.storyOutro.empty(), "the Boardroom owns the tower");
            }
            std::vector<Target> b2 = buildBoard(o);
            int towersListed = 0;
            for (auto& t : b2) if (t.kind == Kind::Tower) ++towersListed;
            CHECK(towersListed == 1, "an owned tower leaves the board");
            roundTrip(o);
        }
    }
    if (!problems) std::printf("selftest: keypad marks, programs, jammer/vault, traits, memorial, subnets, strikes, prototypes, composite, ledger - OK\n");
}

int main(int argc, char** argv) {
    uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 7;
    int runs = argc > 2 ? std::atoi(argv[2]) : 25;
    std::string mode = argc > 3 ? argv[3] : "careful";
    CHAOS = mode == "chaos";
    SUBNET = mode == "subnet";
    TOWER = mode == "tower";
    std::srand(static_cast<unsigned>(seed));
    auto dir = std::filesystem::temp_directory_path() / ("ghostnet-sim-" + std::to_string(seed) + "-" + mode);
    paths::overrideSaveDir(dir.string());
    selfTest();

    GameState g;
    g.worldSeed = seed;
    g.credits = bal::START_CREDITS;
    g.mapRunsLeft = bal::RUNS_PER_MAP;

    for (int i = 0; i < runs && (!g.deepDone || TOWER); ++i) {
        if (g.deepDone && TOWER) {   // Act IV: the bot opens the Ledger and climbs with whatever it built
            if (enterLedger(g)) std::printf("--- ACT IV: THE LEDGER ---\n");
            int k = -1;
            for (int c = 0; c < kTowerCount && k < 0; ++c) if (g.floorsCleared(c) < kFloors) k = c;
            if (k < 0) break;
            repairAll(g);
            for (auto& m : g.net.machines) if (!m.active && !m.dark() && m.bay < 0 && m.job != Job::Compute && m.job != Job::Jammer) activate(g, m);
            bool ready = g.credits >= 2500 && retoolFor(g, towerDoctrine(k));
            if (!ready) goto farm;   // earn the kit on ordinary listings first
            {
            Target t = makeTowerFloor(g, k, g.floorsCleared(k));
            towerPrep(g, t, true);
            Reckoning rk = playFloor(g, t, seed * 7919 + static_cast<uint64_t>(i));
            checkInvariants(g);
            roundTrip(g);
            std::printf("run %2d  %-34s %-10s credits %6d  tower %d floors %d/%d  act %d\n", st.runs + st.towerRuns, t.name.c_str(),
                        rk.end == End::TraceOut ? "TRACED" : rk.end == End::Sacrifice ? "SACRIFICE" : rk.floorCleared >= 0 ? "FLOOR" : "short",
                        g.credits, k, g.floorsCleared(0), g.floorsCleared(1), g.act);
            continue;
            }
        }
    farm:
        manage(g);
        std::vector<Target> board = buildBoard(g);
        auto usable = [&](const Target& t) { return !g.burned.count(t.id); };
        bool deepReady = g.netUnlocked && (g.knownPhrases.count(kDeepId) || CHAOS) &&
                         g.net.relayBonus() + g.net.proxyBonus() >= 45 && g.deck.uplink + g.deck.shroud >= 3;
        bool early = st.deepAttempts == 0 && !SUBNET;
        if (SUBNET) deepReady = false;
        const Target* pick = nullptr;
        auto choose = [&](auto pred) {
            if (pick) return;
            for (auto& t : board) if (usable(t) && pred(t)) { pick = &t; return; }
        };
        if (early || deepReady) choose([](const Target& t) { return t.deep; });
        if (!g.knownPhrases.count(kDeepId)) choose([](const Target& t) { return t.kind == Kind::Side && t.sideIndex == kAnnexSide; });
        if (!CHAOS || i % 2 == 0) choose([](const Target& t) { return t.kind == Kind::Story && !t.deep; });
        for (auto& b : board) if (b.kind == Kind::Prototype && usable(b)) ++st.protoSeen;
        if (SUBNET) choose([&](const Target& t) { return t.kind == Kind::Prototype && g.net.bestScoutTier() > 0; });
        if (SUBNET) choose([&](const Target& t) { return t.kind == Kind::Story && t.deep == false; });
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
        bool wantSteal = t.stealable && g.net.bestScoutTier() > 0 && (i % 3 == 1 || t.kind == Kind::Prototype) && !t.deep &&
                         g.net.externalCount() < (t.kind == Kind::Prototype ? bal::MAX_EXTERNAL : bal::MAX_EXTERNAL - 4);

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
        if (rk.end == End::Steal && t.kind == Kind::Prototype) ++st.protos;
        if (t.kind == Kind::Prototype) ++st.protoTries;
        if (rk.end == End::Sacrifice && rk.sacrificed.empty()) fail("sacrifice without a name");
        checkInvariants(g);
        roundTrip(g);
        std::printf("run %2d  %-34s %-10s credits %6d  relay %2d proxy %2d  deck %d/%d/%d  act %d\n", st.runs, t.name.c_str(),
                    rk.end == End::TraceOut ? "TRACED" : rk.end == End::Sacrifice ? "SACRIFICE" : rk.end == End::Steal ? "CAPTURE"
                    : rk.deepWon ? "DEEP WON" : "clear",
                    g.credits, g.net.relayBonus(), g.net.proxyBonus(), g.deck.breaker, g.deck.uplink, g.deck.shroud, g.act);
    }
    std::printf("\nSIM STATS seed=%llu mode=%s: runs %d contracts %d captures %d (tries %d) traceouts %d sacrifices %d phrases %d "
                "extra %d honeypots %d bought %d decoys %d unpaid %d annex %d deep %d/%d won, shortfall screens %d "
                "gateways %d plugged %d strikes %d cluster-nights %d prototypes %d/%d tries/%d listed-nights\n",
                static_cast<unsigned long long>(seed), mode.c_str(), st.runs, st.contracts, st.captures, st.steals, st.traceouts,
                st.sacrifices, st.phrases, st.extra, st.honeypots, st.bought, st.decoys, st.unpaid, st.annex, st.deepWon, st.deepAttempts,
                st.shortfalls, st.gateways, st.plugged, st.strikes, st.clusters, st.protos, st.protoTries, st.protoSeen);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (problems) { std::printf("problems: %d\n", problems); return 1; }
    if (mode == "deep" && !st.deepWon) { std::printf("FAIL: the deep job was not beaten\n"); return 1; }
    if (mode == "subnet" && st.plugged == 0) { std::printf("FAIL: the subnet bot never used a subnet\n"); return 1; }
    if (TOWER) {
        // Kit trials on a fresh owner: a matched build takes each tower; one build against the wrong doctrine fails.
        int bad = 0;
        const Doctrine docs[2] = {Doctrine::Fortress, Doctrine::Biosentinel};
        for (int k = 0; k < kTowerCount; ++k) {
            GameState o = ledgerOwner(seed * 31 + static_cast<uint64_t>(k));
            Doctrine d = towerDoctrine(k), wrong = docs[d == Doctrine::Fortress ? 1 : 0];
            int used = 0, used2 = 0;
            int got = climb(o, k, d, seed + 1000 * static_cast<uint64_t>(k), 6, &used);
            int miss = climb(o, k, wrong, seed + 1000 * static_cast<uint64_t>(k) + 500, 6, nullptr);
            KIT_TIER = 2;   // all of it bought, nothing captured: matching the doctrine is what counts
            GameState o2 = ledgerOwner(seed * 31 + static_cast<uint64_t>(k));
            int got2 = climb(o2, k, d, seed + 1000 * static_cast<uint64_t>(k) + 250, 6, &used2);
            KIT_TIER = 3;
            std::printf("KIT TRIAL %s (%s): matched kit %d/%d floors in %d runs (T2 kit: %d/%d in %d); %s kit %d/%d floors in 6 runs\n",
                        towerTitle(k), doctrineName(d), got, kFloors, used, got2, kFloors, used2, doctrineName(wrong), miss, kFloors);
            if (got < kFloors || got2 < kFloors) { std::printf("FAIL: the matched kit did not take %s\n", towerTitle(k)); ++bad; }
            if (miss >= kFloors) { std::printf("FAIL: the wrong kit took %s - the doctrine doesn't matter\n", towerTitle(k)); ++bad; }
        }
        std::printf("TOWER STATS: tower runs %d floors %d re-keys %d prep %d sweeps %d logged-misses %d programs %d\n", st.towerRuns, st.floors,
                    st.rekeys, st.prepBought, st.sweeps, st.misses, st.programs);
        if (!g.deepDone || g.act != 4) { std::printf("FAIL: the bot never reached Act IV\n"); ++bad; }
        if (bad) return 1;
    }
    std::printf("problems: none\n");
    return 0;
}
