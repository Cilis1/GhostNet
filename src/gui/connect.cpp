// The connection: route diagram, trace ring, lock mini-games, file browser,
// capture, the reckoning that follows, and the practice range.
#include <algorithm>
#include <cmath>
#include <random>

#include "app.h"
#include "audio.h"
#include "balance.h"
#include "gfx.h"
#include "rules.h"
#include "intel.h"
#include "save.h"
#include "text.h"
#include "world.h"

using namespace gx;

namespace {
constexpr float kStepSeconds = 0.2f;   // real seconds per game second during downloads and holds

Color traceColor(float pct) {
    if (pct < 50) return mix(C_CYAN, C_AMBER, pct / 50.0f);
    return mix(C_AMBER, C_RED, (pct - 50) / 50.0f);
}

std::string fixed1(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

// Starter phrases for the TRY PHRASE box: every phrase you've read anywhere first (techs
// reuse them), then the city's stock phrases in an order fixed per door, minus anything
// that already bounced off this door tonight.
std::vector<std::string> phraseStarters(const RunSession& r, const GameState& g) {
    std::vector<std::string> out;
    auto add = [&](const std::string& raw) {
        std::string p = txt::upper(txt::trim(raw));
        if (p.empty() || p == "?") return;
        if (std::find(out.begin(), out.end(), p) != out.end()) return;
        if (std::find(r.triedPhrases.begin(), r.triedPhrases.end(), p) != r.triedPhrases.end()) return;
        out.push_back(p);
    };
    for (auto& j : g.journal) {
        if (j.rfind("Passphrase for ", 0) != 0) continue;
        size_t c = j.rfind(": ");
        if (c != std::string::npos) add(j.substr(c + 2));
    }
    std::vector<std::string> stock = starterPhrases();
    std::mt19937_64 mix64(g.worldSeed * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(r.t.id) * 7919ULL + 17);
    std::shuffle(stock.begin(), stock.end(), mix64);
    for (auto& p : stock) add(p);
    return out;
}
}  // namespace

// ---------------------------------------------------------------------------
void App::connect(const Target& t) {
    uint64_t entropy = (static_cast<uint64_t>(std::random_device{}()) << 32) ^ static_cast<uint64_t>(std::random_device{}());
    run = std::make_unique<RunSession>(g, t, entropy);
    input.clear();
    traceShown = 0;
    readFile = -1;
    endedAt = -1;
    logSeen = 0;
    rowsSeen = 0;
    floats.clear();
    stepClock = 0;
    beatClock = 0;
    sfx::play(sfx::Boot, 0.7f);
    go(Scr::Run);
    glitch = 1.0f;
    handleEvents();
}

void App::handleEvents() {
    if (!run) return;
    for (auto& e : run->events) {
        switch (e.type) {
            case Ev::Connected: break;
            case Ev::AccessGranted:
            case Ev::PhraseOk:
                sfx::play(sfx::Granted);
                flash = 0.6f, flashColor = C_GREEN, glitch = 0.6f;
                floats.push_back({"ACCESS GRANTED", C_GREEN, Vector2{636, 380}, gx::now});
                break;
            case Ev::PhraseBad:
                sfx::play(sfx::Deny);
                flash = 0.5f, flashColor = C_RED, shake = 0.4f;
                break;
            case Ev::Guess: sfx::play(sfx::Read, 0.7f); break;
            case Ev::Lockout:
                sfx::play(sfx::Lockout);
                flash = 0.7f, flashColor = C_RED, shake = 0.8f, glitch = 0.8f;
                break;
            case Ev::Extra:
                sfx::play(sfx::Boot);
                toast(e.text + " holds the lock open for one more guess.", C_GREEN);
                break;
            case Ev::MachineDark:
                sfx::play(sfx::Deny);
                toast(e.text + " went dark.", C_RED);
                break;
            case Ev::Read: sfx::play(sfx::Read); break;
            case Ev::PhraseLearned:
                sfx::play(sfx::Coin);
                toast("Passphrase noted in your journal: " + e.text, C_GREEN);
                break;
            case Ev::SideFound:
                sfx::play(sfx::Granted, 0.7f);
                toast("A hidden route. It's on your job board now.", C_PURPLE);
                break;
            case Ev::DownloadDone:
                sfx::play(sfx::Coin);
                floats.push_back({"+DATA  " + e.text, C_AMBER, Vector2{636, 420}, gx::now});
                break;
            case Ev::DownloadAborted: sfx::play(sfx::Deny, 0.6f); break;
            case Ev::Honeypot:
                sfx::play(sfx::Honeypot);
                flash = 0.8f, flashColor = C_RED, shake = 1.0f, glitch = 1.0f;
                floats.push_back({"HONEYPOT", C_RED, Vector2{636, 380}, gx::now});
                break;
            case Ev::Objective:
                sfx::play(sfx::Granted, 0.8f);
                floats.push_back({"OBJECTIVE SECURED", C_MAGENTA, Vector2{636, 400}, gx::now});
                break;
            case Ev::ContractDone:
                flash = 0.6f, flashColor = C_MAGENTA;
                floats.push_back({"CONTRACT COMPLETE - NOW GET OUT", C_MAGENTA, Vector2{636, 360}, gx::now});
                break;
            case Ev::Captured:
                sfx::play(sfx::Capture);
                flash = 0.8f, flashColor = C_AMBER, glitch = 0.8f;
                break;
            case Ev::Traced:
                sfx::play(sfx::Traced);
                flash = 1.0f, flashColor = C_RED, shake = 1.2f, glitch = 1.0f;
                break;
            case Ev::Sacrifice:
                sfx::play(sfx::Sacrifice);
                flash = 1.0f, flashColor = C_RED, shake = 1.2f, glitch = 1.0f;
                break;
            case Ev::Program:
                sfx::play(sfx::Boot);
                flash = 0.35f, flashColor = C_CYAN, glitch = 0.3f;
                floats.push_back({e.text, C_CYAN, Vector2{636, 380}, gx::now});
                break;
            case Ev::Spoofed:
                sfx::play(sfx::Granted, 0.7f);
                flash = 0.4f, flashColor = C_MAGENTA;
                floats.push_back({"SPOOFED", C_MAGENTA, Vector2{636, 380}, gx::now});
                break;
            case Ev::Strike:
                sfx::play(sfx::Boot);
                flash = 0.5f, flashColor = Color{255, 140, 60, 255}, glitch = 0.5f, shake = 0.3f;
                floats.push_back({e.text, Color{255, 140, 60, 255}, Vector2{636, 380}, gx::now});
                break;
        }
    }
    run->events.clear();
}

void App::submitGuess() {
    std::string err;
    if (!run->guess(input, err)) {
        toast(err, C_AMBER);
        sfx::play(sfx::Deny, 0.6f);
        return;
    }
    input.clear();
    handleEvents();
}

void App::startCapture() {
    CaptureQuote q = run->quote();
    if (!q.possible) {
        toast(q.reason, C_AMBER);
        sfx::play(sfx::Deny, 0.6f);
        return;
    }
    std::string body = "Tier " + std::to_string(q.tier) + " hardware. It would arrive at condition " + std::to_string(q.condition) +
                       ", noise " + std::to_string(q.noise) + ", upkeep " + std::to_string(q.upkeep) + "cr per run.\n" +
                       "Hold the line for " + std::to_string(q.holdSecs) + "s while the trace runs x1.5 (about " +
                       std::to_string(q.secondsLeft) + "s left at the normal rate). Pull out and nothing happens.";
    if (q.leftCr > 0) body += "\nYou'd leave " + std::to_string(q.leftCr) + "cr of files behind.";
    if (q.leftIntel > 0) body += "\n" + std::to_string(q.leftIntel) + " unread text file(s) go dark with the server.";
    if (q.trailWarning) body += "\nThis server carries a paper trail you haven't read yet.";
    if (q.rackFull) body += "\nYour rack is full: it would go to cold storage.";
    if (q.honeypot) body += "\nThe bait already went off: it arrives worse.";
    if (q.viaPhrase) body += "\nYou came in by passphrase: the hold is shorter.";
    Modal m;
    m.title = "CAPTURE HARDWARE";
    m.body = body;
    m.buttons = {std::string("TAKE AS ") + jobName(q.def), std::string("TAKE AS ") + jobName(q.alt), "CANCEL"};
    m.accent = C_AMBER;
    Job a = q.def, b = q.alt;
    m.onPick = [this, a, b](int c, const std::string&) {
        if (c > 1 || !run) return;
        std::string err;
        if (!run->startHold(c == 0 ? a : b, err)) toast(err, C_AMBER);
        else sfx::play(sfx::Alarm, 0.6f);
    };
    pushModal(m);
}

// ---------------------------------------------------------------------------
// The run screen
// ---------------------------------------------------------------------------
void App::drawRun() {
    if (!run) {
        go(Scr::Hub);
        return;
    }
    RunSession& r = *run;
    traceShown += (static_cast<float>(r.trace) - traceShown) * std::min(1.0f, gx::dt * 5.0f);

    if (!r.over() && r.busy() && modals.empty()) {
        stepClock += gx::dt;
        while (stepClock >= kStepSeconds && r.busy() && !r.over()) {
            stepClock -= kStepSeconds;
            r.step();
            if (r.act == RunSession::Act::Download && r.actDone % 2 == 0) sfx::play(sfx::Tick, 0.25f);
        }
    }
    handleEvents();
    if (!r.over()) {
        beatClock += gx::dt;
        float interval = 1.3f - std::min(traceShown, 100.0f) / 100.0f * 0.95f;
        if (beatClock > interval) {
            beatClock = 0;
            sfx::play(sfx::Beat, 0.3f + traceShown / 140.0f);
        }
    }
    if (r.over() && endedAt < 0) endedAt = gx::now;

    // Top bar.
    DrawRectangle(0, 0, W, 64, Color{4, 3, 14, 235});
    DrawRectangleGradientH(0, 63, W, 1, fade(traceColor(traceShown), 0.2f), traceColor(traceShown));
    Color kc = r.t.deep ? C_RED : kindColor(r.t.kind);
    glowText(F::Display, r.t.name, 24, 12, 20, WHITE, Left, 0.8f);
    text(F::Mono, r.t.host + "   //   " + (r.t.deep ? "THE DEEP JOB" : kindName(r.t.kind)), 24, 40, 12, kc);
    std::string status = r.over() ? "OFFLINE" : r.act == RunSession::Act::Hold ? "HOLDING THE LINE" : r.busy() ? "DOWNLOADING" : r.doorOpen ? "INSIDE" : "DOOR LOCKED";
    Color sc = r.act == RunSession::Act::Hold ? C_AMBER : r.doorOpen ? C_GREEN : C_MAGENTA;
    glowText(F::Display, status, 640, 22, 16, pulse(sc, 3, 0.7f), Center);
    text(F::Mono, "HAUL", W - 200, 14, 12, C_DIM);
    glowText(F::Display, std::to_string(r.haul) + " CR", W - 24, 30, 20, C_AMBER, Right);
    text(F::Mono, "t+" + std::to_string(r.secs) + "s", W - 200, 36, 12, C_DIM);

    hoverHint.clear();
    drawRoute(Rectangle{20, 84, 296, 604});
    Rectangle mid{332, 84, 604, 604};
    if (!r.doorOpen && !r.over()) {
        panel(mid, C_MAGENTA, std::string(lockName(r.lock.type)) + " LOCK");
        bool submit = drawLock(Rectangle{mid.x + 20, mid.y + 20, mid.width - 40, mid.height - 100}, r.lock, input, modals.empty(), true);
        float by = mid.y + mid.height - 62;
        bool full = static_cast<int>(input.size()) == r.lock.length();
        if (button(Rectangle{mid.x + 20, by, 130, 44}, "SUBMIT", C_CYAN, full && !r.lock.sealed(), full, 14) || (submit && full)) submitGuess();
        bool known = g.knownPhrases.count(r.t.id) > 0;
        if (button(Rectangle{mid.x + 160, by, 140, 44}, known ? "USE PHRASE" : "TRY PHRASE", C_GREEN, true, known, 13)) {
            if (known) {
                r.phrase(r.t.passphrase);
                handleEvents();
            } else {
                openPhraseModal();
            }
        }
        bool canExtra = r.emergLeft > 0 && !r.lock.extraUsed;
        if (button(Rectangle{mid.x + 310, by, 150, 44}, "EMERGENCY +1", C_AMBER, canExtra, r.lock.sealed() && canExtra, 13)) {
            std::string err;
            if (!r.extra(err)) toast(err, C_AMBER);
            handleEvents();
        }
        if (button(Rectangle{mid.x + 470, by, 114, 44}, "JACK OUT", C_RED, true, r.lock.sealed() && !canExtra, 13) ||
            (modals.empty() && gx::keyPressed(KEY_ESCAPE)))
            r.disconnect();
    } else {
        drawFiles(mid);
    }
    drawTrace(Vector2{1106, 186}, 82);
    drawLog(Rectangle{952, 300, 308, 388});

    drawHoverHint();

    // Floating callouts.
    for (auto& f : floats) {
        float age = gx::now - f.born;
        if (age > 1.8f) continue;
        float a = age < 0.15f ? age / 0.15f : std::max(0.0f, 1.0f - (age - 1.0f) / 0.8f);
        glowText(F::Display, f.text, f.at.x, f.at.y - age * 40, 24, fade(f.color, a), Center, 1.5f);
    }
    floats.erase(std::remove_if(floats.begin(), floats.end(), [](const FloatText& f) { return gx::now - f.born > 1.8f; }), floats.end());

    if (r.over()) {
        float age = gx::now - endedAt;
        DrawRectangle(0, 64, W, H - 64, Color{0, 0, 0, static_cast<unsigned char>(std::min(170.0f, age * 300))});
        std::string big;
        std::string small;
        Color c = C_CYAN;
        switch (r.end) {
            case End::TraceOut: big = "TRACED"; small = "HELIOS closes the ticket. Your accounts are seized."; c = C_RED; break;
            case End::Sacrifice: big = "DECOY BURNED"; small = "HELIOS traced " + r.sacrificed + " instead of you."; c = C_RED; break;
            case End::Steal: big = "CAPTURED"; small = "'" + r.stolen + "' reboots under your control."; c = C_AMBER; break;
            default: big = "JACKED OUT"; small = "You're clear. Time to count it."; c = C_CYAN; break;
        }
        chromaText(F::Heavy, big, 640, 290, 96, c, Center, 3 + std::max(0.0f, 1.0f - age) * 12);
        text(F::Mono, small, 640, 410, 18, C_TEXT, Center);
        if (age > 2.2f || (age > 0.5f && (gx::clicked || gx::keyPressed(KEY_ENTER) || gx::keyPressed(KEY_SPACE)))) {
            gx::clicked = false;
            finishRun();
        }
    }
}

void App::openPhraseModal() {
    if (!run) return;
    RunSession& r = *run;
    Modal m;
    m.title = "MAINTENANCE PHRASE";
    m.body = "If you've read a maintenance passphrase for this door, type it. Techs across the city reuse a handful of stock "
             "phrases, so a starter is worth a shot - but every wrong one costs " + std::to_string(bal::COST_PHRASE) +
             "s and spikes the trace (+" + std::to_string(static_cast<int>(bal::WRONG_PHRASE_SPIKE)) + "%).";
    m.buttons = {"SEND", "CANCEL"};
    m.input = true;
    m.accent = C_GREEN;
    m.chips = phraseStarters(r, g);
    ReportView rv = reportView(g, r.t);
    if (r.t.deep) m.chipNote = "STARTERS - HELIOS'S DEEP PHRASE IS NOT A STOCK ONE. FIND IT IN THE ANNEX.";
    else if (rv.exists && rv.quality >= 3) m.chipNote = rv.backdoor ? "STARTERS - YOUR SCOUT SAYS THIS DOOR HAS A MAINTENANCE PORT"
                                                                    : "STARTERS - YOUR SCOUT SAYS THIS DOOR HAS NO MAINTENANCE PORT";
    else m.chipNote = "STARTERS - STOCK PHRASES, ROTATING. NOT EVERY DOOR HAS A PORT.";
    m.onPick = [this](int c, const std::string& f) {
        if (c != 0 || !run || f.empty()) return;
        run->phrase(f);
        handleEvents();
    };
    pushModal(m);
}

void App::drawRoute(Rectangle a) {
    RunSession& r = *run;
    panel(a, C_CYAN, "ROUTE");
    Vector2 top{a.x + a.width / 2, a.y + 70};
    const bool strikes = !r.strikeBays.empty();   // room for a row of subnet strikes
    Vector2 bottom{a.x + a.width / 2, a.y + (strikes ? 250.0f : 290.0f)};
    std::vector<const Machine*> nodes;
    if (g.netUnlocked)
        for (auto& m : g.net.machines)
            if (g.net.online(m) && m.bay < 0) nodes.push_back(&m);
    std::vector<Vector2> chain;
    chain.push_back(bottom);
    int n = static_cast<int>(std::min<size_t>(nodes.size(), 5));
    for (int i = 0; i < n; ++i) {
        float t = (i + 1.0f) / (n + 1.0f);
        chain.push_back(Vector2{a.x + a.width / 2 + ((i % 2) ? 52.0f : -52.0f), bottom.y + (top.y - bottom.y) * t});
    }
    chain.push_back(top);
    float pct = std::min(traceShown, 100.0f);
    Color tc = traceColor(pct);
    for (size_t i = 0; i + 1 < chain.size(); ++i) {
        glowLine(chain[i], chain[i + 1], fade(C_CYAN, 0.4f), 1.2f);
        tracer(chain[i], chain[i + 1], gx::now * (r.busy() ? 1.6f : 0.7f) + i * 0.3f, C_CYAN, 2);
        // HELIOS crawling back down the chain toward you.
        float reach = pct / 100.0f * (chain.size() - 1);
        float seg = static_cast<float>(chain.size() - 2 - i);
        if (reach > seg) {
            float f = std::min(1.0f, reach - seg);
            Vector2 s = chain[i + 1];
            Vector2 e{s.x + (chain[i].x - s.x) * f, s.y + (chain[i].y - s.y) * f};
            glowLine(s, e, fade(C_RED, 0.9f), 2.0f);
        }
    }
    hexNode(top, 26, r.t.deep ? C_RED : kindColor(r.t.kind), 0.2f);
    text(F::Display, r.doorOpen ? "OPEN" : "LOCK", top.x, top.y - 7, 12, WHITE, Center);
    text(F::Mono, r.t.host, top.x, top.y - 50, 12, C_TEXT, Center);
    for (int i = 0; i < n; ++i) {
        const Machine& m = *nodes[static_cast<size_t>(i)];
        Vector2 p = chain[static_cast<size_t>(i) + 1];
        hexNode(p, 15, jobColor(m.job), 0.2f);
        std::string nm = m.name;
        if (m.job == Job::Gateway && g.net.bayOnline(m)) {   // a subnet hangs off its gateway
            nm += " +" + std::to_string(g.net.bayMembers(m.id).size());
            for (int k = 0; k < 3; ++k) {
                float ang = gx::now * 1.4f + k * 2.094f;
                DrawCircleV(Vector2{p.x + std::cos(ang) * 21, p.y + std::sin(ang) * 21}, 2.5f, roleColor(m.role));
            }
        }
        text(F::MonoBold, nm, p.x + ((i % 2) ? 22.0f : -22.0f), p.y - 7, 11, C_TEXT, (i % 2) ? Left : Right);
        if (g.net.decoyId == m.id) DrawRing(p, 20, 22, gx::now * 120, gx::now * 120 + 270, 16, C_RED);
    }
    hexNode(bottom, 24, C_PURPLE, 0.25f);
    text(F::Display, "YOU", bottom.x, bottom.y - 7, 12, WHITE, Center);
    text(F::Mono, "your deck", bottom.x, bottom.y + 32, 12, C_DIM, Center);

    float y = a.y + (strikes ? 294.0f : 334.0f);
    auto row = [&](const std::string& k, const std::string& v, Color c) {
        text(F::Mono, k, a.x + 20, y, 12, C_DIM);
        text(F::MonoBold, v, a.x + a.width - 20, y, 13, c, Right);
        y += 19;
    };
    if (r.burstLeft > 0) row("DOWNLOAD", "BURST x2 (" + std::to_string(r.burstLeft) + "s)", pulse(C_AMBER, 6));
    else row("DOWNLOAD", fixed1(downloadSpeed(g)) + " MB/s", C_CYAN);
    if (r.freezeLeft > 0) row("TRACE RATE", "FROZEN (" + std::to_string(r.freezeLeft) + "s)", pulse(C_CYAN, 6));
    else row("TRACE RATE", fixed1(r.rateNow(r.act == RunSession::Act::Download ? bal::DOWNLOAD_TRACE_MULT * (r.burstLeft > 0 ? bal::BURST_TRACE_MULT : 1.0)
                                          : r.anchoring ? bal::ANCHOR_TRACE_MULT : 1.0)) + " %/s", tc);
    row("GUESS COSTS", std::to_string(bal::COST_GUESS) + "s", C_TEXT);
    row("EMERGENCY", std::to_string(r.emergLeft), r.emergLeft ? C_AMBER : C_DIM);
    const Machine* d = g.net.decoy();
    row("DECOY", d ? d->name : "none", d ? C_RED : C_DIM);
    int bill = 0;
    for (int id : r.billIds)
        if (const Machine* m = g.net.find(id)) bill += upkeepOf(*m);
    row("BILL (FIXED)", std::to_string(bill) + "cr", C_AMBER);
    if (r.t.watchdog) {
        if (r.secs < r.jamDelay) row("WATCHDOG", "JAMMED " + std::to_string(r.jamDelay - r.secs) + "s", C_PURPLE);
        else row("WATCHDOG", "ACCELERATING", pulse(C_RED, 4));
    }
    if (r.spoofArmed) row("SPOOF", "ARMED", C_GREEN);

    // Deck programs: two single-use slots. Running one costs no time.
    float py = a.y + a.height - 104;
    // Subnet strikes: once per run per subnet that was online at connect. Free; wears the subnet after.
    if (strikes) {
        int ns = static_cast<int>(std::min<size_t>(r.strikeBays.size(), 2));
        float sw = (a.width - 40 - 8 * (ns - 1)) / ns;
        for (int i = 0; i < ns; ++i) {
            int gid = r.strikeBays[static_cast<size_t>(i)];
            const Machine* gw = g.net.find(gid);
            if (!gw) continue;
            bool spent = std::find(r.strikesUsed.begin(), r.strikesUsed.end(), gid) != r.strikesUsed.end();
            std::string why;
            bool ok = r.strikeUsable(gid, &why);
            Rectangle br{a.x + 20 + i * (sw + 8), py - 38, sw, 30};
            std::string lbl = spent ? std::string("SPENT") : std::string(strikeName(gw->role));
            if (button(br, lbl, roleColor(gw->role), ok && modals.empty(), false, 11)) {
                std::string err;
                if (!r.strike(gid, err)) toast(err, C_AMBER);
                handleEvents();
            }
            if (gx::hovered(br))
                hoverHint = gw->name + " SUBNET (" + roleName(gw->role) + ")\n" + strikeText(gw->role) + " Wears every machine in it by " +
                            std::to_string(bal::STRIKE_WEAR) + "." + (ok ? "" : "\n" + why);
        }
    }
    text(F::Display, "DECK PROGRAMS", a.x + 20, py, 11, C_DIM);
    if (!r.doorOpen) text(F::Mono, r.doorPrograms > 0 ? "lock: used" : "1 per lock", a.x + a.width - 20, py, 11, r.doorPrograms > 0 ? C_AMBER : C_DIM, Right);
    for (int i = 0; i < kProgramSlots; ++i) {
        Program p = g.programs[i];
        Rectangle br{a.x + 20, py + 20 + i * 44, a.width - 40, 38};
        std::string why;
        bool ok = r.programUsable(i, &why);
        Color pc = p == Program::Freeze ? C_CYAN : p == Program::Peek ? C_GREEN : p == Program::Spoof ? C_MAGENTA : p == Program::Burst ? C_AMBER : C_DIM;
        if (button(br, programName(p), pc, ok && modals.empty(), false, 12)) {
            std::string err;
            if (!r.runProgram(i, err)) toast(err, C_AMBER);
            handleEvents();
        }
        if (p != Program::None && gx::hovered(br)) hoverHint = std::string(programText(p)) + (ok ? "" : "\n" + why);
    }
}

void App::drawTrace(Vector2 c, float rad) {
    RunSession& r = *run;
    float pct = std::min(traceShown, 100.0f);
    Color col = traceColor(pct);
    float throb = pct > 75 ? std::sin(gx::now * 10) * 3 : 0;
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(c, rad * 1.8f, fade(col, 0.12f + pct / 600.0f), fade(col, 0));
    EndBlendMode();
    DrawRing(c, rad - 14, rad, 0, 360, 72, Color{20, 16, 40, 255});
    for (int i = 0; i < 60; ++i) {
        float ang = -90 + i * 6.0f;
        float rr = (i % 5 == 0) ? 8.0f : 4.0f;
        float ca = std::cos(ang * DEG2RAD), sa = std::sin(ang * DEG2RAD);
        DrawLineEx(Vector2{c.x + ca * (rad + 6), c.y + sa * (rad + 6)}, Vector2{c.x + ca * (rad + 6 + rr), c.y + sa * (rad + 6 + rr)}, 1,
                   fade(i * 100 / 60 < pct ? col : C_DIM, 0.7f));
    }
    float end = -90 + 360 * pct / 100.0f;
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRing(c, rad - 20 - throb, rad + 6 + throb, -90, end, 72, fade(col, 0.25f));
    EndBlendMode();
    DrawRing(c, rad - 14, rad, -90, end, 72, col);
    float ea = end * DEG2RAD;
    glowDot(Vector2{c.x + std::cos(ea) * (rad - 7), c.y + std::sin(ea) * (rad - 7)}, 4, WHITE);
    text(F::Display, "TRACE", c.x, c.y - 42, 12, C_DIM, Center);
    glowText(F::Display, std::to_string(static_cast<int>(pct)) + "%", c.x, c.y - 22, 36, col, Center, 1.2f);
    std::string left = r.over() ? "--" : "~" + std::to_string(r.secondsLeft()) + "s LEFT";
    text(F::MonoBold, left, c.x, c.y + 22, 13, pct > 75 ? pulse(C_RED, 8) : C_TEXT, Center);
}

void App::drawLog(Rectangle a) {
    panel(a, C_PURPLE, "LOG");
    const auto& log = run->log;
    if (log.size() != logSeen) {
        logSeen = log.size();
        logBorn = gx::now;
    }
    // Lay out from the newest line upward.
    float y = a.y + a.height - 16;
    for (int i = static_cast<int>(log.size()) - 1; i >= 0; --i) {
        std::string s = log[static_cast<size_t>(i)].text;
        if (i == static_cast<int>(log.size()) - 1) {
            size_t show = static_cast<size_t>((gx::now - logBorn) * 120);
            if (show < s.size()) s = s.substr(0, show) + "_";
        }
        auto lines = wrap(F::Mono, s, a.width - 34, 12);
        float h = lines.size() * 16.0f + 6;
        if (y - h < a.y + 16) break;
        y -= h;
        Color c = toneColor(log[static_cast<size_t>(i)].tone);
        float age = static_cast<float>(log.size() - 1 - i);
        Color lc = fade(c, std::max(0.45f, 1.0f - age * 0.08f));
        DrawRectangle(static_cast<int>(a.x + 14), static_cast<int>(y + 3), 2, static_cast<int>(h - 8), fade(c, 0.6f));
        for (size_t k = 0; k < lines.size(); ++k) text(F::Mono, lines[k], a.x + 22, y + k * 16.0f, 12, lc);
    }
}

bool App::drawLock(Rectangle a, Lock& lock, std::string& field, bool interactive, bool live) {
    (void)live;
    if (lock.history.size() != rowsSeen) {
        rowsSeen = lock.history.size();
        rowBorn = gx::now;
    }
    int len = lock.length();
    bool cipher = lock.type == LockType::Cipher;
    // Header.
    text(F::Mono, "GUESSES LEFT", a.x, a.y, 12, C_DIM);
    pips(a.x + 110, a.y + 1, std::max(0, lock.left()), lock.maxGuesses, lock.left() <= 1 ? C_RED : C_CYAN, 11, 4);
    if (lock.extraUsed) text(F::MonoBold, "+1 EMERGENCY", a.x + a.width, a.y, 12, C_AMBER, Right);
    para(F::Mono, lock.rule(), a.x, a.y + 22, a.width, 12, fade(C_TEXT, 0.8f), 1.3f, 3);

    float tile = cipher ? 46 : 48, gap = 8;
    float rowW = len * (tile + gap) - gap;
    float x0 = a.x + (a.width - rowW) / 2;
    float y = a.y + 80;
    int rows = std::max<int>(lock.maxGuesses, static_cast<int>(lock.history.size()) + 1);
    float rowH = std::min(tile + 8, (a.height - 150) / rows);
    float t = std::min(tile, rowH - 6);
    bool inputRow = interactive && !lock.open && !lock.sealed();
    for (int i = 0; i < rows; ++i) {
        float ry = y + i * rowH;
        bool hist = i < static_cast<int>(lock.history.size());
        bool cur = !hist && i == static_cast<int>(lock.history.size()) && inputRow;
        for (int k = 0; k < len; ++k) {
            Rectangle tr{x0 + k * (t + gap), ry, t, t};
            if (hist) {
                const Lock::Row& row = lock.history[static_cast<size_t>(i)];
                bool newest = i == static_cast<int>(lock.history.size()) - 1;
                bool revealed = !newest || gx::now - rowBorn > k * 0.09f;
                Color c = Color{60, 50, 110, 255};
                int mk = revealed && k < static_cast<int>(row.marks.size()) ? row.marks[static_cast<size_t>(k)] : -1;
                if (mk >= 0) c = mk == 2 ? C_GREEN : mk == 1 ? C_AMBER : Color{40, 44, 66, 255};
                DrawRectangleRec(tr, fade(c, 0.85f));
                if (mk > 0) {
                    BeginBlendMode(BLEND_ADDITIVE);
                    DrawRectangleRec(Rectangle{tr.x - 3, tr.y - 3, tr.width + 6, tr.height + 6}, fade(c, 0.18f));
                    EndBlendMode();
                }
                DrawRectangleLinesEx(tr, 1, fade(WHITE, 0.25f));
                std::string ch(1, static_cast<char>(std::toupper(static_cast<unsigned char>(row.guess[static_cast<size_t>(k)]))));
                text(F::MonoBold, ch, tr.x + t / 2, tr.y + (t - 24) / 2, 24, mk > 0 ? C_INK : mk == 0 ? fade(WHITE, 0.55f) : WHITE, Center);
                // A corner mark too, so the colours are never the only signal.
                if (mk >= 0) {
                    const char* tag = lock.type == LockType::Rotor ? (mk == 2 ? "=" : mk == 1 ? "+-1" : "")
                                                                    : (mk == 2 ? "OK" : mk == 1 ? "<>" : "");
                    text(F::MonoBold, tag, tr.x + t - 3, tr.y + 2, 9, C_INK, Right);
                }
            } else if (cur) {
                bool has = k < static_cast<int>(field.size());
                bool caret = k == static_cast<int>(field.size());
                bool peeked = std::find(lock.peeked.begin(), lock.peeked.end(), k) != lock.peeked.end();
                DrawRectangleRec(tr, fade(C_CYAN, has ? 0.18f : 0.05f));
                std::string pc = peeked ? std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(lock.secret[static_cast<size_t>(k)])))) : "";
                if (peeked && !has)   // PEEK: the answer for this box, as a ghost to type over
                    text(F::MonoBold, pc, tr.x + t / 2, tr.y + (t - 24) / 2, 24, fade(C_GREEN, 0.45f), Center);
                if (peeked) text(F::MonoBold, "PEEK " + pc, tr.x + t / 2, tr.y + t + 1, 8, C_GREEN, Center);
                if (caret) glowRect(tr, pulse(C_CYAN, 6, 0.4f), 2, 3);
                else DrawRectangleLinesEx(tr, 1.5f, fade(C_CYAN, 0.7f));
                if (has) {
                    std::string ch(1, static_cast<char>(std::toupper(static_cast<unsigned char>(field[static_cast<size_t>(k)]))));
                    glowText(F::MonoBold, ch, tr.x + t / 2, tr.y + (t - 24) / 2, 24, WHITE, Center, 0.8f);
                }
            } else {
                DrawRectangleLinesEx(tr, 1, fade(C_DIM, 0.25f));
            }
        }
    }
    float py = y + rows * rowH + 10;
    if (lock.open) {
        glowText(F::Display, "OPEN", a.x + a.width / 2, py, 22, C_GREEN, Center);
        return false;
    }
    if (lock.sealed()) {
        glowText(F::Display, "SEALED", a.x + a.width / 2, py, 22, pulse(C_RED, 5), Center);
        return false;
    }
    bool submit = false;
    if (interactive) {
        if (textInput(field, static_cast<size_t>(len), cipher ? 2 : 1)) submit = true;
        if (!cipher) {
            // An on-screen keypad for the mouse.
            float kw = 40, kg = 6;
            float total = 12 * (kw + kg) - kg;
            float kx = a.x + (a.width - total) / 2;
            for (int d = 0; d < 10; ++d) {
                Rectangle kr{kx + d * (kw + kg), py, kw, 34};
                if (button(kr, std::to_string(d), C_PURPLE, static_cast<int>(field.size()) < len, false, 14)) field += static_cast<char>('0' + d);
            }
            if (button(Rectangle{kx + 10 * (kw + kg), py, kw, 34}, "<", C_DIM, !field.empty(), false, 14)) field.pop_back();
            if (button(Rectangle{kx + 11 * (kw + kg), py, kw, 34}, "OK", C_CYAN, static_cast<int>(field.size()) == len, false, 13)) submit = true;
        } else {
            text(F::Mono, "type a 5-letter word, Enter to submit, Backspace to fix", a.x + a.width / 2, py + 8, 12, C_DIM, Center);
        }
    }
    return submit;
}

