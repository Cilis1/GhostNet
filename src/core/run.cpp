#include "run.h"

#include <algorithm>
#include <cmath>

#include "balance.h"
#include "rack.h"
#include "rng.h"
#include "rules.h"
#include "text.h"
#include "world.h"

RunSession::RunSession(GameState& gs, const Target& tg, uint64_t entropy) : g(gs), t(tg) {
    g.runCounter += 1;
    Rng rng(seedOf(g.worldSeed ^ entropy, g.runCounter));
    lock = makeLock(t.lock, rng, g.deck.breaker);
    doorOpen = t.lock == LockType::None;
    emergLeft = g.netUnlocked ? g.net.computeCapacity() : 0;
    startRelay = g.net.relayBonus();
    startProxy = g.net.proxyBonus();
    if (g.netUnlocked) {
        jamDelay = g.net.jamDelay();
        jamCut = g.net.jamSpikeCut();
        if (const Machine* v = g.net.bestVault()) {
            vaultPct = bal::VAULT_KEEP[g.net.effTier(*v)];
            vaultFiles = bal::VAULT_FILES[g.net.effTier(*v)];
            vaultName = v->name;
        }
        for (auto* gw : g.net.openBays())
            if (g.net.bayOnline(*gw)) strikeBays.push_back(gw->id);
    }
    for (auto& m : g.net.machines) {
        m.usedThisRun = false;
        if (g.net.online(m) && !m.home) billIds.push_back(m.id);
    }
    int nodes = 0;
    if (g.netUnlocked)
        for (auto& m : g.net.machines) if (g.net.online(m)) ++nodes;
    say("handshake " + t.host + " ... " +
            (nodes > 0 ? "routed through " + std::to_string(nodes) + " owned node(s)" : std::string("straight from your deck")) +
            " ... connected.",
        Tone::Dim);
    if (t.deep) say("THE DEEP JOB. Three archives. HELIOS answers this one personally.", Tone::Story);
    else if (t.kind == Kind::Story) say("Contract: " + storyInfo(t.storyIndex).title, Tone::Story);
    if (t.stealable && g.netUnlocked) say("This server's hardware is detachable.", Tone::Warn);
    if (t.watchdog) {
        say("A watchdog wakes up. It gets faster the longer you stay.", Tone::Bad);
        if (jamDelay > 0) say("Your jammers hold it back for the first " + std::to_string(jamDelay) + "s.", Tone::Info);
    }
    emit(Ev::Connected);
    if (doorOpen) {
        say("No lock. The door is open.", Tone::Good);
        openDoor(false);
    }
}

double RunSession::rateNow(double mult) const {
    double r = effectiveTraceRate(g, t) * mult;
    if (t.watchdog) r *= 1.0 + bal::WATCHDOG_RAMP * std::max(0, secs - jamDelay) / 60.0;
    return r;
}

int RunSession::secondsLeft() const {
    double r = rateNow(anchoring ? bal::ANCHOR_TRACE_MULT : 1.0);
    return r <= 0 ? 999 : static_cast<int>((100.0 - trace) / r);
}

bool RunSession::advance(int seconds, double mult) {
    for (int i = 0; i < seconds; ++i) {
        if (freezeLeft > 0) --freezeLeft;   // FREEZE: the clock runs, the trace doesn't
        else trace += rateNow(mult);
        secs += 1;
        if (trace >= 100.0) { trace = 100.0; traced(); return true; }
    }
    return false;
}

int RunSession::spikeSize(double base) const {
    return static_cast<int>(std::lround(base * (100 - jamCut) / 100.0));
}

bool RunSession::spike(double amount) {
    trace += amount * (100 - jamCut) / 100.0;
    if (trace >= 100.0) { trace = 100.0; traced(); return true; }
    return false;
}

void RunSession::traced() {
    act = Act::None;
    if (const Machine* b = g.net.decoy()) {
        std::string name = b->name;
        int id = b->id;
        decoyHot_ = b->trait == Trait::HotIron;
        subnetBurned_ = b->job == Job::Gateway;
        // A gateway takes its whole subnet with it. Every machine gets a nameplate.
        std::vector<int> dying = {id};
        if (subnetBurned_)
            for (auto* m : g.net.bayMembers(id)) dying.push_back(m->id);
        for (int d : dying) {
            const Machine* m = g.net.find(d);
            if (!m) continue;
            Memorial plate;
            plate.name = m->name;
            plate.origin = m->origin;
            plate.job = m->job;
            plate.tier = m->tier;
            plate.where = t.name;
            plate.run = g.stats.runs + 1;
            g.wall.push_back(plate);
        }
        auto& v = g.net.machines;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const Machine& m) { return std::find(dying.begin(), dying.end(), m.id) != dying.end(); }),
                v.end());
        g.net.decoyId = -1;
        g.net.burnedCount += static_cast<int>(dying.size());
        if (subnetBurned_) name += " and its subnet (" + std::to_string(dying.size()) + " machines)";
        sacrificed = name;
        end = End::Sacrifice;
        say("TRACE COMPLETE. The ticket resolves to the decoy. HELIOS traced " + name + " instead of you.", Tone::Bad);
        emit(Ev::Sacrifice, name);
    } else {
        end = End::TraceOut;
        say("TRACED. HELIOS closes the ticket. Your accounts are seized; tonight's haul is gone.", Tone::Bad);
        emit(Ev::Traced);
    }
}

