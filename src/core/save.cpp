#include "save.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "balance.h"
#include "paths.h"
#include "rack.h"
#include "world.h"

namespace fs = std::filesystem;

namespace {
constexpr int kVersion = 9;   // v1 (0.1.x), v2 (0.2-0.3), v3 (0.4), v4 (0.5), v5 (0.6), v6 (0.7), v7 (0.8) and v8 (0.9) saves still load; new fields default
// v7 (0.8): HOUSE POWER - which subnet each owned tower powers. (STANDBY is tonight only and never saved.)
// v8 (0.9): CHAIRMAN IRON - a machine's grade, each tower's service site, objectives an ACCORD vault holds. A third tower.
// v9 (0.10): THE INNER CHAIRS - three more towers, SIGNATURE grade (2), and a cage's state (3 = the inner door is open).

// v6: a FORTRESS floor record may only name a Kessler-Maru floor (a FORTRESS tower's floor id).
bool fortressFloorId(int id) {
    int tower = towerOfId(id);
    if (tower < 0) return false;
    int floor = id - floorId(tower, 0);
    return floor >= 0 && floor < kFloors && floorDoctrine(tower, floor) == Doctrine::Fortress;   // 0.10: PROVING's first floor too
}
bool digits4(const std::string& s, bool distinct) {
    if (s.size() != 4) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    if (distinct)
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (s[static_cast<size_t>(i)] == s[static_cast<size_t>(j)]) return false;
    return true;
}

std::string esc(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') o += static_cast<char>(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o.empty() ? "%00" : o;
}
std::string unesc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            unsigned int v = 0;
            if (std::sscanf(s.substr(i + 1, 2).c_str(), "%2x", &v) == 1) {
                if (v) o += static_cast<char>(v);
                i += 2;
                continue;
            }
        }
        o += s[i];
    }
    return o;
}
int clampi(long long v, int lo, int hi) { return static_cast<int>(std::max<long long>(lo, std::min<long long>(hi, v))); }
}  // namespace

std::string savePath() { return (fs::path(paths::saveDir()) / "ghostnet_save.txt").string(); }
std::string saveTempPath() { return savePath() + ".tmp"; }
// A finished write that couldn't be moved into place still counts: loadGame recovers it.
bool saveExists() {
    std::error_code ec;
    return fs::exists(savePath(), ec) || fs::exists(saveTempPath(), ec);
}

