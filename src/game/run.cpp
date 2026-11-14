#include "run.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>

#include "balance.h"
#include "locks.h"
#include "netscreen.h"
#include "platform.h"
#include "rng.h"
#include "rules.h"
#include "ui.h"
#include "world.h"

namespace {

enum class End { None, Disconnect, TraceOut, Sacrifice, Steal };

struct Run {
    GameState& g;
    Target t;
    Lock lock;
    double trace = 0;
    int secs = 0;
    bool doorOpen = false;
    bool viaPhrase = false;
    int haul = 0;
    bool honeypot = false;
    bool jobDone = false;
    bool anchoring = false;
    int emergLeft = 0;
    std::vector<int> billIds;      // decided when the run begins
    int startRelay = 0, startProxy = 0;
    End end = End::None;
    std::string sacrificed;
    std::string stolen;
    std::vector<std::string> notes;

    Run(GameState& gs, const Target& tg) : g(gs), t(tg) {}

    double rateNow(double mult = 1.0) const {
        double r = effectiveTraceRate(g, t) * mult;
        if (t.watchdog) r *= 1.0 + bal::WATCHDOG_RAMP * secs / 60.0;
        return r;
    }
    int secondsLeft() const {
        double r = rateNow(anchoring ? bal::ANCHOR_TRACE_MULT : 1.0);   // at rest (downloads run x1.25)
        return r <= 0 ? 999 : static_cast<int>((100.0 - trace) / r);
    }
};

void traced(Run& r);

// Advance connection time second by second. Returns true if the run ended.
bool advance(Run& r, int seconds, double mult = 1.0) {
    for (int i = 0; i < seconds; ++i) {
        r.trace += r.rateNow(mult);
        r.secs += 1;
        if (r.trace >= 100.0) { r.trace = 100.0; traced(r); return true; }
    }
    return false;
}
bool spike(Run& r, double amt) {
    r.trace += amt;
    if (r.trace >= 100.0) { r.trace = 100.0; traced(r); return true; }
    return false;
}

void traced(Run& r) {
    GameState& g = r.g;
    std::cout << "\n";
    if (const Machine* b = g.net.decoy()) {
        std::string name = b->name;
        int id = b->id;
        plat::soundBurn();
        std::cout << "  " << col::bgRed() << " TRACE COMPLETE " << col::reset() << "\n";
        plat::sleepMs(500);
        ui::typeOut(std::string("  The ticket resolves to the decoy. ") + col::red() + col::bold() + name + col::reset() +
                        " lights up on HELIOS's board and goes silent.", 18);
        ui::typeOut("  HELIOS traced " + name + " instead of you.", 18);
        plat::sleepMs(500);
        auto& v = g.net.machines;
        v.erase(std::remove_if(v.begin(), v.end(), [id](const Machine& m) { return m.id == id; }), v.end());
        g.net.decoyId = -1;
        g.net.burnedCount += 1;
        r.sacrificed = name;
        r.end = End::Sacrifice;
    } else {
        plat::soundAlert();
        std::cout << "  " << col::bgRed() << " TRACED " << col::reset() << col::red()
                  << "  HELIOS closes the ticket. Your accounts are seized; tonight's haul is gone." << col::reset() << "\n";
        r.end = End::TraceOut;
    }
}

void hud(const Run& r) {
    const Network& n = r.g.net;
    std::cout << "\n  " << col::bold() << r.t.host << col::reset() << "  TRACE " << ui::traceBar(r.trace) << " "
              << static_cast<int>(r.trace) << "%  " << col::dim() << "~" << r.secondsLeft() << "s left  t=" << r.secs
              << "s" << col::reset() << "\n";
    if (r.g.netUnlocked) {
        std::cout << "  " << col::dim() << "net: " << n.activeCount() << " running | DL +" << n.relayBonus() << "% | trace -"
                  << n.proxyBonus() << "% | emergency " << r.emergLeft << " | scout " << n.usableScoutCharges() << " | decoy: "
                  << (n.decoy() ? n.decoy()->name : std::string("none")) << col::reset() << "\n";
    }
}

void listFiles(const Run& r) {
    std::cout << "  " << col::dim() << "#   file                                   size    time   value" << col::reset() << "\n";
    for (size_t i = 0; i < r.t.files.size(); ++i) {
        const File& f = r.t.files[i];
        std::string path = f.dir + "/" + f.name;
        std::cout << "  " << ui::pad(std::to_string(i + 1) + ".", 4);
        if (f.taken) std::cout << col::dim();
        std::cout << ui::pad(path, 38) << " ";
        if (f.readable()) {
            std::cout << col::cyan() << "[text - read with 'cat " << (i + 1) << "']" << (f.read ? " (read)" : "") << col::reset();
        } else {
            std::cout << ui::pad(std::to_string(f.size) + "MB", 7) << ui::pad("~" + std::to_string(secondsToDownload(r.g, f.size)) + "s", 7);
            if (f.kind == FileKind::Objective) std::cout << col::magenta() << "OBJECTIVE" << col::reset();
            else std::cout << col::yellow() << f.credits << "cr" << col::reset();
            if (f.taken) std::cout << col::dim() << "  [taken]";
        }
        std::cout << col::reset() << "\n";
    }
}

int indexArg(const Run& r, const std::vector<std::string>& w) {
    if (w.size() < 2 || !ui::isNumber(w[1])) return -1;
    int i = std::stoi(w[1]) - 1;
    if (i < 0 || i >= static_cast<int>(r.t.files.size())) return -1;
    return i;
}

// Real-time progress for downloads and anchors. Each game second plays out on screen.
// Returns: 0 completed, 1 aborted by keypress, 2 run ended (traced).
int progress(Run& r, const std::string& label, int total, double mult, bool completeBeforeDanger, const char* abortHint) {
    bool live = plat::interactive() && !plat::fast;
    if (live) plat::beginRealtime();
    int done = 0;
    int result = 0;
    for (;;) {
        if (completeBeforeDanger) {
            ++done;
            if (done >= total) { result = 0; break; }
            if (advance(r, 1, mult)) { result = 2; break; }
        } else {
            if (advance(r, 1, mult)) { result = 2; break; }
            ++done;
            if (done >= total) { result = 0; break; }
        }
        if (live) {
            std::cout << "\r  " << label << " " << ui::bar(static_cast<double>(done) / total, 16, col::cyan()) << " "
                      << (100 * done / total) << "%  TRACE " << ui::traceBar(r.trace, 12) << " " << static_cast<int>(r.trace)
                      << "%  " << col::dim() << abortHint << col::reset() << "   " << std::flush;
            for (int k = 0; k < 4; ++k) {
                plat::sleepMs(70);
                if (plat::keyPressed()) { result = 1; break; }
            }
            if (result == 1) break;
        }
    }
    if (live) { plat::endRealtime(); std::cout << "\r" << std::string(110, ' ') << "\r"; }
    std::cout << "  " << label << " " << ui::bar(static_cast<double>(done) / total, 16, col::cyan()) << " "
              << (100 * std::min(done, total) / total) << "%  TRACE " << static_cast<int>(r.trace) << "%\n";
    return result;
}

void doRead(Run& r, int i) {
    File& f = r.t.files[static_cast<size_t>(i)];
    if (!f.readable()) {
        std::cout << "  Binary data. Download it with 'get " << (i + 1) << "'.\n";
        return;
    }
    if (advance(r, std::max(1, bal::COST_CAT - r.g.deck.lens))) return;
    f.read = true;
    std::cout << col::cyan();
    std::istringstream is(f.text);
    std::string line;
    while (std::getline(is, line)) std::cout << "   | " << line << "\n";
    std::cout << col::reset();
    if (f.phraseFor >= 0 && !r.g.knownPhrases.count(f.phraseFor)) {
        r.g.knownPhrases.insert(f.phraseFor);
        std::string host;
        size_t a = f.text.find("door at ");
        if (a != std::string::npos) host = f.text.substr(a + 8, f.text.find(' ', a + 8) - (a + 8));
        size_t q1 = f.text.find('\''), q2 = f.text.find('\'', q1 + 1);
        std::string ph = (q1 != std::string::npos && q2 != std::string::npos) ? f.text.substr(q1 + 1, q2 - q1 - 1) : "?";
        r.g.journal.push_back("Passphrase for " + host + ": " + ph);
        std::cout << "  " << col::green() << "Noted in your journal: passphrase for " << host << "." << col::reset() << "\n";
    }
    if (f.revealsSide >= 0) {
        int s = f.revealsSide;
        bool known = std::find(r.g.sidesRevealed.begin(), r.g.sidesRevealed.end(), s) != r.g.sidesRevealed.end();
        if (!known && !r.g.sidesBurned.count(s)) {
            r.g.sidesRevealed.push_back(s);
            r.g.journal.push_back("Side route discovered via " + r.t.host);
            std::cout << "  " << col::magenta() << col::bold() << "A hidden route. It's on your job board now, and it stays there." << col::reset() << "\n";
        }
    }
}

void doGet(Run& r, int i) {
    File& f = r.t.files[static_cast<size_t>(i)];
    if (!f.downloadable()) { std::cout << "  That's text. Read it with 'cat " << (i + 1) << "'.\n"; return; }
    if (f.taken) { std::cout << "  Already taken.\n"; return; }
    int secs = secondsToDownload(r.g, f.size);
    // Downloading is loud: the trace climbs faster while bytes are moving.
    int res = progress(r, "DL " + ui::pad(f.name, 18), secs, bal::DOWNLOAD_TRACE_MULT, false, "(any key: abort)");
    if (res == 2) return;
    if (res == 1) { std::cout << "  Download aborted. The time is gone.\n"; return; }
    f.taken = true;
    if (f.kind == FileKind::Honeypot) {
        r.honeypot = true;
        plat::soundAlert();
        std::cout << "  " << col::bgRed() << " BAIT FILE " << col::reset() << col::red()
                  << " It was a honeypot. ICE floods the channel (+" << static_cast<int>(bal::HONEYPOT_SPIKE) << "% trace)." << col::reset() << "\n";
        spike(r, bal::HONEYPOT_SPIKE);
        return;
    }
    if (f.kind == FileKind::Objective) {
        std::cout << "  " << col::magenta() << "Objective secured: " << f.name << col::reset() << "\n";
        bool all = true;
        for (auto& x : r.t.files) if (x.kind == FileKind::Objective && !x.taken) all = false;
        if (all && !r.jobDone) {
            r.jobDone = true;
            plat::soundOnline();
            std::cout << "  " << col::green() << col::bold() << "CONTRACT COMPLETE." << col::reset()
                      << " Disconnect to keep it - if the trace fills first, it goes with your seized accounts.\n";
        }
        return;
    }
    r.haul += f.credits;
    std::cout << "  " << col::yellow() << "+" << f.credits << "cr" << col::reset() << " (haul so far: " << r.haul << "cr)\n";
}

void doSteal(Run& r) {
    GameState& g = r.g;
    if (!g.netUnlocked) { std::cout << "  You have nowhere to put a server yet.\n"; return; }
    if (!r.t.stealable) {
        if (r.t.kind == Kind::Story || r.t.kind == Kind::Side) std::cout << "  This place is load-bearing. Its hardware can't be taken.\n";
        else if (r.t.kind == Kind::Shrine) std::cout << "  Shrines can never be stolen from.\n";
        else std::cout << "  Nothing here is detachable.\n";
        return;
    }
    if (g.net.externalCount() >= bal::MAX_EXTERNAL) { std::cout << "  Your archive holds 10 external machines. Scrap one first (between runs).\n"; return; }

    HardwareSpec hw = hardwareFor(r.t.kind);
    int cond, noise;
    captureQuote(r.t, r.honeypot, cond, noise);
    Machine probe;
    probe.tier = hw.tier;
    probe.noise = noise;
    int up = upkeepOf(probe);
    int anchor = anchorSeconds(r.t, r.viaPhrase);
    int leftCr = 0, leftIntel = 0;
    for (auto& f : r.t.files) {
        if ((f.kind == FileKind::Paydata) && !f.taken) leftCr += f.credits;
        if (f.readable() && !f.read) ++leftIntel;
    }
    ui::header("HARDWARE QUOTE");
    std::cout << "  Becomes:     " << col::bold() << jobName(hw.def) << " T" << hw.tier << col::reset() << "  (or " << jobName(hw.alt) << " T" << hw.tier << ")\n";
    std::cout << "  Condition:   " << ui::conditionBar(cond) << " " << cond << "%"
              << (r.honeypot ? std::string(col::red()) + "  (still reeling from the bait file)" + col::reset() : "") << "\n";
    std::cout << "  Signal:      " << ui::noiseStr(noise) << "  -> upkeep ~" << up << "cr per run\n";
    std::cout << "  Hold time:   " << anchor << "s" << (r.viaPhrase ? " (shorter: you were invited in)" : "") << ", trace x"
              << bal::ANCHOR_TRACE_MULT << " while you hold the line. You have ~" << r.secondsLeft() << "s at normal rate.\n";
    std::cout << "  Giving up:   " << col::yellow() << leftCr << "cr" << col::reset() << " unclaimed + " << leftIntel << " unread file(s). The run ends when it's done.\n";
    if (r.t.trail) {
        bool known = std::find(g.sidesRevealed.begin(), g.sidesRevealed.end(), r.t.trailSide) != g.sidesRevealed.end();
        if (!known) std::cout << "  " << col::red() << "Warning:" << col::reset() << " this place carries the trail to a hidden route. Stealing burns that trail until the next refresh.\n";
    }
    if (g.net.activeCount() >= g.net.slots) std::cout << "  Rack is full: it will arrive in cold storage.\n";
    std::string a = ui::prompt(std::string("  Type '") + jobLower(hw.def) + "' or '" + jobLower(hw.alt) + "' to commit, anything else cancels: ");
    Job chosen;
    if (!parseJob(a, chosen) || (chosen != hw.def && chosen != hw.alt)) { std::cout << "  Cancelled.\n"; return; }

    std::cout << "  Holding the line... " << col::dim() << "disconnecting now aborts the whole thing." << col::reset() << "\n";
    r.anchoring = true;
    int res = progress(r, "HOLD  ", anchor, bal::ANCHOR_TRACE_MULT, true, "(any key: disconnect)");
    r.anchoring = res == 2 && r.end != End::None;   // keep the flag for the fine if traced mid-anchor
    if (res == 2) { std::cout << "  The capture dies with the run.\n"; return; }
    if (res == 1) {
        std::cout << "  You pull out. The hardware stays exactly where it was.\n";
        r.end = End::Disconnect;
        return;
    }
    Machine m;
    m.id = g.net.nextId++;
    m.job = chosen;
    m.tier = hw.tier;
    m.captured = true;
    m.condition = cond;
    m.noise = noise;
    std::string base = r.t.host.substr(0, r.t.host.find_first_of(".-"));
    m.name = base.substr(0, 10) + "-" + std::to_string(m.id);
    m.origin = "Captured from " + r.t.name + " (board " + std::to_string(g.mapIndex + 1) + ").";
    m.active = g.net.activeCount() < g.net.slots;
    g.net.machines.push_back(m);
    plat::soundOnline();
    std::cout << "  " << col::green() << col::bold() << "CAPTURED." << col::reset() << " " << r.t.name
              << " reboots under your control and goes dark.\n";
    std::cout << "  '" << m.name << "' (" << jobName(m.job) << " T" << m.tier << ") arrives "
              << (m.active ? "in the rack, running." : "in cold storage (rack full).") << "\n";
    r.stolen = m.name;
    r.end = End::Steal;
}

void actCard(const std::string& title, const std::string& text) {
    std::cout << "\n  " << col::bold() << col::magenta() << title << col::reset() << "\n" << col::cyan();
    ui::typeOut(text, 8);
    std::cout << col::reset();
}

// How far short a failed deep job fell, in plain numbers.
void deepShortfall(const Run& r) {
    const GameState& g = r.g;
    int got = 0, total = 0, mbLeft = 0, secsLeft = 0;
    for (auto& f : r.t.files) {
        if (f.kind != FileKind::Objective) continue;
        ++total;
        if (f.taken) ++got;
        else { mbLeft += f.size; secsLeft += secondsToDownload(g, f.size); }
    }
    ui::header("THE DEEP JOB - HOW FAR SHORT");
    std::cout << "  Archives secured:   " << got << "/" << total << "  (" << mbLeft << "MB still on the server)\n";
    std::cout << "  Connection held:    " << r.secs << "s, trace " << static_cast<int>(r.trace) << "%\n";
    if (!r.doorOpen) std::cout << "  The door never opened. The maintenance phrase lives somewhere on HELIOS's own facilities.\n";
    std::cout << "  Still needed:       ~" << secsLeft << "s of downloading at your current speed ("
              << std::fixed;
    std::cout.precision(1);
    std::cout << downloadSpeed(g) << " MB/s)\n";
    std::cout.unsetf(std::ios::fixed);
    int pct = r.secs + secsLeft > 0 ? 100 * secsLeft / (r.secs + secsLeft) : 100;
    std::cout << "  You were roughly " << col::bold() << pct << "% short" << col::reset() << ".\n";
    std::cout << "  Your gear: deck uplink " << g.deck.uplink << "/" << bal::DECK_MAX << ", shroud " << g.deck.shroud << "/" << bal::DECK_MAX
              << "; network relay +" << r.startRelay << "% of " << bal::RELAY_CAP << ", proxy -" << r.startProxy
              << "% of " << bal::PROXY_CAP << ".\n";
    std::cout << col::dim() << "  No deck alone finishes this. It was tuned that way. Owned machines close the gap." << col::reset() << "\n";
}

void settle(Run& r) {
    GameState& g = r.g;
    ui::header("THE RECKONING");
    std::vector<std::string> lines;
    const bool traceOut = r.end == End::TraceOut;
    const bool sacrifice = r.end == End::Sacrifice;
    // A trace-out loses the night: the haul and the contract go with the seized accounts.
    const bool jobCounts = r.jobDone && !traceOut;
    // [doc] the bill comes due after a completion, a repeat clear, a trace-out, a decoy loss or a capture.
    const bool billable = jobCounts || r.haul > 0 || traceOut || sacrifice || r.end == End::Steal;

    // Haul.
    if (r.haul > 0) {
        if (traceOut) {
            lines.push_back(std::string(col::red()) + "Accounts seized: tonight's " + std::to_string(r.haul) + "cr haul is gone." + col::reset());
        } else {
            g.credits += r.haul;
            g.stats.earned += r.haul;
            lines.push_back("Haul: +" + std::to_string(r.haul) + "cr");
        }
    }
    if (sacrifice)
        lines.push_back(std::string(col::red()) + "Escape bought with hardware: " + r.sacrificed + " is gone for good (machines burned, lifetime: " +
                        std::to_string(g.net.burnedCount) + ")." + col::reset() + " You keep the night.");
    if (traceOut && r.jobDone) lines.push_back(std::string(col::red()) + "The contract files went with the seized accounts. The job is not done." + col::reset());
    // Fine.
    if (traceOut || sacrifice) {
        int fine = baseFine(g, r.t, r.honeypot, r.anchoring);
        if (sacrifice) fine = (fine + 1) / 2;   // halved, rounded up
        fine = std::min(fine, g.credits);
        g.credits -= fine;
        g.stats.finesPaid += fine;
        lines.push_back("Fine: -" + std::to_string(fine) + "cr" + (sacrifice ? " (halved: " + r.sacrificed + " took the ticket)" : ""));
    }
    // Contract.
    if (jobCounts && r.t.kind == Kind::Story) {
        g.credits += r.t.reward;
        g.stats.earned += r.t.reward;
        if (!r.t.deep) g.stats.jobs += 1;
        lines.push_back("Contract '" + (r.t.deep ? std::string("THE DEEP JOB") : storyInfo(r.t.storyIndex).title) + "': +" +
                        std::to_string(r.t.reward) + "cr" + (r.t.infoReward.empty() ? "" : " and information"));
    }
    // Upkeep, fixed when the run began.
    int bill = 0;
    if (billable)
        for (int id : r.billIds)
            if (const Machine* m = g.net.find(id)) bill += upkeepOf(*m);
    if (bill > 0) {
        if (g.credits >= bill) {
            g.credits -= bill;
            g.stats.billsPaid += bill;
            lines.push_back("The bill: -" + std::to_string(bill) + "cr");
        } else {
            g.stats.unpaidBills += 1;
            for (auto& m : g.net.machines) if (!m.home) m.active = false;
            g.net.decoyId = -1;
            lines.push_back(std::string(col::red()) + "The bill of " + std::to_string(bill) +
                            "cr went UNPAID. Nothing was taken. Every machine except N0 has gone dark, and the decoy is disarmed." + col::reset());
            lines.push_back("No debt. Earn credits, then switch them back on.");
        }
    }
    // Wear, after the bill.
    std::vector<std::string> darkNow;
    int worn = 0;
    if (billable) {
        for (int id : r.billIds) {
            Machine* m = g.net.find(id);
            if (!m || m->home) continue;
            ++worn;
            int w = m->captured ? bal::WEAR_NORMAL_CAPTURED : bal::WEAR_NORMAL_BOUGHT;
            if (traceOut) w += m->captured ? bal::WEAR_TRACED_EXTRA_CAPTURED : bal::WEAR_TRACED_EXTRA_BOUGHT;
            bool wasDark = m->dark();
            m->condition = std::max(0, m->condition - w);
            if (m->dark() && !wasDark) {
                m->active = false;
                if (g.net.decoyId == m->id) g.net.decoyId = -1;
                darkNow.push_back(m->name);
            }
        }
    }
    if (worn > 0)
        lines.push_back(std::string("Wear: ") + (traceOut ? "HEAVY (traced with no decoy)" : "normal") + " on " +
                        std::to_string(worn) + " machine(s)");
    for (auto& n : darkNow) lines.push_back(std::string(col::red()) + n + " wore out and went dark. Repair it, then switch it back on." + col::reset());

    // Records.
    g.stats.runs += 1;
    if (r.t.deep) g.stats.deepAttempts += 1;
    std::string what;
    switch (r.end) {
        case End::Steal: what = "CAPTURE (" + r.stolen + ")"; g.stats.captures += 1; break;
        case End::TraceOut: what = "TRACE-OUT"; g.stats.traceouts += 1; break;
        case End::Sacrifice: what = "ESCAPE BOUGHT WITH HARDWARE (" + r.sacrificed + ")"; g.stats.sacrifices += 1; break;
        default:
            if (r.haul > 0 || r.jobDone) { what = "CLEAR"; g.stats.robberies += 1; }
            else { what = "WALK-AWAY"; g.stats.walkaways += 1; }
    }
    if (jobCounts) what += " + CONTRACT";
    g.stats.log.push_back("Run " + std::to_string(g.stats.runs) + ": " + r.t.name + " - " + what + ", haul " +
                          std::to_string(traceOut ? 0 : r.haul) + "cr, bill " + std::to_string(bill) + "cr");
    if (g.stats.log.size() > 30) g.stats.log.erase(g.stats.log.begin());

    for (auto& l : lines) std::cout << "  - " << l << "\n";
    if (!billable) std::cout << "  - " << col::dim() << "Nothing happened worth billing. No bill, no wear." << col::reset() << "\n";
    std::cout << "  Credits now: " << col::yellow() << g.credits << "cr" << col::reset() << "\n";
    if (traceOut) std::cout << "  You survive. Poorer and wiser. The rack is still there.\n";

    // Burn the door. The annex stays open until you have what it holds.
    if (isMapLocal(r.t.id)) g.burned.insert(r.t.id);
    if (r.t.kind == Kind::Side && (r.t.sideIndex != kAnnexSide || g.knownPhrases.count(kDeepId)))
        g.sidesBurned.insert(r.t.sideIndex);

    // The deep job.
    if (r.t.deep) {
        if (jobCounts) {
            g.deepDone = true;
            std::cout << "\n" << col::cyan();
            ui::typeOut("  Three archives. Every ticket HELIOS ever opened - including yours, #48213, still OPEN.\n"
                        "  You close it yourself. There is nobody left in the city who can reopen it.", 8);
            std::cout << col::reset();
        } else {
            deepShortfall(r);
        }
    }

    // Contract consequences.
    if (jobCounts && r.t.kind == Kind::Story && !r.t.deep) {
        int idx = r.t.storyIndex;
        g.story = idx + 1;
        std::cout << "\n" << col::cyan();
        ui::typeOut(storyInfo(idx).outro, 8);
        std::cout << col::reset();
        if (!r.t.infoReward.empty()) {
            g.journal.push_back(r.t.infoReward);
            std::cout << "  " << col::green() << "Paid in information: " << r.t.infoReward << col::reset() << "\n";
        }
        if (idx == 2) {   // QUIET FLOOR: the Facilities Annex, where the deep job's phrase is kept
            if (std::find(g.sidesRevealed.begin(), g.sidesRevealed.end(), kAnnexSide) == g.sidesRevealed.end())
                g.sidesRevealed.push_back(kAnnexSide);
            std::cout << "  " << col::magenta() << "THE DEEP JOB is on your board. So is the HELIOS Facilities Annex." << col::reset() << "\n";
        }
        if (idx == 0 && !g.netUnlocked) unlockNetwork(g);
        if (g.netUnlocked) {
            g.net.refillScout();
            if (g.net.scoutCapacity() > 0) std::cout << "  Scouting charges refilled.\n";
        }
        if (g.story >= storyCount() && !g.deepDone)
            std::cout << "\n  " << col::magenta() << "No more contracts are coming. Only the deep job is left." << col::reset() << "\n";
    }

    // The acts.
    if (traceOut && g.stats.traceouts == 1 && g.act == 1)
        actCard("END OF ACT ONE - RENTING",
                "  You were traced. The haul went with the seized accounts and the fine came out of what was left.\n"
                "  Everything you had tonight was borrowed: borrowed time, borrowed bandwidth, borrowed luck.");
    if (r.end == End::Steal && g.act < 2) {
        g.act = 2;
        actCard("ACT TWO - OWNING",
                "  A machine that was HELIOS's is yours now. Nobody rents it to you. Nobody can take it back.\n"
                "  It bills you, it wears, and it is louder than anything you could buy. It is also yours.");
    }
    if (sacrifice && g.act < 3) {
        g.act = 3;
        actCard("ACT THREE - THE DEEP JOB",
                "  " + r.sacrificed + " died so you didn't have to. You understand the rack now: it is not decoration.\n"
                "  Somewhere HELIOS keeps every ticket it ever opened. You know what a network is for.");
    }

    // Board refresh cadence.
    g.mapRunsLeft -= 1;
    if (g.mapRunsLeft <= 0) {
        g.mapIndex += 1;
        g.mapRunsLeft = bal::RUNS_PER_MAP;
        g.burned.clear();
        for (auto it = g.reports.begin(); it != g.reports.end();)
            it = isMapLocal(it->first) ? g.reports.erase(it) : std::next(it);
        g.net.refillScout();
        std::cout << "\n  " << col::magenta() << "The board refreshes. Old listings are gone, and what you knew about them with it"
                  << (g.netUnlocked && g.net.scoutCapacity() ? ". Scouting charges are full." : ".") << col::reset() << "\n";
    }
    for (auto& m : g.net.machines) m.usedThisRun = false;
}

bool useEmergency(Run& r) {
    GameState& g = r.g;
    if (!g.netUnlocked) { std::cout << "  You have no network.\n"; return false; }
    if (r.lock.open || r.lock.type == LockType::None) { std::cout << "  No lock to spend it on.\n"; return false; }
    if (r.lock.extraUsed) { std::cout << "  One emergency attempt per lock. Ever.\n"; return false; }
    if (r.emergLeft <= 0) { std::cout << "  Your network holds no emergency attempts this run.\n"; return false; }
    Machine* who = nullptr;
    Machine* h = g.net.home();
    if (h && h->working() && h->job == Job::Compute && !h->usedThisRun) who = h;
    else
        for (auto& m : g.net.machines)
            if (m.working() && m.job == Job::Compute && !m.usedThisRun && (!who || m.tier > who->tier)) who = &m;
    if (!who) { std::cout << "  No compute machine is able to answer.\n"; return false; }
    r.lock.maxGuesses += 1;
    r.lock.extraUsed = true;
    r.emergLeft -= 1;
    who->usedThisRun = true;
    std::cout << "  " << col::green() << who->name << col::reset() << " spins up and holds the lock open for one more guess.\n";
    if (!who->home) {
        who->condition = std::max(0, who->condition - bal::COMPUTE_WEAR[who->tier]);
        if (who->dark()) {
            who->active = false;
            plat::soundBurn();
            std::cout << "  " << col::red() << who->name << " gave everything it had and went dark." << col::reset() << "\n";
        }
    }
    advance(r, bal::COST_EXTRA);
    return true;
}

void doorHelp(const Run& r) {
    std::cout << "  " << r.lock.rule() << "\n";
    std::cout << "  Type a guess (" << r.lock.format() << ")   phrase <word>   extra (emergency attempt)\n"
              << "  status   help   disconnect\n";
    std::cout << col::dim() << "  Costs: guess " << bal::COST_GUESS << "s, phrase " << bal::COST_PHRASE << "s, extra " << bal::COST_EXTRA << "s." << col::reset() << "\n";
}
void insideHelp() {
    std::cout << "  ls          list files (" << bal::COST_LS << "s)\n"
              << "  cat <n>     read a text file (" << bal::COST_CAT << "s, less with LENS)\n"
              << "  get <n>     download a file (time depends on size and relays; the trace runs x1.25 meanwhile)\n"
              << "  steal       quote / capture this server's hardware (if detachable)\n"
              << "  status      your network readout\n"
              << "  disconnect  leave with what you have\n";
}

}  // namespace