void RunSession::openDoor(bool phrase) {
    doorOpen = true;
    viaPhrase = phrase;
    emit(phrase ? Ev::PhraseOk : Ev::AccessGranted);
    advance(bal::COST_LS);   // listing the files
}

bool RunSession::guess(const std::string& raw, std::string& err) {
    if (over() || doorOpen) { err = "The door is already open."; return false; }
    if (lock.sealed()) { err = "The lock is sealed. Use an emergency attempt, a phrase, or disconnect."; return false; }
    if (!lock.guess(raw, err)) return false;
    emit(Ev::Guess);
    if (advance(bal::COST_GUESS)) return true;
    if (lock.open) {
        say("ACCESS GRANTED.", Tone::Good);
        openDoor(false);
    } else if (lock.sealed()) {
        say("LOCKOUT. The lock seals and an alarm pings the trace (+" + std::to_string(spikeSize(bal::LOCKOUT_SPIKE)) + "%).",
            Tone::Bad);
        emit(Ev::Lockout);
        if (spike(bal::LOCKOUT_SPIKE)) return true;
        if (!lock.extraUsed && emergLeft > 0) say("Your network is holding an emergency attempt.", Tone::Warn);
    }
    return true;
}

bool RunSession::phrase(const std::string& word) {
    if (over() || doorOpen) return false;
    if (advance(bal::COST_PHRASE)) return false;
    std::string ph = txt::upper(txt::trim(word));
    if (t.backdoor && !ph.empty() && ph == t.passphrase) {
        say("The maintenance port accepts the phrase. You were invited in.", Tone::Good);
        openDoor(true);
        return true;
    }
    if (!ph.empty() && std::find(triedPhrases.begin(), triedPhrases.end(), ph) == triedPhrases.end()) triedPhrases.push_back(ph);
    say("The maintenance port rejects '" + ph + "' (+" + std::to_string(spikeSize(bal::WRONG_PHRASE_SPIKE)) + "% trace).", Tone::Bad);
    emit(Ev::PhraseBad);
    spike(bal::WRONG_PHRASE_SPIKE);
    return false;
}

bool RunSession::extra(std::string& err) {
    if (over() || doorOpen) { err = "The door is already open."; return false; }
    if (!g.netUnlocked) { err = "You have no network."; return false; }
    if (lock.type == LockType::None) { err = "No lock to spend it on."; return false; }
    if (lock.extraUsed) { err = "One emergency attempt per lock. Ever."; return false; }
    if (emergLeft <= 0) { err = "Your network holds no emergency attempts this run."; return false; }
    Machine* who = nullptr;
    Machine* h = g.net.home();
    if (h && h->working() && h->job == Job::Compute && !h->usedThisRun) who = h;
    else
        for (auto& m : g.net.machines)
            if (g.net.online(m) && m.job == Job::Compute && !m.usedThisRun && (!who || m.tier > who->tier)) who = &m;
    if (!who)   // a SIEGE subnet answers through its gateway
        for (auto& m : g.net.machines)
            if (m.job == Job::Gateway && g.net.bayOnline(m) && m.role == BayRole::Siege && !m.usedThisRun) { who = &m; break; }
    if (!who) { err = "No compute machine is able to answer."; return false; }
    lock.maxGuesses += 1;
    lock.extraUsed = true;
    emergLeft -= 1;
    who->usedThisRun = true;
    say(who->name + " spins up and holds the lock open for one more guess.", Tone::Good);
    emit(Ev::Extra, who->name);
    if (!who->home) {
        who->condition = std::max(0, who->condition - bal::COMPUTE_WEAR[g.net.effTier(*who)]);
        if (who->dark()) {
            who->active = false;
            say(who->name + " gave everything it had and went dark.", Tone::Bad);
            emit(Ev::MachineDark, who->name);
        }
    }
    advance(bal::COST_EXTRA);
    return true;
}

