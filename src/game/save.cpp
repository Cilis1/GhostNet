#include "save.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "balance.h"
#include "platform.h"

namespace fs = std::filesystem;

namespace {
constexpr int kVersion = 2;   // v1 saves (0.1.x) still load; new fields default

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

std::string savePath() { return (fs::path(plat::saveDir()) / "ghostnet_save.txt").string(); }
bool saveExists() { std::error_code ec; return fs::exists(savePath(), ec); }

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
          << m.home << " " << m.active << " " << m.condition << " " << m.noise << " " << m.price << " " << esc(m.origin) << "\n";
    for (int id : g.burned) o << "burned " << id << "\n";
    for (auto& kv : g.reports) o << "report " << kv.first << " " << kv.second << "\n";
    for (int id : g.knownPhrases) o << "phrase " << id << "\n";
    for (int sd : g.sidesRevealed) o << "side " << sd << "\n";
    for (int sd : g.sidesBurned) o << "sideburned " << sd << "\n";
    for (auto& j : g.journal) o << "journal " << esc(j) << "\n";
    for (auto& l : g.stats.log) o << "log " << esc(l) << "\n";
    o << "end\n";

    std::string path = savePath();
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (err) *err = "cannot write " + tmp; return false; }
        f << o.str();
        if (!f) { if (err) *err = "write failed"; return false; }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {   // some platforms refuse to rename over an existing file
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) { if (err) *err = ec.message(); return false; }
    }
    return true;
}

bool loadGame(GameState& g, std::string* err) {
    std::ifstream f(savePath(), std::ios::binary);
    if (!f) { if (err) *err = "no save file"; return false; }
    GameState n;
    std::string line;
    bool header = false, end = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string k;
        is >> k;
        if (k == "GHOSTNET") { int v = 0; is >> v; header = v >= 1 && v <= kVersion; }
        else if (k == "seed") is >> n.worldSeed;
        else if (k == "credits") { long long v = 0; is >> v; n.credits = clampi(v, 0, 100000000); }
        else if (k == "map") { long long a = 0, b = 3; is >> a >> b; n.mapIndex = clampi(a, 0, 1000000); n.mapRunsLeft = clampi(b, 1, bal::RUNS_PER_MAP); }
        else if (k == "story") { long long v = 0; is >> v; n.story = clampi(v, 0, storyCount()); }
        else if (k == "act") {
            long long a = 1; int d = 0, r = 0;
            is >> a >> d >> r;
            n.act = clampi(a, 1, 3);
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
            m.id = clampi(id, 0, 1000000000);
            m.name = unesc(name).substr(0, 24);
            m.job = static_cast<Job>(clampi(job, 0, 3));
            m.tier = clampi(tier, 1, 3);
            m.captured = cap != 0;
            m.home = home != 0;
            m.active = act != 0;
            m.condition = m.home ? 100 : clampi(cond, 0, 100);
            m.noise = clampi(noise, 1, bal::MAX_NOISE);
            m.price = clampi(price, 0, 100000);
            m.origin = unesc(origin).substr(0, 120);
            if (m.dark()) m.active = false;
            if (m.home) m.active = true;
            n.net.machines.push_back(m);
            n.net.nextId = std::max(n.net.nextId, m.id + 1);
        } else if (k == "burned") { int v; if (is >> v) n.burned.insert(v); }
        else if (k == "report") { int a, b; if (is >> a >> b) n.reports[a] = clampi(b, 1, 3); }
        else if (k == "phrase") { int v; if (is >> v) n.knownPhrases.insert(v); }
        else if (k == "side") { int v; if (is >> v && v >= 0) n.sidesRevealed.push_back(v); }
        else if (k == "sideburned") { int v; if (is >> v) n.sidesBurned.insert(v); }
        else if (k == "journal") { std::string t; is >> t; n.journal.push_back(unesc(t)); }
        else if (k == "log") { std::string t; is >> t; n.stats.log.push_back(unesc(t)); }
        else if (k == "end") { end = true; break; }
    }
    if (!header || !end) { if (err) *err = "save file is damaged or from another version"; return false; }
    // Integrity: the home server comes first, the active count fits the rack, the decoy is legal.
    std::stable_partition(n.net.machines.begin(), n.net.machines.end(), [](const Machine& m) { return m.home; });
    int active = 0;
    for (auto& m : n.net.machines) {
        if (m.active) {
            if (active >= n.net.slots && !m.home) m.active = false;
            else ++active;
        }
    }
    if (!n.net.decoy()) n.net.decoyId = -1;
    if (n.stats.captures > 0 && n.act < 2) n.act = 2;      // 0.1.x saves: derive the act from history
    if (n.stats.sacrifices > 0 && n.act < 3) n.act = 3;
    if (n.netUnlocked && !n.net.home()) n.netUnlocked = false;
    g = n;
    return true;
}
