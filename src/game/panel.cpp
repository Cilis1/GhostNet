#include "panel.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

#include "balance.h"
#include "locks.h"
#include "platform.h"
#include "report.h"
#include "rules.h"
#include "ui.h"
#include "world.h"

namespace {
enum class Vote { Go, Caution, Abort };

const char* voteStr(Vote v) {
    switch (v) {
        case Vote::Go: return "GO";
        case Vote::Caution: return "CAUTION";
        case Vote::Abort: return "ABORT";
    }
    return "?";
}
const char* voteCol(Vote v) {
    return v == Vote::Go ? col::green() : v == Vote::Caution ? col::yellow() : col::red();
}

void speak(const char* who, const char* color, const std::string& line) {
    std::cout << "  " << color << col::bold() << ui::pad(who, 8) << col::reset() << " " << line << "\n";
}

const char* VESPER = "VESPER";   // risk officer
const char* LEDGER = "LEDGER";   // economist
const char* MOTH = "MOTH";       // intelligence analyst
}  // namespace

void panelBriefing(const GameState& g, const Target& t) {
    ui::header("THE PANEL - pre-run briefing: " + t.name);
    std::cout << col::dim() << "  Three advisor constructs living in your deck. They disagree on purpose." << col::reset() << "\n";

    bool reported = hasReport(g, t);
    int quality = reported ? g.reports.at(t.id) : 0;
    bool phrase = g.knownPhrases.count(t.id) > 0;
    double eff = effectiveTraceRate(g, t);
    int budget = static_cast<int>(100.0 / eff);
    int lockCost = t.lock == LockType::None ? 0
                 : phrase ? bal::COST_PHRASE
                          : static_cast<int>((baseGuesses(t.lock) + g.deck.breaker) * 0.75) * bal::COST_GUESS;
    int objSecs = 0;
    for (auto& f : t.files) if (f.kind == FileKind::Objective) objSecs += secondsToDownload(g, f.size);
    objSecs = static_cast<int>(std::ceil(objSecs * bal::DOWNLOAD_TRACE_MULT));   // downloads are loud
    int need = lockCost + objSecs + 4;

    // VESPER - risk.
    Vote vv = Vote::Go;
    std::string vl = "Trace budget ~" + std::to_string(budget) + "s. Door costs ~" + std::to_string(lockCost) + "s";
    if (objSecs) vl += ", objectives ~" + std::to_string(objSecs) + "s";
    vl += ".";
    if (t.watchdog) budget = static_cast<int>(budget * 0.8);   // the ramp eats about a fifth
    if (budget < need) { vv = Vote::Abort; vl += t.deep ? " The deep job. Your gear can't carry it yet - the math is a wall." : " That math does not work. Bring proxies or a phrase."; }
    else if (budget < need + 15) { vv = Vote::Caution; vl += " Thin margin. Don't browse."; }
    else vl += " Comfortable, if you stay disciplined.";
    speak(VESPER, col::magenta(), vl);
    if (t.watchdog) speak(VESPER, col::magenta(), "Watchdog on this box: every minute you stay, the hunt gets 50% faster.");
    if (const Machine* b = g.net.decoy()) {
        speak(VESPER, col::magenta(), "Decoy armed: '" + b->name + "'. If this goes wrong, HELIOS traces it instead of you.");
    } else if (g.netUnlocked && t.security >= 3) {
        bool hasProxy = false;
        for (auto& m : g.net.machines) if (m.working() && m.job == Job::Proxy && !m.home) hasProxy = true;
        speak(VESPER, col::magenta(), hasProxy ? "No decoy armed. You have a running proxy that could take the fall."
                                               : "No decoy, no proxy to sacrifice. A trace-out here seizes the night's haul.");
        if (vv == Vote::Go) vv = Vote::Caution;
    }

    // LEDGER - money.
    Vote lv = Vote::Go;
    int bill = g.net.upkeepTotal();
    int fine = std::min(g.credits, baseFine(g, t, false, false));
    std::string ll = "Bill after this run: " + ui::credits(bill) + ". If traced: fine up to " + ui::credits(fine) + " and the haul is seized.";
    if (bill > g.credits) {
        lv = Vote::Caution;
        ll += " You can't cover the bill right now - earn at least " + ui::credits(bill - g.credits) + " or everything goes dark.";
    }
    if (t.reward) ll += " Contract pays " + ui::credits(t.reward) + " on completion.";
    speak(LEDGER, col::yellow(), ll);
    if (t.stealable && g.net.bestScoutTier() > 0) {
        HardwareSpec hw = hardwareFor(t.kind);
        int up = bal::TIER_UPKEEP[hw.tier] + (hw.noise - 1) * bal::NOISE_UPKEEP;
        speak(LEDGER, col::yellow(), std::string("Detachable ") + jobName(hw.def) + " T" + std::to_string(hw.tier) +
                                         " here. Free to take, ~" + std::to_string(up) + "cr/run to keep running." +
                                         (hw.tier == 3 ? " Ruinous, but it's the only way to get top-tier iron." : ""));
    }
    if (g.net.externalCount() >= bal::MAX_EXTERNAL && t.stealable)
        speak(LEDGER, col::yellow(), "You own 10 machines already. Scrap something before you try to capture.");

    // MOTH - intel.
    Vote mv = Vote::Go;
    if (!reported) {
        if (g.net.usableScoutCharges() > 0) { speak(MOTH, col::cyan(), "We have " + std::to_string(g.net.usableScoutCharges()) + " scouting charge(s). Spend one. Knowing the room beats walking into it."); mv = Vote::Caution; }
        else speak(MOTH, col::cyan(), "No report. We'd be walking in blind. I don't love it, but people do.");
    } else {
        speak(MOTH, col::cyan(), "Report on file at quality " + std::to_string(quality) + "/3. Read it again - it's free.");
    }
    if (phrase) speak(MOTH, col::cyan(), "You've read the maintenance phrase for this door. Use 'phrase <word>' and skip the lock.");
    else if (t.backdoor && quality >= 3) speak(MOTH, col::cyan(), "There IS a maintenance way in. Somebody wrote the phrase down. Shrines hoard memos.");
    if (t.lock != LockType::None && lockHasOddNearRule(t.lock) && (quality >= 1 || t.kind == Kind::Story))
        speak(MOTH, col::cyan(), "Rotor lock. '~' means one notch off, NOT misplaced. It's how most ghosts die.");
    if (t.hasHoneypot() && quality >= 1) { speak(MOTH, col::cyan(), "There's a bait file in there. Read before you grab."); if (mv == Vote::Go) mv = Vote::Caution; }

    // Verdict.
    std::cout << "\n  Votes: " << VESPER << " " << voteCol(vv) << voteStr(vv) << col::reset() << "  " << LEDGER << " "
              << voteCol(lv) << voteStr(lv) << col::reset() << "  " << MOTH << " " << voteCol(mv) << voteStr(mv) << col::reset() << "\n";
    int score = (vv == Vote::Abort ? 2 : vv == Vote::Caution) + (lv == Vote::Abort ? 2 : lv == Vote::Caution) +
                (mv == Vote::Abort ? 2 : mv == Vote::Caution);
    std::string verdict = score == 0 ? "Unanimous: go." : score <= 2 ? "Split panel. Your call." : "The panel advises against it.";
    std::cout << "  " << col::bold() << verdict << col::reset() << " " << col::dim() << "(They advise. You decide.)" << col::reset() << "\n";
}