// ---------------------------------------------------------------------------
// Deck programs
// ---------------------------------------------------------------------------
bool RunSession::programUsable(int slot, std::string* why) const {
    auto no = [&](const std::string& s) { if (why) *why = s; return false; };
    if (slot < 0 || slot >= kProgramSlots) return no("No such slot.");
    Program p = g.programs[slot];
    if (p == Program::None) return no("That slot is empty.");
    if (over()) return no("The connection is closed.");
    const bool atDoor = !doorOpen;
    if (atDoor && doorPrograms >= 1) return no("One program per lock. This lock has had its one.");
    switch (p) {
        case Program::Freeze:
            if (freezeLeft > 0) return no("The trace is already frozen.");
            break;
        case Program::Peek:
            if (!atDoor || lock.type == LockType::None) return no("PEEK needs a closed lock in front of you.");
            {
                bool any = false;
                for (int i = 0; i < lock.length(); ++i) if (!lock.known(i)) any = true;
                if (!any) return no("You already know every position.");
            }
            break;
        case Program::Spoof:
            if (spoofArmed) return no("SPOOF is already armed.");
            if (honeypot) return no("Too late - the honeypot already went off.");
            break;
        case Program::Burst:
            if (atDoor) return no("BURST speeds up downloads. Get inside first.");
            if (burstLeft > 0) return no("BURST is already running.");
            break;
        default: break;
    }
    return true;
}

bool RunSession::runProgram(int slot, std::string& err) {
    if (!programUsable(slot, &err)) return false;
    Program p = g.programs[slot];
    g.programs[slot] = Program::None;   // single use, gone the moment it runs
    if (!doorOpen) ++doorPrograms;
    switch (p) {
        case Program::Freeze:
            freezeLeft = bal::FREEZE_SECS;
            say("FREEZE. The trace stands still for " + std::to_string(bal::FREEZE_SECS) + " seconds.", Tone::Info);
            break;
        case Program::Peek: {
            int pos = lock.peek();
            std::string ch = pos >= 0 ? txt::upper(lock.secret.substr(static_cast<size_t>(pos), 1)) : "?";
            say("PEEK. Position " + std::to_string(pos + 1) + " is '" + ch + "'.", Tone::Info);
            break;
        }
        case Program::Spoof:
            spoofArmed = true;
            say("SPOOF armed. The next bait file you take answers with a ghost.", Tone::Info);
            break;
        case Program::Burst:
            burstLeft = bal::BURST_SECS;
            say("BURST. The next " + std::to_string(bal::BURST_SECS) + " download seconds run at double speed. So does the trace, almost.",
                Tone::Warn);
            break;
        default: break;
    }
    emit(Ev::Program, programName(p));
    return true;
}

// ---------------------------------------------------------------------------
// Subnet strikes
// ---------------------------------------------------------------------------
int RunSession::vaultKeeps() const {
    if (vaultPct <= 0 || vaultFiles <= 0) return 0;
    std::vector<int> v = takenFiles;
    std::sort(v.rbegin(), v.rend());
    int sum = 0;
    for (int i = 0; i < vaultFiles && i < static_cast<int>(v.size()); ++i) sum += v[static_cast<size_t>(i)];
    return sum * vaultPct / 100;
}

bool RunSession::strikeUsable(int gwId, std::string* why) const {
    auto no = [&](const std::string& s) { if (why) *why = s; return false; };
    if (over()) return no("The connection is closed.");
    if (std::find(strikeBays.begin(), strikeBays.end(), gwId) == strikeBays.end()) return no("That subnet wasn't online when you connected.");
    if (std::find(strikesUsed.begin(), strikesUsed.end(), gwId) != strikesUsed.end()) return no("That subnet has already struck tonight.");
    const Machine* gw = g.net.find(gwId);
    if (!gw || !g.net.bayOnline(*gw)) return no("That subnet is offline.");
    switch (gw->role) {
        case BayRole::Stealth:
            if (freezeLeft >= bal::STRIKE_BLACKOUT) return no("The trace is already frozen.");
            break;
        case BayRole::Speed:
            if (act != Act::Download) return no("MIRROR needs a download in progress.");
            break;
        case BayRole::Siege: {
            if (doorOpen || lock.type == LockType::None) return no("CRACK needs a closed lock in front of you.");
            bool any = false;
            for (int i = 0; i < lock.length(); ++i) if (!lock.known(i)) any = true;
            if (!any) return no("You already know every box.");
            break;
        }
    }
    return true;
}