void playRun(GameState& g, const Target& target) {
    Run r(g, target);
    g.runCounter += 1;
    Rng rng(seedOf(g.worldSeed ^ static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()),
                   g.runCounter));
    r.lock = makeLock(target.lock, rng, g.deck.breaker);
    r.doorOpen = target.lock == LockType::None;
    r.emergLeft = g.netUnlocked ? g.net.computeCapacity() : 0;
    r.startRelay = g.net.relayBonus();
    r.startProxy = g.net.proxyBonus();
    for (auto& m : g.net.machines) {
        m.usedThisRun = false;
        if (m.working() && !m.home) r.billIds.push_back(m.id);
    }

    ui::header("CONNECTING: " + target.name);
    ui::typeOut("  > handshake " + target.host + " ... routed through " +
                    std::to_string(g.netUnlocked ? g.net.activeCount() : 0) + " owned node(s) ... connected.", 4);
    if (target.deep) std::cout << "  " << col::magenta() << col::bold() << "THE DEEP JOB." << col::reset() << col::magenta() << " Three archives. HELIOS answers this one personally." << col::reset() << "\n";
    else if (target.kind == Kind::Story) std::cout << "  " << col::magenta() << "Contract: " << storyInfo(target.storyIndex).title << col::reset() << "\n";
    if (target.stealable && g.netUnlocked) std::cout << "  " << col::yellow() << "This server's hardware is detachable." << col::reset() << "\n";
    if (target.watchdog) std::cout << "  " << col::red() << "A watchdog wakes up. It gets faster the longer you stay." << col::reset() << "\n";

    // ---- Door phase.
    bool lockoutAnnounced = false;
    while (!r.doorOpen && r.end == End::None) {
        hud(r);
        r.lock.print();
        if (r.lock.history.empty()) doorHelp(r);
        std::string line = ui::prompt("  door> ");
        auto w = ui::split(line);
        if (w.empty()) continue;
        std::string c = ui::lower(w[0]);
        if (c == "help" || c == "?") { doorHelp(r); continue; }
        if (c == "disconnect" || c == "dc" || c == "quit" || c == "exit") { r.end = End::Disconnect; break; }
        if (c == "status" || c == "s") { continue; }
        if (c == "extra" || c == "compute" || c == "e") { useEmergency(r); lockoutAnnounced = false; continue; }
        if (c == "ls" || c == "cat" || c == "get" || c == "steal") { std::cout << "  The door is still locked.\n"; continue; }
        if (c == "phrase" || c == "p") {
            if (w.size() < 2) { std::cout << "  phrase <word>\n"; continue; }
            if (advance(r, bal::COST_PHRASE)) break;
            std::string ph = w[1];
            for (auto& ch : ph) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (target.backdoor && ph == target.passphrase) {
                r.doorOpen = true;
                r.viaPhrase = true;
                std::cout << "  " << col::green() << "The maintenance port accepts the phrase. You were invited in." << col::reset() << "\n";
            } else {
                std::cout << "  " << col::red() << "The maintenance port rejects it." << col::reset() << "\n";
                spike(r, bal::WRONG_PHRASE_SPIKE);
            }
            continue;
        }
        std::string guess = (c == "guess" || c == "g") && w.size() > 1 ? w[1] : w[0];
        std::string err;
        if (r.lock.sealed()) { std::cout << "  The lock is sealed. 'extra', 'phrase <word>', or 'disconnect'.\n"; continue; }
        if (!r.lock.guess(guess, err)) { std::cout << "  " << err << "\n"; continue; }
        std::cout << "   -> " << r.lock.history.back().feedback << "\n";
        if (advance(r, bal::COST_GUESS)) break;
        if (r.lock.open) {
            r.doorOpen = true;
            std::cout << "  " << col::green() << col::bold() << "ACCESS GRANTED." << col::reset() << "\n";
        } else if (r.lock.sealed() && !lockoutAnnounced) {
            lockoutAnnounced = true;
            std::cout << "  " << col::red() << "LOCKOUT. The lock seals and an alarm pings the trace (+"
                      << static_cast<int>(bal::LOCKOUT_SPIKE) << "%)." << col::reset() << "\n";
            if (spike(r, bal::LOCKOUT_SPIKE)) break;
            if (!r.lock.extraUsed && r.emergLeft > 0) std::cout << "  Your network is holding an emergency attempt: type 'extra'.\n";
        }
    }

    // ---- Inside.
    if (r.doorOpen && r.end == End::None) {
        if (!advance(r, bal::COST_LS)) {
            hud(r);
            listFiles(r);
            std::cout << col::dim() << "  (type 'help' for commands)" << col::reset() << "\n";
        }
    }
    while (r.end == End::None) {
        std::string line = ui::prompt("  " + target.host + "> ");
        auto w = ui::split(line);
        if (w.empty()) { hud(r); continue; }
        std::string c = ui::lower(w[0]);
        if (c == "help" || c == "?") insideHelp();
        else if (c == "ls" || c == "dir") { if (advance(r, bal::COST_LS)) break; listFiles(r); }
        else if (c == "cat" || c == "read") {
            int i = indexArg(r, w);
            if (i < 0) std::cout << "  cat <file number>\n"; else doRead(r, i);
        } else if (c == "get" || c == "dl" || c == "download") {
            int i = indexArg(r, w);
            if (i < 0) std::cout << "  get <file number>\n"; else doGet(r, i);
        } else if (c == "steal") doSteal(r);
        else if (c == "status" || c == "s") { /* hud below */ }
        else if (c == "disconnect" || c == "dc" || c == "quit" || c == "exit") { r.end = End::Disconnect; break; }
        else if (c == "extra" || c == "phrase") std::cout << "  The door is already open.\n";
        else std::cout << "  Unknown command. Type 'help'.\n";
        if (r.end == End::None) hud(r);
    }

    if (r.end == End::Disconnect) std::cout << "  " << col::dim() << "You jack out." << col::reset() << "\n";
    settle(r);
    ui::pause();
}

