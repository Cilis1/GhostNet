#include "intel.h"

#include <algorithm>
#include <cmath>

#include "balance.h"
#include "locks.h"
#include "rules.h"
#include "text.h"
#include "world.h"

bool hasReport(const GameState& g, const Target& t) { return g.reports.count(t.id) > 0; }

Result scanTarget(GameState& g, const Target& t) {
    if (!g.netUnlocked) return {false, "You have no network to scout with yet."};
    int best = g.net.bestScoutTier();
    auto it = g.reports.find(t.id);
    if (it != g.reports.end()) {
        if (best > it->second) {
            it->second = best;
            return {true, "Re-reading is free. Your better scout sharpened the report."};
        }
        return {true, ""};
    }
    if (g.net.scoutCapacity() == 0) return {false, "No scouting machine is running. Put one in the rack (job: scout) first."};
    if (g.net.usableScoutCharges() <= 0) return {false, "No scouting charges left. They refill on a contract or when the board refreshes."};
    Machine* who = nullptr;
    Machine* h = g.net.home();
    if (h && h->working() && h->job == Job::Scout) who = h;
    else
        for (auto& m : g.net.machines)
            if (g.net.online(m) && m.job == Job::Scout && (!who || m.tier > who->tier)) who = &m;
    if (!who) return {false, "No scouting machine is online."};
    g.net.scoutCharges -= 1;
    g.reports[t.id] = best;
    std::string msg = who->name + " reached out quietly.";
    if (!who->home) {
        who->condition = std::max(0, who->condition - bal::SCOUT_WEAR);
        if (who->dark()) {
            who->active = false;
            who->bay = -1;
            msg += " It burned out answering and went dark.";
        }
    }
    return {true, msg};
}

ReportView reportView(const GameState& g, const Target& t) {
    ReportView r;
    auto it = g.reports.find(t.id);
    if (it == g.reports.end()) return r;
    r.exists = true;
    r.quality = g.net.bestScoutTier() > 0 ? it->second : 1;
    r.lockName = lockName(t.lock);
    r.lockRule = t.lock == LockType::None ? "No lock - the door is open." : lockRuleText(t.lock);
    r.lockWarning = lockHasOddNearRule(t.lock);
    r.danger = dangerWord(baseTraceRate(t));
    r.watchdog = t.watchdog;
    if (r.quality >= 2) {
        r.security = t.security;
        r.objectives = t.objectiveCount();
        r.objectiveDir = t.objectiveDir();
        for (auto& f : t.files) if (f.kind == FileKind::Paydata) ++r.payFiles;
    }
    if (r.quality >= 3) {
        r.secondsToTrace = static_cast<int>(100.0 / effectiveTraceRate(g, t));
        r.backdoor = t.backdoor;
    }
    if (t.deep) r.facts.push_back("THE DEEP JOB. Tuned past any deck alone. Its maintenance phrase is kept by HELIOS facilities.");
    if (t.stealable && g.net.bestScoutTier() > 0) r.facts.push_back("Detachable hardware: you could capture a server here.");
    if (t.kind == Kind::Side) r.facts.push_back("Hidden route found through a paper trail: richer haul, real watchdog.");
    if (t.kind == Kind::Shrine) r.facts.push_back("Poor in money, rich in information. Can never be stolen from.");
    if (t.trail) r.facts.push_back("Its ordinary maintenance text probably hides a route somewhere else.");
    if (t.jackpot) r.facts.push_back("Storage is absurdly dense for its hardware. Jackpot.");
    if (t.hasHoneypot()) {
        if (r.quality >= 3) {
            for (auto& f : t.files)
                if (f.kind == FileKind::Honeypot) r.warnings.push_back("Confirmed bait file: " + f.dir + "/" + f.name);
        } else {
            r.warnings.push_back("One of the payout files is suspected to be a bait file.");
        }
    }
    return r;
}