bool RunSession::strike(int gwId, std::string& err) {
    if (!strikeUsable(gwId, &err)) return false;
    const Machine* gw = g.net.find(gwId);
    strikesUsed.push_back(gwId);
    std::string who = gw->name + "'s subnet";
    switch (gw->role) {
        case BayRole::Stealth:
            freezeLeft = std::max(freezeLeft, bal::STRIKE_BLACKOUT);
            say("BLACKOUT. " + who + " floods HELIOS with ghosts: the trace stands still for " + std::to_string(bal::STRIKE_BLACKOUT) + "s.",
                Tone::Info);
            break;
        case BayRole::Speed: {
            int jump = std::min(bal::STRIKE_MIRROR, actTotal - actDone);
            actDone += jump;
            say("MIRROR. " + who + " pulls " + std::to_string(jump) + "s of the download in parallel. No trace for it.", Tone::Info);
            if (actDone >= actTotal) finishDownload();
            break;
        }
        case BayRole::Siege: {
            std::string got;
            for (int k = 0; k < bal::STRIKE_CRACK; ++k) {
                int pos = lock.peek();
                if (pos < 0) break;
                got += (got.empty() ? "" : ", ") + std::string("box ") + std::to_string(pos + 1) + " = '" +
                       txt::upper(lock.secret.substr(static_cast<size_t>(pos), 1)) + "'";
            }
            say("CRACK. " + who + " brute-forces part of the lock: " + got + ".", Tone::Info);
            break;
        }
    }
    emit(Ev::Strike, strikeName(gw->role));
    return true;
}

bool RunSession::read(int i) {
    if (over() || !doorOpen || busy() || i < 0 || i >= static_cast<int>(t.files.size())) return false;
    File& f = t.files[static_cast<size_t>(i)];
    if (!f.readable()) return false;
    if (advance(std::max(1, bal::COST_CAT - g.deck.lens))) return false;
    f.read = true;
    emit(Ev::Read, f.name);
    if (f.phraseFor >= 0 && !g.knownPhrases.count(f.phraseFor)) {
        g.knownPhrases.insert(f.phraseFor);
        std::string host;
        size_t a = f.text.find("door at ");
        if (a != std::string::npos) host = f.text.substr(a + 8, f.text.find(' ', a + 8) - (a + 8));
        size_t q1 = f.text.find('\''), q2 = f.text.find('\'', q1 + 1);
        std::string ph = (q1 != std::string::npos && q2 != std::string::npos) ? f.text.substr(q1 + 1, q2 - q1 - 1) : "?";
        g.journal.push_back("Passphrase for " + host + ": " + ph);
        say("Noted in your journal: passphrase for " + host + ".", Tone::Good);
        emit(Ev::PhraseLearned, host);
    }
    if (f.revealsSide >= 0) {
        int s = f.revealsSide;
        bool known = std::find(g.sidesRevealed.begin(), g.sidesRevealed.end(), s) != g.sidesRevealed.end();
        if (!known && !g.sidesBurned.count(s)) {
            g.sidesRevealed.push_back(s);
            g.journal.push_back("Side route discovered via " + t.host);
            say("A hidden route. It's on your job board now, and it stays there.", Tone::Story);
            emit(Ev::SideFound);
        }
    }
    return true;
}

bool RunSession::startDownload(int i, std::string& err) {
    if (over() || !doorOpen || busy() || i < 0 || i >= static_cast<int>(t.files.size())) { err = "Not now."; return false; }
    File& f = t.files[static_cast<size_t>(i)];
    if (!f.downloadable()) { err = "That's text. Read it instead."; return false; }
    if (f.taken) { err = "Already taken."; return false; }
    act = Act::Download;
    actFile = i;
    actTotal = secondsToDownload(g, f.size);
    actDone = 0;
    return true;
}

void RunSession::finishDownload() {
    File& f = t.files[static_cast<size_t>(actFile)];
    act = Act::None;
    f.taken = true;
    if (f.kind == FileKind::Honeypot && spoofArmed) {
        spoofArmed = false;
        say("BAIT FILE - but SPOOF answers for you. The honeypot logs a ghost and goes back to sleep. No alarm.", Tone::Good);
        emit(Ev::Spoofed, f.name);
        return;
    }
    if (f.kind == FileKind::Honeypot) {
        honeypot = true;
        say("BAIT FILE. It was a honeypot. ICE floods the channel (+" + std::to_string(spikeSize(bal::HONEYPOT_SPIKE)) + "% trace).",
            Tone::Bad);
        emit(Ev::Honeypot, f.name);
        spike(bal::HONEYPOT_SPIKE);
        return;
    }
    if (f.kind == FileKind::Objective) {
        say("Objective secured: " + f.name, Tone::Story);
        emit(Ev::Objective, f.name);
        bool all = true;
        for (auto& x : t.files) if (x.kind == FileKind::Objective && !x.taken) all = false;
        if (all && !jobDone) {
            jobDone = true;
            say("CONTRACT COMPLETE. Disconnect to keep it - if the trace fills first, it goes with your seized accounts.", Tone::Good);
            emit(Ev::ContractDone);
        }
        return;
    }
    haul += f.credits;
    bestFile = std::max(bestFile, f.credits);
    takenFiles.push_back(f.credits);
    say("+" + std::to_string(f.credits) + "cr  " + f.name + "  (haul so far: " + std::to_string(haul) + "cr)", Tone::Good);
    emit(Ev::DownloadDone, f.name);
}