void App::drawFiles(Rectangle a) {
    RunSession& r = *run;
    panel(a, C_GREEN, "INSIDE");
    float y = a.y + 22;
    text(F::Mono, "FILE", a.x + 70, y, 11, C_DIM);
    text(F::Mono, "SIZE", a.x + 330, y, 11, C_DIM);
    text(F::Mono, "TIME", a.x + 390, y, 11, C_DIM);
    text(F::Mono, "VALUE", a.x + 440, y, 11, C_DIM);
    y += 20;
    bool reader = readFile >= 0 && readFile < static_cast<int>(r.t.files.size());
    float listBottom = reader ? a.y + a.height - 250 : a.y + a.height - 80;
    float rh = std::min(42.0f, (listBottom - y) / std::max<size_t>(1, r.t.files.size()));
    for (size_t i = 0; i < r.t.files.size(); ++i) {
        File& f = r.t.files[i];
        Rectangle row{a.x + 16, y, a.width - 32, rh - 4};
        bool active = r.busy() && r.act == RunSession::Act::Download && r.actFile == static_cast<int>(i);
        Color kc = f.readable() ? C_CYAN : f.kind == FileKind::Objective ? C_MAGENTA : C_AMBER;
        DrawRectangleRec(row, fade(kc, active ? 0.14f : 0.05f));
        DrawRectangleRec(Rectangle{row.x, row.y, 3, row.height}, fade(kc, f.taken || f.read ? 0.4f : 1.0f));
        const char* icon = f.readable() ? "TXT" : f.kind == FileKind::Objective ? "OBJ" : "DAT";
        text(F::MonoBold, icon, row.x + 14, row.y + (row.height - 13) / 2, 12, kc);
        std::string path = f.dir + "/" + f.name;
        if (path.size() > 30) path = "..." + path.substr(path.size() - 27);
        text(F::Mono, path, a.x + 70, row.y + (row.height - 13) / 2, 13, (f.taken) ? C_DIM : C_TEXT);
        float by = row.y + (row.height - 28) / 2;
        if (f.readable()) {
            text(F::Mono, "text", a.x + 330, row.y + (row.height - 13) / 2, 12, C_DIM);
            int cost = std::max(1, bal::COST_CAT - g.deck.lens);
            text(F::Mono, std::to_string(cost) + "s", a.x + 390, row.y + (row.height - 13) / 2, 12, C_DIM);
            if (f.read) text(F::MonoBold, "READ", a.x + 440, row.y + (row.height - 13) / 2, 12, C_GREEN);
            if (button(Rectangle{row.x + row.width - 96, by, 90, 28}, f.read ? "OPEN" : "READ", C_CYAN, !r.busy() && !r.over(), false, 12)) {
                if (!f.read) r.read(static_cast<int>(i));
                readFile = static_cast<int>(i);
                handleEvents();
            }
        } else {
            text(F::Mono, std::to_string(f.size) + "MB", a.x + 330, row.y + (row.height - 13) / 2, 12, C_TEXT);
            text(F::Mono, "~" + std::to_string(secondsToDownload(g, f.size)) + "s", a.x + 390, row.y + (row.height - 13) / 2, 12, C_TEXT);
            if (active) {
                progress(Rectangle{a.x + 440, row.y + row.height / 2 - 5, row.width - 440 + row.x - a.x - 110, 10},
                         static_cast<float>(r.actDone) / std::max(1, r.actTotal), kc);
            } else if (f.kind == FileKind::Objective) {
                text(F::MonoBold, "OBJECTIVE", a.x + 440, row.y + (row.height - 13) / 2, 12, C_MAGENTA);
            } else {
                text(F::MonoBold, std::to_string(f.credits) + "cr", a.x + 440, row.y + (row.height - 13) / 2, 12, C_AMBER);
            }
            if (f.taken) text(F::MonoBold, "TAKEN", row.x + row.width - 50, row.y + (row.height - 13) / 2, 12, C_GREEN, Center);
            else if (button(Rectangle{row.x + row.width - 96, by, 90, 28}, "GET", kc, !r.busy() && !r.over(), false, 12)) {
                std::string err;
                if (!r.startDownload(static_cast<int>(i), err)) toast(err, C_AMBER);
                stepClock = 0;
            }
        }
        y += rh;
    }
    if (reader) {
        const File& f = r.t.files[static_cast<size_t>(readFile)];
        Rectangle rd{a.x + 16, a.y + a.height - 240, a.width - 32, 160};
        DrawRectangleRec(rd, Color{4, 10, 16, 240});
        DrawRectangleLinesEx(rd, 1, fade(C_CYAN, 0.6f));
        text(F::MonoBold, f.dir + "/" + f.name, rd.x + 12, rd.y + 10, 12, C_CYAN);
        if (button(Rectangle{rd.x + rd.width - 34, rd.y + 6, 26, 22}, "X", C_DIM, true, false, 11)) readFile = -1;
        para(F::Mono, f.text, rd.x + 12, rd.y + 32, rd.width - 24, 12, C_TEXT, 1.35f, 8);
    }
    float by = a.y + a.height - 62;
    if (r.busy()) {
        bool hold = r.act == RunSession::Act::Hold;
        Color c = hold ? C_AMBER : C_CYAN;
        std::string lbl = hold ? "HOLDING THE LINE " : "DOWNLOADING ";
        text(F::Display, lbl + std::to_string(r.actDone) + "/" + std::to_string(r.actTotal) + "s", a.x + 20, by - 4, 13, c);
        progress(Rectangle{a.x + 20, by + 18, 400, 20}, static_cast<float>(r.actDone) / std::max(1, r.actTotal), c);
        if (button(Rectangle{a.x + 440, by, 144, 44}, hold ? "PULL OUT" : "ABORT", C_RED, true, false, 13) || (modals.empty() && gx::keyPressed(KEY_ESCAPE)))
            r.abortAction();
        handleEvents();
        return;
    }
    if (!r.over()) {
        bool canSteal = g.netUnlocked && r.t.stealable;
        if (button(Rectangle{a.x + 20, by, 250, 44}, "CAPTURE HARDWARE", C_AMBER, canSteal, false, 13)) startCapture();
        std::string out = r.jobDone ? "JACK OUT - KEEP IT" : "JACK OUT";
        if (button(Rectangle{a.x + 284, by, 300, 44}, out, r.jobDone ? C_GREEN : C_CYAN, true, true, 15) || (modals.empty() && gx::keyPressed(KEY_ESCAPE)))
            r.disconnect();
    }
}

