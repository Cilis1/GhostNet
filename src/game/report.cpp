#include "report.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "balance.h"
#include "locks.h"
#include "platform.h"
#include "rules.h"
#include "ui.h"
#include "world.h"

bool hasReport(const GameState& g, const Target& t) { return g.reports.count(t.id) > 0; }

void scanTarget(GameState& g, const Target& t) {
    if (!g.netUnlocked) { std::cout << "  You have no network to scout with yet.\n"; return; }
    int best = g.net.bestScoutTier();
    auto it = g.reports.find(t.id);
    if (it != g.reports.end()) {
        // Scanning something you already scanned is free (and may sharpen the picture).
        if (best > it->second) {
            it->second = best;
            std::cout << "  " << col::green() << "Re-scan is free. Your better scout sharpened the report." << col::reset() << "\n";
        }
        printReport(g, t);
        return;
    }
    if (g.net.scoutCapacity() == 0) {
        std::cout << "  No scouting machine is running. Put one in the rack (job: scout) first.\n";
        return;
    }
    if (g.net.usableScoutCharges() <= 0) {
        std::cout << "  No scouting charges left. They refill when you complete a contract or the map refreshes.\n";
        return;
    }
    // Who answers: the home server answers for free if it scouts; otherwise the best external scout.
    Machine* who = nullptr;
    Machine* h = g.net.home();
    if (h && h->working() && h->job == Job::Scout) who = h;
    else
        for (auto& m : g.net.machines)
            if (m.working() && m.job == Job::Scout && (!who || m.tier > who->tier)) who = &m;
    g.net.scoutCharges -= 1;
    g.reports[t.id] = best;
    std::cout << "  " << col::cyan() << who->name << col::reset() << " reaches out quietly...\n";
    plat::sleepMs(400);
    if (!who->home) {
        who->condition = std::max(0, who->condition - bal::SCOUT_WEAR);
        if (who->dark()) {
            who->active = false;
            std::cout << "  " << col::red() << who->name << " burned out answering the request and went dark." << col::reset() << "\n";
        }
    }
    printReport(g, t);
}

void printReport(const GameState& g, const Target& t) {
    auto it = g.reports.find(t.id);
    if (it == g.reports.end()) { std::cout << "  No report on file.\n"; return; }
    // Facts you learned stay true; with no scout running, the report opens at its most basic level.
    int q = g.net.bestScoutTier() > 0 ? it->second : 1;
    ui::header("REPORT: " + t.name + "  (quality " + std::to_string(q) + "/3)");

    // Always.
    if (t.lock == LockType::None) std::cout << "  Door: " << col::green() << "no lock - the door is open." << col::reset() << "\n";
    else {
        std::cout << "  Door: " << col::yellow() << lockName(t.lock) << " lock" << col::reset() << "\n";
        if (lockHasOddNearRule(t.lock))
            std::cout << "        " << col::red() << "Careful: " << col::reset() << lockRuleText(t.lock) << "\n";
    }
    double rate = baseTraceRate(t);
    std::cout << "  Danger: trace rises " << col::bold() << dangerWord(rate) << col::reset() << (t.watchdog ? ", and a watchdog makes it accelerate" : "") << ".\n";

    if (q >= 2) {
        std::string bars(static_cast<size_t>(t.security), '#');
        std::cout << "  Security grade: " << col::red() << bars << col::dim() << std::string(static_cast<size_t>(5 - t.security), '-') << col::reset()
                  << " (" << t.security << "/5)\n";
        if (t.objectiveCount() > 0)
            std::cout << "  Objective files: " << t.objectiveCount() << ", kept in " << col::cyan() << t.objectiveDir() << col::reset() << "\n";
        int pay = 0;
        for (auto& f : t.files) if (f.kind == FileKind::Paydata) ++pay;
        std::cout << "  Valuable files still there: " << pay << "\n";
    }
    if (q >= 3) {
        double eff = effectiveTraceRate(g, t);
        std::cout << "  Time before the connection kills you (current gear): ~" << static_cast<int>(100.0 / eff) << "s\n";
        std::cout << "  Maintenance way in: " << (t.backdoor ? std::string(col::green()) + "yes - worth looking for the phrase." : "none.")
                  << col::reset() << "\n";
    }

    // World facts, independent of quality.
    std::cout << col::dim() << "  -- what is true about this place --" << col::reset() << "\n";
    bool any = false;
    if (t.stealable && g.netUnlocked) { std::cout << "  * Detachable hardware: you could steal a server here instead of robbing it.\n"; any = true; }
    if (t.kind == Kind::Side) { std::cout << "  * Hidden route found through a paper trail: richer haul, real watchdog.\n"; any = true; }
    if (t.kind == Kind::Shrine) { std::cout << "  * Money is poor here but information is rich. It can never be stolen from.\n"; any = true; }
    if (t.trail) { std::cout << "  * Its ordinary maintenance text probably hides a route somewhere else.\n"; any = true; }
    if (t.hasHoneypot()) {
        if (q >= 3) {
            for (auto& f : t.files)
                if (f.kind == FileKind::Honeypot)
                    std::cout << "  * " << col::red() << "Confirmed bait file: " << f.dir << "/" << f.name << col::reset() << "\n";
        } else {
            std::cout << "  * One of the payout files is " << col::yellow() << "suspected" << col::reset() << " to be bait.\n";
        }
        any = true;
    }
    if (t.jackpot) { std::cout << "  * Storage is absurdly dense for its hardware. " << col::green() << "Jackpot." << col::reset() << "\n"; any = true; }
    if (!any) std::cout << "  * Nothing unusual.\n";
}