CaptureQuote RunSession::quote() const {
    CaptureQuote q;
    if (!g.netUnlocked) { q.reason = "You have nowhere to put a server yet."; return q; }
    if (!t.stealable) {
        if (t.kind == Kind::Story || t.kind == Kind::Side) q.reason = "This place is load-bearing. Its hardware can't be taken.";
        else if (t.kind == Kind::Shrine) q.reason = "Shrines can never be stolen from.";
        else q.reason = "Nothing here is detachable.";
        return q;
    }
    if (g.net.externalCount() >= bal::MAX_EXTERNAL) { q.reason = "You already own " + std::to_string(bal::MAX_EXTERNAL) + " machines besides N0. Scrap one first (between runs)."; return q; }
    HardwareSpec hw = hardwareFor(t);
    captureQuote(t, honeypot, q.condition, q.noise);
    Machine probe;
    probe.tier = hw.tier;
    probe.noise = q.noise;
    probe.trait = traitFor(t.kind);
    q.trait = probe.trait;
    q.possible = true;
    q.def = hw.def;
    q.alt = hw.alt;
    q.tier = hw.tier;
    q.upkeep = upkeepOf(probe);
    q.holdSecs = anchorSeconds(t, viaPhrase);
    q.secondsLeft = secondsLeft();
    for (auto& f : t.files) {
        if (f.kind == FileKind::Paydata && !f.taken) q.leftCr += f.credits;
        if (f.readable() && !f.read) ++q.leftIntel;
    }
    if (t.trail) q.trailWarning = std::find(g.sidesRevealed.begin(), g.sidesRevealed.end(), t.trailSide) == g.sidesRevealed.end();
    q.rackFull = g.net.activeCount() >= g.net.slots;
    q.honeypot = honeypot;
    q.viaPhrase = viaPhrase;
    return q;
}

bool RunSession::startHold(Job j, std::string& err) {
    if (over() || !doorOpen || busy()) { err = "Not now."; return false; }
    CaptureQuote q = quote();
    if (!q.possible) { err = q.reason; return false; }
    if (j != q.def && j != q.alt) { err = "That hardware can't become that."; return false; }
    act = Act::Hold;
    actJob = j;
    actTotal = q.holdSecs;
    actDone = 0;
    anchoring = true;
    say("Holding the line... disconnecting now aborts the whole thing.", Tone::Warn);
    return true;
}

void RunSession::finishCapture() {
    act = Act::None;
    anchoring = false;
    HardwareSpec hw = hardwareFor(t);
    int cond, noise;
    captureQuote(t, honeypot, cond, noise);
    Machine m;
    m.id = g.net.nextId++;
    m.job = actJob;
    m.tier = hw.tier;
    m.captured = true;
    m.condition = cond;
    m.noise = noise;
    m.trait = traitFor(t.kind);
    std::string base = t.host.substr(0, t.host.find_first_of(".-"));
    m.name = base.substr(0, 10) + "-" + std::to_string(m.id);
    m.origin = "Captured from " + t.name + " (board " + std::to_string(g.mapIndex + 1) + ").";
    m.active = g.net.activeCount() < g.net.slots;
    g.net.machines.push_back(m);
    say("CAPTURED. " + t.name + " reboots under your control and goes dark.", Tone::Good);
    say("'" + m.name + "' (" + jobName(m.job) + " T" + std::to_string(m.tier) + ") arrives " +
            (m.active ? "in the rack, running." : "in cold storage (rack full)."),
        Tone::Good);
    if (m.trait != Trait::None) say(std::string("It carries a trait for life: ") + traitName(m.trait) + ". " + traitText(m.trait), Tone::Info);
    stolen = m.name;
    end = End::Steal;
    emit(Ev::Captured, m.name);
}