bool saveGame(const GameState& g, std::string* err) {
    std::ostringstream o;
    o << "GHOSTNET " << kVersion << "\n";
    o << "seed " << g.worldSeed << "\n";
    o << "credits " << g.credits << "\n";
    o << "map " << g.mapIndex << " " << g.mapRunsLeft << "\n";
    o << "story " << g.story << "\n";
    o << "act " << g.act << " " << (g.deepDone ? 1 : 0) << " " << (g.retired ? 1 : 0) << "\n";
    o << "unlocked " << (g.netUnlocked ? 1 : 0) << "\n";
    o << "net " << g.net.slots << " " << g.net.decoyId << " " << g.net.scoutCharges << " " << g.net.nextId << " " << g.net.burnedCount << "\n";
    o << "sound " << (g.sound ? 1 : 0) << "\n";
    o << "runcounter " << g.runCounter << "\n";
    o << "deck " << g.deck.breaker << " " << g.deck.uplink << " " << g.deck.shroud << " " << g.deck.lens << "\n";
    const Stats& s = g.stats;
    o << "stats " << s.runs << " " << s.jobs << " " << s.robberies << " " << s.traceouts << " " << s.sacrifices << " " << s.captures
      << " " << s.walkaways << " " << s.unpaidBills << " " << s.earned << " " << s.billsPaid << " " << s.finesPaid << " "
      << s.repairsPaid << " " << s.deepAttempts << "\n";
    for (auto& m : g.net.machines)
        o << "m " << m.id << " " << esc(m.name) << " " << static_cast<int>(m.job) << " " << m.tier << " " << m.captured << " "
          << m.home << " " << m.active << " " << m.condition << " " << m.noise << " " << m.price << " " << esc(m.origin) << " "
          << static_cast<int>(m.trait) << " " << m.bay << " " << static_cast<int>(m.role) << " " << (m.bayOn ? 1 : 0) << " "
          << static_cast<int>(m.grade) << "\n";
    o << "programs";
    for (auto p : g.programs) o << " " << static_cast<int>(p);
    o << "\n";
    for (auto& w : g.wall)
        o << "wall " << esc(w.name) << " " << static_cast<int>(w.job) << " " << w.tier << " " << w.run << " " << esc(w.where) << " "
          << esc(w.origin) << "\n";
    for (int id : g.burned) o << "burned " << id << "\n";
    for (auto& kv : g.reports) o << "report " << kv.first << " " << kv.second << "\n";
    for (int id : g.knownPhrases) o << "phrase " << id << "\n";
    for (int sd : g.sidesRevealed) o << "side " << sd << "\n";
    for (int sd : g.sidesBurned) o << "sideburned " << sd << "\n";
    for (size_t i = 0; i < g.towerFloor.size() || i < g.towerKey.size(); ++i)   // v5: Act IV towers
        o << "tower " << i << " " << g.floorsCleared(static_cast<int>(i)) << " " << g.keyOf(static_cast<int>(i)) << "\n";
    for (size_t i = 0; i < g.net.house.size(); ++i)   // v7: HOUSE POWER
        if (g.net.house[i] >= 0) o << "house " << i << " " << g.net.house[i] << "\n";
    for (size_t i = 0; i < g.siteState.size(); ++i)   // v8: CHAIRMAN IRON
        if (g.siteState[i] > 0) o << "site " << i << " " << g.siteState[i] << "\n";
    for (auto& [id, name] : g.vaultHeld) o << "vheld " << id << " " << esc(name) << "\n";
    for (auto& [id, r] : g.fortress) {   // v6: FORTRESS floor records - guesses only, never marks
        o << "fortress " << id << " " << r.rekeys << " " << (r.survey ? 1 : 0) << "\n";
        for (auto& row : r.rows) o << "frow " << id << " " << row.stage << " " << row.guess << "\n";
        for (auto& pk : r.peeks) o << "fpeek " << id << " " << pk.first << " " << pk.second << "\n";
    }
    for (auto& j : g.journal) o << "journal " << esc(j) << "\n";
    for (auto& l : g.stats.log) o << "log " << esc(l) << "\n";
    o << "end\n";

    std::string path = savePath();
    std::string tmp = saveTempPath();
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (err) *err = "cannot write " + tmp; return false; }
        f << o.str();
        if (!f) { if (err) *err = "write failed"; return false; }
    }
    // rename replaces the old save in one step (POSIX rename; MoveFileExW with REPLACE_EXISTING
    // under MSVC). It can fail for a moment while something else holds the file - a virus
    // scanner, a backup tool - so retry briefly. Never delete the old save to make room: if the
    // move still fails, the old save stays loadable and the new one waits in the temp file,
    // which loadGame falls back to.
    std::error_code ec;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(40 * attempt));
        fs::rename(tmp, path, ec);
        if (!ec) return true;
    }
    if (err) *err = "could not replace the save (" + ec.message() + "); the previous save is untouched";
    return false;
}

namespace {
bool loadFrom(const std::string& file, GameState& g, std::string* err);
}

// A temp file left behind is either a finished write whose move into place failed (the newest
// save there is) or a write cut short (no "end" line, so it fails to load). Try the newer file
// first; whichever loads, the other is the fallback.
bool loadGame(GameState& g, std::string* err) {
    std::error_code ec;
    const std::string mainF = savePath(), tmpF = saveTempPath();
    bool tmpFirst = false;
    if (fs::exists(tmpF, ec)) {
        if (!fs::exists(mainF, ec)) tmpFirst = true;
        else {
            std::error_code e1, e2;
            auto tm = fs::last_write_time(tmpF, e1), mm = fs::last_write_time(mainF, e2);
            tmpFirst = !e1 && !e2 && tm >= mm;
        }
    }
    const std::string order[2] = {tmpFirst ? tmpF : mainF, tmpFirst ? mainF : tmpF};
    std::string firstErr;
    for (int i = 0; i < 2; ++i) {
        std::string e;
        if (loadFrom(order[i], g, &e)) {
            if (order[i] == tmpF) fs::rename(tmpF, mainF, ec);   // promote it; harmless if this fails too
            return true;
        }
        if (order[i] == mainF || firstErr.empty()) firstErr = e;
    }
    if (err) *err = firstErr;
    return false;
}