const char* advisorName(Advisor a) {
    switch (a) {
        case Advisor::Vesper: return "VESPER";
        case Advisor::Ledger: return "LEDGER";
        case Advisor::Moth: return "MOTH";
    }
    return "?";
}
const char* advisorRole(Advisor a) {
    switch (a) {
        case Advisor::Vesper: return "risk";
        case Advisor::Ledger: return "money";
        case Advisor::Moth: return "intel";
    }
    return "";
}
const char* voteName(Vote v) {
    switch (v) {
        case Vote::Go: return "GO";
        case Vote::Caution: return "CAUTION";
        case Vote::Abort: return "ABORT";
    }
    return "?";
}

PanelView panelBriefing(const GameState& g, const Target& t) {
    PanelView p;
    auto say = [&](Advisor a, const std::string& s) { p.lines.push_back({a, s}); };
    bool reported = hasReport(g, t);
    int quality = reported ? g.reports.at(t.id) : 0;
    bool phrase = g.knownPhrases.count(t.id) > 0;
    double eff = effectiveTraceRate(g, t);
    int budget = static_cast<int>(100.0 / eff);
    int lockCost = t.lock == LockType::None ? 0
                 : phrase                   ? bal::COST_PHRASE
                                            : static_cast<int>((baseGuesses(t.lock) + g.deck.breaker) * 0.75) * bal::COST_GUESS;
    int objSecs = 0;
    for (auto& f : t.files) if (f.kind == FileKind::Objective) objSecs += secondsToDownload(g, f.size);
    objSecs = static_cast<int>(std::ceil(objSecs * bal::DOWNLOAD_TRACE_MULT));
    int need = lockCost + objSecs + 4;

    Vote vv = Vote::Go;
    std::string vl = "Trace budget ~" + std::to_string(budget) + "s. Door costs ~" + std::to_string(lockCost) + "s";
    if (objSecs) vl += ", objectives ~" + std::to_string(objSecs) + "s";
    vl += ".";
    if (t.watchdog) budget = static_cast<int>(budget * 0.8);
    if (budget < need) {
        vv = Vote::Abort;
        vl += t.deep ? " The deep job. Your gear can't carry it yet - the math is a wall." : " That math does not work. Bring proxies or a phrase.";
    } else if (budget < need + 15) {
        vv = Vote::Caution;
        vl += " Thin margin. Don't browse.";
    } else {
        vl += " Comfortable, if you stay disciplined.";
    }
    say(Advisor::Vesper, vl);
    if (t.watchdog) say(Advisor::Vesper, "Watchdog on this box: every minute you stay, the hunt gets 50% faster.");
    if (const Machine* b = g.net.decoy()) {
        say(Advisor::Vesper, "Decoy armed: '" + b->name + "'. If this goes wrong, HELIOS traces it instead of you.");
    } else if (g.netUnlocked && t.security >= 3) {
        bool hasProxy = false;
        for (auto& m : g.net.machines) if (g.net.online(m) && (m.job == Job::Proxy || m.job == Job::Gateway) && !m.home) hasProxy = true;
        say(Advisor::Vesper, hasProxy ? "No decoy armed. You have a running proxy that could take the fall."
                                      : "No decoy, no proxy to sacrifice. A trace-out here seizes the night's haul.");
        if (vv == Vote::Go) vv = Vote::Caution;
    }

    Vote lv = Vote::Go;
    int bill = g.net.upkeepTotal();
    int fine = std::min(g.credits, baseFine(g, t, false, false));
    std::string ll = "Bill after this run: " + txt::cr(bill) + ". If traced: fine up to " + txt::cr(fine) + " and the haul is seized.";
    if (bill > g.credits) {
        lv = Vote::Caution;
        ll += " You can't cover the bill right now - earn " + txt::cr(bill - g.credits) + " or everything goes dark.";
    }
    if (t.reward) ll += " Contract pays " + txt::cr(t.reward) + " on completion.";
    say(Advisor::Ledger, ll);
    if (t.stealable && g.net.bestScoutTier() > 0) {
        HardwareSpec hw = hardwareFor(t);
        int up = bal::TIER_UPKEEP[hw.tier] + (hw.noise - 1) * bal::NOISE_UPKEEP;
        if (t.kind == Kind::Prototype) up += up * bal::UNSTABLE_UPKEEP_PCT / 100;
        say(Advisor::Ledger, std::string("Detachable ") + jobName(hw.def) + " T" + std::to_string(hw.tier) + " here. Free to take, ~" +
                                 std::to_string(up) + "cr/run to keep running." +
                                 (hw.tier == 4 ? " PROTOTYPE. Tier 4 - nothing else in the city is. Unstable, and it bills like a landlord."
                                  : hw.tier == 3 ? " Ruinous, but it's the only stolen tier 3." : ""));
    }
    if (g.net.externalCount() >= bal::MAX_EXTERNAL && t.stealable)
        say(Advisor::Ledger, "You own " + std::to_string(bal::MAX_EXTERNAL) + " machines already. Scrap something before you try to capture.");

    Vote mv = Vote::Go;
    if (!reported) {
        if (g.net.usableScoutCharges() > 0) {
            say(Advisor::Moth, "We have " + std::to_string(g.net.usableScoutCharges()) + " scouting charge(s). Spend one. Knowing the room beats walking into it.");
            mv = Vote::Caution;
        } else {
            say(Advisor::Moth, "No report. We'd be walking in blind. I don't love it, but people do.");
        }
    } else {
        say(Advisor::Moth, "Report on file at quality " + std::to_string(quality) + "/3. Re-reading it is free.");
    }
    if (phrase) say(Advisor::Moth, "You've read the maintenance phrase for this door. Use it and skip the lock.");
    else if (t.backdoor && quality >= 3) say(Advisor::Moth, "There IS a maintenance way in. Somebody wrote the phrase down. Shrines hoard memos.");
    if (t.lock != LockType::None && lockHasOddNearRule(t.lock) && (quality >= 1 || t.kind == Kind::Story))
        say(Advisor::Moth, "Rotor lock. Amber means one notch off, NOT misplaced. It's how most runners die.");
    if (t.hasHoneypot() && quality >= 1) {
        say(Advisor::Moth, "There's a bait file in there. Read before you grab.");
        if (mv == Vote::Go) mv = Vote::Caution;
    }
    p.votes[0] = vv;
    p.votes[1] = lv;
    p.votes[2] = mv;
    int score = 0;
    for (Vote v : p.votes) score += v == Vote::Abort ? 2 : v == Vote::Caution ? 1 : 0;
    p.verdict = score == 0 ? "Unanimous: go." : score <= 2 ? "Split panel. Your call." : "The panel advises against it.";
    return p;
}