// ---------------------------------------------------------------------------
// The reckoning
// ---------------------------------------------------------------------------
void App::finishRun() {
    if (!run) return;
    rkCreditsBefore = g.credits;
    rk = run->settle();
    autosave();
    refreshBoard();
    sfx::play(rk.end == End::TraceOut ? sfx::Lockout : sfx::Coin, 0.7f);
    go(Scr::Reckon);
}

void App::drawReckon() {
    float t = gx::now - scrBorn;
    std::string title, sub;
    Color c = C_CYAN;
    switch (rk.end) {
        case End::TraceOut: title = "TRACE-OUT"; sub = "The night is lost."; c = C_RED; break;
        case End::Sacrifice: title = "ESCAPE BOUGHT WITH HARDWARE"; sub = rk.sacrificed + " took the ticket. You keep the night."; c = C_RED; break;
        case End::Steal: title = "HARDWARE CAPTURED"; sub = rk.captured + " is yours. Nobody rents it to you."; c = C_AMBER; break;
        default: title = "THE RECKONING"; sub = "Clean disconnect."; c = C_CYAN; break;
    }
    if (rk.deepWon) title = "THE DEEP JOB", sub = "Done. Every ticket HELIOS ever opened.", c = C_GREEN;
    glowText(F::Display, run ? run->t.name : "", 640, 36, 14, C_DIM, Center);
    chromaText(F::Heavy, title, 640, 60, title.size() > 18 ? 40 : 56, c, Center, 2.0f);
    text(F::Mono, sub, 640, 132, 16, C_TEXT, Center);

    // Credits counter.
    float k = std::clamp((t - 0.4f) / 1.5f, 0.0f, 1.0f);
    int shown = static_cast<int>(rkCreditsBefore + (rk.credits - rkCreditsBefore) * k);
    Rectangle l{24, 170, 700, 450};
    panel(l, c, "TONIGHT");
    size_t reveal = static_cast<size_t>(std::max(0.0f, (t - 0.3f) / 0.35f));
    float y = l.y + 26;
    for (size_t i = 0; i < rk.lines.size() && i < reveal; ++i) {
        Color lc = toneColor(rk.lines[i].tone);
        DrawRectangle(static_cast<int>(l.x + 20), static_cast<int>(y + 4), 4, 12, lc);
        y += para(F::Mono, rk.lines[i].text, l.x + 34, y, l.width - 60, 14, lc) + 10;
    }
    DrawRectangle(static_cast<int>(l.x + 20), static_cast<int>(l.y + l.height - 70), static_cast<int>(l.width - 40), 1, fade(c, 0.4f));
    text(F::Mono, "CREDITS", l.x + 24, l.y + l.height - 50, 13, C_DIM);
    glowText(F::Display, std::to_string(shown) + " CR", l.x + l.width - 24, l.y + l.height - 56, 30, C_AMBER, Right, 1.2f);

    Rectangle r{740, 170, 516, 450};
    if (rk.deep && !rk.deepWon) {
        const Shortfall& s = rk.shortfall;
        panel(r, C_RED, "THE DEEP JOB - HOW FAR SHORT");
        glowText(F::Heavy, std::to_string(s.pct) + "%", r.x + r.width / 2, r.y + 30, 72, C_RED, Center, 1.3f);
        text(F::Display, "SHORT", r.x + r.width / 2, r.y + 112, 16, fade(C_RED, 0.8f), Center);
        float bx = r.x + r.width / 2 - (s.total * 70 - 10) / 2.0f;
        for (int i = 0; i < s.total; ++i) {
            Rectangle b{bx + i * 70.0f, r.y + 146, 60, 40};
            bool got = i < s.got;
            DrawRectangleRec(b, fade(got ? C_GREEN : C_RED, got ? 0.5f : 0.12f));
            DrawRectangleLinesEx(b, 1.5f, got ? C_GREEN : C_RED);
            text(F::MonoBold, got ? "GOT" : "LEFT", b.x + 30, b.y + 13, 12, got ? C_INK : C_RED, Center);
        }
        float yy = r.y + 204;
        auto line = [&](const std::string& k, const std::string& v) {
            text(F::Mono, k, r.x + 24, yy, 13, C_DIM);
            text(F::MonoBold, v, r.x + r.width - 24, yy, 13, C_TEXT, Right);
            yy += 22;
        };
        line("archives secured", std::to_string(s.got) + " / " + std::to_string(s.total) + "  (" + std::to_string(s.mbLeft) + "MB still there)");
        line("connection held", std::to_string(s.secsHeld) + "s, trace " + std::to_string(s.trace) + "%");
        line("still needed", "~" + std::to_string(s.secsLeft) + "s at " + fixed1(s.speed) + " MB/s");
        if (!s.doorOpened) {
            para(F::Mono, "The door never opened. The maintenance phrase lives somewhere on HELIOS's own facilities.", r.x + 24, yy, r.width - 48, 12, C_AMBER);
            yy += 34;
        }
        auto gear = [&](const std::string& k, float frac, const std::string& v, Color gc) {
            text(F::Mono, k, r.x + 24, yy, 12, C_DIM);
            progress(Rectangle{r.x + 130, yy + 2, 250, 10}, frac, gc, false);
            text(F::MonoBold, v, r.x + r.width - 24, yy, 12, gc, Right);
            yy += 20;
        };
        gear("uplink", s.uplink / 3.0f, std::to_string(s.uplink) + "/3", C_CYAN);
        gear("shroud", s.shroud / 3.0f, std::to_string(s.shroud) + "/3", C_MAGENTA);
        gear("relay", s.relay / static_cast<float>(bal::RELAY_CAP), "+" + std::to_string(s.relay) + "%", C_CYAN);
        gear("proxy", s.proxy / static_cast<float>(bal::PROXY_CAP), "-" + std::to_string(s.proxy) + "%", C_MAGENTA);
        para(F::Mono, "No deck alone finishes this. It was tuned that way. Owned machines close the gap.", r.x + 24, r.y + r.height - 44, r.width - 48, 12, C_DIM);
    } else if (!rk.storyOutro.empty()) {
        panel(r, C_MAGENTA, rk.storyTitle.empty() ? "CONTRACT" : rk.storyTitle);
        float yy = r.y + 30;
        yy += para(F::Mono, flow(rk.storyOutro), r.x + 24, yy, r.width - 48, 15, C_TEXT) + 16;
        if (!rk.infoReward.empty()) para(F::Mono, "Paid in information: " + rk.infoReward, r.x + 24, yy, r.width - 48, 13, C_GREEN);
    } else {
        panel(r, C_PURPLE, "THE RACK");
        float yy = r.y + 30;
        if (g.netUnlocked) {
            text(F::Mono, "machines running", r.x + 24, yy, 13, C_DIM);
            text(F::MonoBold, std::to_string(g.net.activeCount()) + " / " + std::to_string(g.net.slots), r.x + r.width - 24, yy, 13, C_CYAN, Right);
            yy += 24;
            text(F::Mono, "next run costs", r.x + 24, yy, 13, C_DIM);
            text(F::MonoBold, std::to_string(g.net.upkeepTotal()) + "cr", r.x + r.width - 24, yy, 13, C_AMBER, Right);
            yy += 24;
            const Machine* d = g.net.decoy();
            text(F::Mono, "armed to die", r.x + 24, yy, 13, C_DIM);
            text(F::MonoBold, d ? d->name : "nobody", r.x + r.width - 24, yy, 13, d ? C_RED : C_DIM, Right);
            yy += 36;
            for (auto& m : g.net.machines) {
                if (m.home || yy > r.y + r.height - 40) continue;
                text(F::MonoBold, m.name, r.x + 24, yy, 12, jobColor(m.job));
                progress(Rectangle{r.x + 170, yy + 2, 220, 9}, m.condition / 100.0f, mix(C_RED, C_GREEN, m.condition / 100.0f), false);
                text(F::Mono, m.active ? (m.dark() ? "DARK" : "ON") : "STORED", r.x + r.width - 24, yy, 12, m.dark() ? C_RED : C_DIM, Right);
                yy += 20;
            }
        } else {
            para(F::Mono, "No network yet. Just you, a deck, and a rented room. Finish the first contract.", r.x + 24, yy, r.width - 48, 14, C_DIM);
        }
    }
    if (rk.boardRefreshed) text(F::MonoBold, "THE BOARD REFRESHED - old listings are forgotten", 640, 636, 13, C_AMBER, Center);
    bool done = reveal >= rk.lines.size();
    if (button(Rectangle{540, 656, 200, 44}, done ? "CONTINUE" : "SKIP", c, true, done, 16) || (modals.empty() && gx::keyPressed(KEY_ENTER))) {
        if (!done) scrBorn -= 30;
        else leaveReckoning();
    }
}