void panelReview(const GameState& g) {
    ui::header("THE PANEL - network review");
    if (!g.netUnlocked) {
        speak(MOTH, col::cyan(), "We're just voices in a rented deck right now. Finish a contract; the rack has more in it than it looks.");
        speak(LEDGER, col::yellow(), "Meanwhile: tools for your own deck are permanent and never bill you. Worth remembering.");
        speak(VESPER, col::magenta(), "Open targets have the slowest trace. Learn the keypad there. Practice range is free.");
        return;
    }
    const Network& n = g.net;
    int bill = n.upkeepTotal();
    speak(LEDGER, col::yellow(), "Running " + std::to_string(n.activeCount()) + "/" + std::to_string(n.slots) + " slots. Bill per run: " +
                                     ui::credits(bill) + ". You hold " + ui::credits(g.credits) + ".");
    const Machine* loud = nullptr;
    for (auto& m : n.machines) if (m.working() && !m.home && (!loud || upkeepOf(m) > upkeepOf(*loud))) loud = &m;
    if (loud && upkeepOf(*loud) >= 11)
        speak(LEDGER, col::yellow(), "'" + loud->name + "' costs " + ui::credits(upkeepOf(*loud)) + "/run on its own. Store it on runs that don't need it.");
    int repair = 0;
    for (auto& m : n.machines) if (!m.home) repair += (100 - m.condition) * bal::REPAIR_PER_POINT;
    if (repair > 0) speak(LEDGER, col::yellow(), "Full repair would cost " + ui::credits(repair) + ".");
    if (bill > g.credits) speak(LEDGER, col::yellow(), "Warning: you cannot pay the next bill. Everything except N0 would go dark.");

    for (auto& m : n.machines)
        if (!m.home && m.condition > 0 && m.condition < 25)
            speak(VESPER, col::magenta(), "'" + m.name + "' is at " + std::to_string(m.condition) + "%. One bad night and it goes dark.");
    if (const Machine* b = n.decoy()) speak(VESPER, col::magenta(), "Decoy armed: '" + b->name + "'. I hope you said goodbye.");
    else speak(VESPER, col::magenta(), "No decoy armed. Proxy bonus " + std::to_string(n.proxyBonus()) + "% of a " + std::to_string(bal::PROXY_CAP) + "% cap.");
    speak(VESPER, col::magenta(), "Relay bonus " + std::to_string(n.relayBonus()) + "% of a " + std::to_string(bal::RELAY_CAP) + "% cap. Past the cap, more relays do nothing.");

    speak(MOTH, col::cyan(), "Scouting: " + std::to_string(n.usableScoutCharges()) + " charge(s), capacity " + std::to_string(n.scoutCapacity()) +
                                 ", best report quality " + std::to_string(n.bestScoutTier()) + "/3.");
    speak(MOTH, col::cyan(), "Emergency attempts next run: " + std::to_string(n.computeCapacity()) + " (max " + std::to_string(bal::COMPUTE_MAX) + ", one per lock).");
    if (!g.knownPhrases.empty()) speak(MOTH, col::cyan(), "Passphrases in your journal: " + std::to_string(g.knownPhrases.size()) + ".");
    if (n.home() && n.home()->tier < 3) speak(MOTH, col::cyan(), n.home()->name + " upgrades never bill and never wear. It's the only tier 3 you can get without stealing.");
    if (deepListed(g)) {
        int relayGap = bal::RELAY_CAP - n.relayBonus(), proxyGap = bal::PROXY_CAP - n.proxyBonus();
        speak(MOTH, col::cyan(), "The deep job: relays " + std::to_string(n.relayBonus()) + "/" + std::to_string(bal::RELAY_CAP) + "%, proxies " +
                                     std::to_string(n.proxyBonus()) + "/" + std::to_string(bal::PROXY_CAP) + "%" +
                                     (g.knownPhrases.count(kDeepId) ? ", phrase in hand." : ", no phrase yet - the Facilities Annex has it.") +
                                     (relayGap + proxyGap > 30 ? " Not yet." : " Close enough to try."));
    }
}