namespace {
bool loadFrom(const std::string& file, GameState& g, std::string* err) {
    std::ifstream f(file, std::ios::binary);
    if (!f) { if (err) *err = "no save file"; return false; }
    GameState n;
    std::string line;
    bool header = false, end = false;
    int version = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string k;
        is >> k;
        if (k == "GHOSTNET") { int v = 0; is >> v; header = v >= 1 && v <= kVersion; version = v; }
        else if (k == "seed") is >> n.worldSeed;
        else if (k == "credits") { long long v = 0; is >> v; n.credits = clampi(v, 0, 100000000); }
        else if (k == "map") { long long a = 0, b = 3; is >> a >> b; n.mapIndex = clampi(a, 0, 1000000); n.mapRunsLeft = clampi(b, 1, bal::RUNS_PER_MAP); }
        else if (k == "story") { long long v = 0; is >> v; n.story = clampi(v, 0, storyCount()); }
        else if (k == "act") {
            long long a = 1; int d = 0, r = 0;
            is >> a >> d >> r;
            n.act = clampi(a, 1, kMaxAct);
            n.deepDone = d != 0;
            n.retired = r != 0;
        } else if (k == "unlocked") { int v = 0; is >> v; n.netUnlocked = v != 0; }
        else if (k == "net") {
            long long a = 3, b = -1, c = 0, d = 1, e = 0;
            is >> a >> b >> c >> d >> e;
            n.net.slots = clampi(a, bal::START_SLOTS, bal::MAX_SLOTS);
            n.net.decoyId = static_cast<int>(b);
            n.net.scoutCharges = clampi(c, 0, bal::SCOUT_MAX_CHARGES);
            n.net.nextId = clampi(d, 1, 1000000000);
            n.net.burnedCount = clampi(e, 0, 1000000);
        } else if (k == "sound") { int v = 1; is >> v; n.sound = v != 0; }
        else if (k == "runcounter") is >> n.runCounter;
        else if (k == "deck") {
            long long a = 0, b = 0, c = 0, d = 0;
            is >> a >> b >> c >> d;
            n.deck.breaker = clampi(a, 0, bal::DECK_MAX);
            n.deck.uplink = clampi(b, 0, bal::DECK_MAX);
            n.deck.shroud = clampi(c, 0, bal::DECK_MAX);
            n.deck.lens = clampi(d, 0, bal::LENS_MAX);
        } else if (k == "stats") {
            Stats& s = n.stats;
            is >> s.runs >> s.jobs >> s.robberies >> s.traceouts >> s.sacrifices >> s.captures >> s.walkaways >> s.unpaidBills >>
                s.earned >> s.billsPaid >> s.finesPaid >> s.repairsPaid;
            if (!(is >> s.deepAttempts)) s.deepAttempts = 0;
        } else if (k == "m") {
            Machine m;
            std::string name, origin;
            int job = 0, cap = 0, home = 0, act = 0;
            long long id = 0, tier = 1, cond = 100, noise = 1, price = 0;
            is >> id >> name >> job >> tier >> cap >> home >> act >> cond >> noise >> price >> origin;
            if (!is && !is.eof()) continue;
            int trait = 0;
            if (!(is >> trait)) trait = 0;   // v2 saves: no traits
            m.trait = static_cast<Trait>(clampi(trait, 0, kTraitCount - 1));
            long long bay = -1;
            int role = 1, bayOn = 1;
            if (!(is >> bay >> role >> bayOn)) { bay = -1; role = 1; bayOn = 1; }   // v3 saves: no subnets
            m.bay = static_cast<int>(std::max<long long>(-1, std::min<long long>(1000000000, bay)));
            m.role = static_cast<BayRole>(clampi(role, 0, kRoleCount - 1));
            m.bayOn = bayOn != 0;
            int grade = 0;
            if (!(is >> grade)) grade = 0;   // v7 and older: no chairman iron
            m.grade = grade == 1 ? Grade::Accord : grade == 2 ? Grade::Signature : Grade::None;
            m.id = clampi(id, 0, 1000000000);
            m.name = unesc(name).substr(0, 24);
            m.job = static_cast<Job>(clampi(job, 0, kJobCount - 1));
            m.tier = m.chairIron() ? kMaxTier : clampi(tier, 1, 4);   // tier 5 iron is chairman or signature iron, nothing else
            m.captured = cap != 0;
            m.home = home != 0;
            m.active = act != 0;
            m.condition = m.home ? 100 : clampi(cond, 0, 100);
            m.noise = clampi(noise, 1, bal::MAX_NOISE);
            m.price = clampi(price, 0, 100000);
            m.origin = unesc(origin).substr(0, 120);
            if (m.signature()) m.job = m.job == Job::Vault || m.job == Job::Proxy ? m.job : Job::Gateway;   // a signature job is one of three
            if (m.chairIron()) { m.noise = 1; m.trait = Trait::None; m.captured = true; m.home = false; }
            if (m.dark()) m.active = false;
            if (m.home) m.active = true;
            n.net.machines.push_back(m);
            n.net.nextId = std::max(n.net.nextId, m.id + 1);
        } else if (k == "programs") {
            for (auto& p : n.programs) {
                int v = 0;
                if (!(is >> v)) break;
                p = static_cast<Program>(clampi(v, 0, kProgramCount - 1));
            }
        } else if (k == "wall") {
            Memorial w;
            std::string name, where, origin;
            int job = 1;
            long long tier = 1, run = 0;
            if (!(is >> name >> job >> tier >> run >> where >> origin)) continue;
            w.name = unesc(name).substr(0, 24);
            w.job = static_cast<Job>(clampi(job, 0, kJobCount - 1));
            w.tier = clampi(tier, 1, kMaxTier);
            w.run = clampi(run, 0, 1000000);
            w.where = unesc(where).substr(0, 60);
            w.origin = unesc(origin).substr(0, 120);
            if (n.wall.size() < 200) n.wall.push_back(w);
        } else if (k == "burned") { int v; if (is >> v) n.burned.insert(v); }
        else if (k == "report") { int a, b; if (is >> a >> b) n.reports[a] = clampi(b, 1, 3); }
        else if (k == "phrase") { int v; if (is >> v) n.knownPhrases.insert(v); }
        else if (k == "side") { int v; if (is >> v && v >= 0) n.sidesRevealed.push_back(v); }
        else if (k == "sideburned") { int v; if (is >> v) n.sidesBurned.insert(v); }
        else if (k == "tower") {
            long long i = -1, fl = 0, key = 0;
            if (!(is >> i >> fl >> key) || i < 0 || i >= kTowerCount) continue;
            size_t at = static_cast<size_t>(i);
            if (n.towerFloor.size() <= at) n.towerFloor.resize(at + 1, 0);
            if (n.towerKey.size() <= at) n.towerKey.resize(at + 1, 0);
            n.towerFloor[at] = clampi(fl, 0, kFloors);
            n.towerKey[at] = clampi(key, 0, 1000000);
        }
        else if (k == "house") {   // v7
            long long tw = -1, gw = -1;
            if (!(is >> tw >> gw) || tw < 0 || tw >= kTowerCount || gw < 0 || gw > 1000000000) continue;
            if (n.net.house.size() <= static_cast<size_t>(tw)) n.net.house.resize(static_cast<size_t>(tw) + 1, -1);
            n.net.house[static_cast<size_t>(tw)] = static_cast<int>(gw);
        }
        else if (k == "site") {   // v8
            long long tw = -1, st = 0;
            if (!(is >> tw >> st) || tw < 0 || tw >= kTowerCount || st < 0 || st > 3) continue;
            if (n.siteState.size() < static_cast<size_t>(kTowerCount)) n.siteState.resize(kTowerCount, 0);
            n.siteState[static_cast<size_t>(tw)] = static_cast<int>(st);
        } else if (k == "vheld") {   // v8
            long long id = -1;
            std::string name;
            if (!(is >> id >> name) || towerOfId(static_cast<int>(id)) < 0) continue;
            n.vaultHeld[static_cast<int>(id)] = unesc(name).substr(0, 40);
        }
        else if (k == "fortress") {
            long long id = -1, rekeys = 0;
            int survey = 0;
            if (!(is >> id >> rekeys >> survey) || id < 0 || id > 100000 || !fortressFloorId(static_cast<int>(id))) continue;
            FortressRecord& r = n.fortress[static_cast<int>(id)];
            r.rekeys = clampi(rekeys, 0, 1000000);
            r.survey = survey != 0;
        } else if (k == "frow") {
            long long id = -1, stage = -1;
            std::string guess;
            if (!(is >> id >> stage >> guess) || id < 0 || id > 100000 || !fortressFloorId(static_cast<int>(id))) continue;
            if ((stage != 0 && stage != 1) || !digits4(guess, stage == 0)) continue;
            FortressRecord& r = n.fortress[static_cast<int>(id)];
            if (static_cast<int>(r.rows.size()) < bal::FORTRESS_ROWS_MAX) r.rows.push_back({static_cast<int>(stage), guess});
        } else if (k == "fpeek") {
            long long id = -1, stage = -1, box = -1;
            if (!(is >> id >> stage >> box) || id < 0 || id > 100000 || !fortressFloorId(static_cast<int>(id))) continue;
            if ((stage != 0 && stage != 1) || box < 0 || box > 3) continue;
            FortressRecord& r = n.fortress[static_cast<int>(id)];
            std::pair<int, int> pk{static_cast<int>(stage), static_cast<int>(box)};
            if (std::find(r.peeks.begin(), r.peeks.end(), pk) == r.peeks.end()) r.peeks.push_back(pk);
        }
        else if (k == "journal") { std::string t; is >> t; n.journal.push_back(unesc(t)); }
        else if (k == "log") { std::string t; is >> t; n.stats.log.push_back(unesc(t)); }
        else if (k == "end") { end = true; break; }
    }
    if (!header || !end) { if (err) *err = "save file is damaged or from another version"; return false; }
    // Integrity: the home server comes first, the active count fits the rack, the decoy is legal.
    std::stable_partition(n.net.machines.begin(), n.net.machines.end(), [](const Machine& m) { return m.home; });
    // Subnet integrity: members point at a real gateway in the main rack, never more than a full bay.
    std::map<int, int> filled;
    for (auto& m : n.net.machines) {
        if (m.bay < 0) continue;
        const Machine* gw = n.net.find(m.bay);
        bool ok = gw && gw->job == Job::Gateway && gw->bay < 0 && !m.home && m.job != Job::Gateway && m.active &&
                  filled[m.bay] < n.net.baySlots(*gw);
        if (ok) ++filled[m.bay];
        else { m.bay = -1; m.active = false; }
    }
    int active = 0;
    for (auto& m : n.net.machines) {
        if (m.active && m.bay < 0) {
            if (active >= n.net.slots && !m.home) m.active = false;
            else ++active;
        }
    }
    if (!n.net.decoy()) n.net.decoyId = -1;
    if (n.stats.captures > 0 && n.act < 2) n.act = 2;      // 0.1.x saves: derive the act from history
    if (n.stats.sacrifices > 0 && n.act < 3) n.act = 3;
    if (n.netUnlocked && !n.net.home()) n.netUnlocked = false;
    if (!n.deepDone) { n.towerFloor.clear(); n.towerKey.clear(); n.fortress.clear(); n.siteState.clear(); n.vaultHeld.clear(); if (n.act > 3) n.act = 3; }   // the Ledger opens after the deep job
    for (auto it = n.vaultHeld.begin(); it != n.vaultHeld.end();) {   // a held objective belongs to a floor not yet taken
        int tower = towerOfId(it->first);
        bool ok = tower >= 0 && it->first - floorId(tower, 0) >= n.floorsCleared(tower);
        it = ok ? std::next(it) : n.vaultHeld.erase(it);
    }
    if (n.act >= 4) {   // v7 and older: the Ledger grew a tower (0.9) - it starts unclimbed
        if (n.towerFloor.size() < static_cast<size_t>(kTowerCount)) n.towerFloor.resize(kTowerCount, 0);
        if (n.towerKey.size() < static_cast<size_t>(kTowerCount)) n.towerKey.resize(kTowerCount, 0);
        if (version > 0 && version < 8)
            n.journal.push_back("THE LEDGER: a third tower - Halden Grid (LOADSHED) - is on your board. Every Boardroom now keeps a maintenance log.");
        if (version > 0 && version < 9 && innerOpen(n))
            n.journal.push_back("THE INNER CHAIRS: Aurex (LEDGERLOCK), Sable Wren (COUNTERTRACE) and the HELIOS PROVING GROUND are on your board.");
        for (int k = 0; k < kOuterTowers; ++k)   // only a cage has an inner door
            if (n.siteOf(k) == 3) n.siteState[static_cast<size_t>(k)] = 1;
        for (int k = 0; k < kTowerCount; ++k)   // a tower you own has handed over its maintenance log
            if (n.floorsCleared(k) >= kFloors && n.siteOf(k) == 0) {
                if (n.siteState.size() < static_cast<size_t>(kTowerCount)) n.siteState.resize(kTowerCount, 0);
                n.siteState[static_cast<size_t>(k)] = 1;
                n.journal.push_back(std::string("CHAIRMAN IRON: ") + towerCorp(k) + "'s maintenance log names " +
                                    (innerTower(k) ? "HELIOS CAGE " : "HELIOS SERVICE SITE ") + siteName(k) + ". It's on your board.");
            }
    }
    for (auto it = n.fortress.begin(); it != n.fortress.end();) {   // a taken floor keeps no record; an empty record is no record
        int tower = towerOfId(it->first);
        bool taken = it->first - floorId(tower, 0) < n.floorsCleared(tower);
        it = taken || it->second.blank() ? n.fortress.erase(it) : std::next(it);
    }
    tidyHousePower(n);   // a tower you no longer own (or a gateway that's gone) powers nothing
    g = n;
    return true;
}
}  // namespace