PanelView panelReview(const GameState& g) {
    PanelView p;
    auto say = [&](Advisor a, const std::string& s) { p.lines.push_back({a, s}); };
    if (!g.netUnlocked) {
        say(Advisor::Moth, "We're just voices in a rented deck right now. Finish a contract; the rack has more in it than it looks.");
        say(Advisor::Ledger, "Meanwhile: tools for your own deck are permanent and never bill you. Worth remembering.");
        say(Advisor::Vesper, "Open targets have the slowest trace. Learn the keypad there. The practice range is free.");
        p.verdict = "Survive first.";
        return p;
    }
    const Network& n = g.net;
    int bill = n.upkeepTotal();
    say(Advisor::Ledger, "Running " + std::to_string(n.activeCount()) + "/" + std::to_string(n.slots) + " slots. Bill per run: " + txt::cr(bill) +
                             ". You hold " + txt::cr(g.credits) + ".");
    const Machine* loud = nullptr;
    for (auto& m : n.machines) if (n.online(m) && !m.home && (!loud || upkeepOf(m) > upkeepOf(*loud))) loud = &m;
    if (loud && upkeepOf(*loud) >= 35)
        say(Advisor::Ledger, "'" + loud->name + "' costs " + txt::cr(upkeepOf(*loud)) + "/run on its own. Store it on runs that don't need it.");
    int repair = repairAllCost(g);
    if (repair > 0) say(Advisor::Ledger, "Full repair would cost " + txt::cr(repair) + ".");
    if (bill > g.credits) {
        say(Advisor::Ledger, "Warning: you cannot pay the next bill. Everything except N0 would go dark.");
        p.votes[1] = Vote::Abort;
    }
    for (auto& m : n.machines)
        if (!m.home && m.condition > 0 && m.condition < 25) {
            say(Advisor::Vesper, "'" + m.name + "' is at " + std::to_string(m.condition) + "%. One bad night and it goes dark.");
            p.votes[0] = Vote::Caution;
        }
    if (const Machine* b = n.decoy()) say(Advisor::Vesper, "Decoy armed: '" + b->name + "'. I hope you said goodbye.");
    else say(Advisor::Vesper, "No decoy armed. Proxy bonus " + std::to_string(n.proxyBonus()) + "% of a " + std::to_string(bal::PROXY_CAP) + "% cap.");
    say(Advisor::Vesper, "Relay bonus " + std::to_string(n.relayBonus()) + "% of a " + std::to_string(bal::RELAY_CAP) + "% cap. Past the cap, more relays do nothing.");
    for (auto* gw : n.openBays()) {
        int used = static_cast<int>(n.bayMembers(gw->id).size());
        std::string st = !gw->bayOn ? "switched OFF" : !n.bayOnline(*gw) ? "BROWNED OUT - over its power budget" : std::string(roleName(gw->role));
        say(Advisor::Moth, "Subnet " + gw->name + ": " + std::to_string(used) + "/" + std::to_string(bal::BAY_SLOTS) + " plugged, " +
                               std::to_string(n.bayPower(gw->id)) + "/" + std::to_string(n.bayBudget(*gw)) + " power, " + st + ", " +
                               txt::cr(n.bayUpkeep(gw->id)) + "/run." + (n.clusterBonus(gw->id) ? " CLUSTER live: acting a tier higher." : ""));
    }
    say(Advisor::Moth, "Scouting: " + std::to_string(n.usableScoutCharges()) + " charge(s), capacity " + std::to_string(n.scoutCapacity()) +
                           ", best report quality " + std::to_string(n.bestScoutTier()) + "/3.");
    say(Advisor::Moth, "Emergency attempts next run: " + std::to_string(n.computeCapacity()) + " (max " + std::to_string(bal::COMPUTE_MAX) + ", one per lock).");
    if (!g.knownPhrases.empty()) say(Advisor::Moth, "Passphrases in your journal: " + std::to_string(g.knownPhrases.size()) + ".");
    if (n.home() && n.home()->tier < 3)
        say(Advisor::Moth, n.home()->name + " upgrades never bill and never wear. It's the only tier 3 you can get without stealing.");
    if (deepListed(g)) {
        int gap = (bal::RELAY_CAP - n.relayBonus()) + (bal::PROXY_CAP - n.proxyBonus());
        say(Advisor::Moth, "The deep job: relays " + std::to_string(n.relayBonus()) + "/" + std::to_string(bal::RELAY_CAP) + "%, proxies " +
                               std::to_string(n.proxyBonus()) + "/" + std::to_string(bal::PROXY_CAP) + "%" +
                               (g.knownPhrases.count(kDeepId) ? ", phrase in hand." : ", no phrase yet - the Facilities Annex has it.") +
                               (gap > 30 ? " Not yet." : " Close enough to try."));
    }
    p.verdict = "Between runs is the only time you can change anything.";
    return p;
}