void RunSession::step() {
    if (over()) { act = Act::None; return; }
    if (act == Act::Download) {
        bool burst = burstLeft > 0;
        if (advance(1, bal::DOWNLOAD_TRACE_MULT * (burst ? bal::BURST_TRACE_MULT : 1.0))) return;
        actDone += burst ? 2 : 1;
        if (burst) --burstLeft;
        if (actDone >= actTotal) finishDownload();
    } else if (act == Act::Hold) {
        if (++actDone >= actTotal) { finishCapture(); return; }
        advance(1, bal::ANCHOR_TRACE_MULT);   // if this traces you, the anchoring flag raises the fine
    }
}

void RunSession::abortAction() {
    if (act == Act::Download) {
        act = Act::None;
        say("Download aborted. The time is gone.", Tone::Dim);
        emit(Ev::DownloadAborted);
    } else if (act == Act::Hold) {
        act = Act::None;
        anchoring = false;
        say("You pull out. The hardware stays exactly where it was.", Tone::Dim);
        end = End::Disconnect;
    }
}

void RunSession::disconnect() {
    if (over()) return;
    if (act == Act::Hold) { abortAction(); return; }
    act = Act::None;
    say("You jack out.", Tone::Dim);
    end = End::Disconnect;
}

// ---------------------------------------------------------------------------
// The reckoning
// ---------------------------------------------------------------------------
Reckoning RunSession::settle() {
    Reckoning rk;
    if (settled_) return rk;
    settled_ = true;
    rk.end = end;
    rk.sacrificed = sacrificed;
    rk.captured = stolen;
    const bool traceOut = end == End::TraceOut;
    const bool sacrifice = end == End::Sacrifice;
    const bool jobCounts = jobDone && !traceOut;   // a trace-out loses the night
    const bool billable = jobCounts || haul > 0 || traceOut || sacrifice || end == End::Steal;
    rk.billable = billable;
    auto line = [&](const std::string& s, Tone tone = Tone::Normal) { rk.lines.push_back({s, tone}); };

    if (haul > 0) {
        if (traceOut) {
            line("Accounts seized: tonight's " + std::to_string(haul) + "cr haul is gone.", Tone::Bad);
            int kept = vaultKeeps();
            if (kept > 0) {
                g.credits += kept;
                g.stats.earned += kept;
                line("...except what " + vaultName + " was holding offline: " +
                         (vaultFiles > 1 ? "your best " + std::to_string(vaultFiles) + " files" : std::string("your best file")) + ", +" +
                         std::to_string(kept) + "cr kept (VAULT, " + std::to_string(vaultPct) + "%).",
                     Tone::Good);
            }
        }
        else {
            g.credits += haul;
            g.stats.earned += haul;
            line("Haul: +" + std::to_string(haul) + "cr", Tone::Good);
        }
    }
    if (sacrifice)
        line("Escape bought with hardware: " + sacrificed + " is gone for good (machines burned, lifetime: " +
                 std::to_string(g.net.burnedCount) + "). You keep the night.",
             Tone::Bad);
    if (sacrifice) line("Its nameplate goes up on the MEMORIAL WALL (records).", Tone::Dim);
    if (traceOut && jobDone) line("The contract files went with the seized accounts. The job is not done.", Tone::Bad);
    if (traceOut || sacrifice) {
        int fine = baseFine(g, t, honeypot, anchoring);
        if (sacrifice) fine = (decoyHot_ || subnetBurned_) ? 0 : (fine + 1) / 2;
        fine = std::min(fine, g.credits);
        g.credits -= fine;
        g.stats.finesPaid += fine;
        if (sacrifice && subnetBurned_)
            line("Fine: waived. A whole subnet took the ticket - HELIOS is still counting the bodies.", Tone::Info);
        else if (sacrifice && decoyHot_)
            line("Fine: waived. " + sacrificed + " was HOT IRON - HELIOS got its own hardware back and called it even.", Tone::Info);
        else
            line("Fine: -" + std::to_string(fine) + "cr" + (sacrifice ? " (halved: " + sacrificed + " took the ticket)" : ""), Tone::Bad);
    }
    if (jobCounts && t.kind == Kind::Story) {
        g.credits += t.reward;
        g.stats.earned += t.reward;
        if (!t.deep) g.stats.jobs += 1;
        line("Contract '" + (t.deep ? std::string("THE DEEP JOB") : storyInfo(t.storyIndex).title) + "': +" + std::to_string(t.reward) +
                 "cr" + (t.infoReward.empty() ? "" : " and information"),
             Tone::Good);
    }
    // The main rack and every subnet bill separately. A subnet that can't be paid powers
    // down on its own; the main rack going unpaid takes its gateways (and so their subnets) with it.
    int bill = 0, mainBill = 0;
    std::vector<std::pair<int, int>> bayBills;   // gateway id, amount
    if (billable)
        for (int id : billIds) {
            const Machine* m = g.net.find(id);
            if (!m) continue;
            int u = upkeepOf(*m);
            bill += u;
            if (m->bay < 0) { mainBill += u; continue; }
            auto it = std::find_if(bayBills.begin(), bayBills.end(), [&](const std::pair<int, int>& b) { return b.first == m->bay; });
            if (it == bayBills.end()) bayBills.push_back({m->bay, u});
            else it->second += u;
        }
    rk.bill = bill;
    bool mainPaid = true;
    if (mainBill > 0) {
        if (g.credits >= mainBill) {
            g.credits -= mainBill;
            g.stats.billsPaid += mainBill;
            line("The bill: -" + std::to_string(mainBill) + "cr" + (bayBills.empty() ? "" : " (main rack)"), Tone::Warn);
        } else {
            mainPaid = false;
            g.stats.unpaidBills += 1;
            for (auto& m : g.net.machines) if (!m.home && m.bay < 0) m.active = false;
            g.net.decoyId = -1;
            line("The bill of " + std::to_string(mainBill) +
                     "cr went UNPAID. Nothing was taken. Every machine except N0 has gone dark, and the decoy is disarmed.",
                 Tone::Bad);
            line("No debt. Earn credits, then switch them back on.", Tone::Info);
        }
    }
    for (auto& [gwId, amount] : bayBills) {
        Machine* gw = g.net.find(gwId);
        std::string nm = gw ? gw->name : std::string("a subnet");
        if (mainPaid && g.credits >= amount) {
            g.credits -= amount;
            g.stats.billsPaid += amount;
            line("Subnet " + nm + (gw ? " (" + std::string(roleName(gw->role)) + ")" : "") + ": -" + std::to_string(amount) + "cr", Tone::Warn);
        } else if (mainPaid) {
            g.stats.unpaidBills += 1;
            if (gw) gw->bayOn = false;
            if (gw && g.net.decoyId == gw->id) g.net.decoyId = -1;
            line("Subnet " + nm + "'s bill of " + std::to_string(amount) + "cr went unpaid. That subnet powers down on its own; the rest keeps running.",
                 Tone::Bad);
        }
    }
    std::vector<std::string> darkNow;
    int worn = 0;
    if (billable) {
        for (int id : billIds) {
            Machine* m = g.net.find(id);
            if (!m || m->home) continue;
            ++worn;
            bool wasDark = m->dark();
            m->condition = conditionAfterRun(*m, traceOut);
            if (m->dark() && !wasDark) {
                m->active = false;
                m->bay = -1;
                if (g.net.decoyId == m->id) g.net.decoyId = -1;
                darkNow.push_back(m->name);
            }
        }
    }
    for (int gwId : strikesUsed) {   // a strike pushes every machine in the bay past its limits
        Machine* gw = g.net.find(gwId);
        if (!gw) continue;
        std::vector<Machine*> bay = {gw};
        for (auto& m : g.net.machines) if (m.bay == gwId && m.active) bay.push_back(&m);
        for (Machine* m : bay) {
            bool wasDark = m->dark();
            m->condition = std::max(0, m->condition - bal::STRIKE_WEAR);
            if (m->dark() && !wasDark) {
                if (m->bay < 0) m->active = false;
                else { m->bay = -1; m->active = false; }
                if (g.net.decoyId == m->id) g.net.decoyId = -1;
                darkNow.push_back(m->name);
            }
        }
        line("Strike wear: -" + std::to_string(bal::STRIKE_WEAR) + " on " + gw->name + "'s subnet (" + std::to_string(bay.size()) + " machines).",
             Tone::Warn);
    }
    if (worn > 0)
        line(std::string("Wear: ") + (traceOut ? "HEAVY (traced with no decoy)" : "normal") + " on " + std::to_string(worn) + " machine(s)",
             traceOut ? Tone::Bad : Tone::Dim);
    for (auto& n : darkNow) line(n + " wore out and went dark. Repair it, then switch it back on.", Tone::Bad);
    if (!billable) line("Nothing happened worth billing. No bill, no wear.", Tone::Dim);

    g.stats.runs += 1;
    if (t.deep) g.stats.deepAttempts += 1;
    std::string what;
    switch (end) {
        case End::Steal: what = "CAPTURE (" + stolen + ")"; g.stats.captures += 1; break;
        case End::TraceOut: what = "TRACE-OUT"; g.stats.traceouts += 1; break;
        case End::Sacrifice: what = "ESCAPE BOUGHT WITH HARDWARE (" + sacrificed + ")"; g.stats.sacrifices += 1; break;
        default:
            if (haul > 0 || jobDone) { what = "CLEAR"; g.stats.robberies += 1; }
            else { what = "WALK-AWAY"; g.stats.walkaways += 1; }
    }
    if (jobCounts) what += " + CONTRACT";
    g.stats.log.push_back("Run " + std::to_string(g.stats.runs) + ": " + t.name + " - " + what + ", haul " +
                          std::to_string(traceOut ? 0 : haul) + "cr, bill " + std::to_string(bill) + "cr");
    if (g.stats.log.size() > 30) g.stats.log.erase(g.stats.log.begin());

    if (isMapLocal(t.id)) g.burned.insert(t.id);
    if (t.kind == Kind::Side && (t.sideIndex != kAnnexSide || g.knownPhrases.count(kDeepId))) g.sidesBurned.insert(t.sideIndex);

    if (t.deep) {
        rk.deep = true;
        if (jobCounts) {
            g.deepDone = true;
            rk.deepWon = true;
            rk.storyTitle = "THE DEEP JOB";
            rk.storyOutro = "Three archives. Every ticket HELIOS ever opened - including yours, #48213, still OPEN.\n"
                            "You close it yourself. There is nobody left in the city who can reopen it.";
        } else {
            Shortfall& s = rk.shortfall;
            for (auto& f : t.files) {
                if (f.kind != FileKind::Objective) continue;
                ++s.total;
                if (f.taken) ++s.got;
                else { s.mbLeft += f.size; s.secsLeft += secondsToDownload(g, f.size); }
            }
            s.secsHeld = secs;
            s.trace = static_cast<int>(trace);
            s.doorOpened = doorOpen;
            s.speed = downloadSpeed(g);
            s.pct = secs + s.secsLeft > 0 ? 100 * s.secsLeft / (secs + s.secsLeft) : 100;
            s.uplink = g.deck.uplink;
            s.shroud = g.deck.shroud;
            s.relay = startRelay;
            s.proxy = startProxy;
        }
    }

    if (jobCounts && t.kind == Kind::Story && !t.deep) {
        int idx = t.storyIndex;
        g.story = idx + 1;
        rk.storyTitle = storyInfo(idx).title;
        rk.storyOutro = storyInfo(idx).outro;
        if (!t.infoReward.empty()) {
            g.journal.push_back(t.infoReward);
            rk.infoReward = t.infoReward;
        }
        if (idx == 2 && std::find(g.sidesRevealed.begin(), g.sidesRevealed.end(), kAnnexSide) == g.sidesRevealed.end())
            g.sidesRevealed.push_back(kAnnexSide);
        if (idx == 0 && !g.netUnlocked) {
            unlockNetwork(g);
            rk.unlockedNetwork = true;
        }
        if (g.netUnlocked) g.net.refillScout();
        if (g.story >= storyCount() && !g.deepDone) line("No more contracts are coming. Only the deep job is left.", Tone::Story);
    }

    if (traceOut && g.stats.traceouts == 1 && g.act == 1)
        rk.actCards.push_back({"END OF ACT ONE - RENTING",
                               "You were traced. The haul went with the seized accounts and the fine came out of what was left.\n"
                               "Everything you had tonight was borrowed: borrowed time, borrowed bandwidth, borrowed luck."});
    if (end == End::Steal && g.act < 2) {
        g.act = 2;
        rk.actCards.push_back({"ACT TWO - OWNING",
                               "A machine that was HELIOS's is yours now. Nobody rents it to you. Nobody can take it back.\n"
                               "It bills you, it wears, and it is louder than anything you could buy. It is also yours."});
    }
    if (sacrifice && g.act < 3) {
        g.act = 3;
        rk.actCards.push_back({"ACT THREE - THE DEEP JOB",
                               sacrificed + " died so you didn't have to. You understand the rack now: it is not decoration.\n"
                                            "Somewhere HELIOS keeps every ticket it ever opened. You know what a network is for."});
    }

    g.mapRunsLeft -= 1;
    if (g.mapRunsLeft <= 0) {
        g.mapIndex += 1;
        g.mapRunsLeft = bal::RUNS_PER_MAP;
        g.burned.clear();
        for (auto it = g.reports.begin(); it != g.reports.end();) it = isMapLocal(it->first) ? g.reports.erase(it) : std::next(it);
        g.net.refillScout();
        rk.boardRefreshed = true;
    }
    for (auto& m : g.net.machines) m.usedThisRun = false;
    rk.credits = g.credits;
    if (g.net.decoyId >= 0 && !g.net.decoy()) {   // e.g. a decoy subnet whose last member went dark
        g.net.decoyId = -1;
        line("Your decoy can't take a ticket any more. It's disarmed.", Tone::Warn);
    }
    return rk;
}