// ---------------------------------------------------------------------------
// Practice range: no bills, no trace, no emergency attempts. Just locks.
// ---------------------------------------------------------------------------
void practiceRange(GameState& g) {
    Rng rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    for (;;) {
        ui::header("PRACTICE RANGE (training hardware - nothing here is real)");
        std::cout << "  1) Keypad   2) Rotor   3) Cipher   0) back\n";
        std::string a = ui::prompt("  > ");
        LockType t;
        if (a == "1") t = LockType::Keypad;
        else if (a == "2") t = LockType::Rotor;
        else if (a == "3") t = LockType::Cipher;
        else return;
        Lock L = makeLock(t, rng, g.deck.breaker);
        std::cout << "  " << L.rule() << "\n";
        while (!L.open && !L.sealed()) {
            L.print();
            std::string in = ui::prompt("  practice> ");
            if (ui::lower(in) == "quit" || ui::lower(in) == "q") break;
            if (ui::lower(in) == "extra") { std::cout << "  Emergency attempts don't work on training hardware.\n"; continue; }
            std::string err;
            if (!L.guess(in, err)) std::cout << "  " << err << "\n";
            else std::cout << "   -> " << L.history.back().feedback << "\n";
        }
        if (L.open) std::cout << "  " << col::green() << "Opened in " << L.used << " guess(es)." << col::reset() << "\n";
        else std::cout << "  " << col::red() << "Sealed. The answer was " << L.secret << "." << col::reset() << "\n";
    }
}