void App::showNetworkUnlock() {
    Modal m;
    m.cinematic = true;
    m.title = "GHOSTNET";
    m.accent = C_CYAN;
    m.body = "Bad news first: the room's landlord bills by the rack unit now, and somebody has to pay it.\n"
             "Then the good news. Behind the false panel in THE RACK, under SAINT's old courier tags: three slots of salvaged "
             "hardware HELIOS logged as melted down. SAINT never melted anything. One machine is still warm. It boots when you "
             "touch it.\n\n"
             "This is your own server mesh. It is optional infrastructure, and it never replaces your tools. Nothing on it earns "
             "money. Nothing runs while you're away. It only helps you on runs - and it bills you.\n\n"
             "Name your home server:";
    m.input = true;
    m.field = "N0";
    m.buttons = {"BRING IT ONLINE"};
    m.onPick = [this](int, const std::string& f) {
        if (Machine* h = g.net.home()) {
            if (!f.empty() && f != h->name) renameMachine(g, *h, f);
            toast(h->name + " comes online: RELAY, tier 1, free to run, takes one slot.", C_GREEN);
        }
        sfx::play(sfx::Boot);
        flash = 0.5f, flashColor = C_CYAN;
        tab = Tab::Rack;
        selMachine = 0;
        autosave();
    };
    pushModal(m);
}

void App::leaveReckoning() {
    bool ending = g.deepDone && !g.retired;
    if (ending) {
        g.retired = true;
        autosave();
        run.reset();
        go(Scr::Ending);
        return;
    }
    tab = g.netUnlocked ? Tab::Rack : Tab::Board;
    go(Scr::Hub);
    // Modals stack: push in reverse so the first beat shows on top.
    std::vector<std::function<void()>> beats;
    for (auto& card : rk.actCards) {
        auto c = card;
        beats.push_back([this, c] {
            Modal m;
            m.cinematic = true;
            m.title = c.first;
            m.body = flow(c.second);
            m.buttons = {"CONTINUE"};
            m.accent = C_MAGENTA;
            pushModal(m);
        });
    }
    if (rk.unlockedNetwork) beats.push_back([this] { showNetworkUnlock(); });
    for (auto it = beats.rbegin(); it != beats.rend(); ++it) (*it)();
    if (rk.boardRefreshed) toast("The board refreshed. New listings; the old ones are forgotten.", C_AMBER);
    run.reset();
    refreshBoard();
}

// ---------------------------------------------------------------------------
// Practice range
// ---------------------------------------------------------------------------
void App::drawPractice() {
    glowText(F::Display, "PRACTICE RANGE", 640, 40, 30, C_AMBER, Center);
    text(F::Mono, "training hardware - nothing here is real, nothing is billed, no trace", 640, 82, 14, C_DIM, Center);
    const LockType types[3] = {LockType::Keypad, LockType::Rotor, LockType::Cipher};
    for (int i = 0; i < 3; ++i) {
        bool cur = practiceOn && practice.type == types[i];
        if (button(Rectangle{400.0f + i * 166, 112, 150, 40}, lockName(types[i]), C_AMBER, true, cur, 14)) {
            practice = makeLock(types[i], practiceRng, g.deck.breaker);
            practiceOn = true;
            input.clear();
            rowsSeen = 0;
        }
    }
    Rectangle a{290, 170, 700, 470};
    panel(a, C_AMBER, practiceOn ? std::string(lockName(practice.type)) + " LOCK" : "PICK A LOCK");
    if (practiceOn) {
        bool submit = drawLock(Rectangle{a.x + 24, a.y + 24, a.width - 48, a.height - 48}, practice, input, modals.empty(), false);
        if (submit && static_cast<int>(input.size()) == practice.length()) {
            std::string err;
            if (!practice.guess(input, err)) toast(err, C_AMBER);
            else {
                sfx::play(practice.open ? sfx::Granted : practice.sealed() ? sfx::Lockout : sfx::Read);
                if (practice.open) flash = 0.4f, flashColor = C_GREEN;
            }
            input.clear();
        }
        if (practice.open)
            text(F::Mono, "Opened in " + std::to_string(practice.used) + " guess(es).", a.x + a.width / 2, a.y + a.height - 40, 15, C_GREEN, Center);
        if (practice.sealed())
            text(F::Mono, "Sealed. The answer was " + practice.secret + ".", a.x + a.width / 2, a.y + a.height - 40, 15, C_RED, Center);
    } else {
        float y = a.y + 40;
        y += para(F::Mono, "KEYPAD: 4 different digits, judged box by box. GREEN = right digit, right box. AMBER = in the code, another box. Dark = not in the code.", a.x + 30, y, a.width - 60, 15, C_TEXT) + 14;
        y += para(F::Mono, "ROTOR: 4 digits, judged per wheel. GREEN = exact. AMBER = ONE NOTCH AWAY (not misplaced). Dark = further off.", a.x + 30, y, a.width - 60, 15, C_TEXT) + 14;
        para(F::Mono, "CIPHER: a 5-letter word. GREEN = right letter, right place. AMBER = in the word, elsewhere.", a.x + 30, y, a.width - 60, 15, C_TEXT);
    }
    if (button(Rectangle{560, 656, 160, 42}, "BACK", C_DIM) || (modals.empty() && gx::keyPressed(KEY_ESCAPE))) go(practiceFromHub ? Scr::Hub : Scr::Title);
}
