#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

#include "audio.h"
#include "balance.h"
#include "gfx.h"
#include "rlgl.h"
#include "save.h"
#include "text.h"
#include "world.h"

using namespace gx;

// ---------------------------------------------------------------------------
// Shared colour language
// ---------------------------------------------------------------------------
const char* actName(int act) {
    switch (act) {
        case 1: return "ACT ONE - RENTING";
        case 2: return "ACT TWO - OWNING";
        default: return "ACT THREE - THE DEEP JOB";
    }
}

Color jobColor(Job j) {
    switch (j) {
        case Job::Relay: return C_CYAN;
        case Job::Proxy: return C_MAGENTA;
        case Job::Scout: return C_AMBER;
        case Job::Compute: return C_GREEN;
        case Job::Jammer: return C_PURPLE;
        case Job::Vault: return Color{205, 215, 235, 255};   // vault silver
    }
    return C_TEXT;
}

Color kindColor(Kind k) {
    switch (k) {
        case Kind::Open: return C_GREEN;
        case Kind::Hidden: return C_CYAN;
        case Kind::Guarded: return C_AMBER;
        case Kind::Corporate: return C_RED;
        case Kind::Shrine: return C_PURPLE;
        case Kind::Story: return C_MAGENTA;
        case Kind::Side: return C_PURPLE;
    }
    return C_TEXT;
}

Color toneColor(Tone t) {
    switch (t) {
        case Tone::Good: return C_GREEN;
        case Tone::Warn: return C_AMBER;
        case Tone::Bad: return C_RED;
        case Tone::Info: return C_CYAN;
        case Tone::Story: return C_MAGENTA;
        case Tone::Dim: return C_DIM;
        default: return C_TEXT;
    }
}

namespace {
const std::vector<std::string> kTicker = {
    "HELIOS reports 3,214 open tickets across the city tonight",
    "Rack-unit rents up 9% in the Kettle Street district",
    "Courier tags spotted on a building HELIOS says it melted down",
    "Landlords remind tenants: the bill comes due after every run",
    "Noodle stand POS outage blamed on 'routine maintenance'",
    "Ticket #48213 remains OPEN",
    "Server space is real estate. Real estate is power",
    "Weather: rain, then more rain",
};
}  // namespace

// ---------------------------------------------------------------------------
App::App() {
    haveSave = saveExists();
    practiceRng = Rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
}

void App::shutdown() {
    if (g.worldSeed && scr != Scr::Title) autosave();
}

void App::go(Scr s) {
    if (s != scr) sfx::play(sfx::Whoosh, 0.6f);
    scr = s;
    scrBorn = gx::now;
    glitch = std::max(glitch, 0.5f);
}

void App::toast(const std::string& s, Color c) {
    toasts.push_back({s, c, gx::now});
    if (toasts.size() > 4) toasts.erase(toasts.begin());
}

void App::result(const Result& r) {
    toast(r.msg, r.ok ? C_GREEN : C_AMBER);
    if (r.ok) {
        sfx::play(sfx::Tick);
        autosave();
    } else {
        sfx::play(sfx::Deny, 0.7f);
    }
}

void App::autosave() {
    std::string err;
    if (!saveGame(g, &err)) toast("Save failed: " + err, C_RED);
    haveSave = true;
}

void App::confirm(const std::string& title, const std::string& body, const std::string& yes, Color accent, std::function<void()> fn) {
    Modal m;
    m.title = title;
    m.body = body;
    m.buttons = {yes, "CANCEL"};
    m.accent = accent;
    m.onPick = [fn](int c, const std::string&) {
        if (c == 0) fn();
    };
    pushModal(m);
}

void App::newGame(uint64_t seed) {
    g = GameState{};
    g.worldSeed = seed;
    g.credits = bal::START_CREDITS;
    g.mapRunsLeft = bal::RUNS_PER_MAP;
    g.sound = sfx::enabled;
    selMachine = -1;
    tab = Tab::Board;
    go(Scr::Intro);
}

void App::loadOrNew() {
    std::string err;
    GameState loaded;
    if (!loadGame(loaded, &err)) {
        toast("Could not load the save: " + err, C_RED);
        return;
    }
    g = loaded;
    sfx::enabled = g.sound;
    selMachine = -1;
    tab = g.netUnlocked ? Tab::Rack : Tab::Board;
    sfx::play(sfx::Boot);
    go(Scr::Hub);
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------
void App::frame() {
    gx::beginFrame(static_cast<int>(modals.size()));
    float danger = 0;
    if (scr == Scr::Run && run) danger = std::clamp(traceShown / 100.0f, 0.0f, 1.0f);
    if (scr == Scr::Reckon && (rk.end == End::TraceOut || rk.end == End::Sacrifice)) danger = 0.6f;

    glitch = std::max(0.0f, glitch - gx::dt * 1.6f);
    shake = std::max(0.0f, shake - gx::dt * 2.5f);
    flash = std::max(0.0f, flash - gx::dt * 1.8f);

    rlPushMatrix();
    if (shake > 0) {
        float s = shake * 9.0f;
        rlTranslatef(std::sin(gx::now * 91.0f) * s, std::cos(gx::now * 73.0f) * s, 0);
    }
    float bright = scr == Scr::Title ? 1.0f : scr == Scr::Run ? 0.55f : scr == Scr::Ending ? 0.8f : 0.7f;
    city(bright, danger);
    if (scr != Scr::Title && scr != Scr::Ending && scr != Scr::Intro) DrawRectangle(0, 0, W, H, Color{3, 2, 10, 120});

    switch (scr) {
        case Scr::Title: drawTitle(); break;
        case Scr::Intro: drawIntro(); break;
        case Scr::Hub: drawHub(); break;
        case Scr::Brief: drawBrief(); break;
        case Scr::Run: drawRun(); break;
        case Scr::Reckon: drawReckon(); break;
        case Scr::Ending: drawEnding(); break;
        case Scr::Practice: drawPractice(); break;
    }
    if (scr == Scr::Title || scr == Scr::Hub || scr == Scr::Brief) ticker(H - 22, kTicker);
    drawModals();
    drawToasts();
    rlPopMatrix();
    drawOverlayFx();
    crt(glitch);
    sfx::ambience(scr != Scr::Title, danger);
}

void App::drawOverlayFx() {
    if (flash > 0) {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangle(0, 0, W, H, fade(flashColor, flash * 0.35f));
        EndBlendMode();
    }
}

// ---------------------------------------------------------------------------
// Title & intro
// ---------------------------------------------------------------------------
namespace {
const char* kLines[] = {
    "In this city, server space is real estate, and you own none of it.",
    "You rent a room the listings call THE RACK: four walls, one socket, the only safe place you have.",
    "HELIOS keeps the city's machines running. Every time a runner touches one, HELIOS opens a ticket.",
    "The trace meter is just that ticket: how long you've been an open problem.",
    "The room used to belong to a courier called SAINT. Nobody says what happened to SAINT.",
};
}

void App::drawTitle() {
    float t = gx::now - scrBorn;
    if (titleHelp) {
        panel(Rectangle{60, 40, 1160, 610}, C_CYAN, "HOW TO PLAY");
        drawHelp(Rectangle{80, 64, 1120, 520});
        if (button(Rectangle{560, 596, 160, 40}, "BACK", C_CYAN) || (modals.empty() && gx::keyPressed(KEY_ESCAPE))) titleHelp = false;
        return;
    }
    float split = 2.0f + (std::fmod(gx::now, 3.7f) < 0.15f ? 8.0f : 0.0f) + glitch * 10;
    float y = 120 + std::sin(t * 0.8f) * 3;
    chromaText(F::Heavy, "GHOSTNET", 640, y, 128, WHITE, Center, split);
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRectangleGradientH(240, static_cast<int>(y + 140), 400, 2, fade(C_CYAN, 0), C_CYAN);
    DrawRectangleGradientH(640, static_cast<int>(y + 140), 400, 2, C_CYAN, fade(C_CYAN, 0));
    EndBlendMode();
    glowText(F::Display, "STOP RENTING.  START OWNING.", 640, y + 160, 20, C_MAGENTA, Center, 1.2f);

    float bx = 520, by = 380, bw = 240, bh = 44;
    int idx = 0;
    auto item = [&](const std::string& label, Color c, bool primary) {
        Rectangle r{bx, by + idx * 56.0f, bw, bh};
        ++idx;
        return button(r, label, c, true, primary, 17);
    };
    if (haveSave && item("CONTINUE", C_CYAN, true)) loadOrNew();
    if (item("NEW GAME", haveSave ? C_MAGENTA : C_CYAN, !haveSave)) {
        auto start = [this] {
            uint64_t s = (static_cast<uint64_t>(std::random_device{}()) << 32) ^
                         static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
            newGame(s);
        };
        if (haveSave)
            confirm("NEW GAME", "This overwrites your current save. The city forgets you, SAINT's rack included.", "OVERWRITE", C_RED, start);
        else
            start();
    }
    if (item("PRACTICE RANGE", C_AMBER, false)) {
        practiceOn = false;
        practiceFromHub = false;
        go(Scr::Practice);
    }
    if (item("HOW TO PLAY", C_PURPLE, false)) titleHelp = true;
    if (item("QUIT", C_DIM, false)) quit_ = true;

    bool blink = std::fmod(gx::now, 1.2f) < 0.8f;
    text(F::MonoBold, "HELIOS TICKET #48213", 30, H - 70, 14, fade(C_RED, blink ? 1.0f : 0.3f));
    text(F::Mono, "STATUS: OPEN  //  ASSIGNED: NOBODY  //  PRIORITY: RISING", 30, H - 50, 13, C_DIM);
    text(F::Mono, std::string("v") + GHOSTNET_VERSION, W - 30, H - 50, 13, C_DIM, Right);
    text(F::Mono, "sound: " + std::string(sfx::enabled ? "on" : "off") + "  [M]", W - 30, H - 70, 13, C_DIM, Right);
    if (modals.empty() && gx::keyPressed(KEY_M)) sfx::enabled = !sfx::enabled;
}

void App::drawIntro() {
    float t = gx::now - scrBorn;
    glowText(F::Display, "ACT ONE", 640, 110, 22, C_MAGENTA, Center);
    chromaText(F::Heavy, "RENTING", 640, 140, 72, WHITE, Center, 2.5f);
    shade(250, 380);
    float y = 270;
    float chars = t * 70.0f;
    for (const char* line : kLines) {
        std::string s = line;
        int show = static_cast<int>(std::min<float>(chars, static_cast<float>(s.size())));
        chars -= static_cast<float>(s.size()) + 25;
        if (show <= 0) break;
        std::string part = s.substr(0, static_cast<size_t>(show));
        float h = para(F::Mono, part, 240, y, 800, 18, C_TEXT);
        y += std::max(h, 24.0f) + 14;
    }
    text(F::Mono, "Tip: open the JOB BOARD and look for the CONTRACT. Try the practice range if locks are new to you.", 640, 600, 14,
         C_DIM, Center);
    if (button(Rectangle{540, 630, 200, 46}, t > 3 ? "BEGIN" : "SKIP", C_CYAN, true, t > 3, 18) ||
        (modals.empty() && gx::keyPressed(KEY_ENTER))) {
        autosave();
        sfx::play(sfx::Boot);
        tab = Tab::Board;
        go(Scr::Hub);
    }
}

// ---------------------------------------------------------------------------
// Ending
// ---------------------------------------------------------------------------
void App::drawEnding() {
    float t = gx::now - scrBorn;
    bool owner = g.deepDone;
    std::vector<std::string> lines;
    std::string title, final;
    if (owner) {
        title = "AN OWNER";
        lines = {"You stop taking listings. Nobody tells you to; there's just nothing left you need to take.",
                 std::to_string(g.net.activeCount()) + " machine(s) hum in a room you used to rent. " +
                     (g.net.home() ? g.net.home()->name : std::string("N0")) + " is still on, the way SAINT left it.",
                 "HELIOS never closes ticket #48213. It just stops mattering."};
        final = "The rack goes quiet. The machines stay on.";
    } else {
        title = "A RUNNER WHO KEPT RENTING";
        lines = {"You hand back the key to THE RACK. Somebody else will rent it next month.",
                 "HELIOS files you the way it files most runners: a closed ticket, a good night or two, nothing owned.",
                 "The deep archive is still out there. It was always going to need more than a deck."};
        final = "Filed: a runner who kept renting.";
    }
    glowText(F::Display, "ENDING", 640, 90, 20, owner ? C_CYAN : C_AMBER, Center);
    chromaText(F::Heavy, title, 640, 118, owner ? 64 : 44, WHITE, Center, 2.0f);
    shade(230, 290);
    float y = 250;
    for (size_t i = 0; i < lines.size(); ++i) {
        float a = std::clamp((t - 1.0f - i * 1.6f) / 1.0f, 0.0f, 1.0f);
        y += para(F::Mono, lines[i], 260, y, 760, 19, fade(C_TEXT, a)) + 16;
    }
    float a = std::clamp((t - 6.0f) / 1.2f, 0.0f, 1.0f);
    glowText(F::Display, final, 640, y + 20, 26, fade(owner ? C_CYAN : C_AMBER, a), Center, 1.4f);
    const Stats& s = g.stats;
    std::string st = std::to_string(s.runs) + " runs   " + std::to_string(s.jobs) + " contracts   " + std::to_string(s.captures) +
                     " captures   " + std::to_string(s.traceouts) + " trace-outs   " + std::to_string(g.net.burnedCount) +
                     " machine(s) given to HELIOS in your place   " + std::to_string(s.earned) + "cr earned";
    text(F::Mono, st, 640, y + 80, 14, fade(C_DIM, a), Center);
    text(F::Mono, "Your save stays. Continue any time - the city doesn't care that you're done.", 640, y + 104, 13, fade(C_DIM, a), Center);
    if (t > 2.0f) {
        if (button(Rectangle{450, 620, 180, 44}, "THE RACK", C_CYAN, true, true)) go(Scr::Hub);
        if (button(Rectangle{650, 620, 180, 44}, "TITLE", C_DIM)) {
            haveSave = saveExists();
            go(Scr::Title);
        }
    }
}

// ---------------------------------------------------------------------------
// Modals & toasts
// ---------------------------------------------------------------------------
void App::drawModals() {
    for (size_t i = 0; i < modals.size(); ++i) {
        gx::layer = static_cast<int>(i) + 1;
        Modal& m = modals[i];
        DrawRectangle(0, 0, W, H, Color{2, 1, 8, static_cast<unsigned char>(m.cinematic ? 215 : 170)});
        float w = m.cinematic ? 860.0f : 600.0f;
        float bodySize = m.cinematic ? 18.0f : 15.0f;
        auto lines = wrap(F::Mono, m.body, w - 70, bodySize);
        float bodyH = lines.size() * bodySize * 1.4f;
        bool chips = m.input && !m.chips.empty();
        float h = 70 + (m.cinematic ? 40 : 0) + bodyH + (m.input ? 64 : 0) + (chips ? 78 : 0) + 80;
        Rectangle r{(W - w) / 2, (H - h) / 2, w, h};
        panel(r, m.accent, "", 0.97f);
        float y = r.y + 26;
        if (m.cinematic) {
            chromaText(F::Display, m.title, r.x + w / 2, y, 30, WHITE, Center, 1.5f);
            y += 60;
        } else {
            glowText(F::Display, m.title, r.x + 34, y, 20, m.accent);
            y += 44;
        }
        for (auto& l : lines) {
            text(F::Mono, l, r.x + 35, y, bodySize, C_TEXT);
            y += bodySize * 1.4f;
        }
        bool enter = false;
        if (m.input) {
            y += 12;
            Rectangle f{r.x + 35, y, w - 70, 40};
            DrawRectangleRec(f, fade(m.accent, 0.08f));
            glowRect(f, m.accent, 1.5f, 2);
            bool cursor = std::fmod(gx::now, 1.0f) < 0.55f;
            text(F::MonoBold, m.field + (cursor ? "_" : ""), f.x + 12, f.y + 10, 18, WHITE);
            enter = textInput(m.field, 14, 3);
            y += 52;
        } else if (gx::layer == gx::topLayer) {
            enter = gx::keyPressed(KEY_ENTER) || gx::keyPressed(KEY_KP_ENTER);
        }
        int picked = -1;
        float gap = 14, bh = 42;
        float bw = std::min(180.0f, (w - 68 - gap * (m.buttons.size() - 1)) / m.buttons.size());
        float bx = r.x + w - 34 - m.buttons.size() * (bw + gap) + gap;
        for (size_t b = 0; b < m.buttons.size(); ++b) {
            Color c = b == 0 ? m.accent : C_DIM;
            if (button(Rectangle{bx + b * (bw + gap), r.y + h - 66, bw, bh}, m.buttons[b], c, true, b == 0)) picked = static_cast<int>(b);
        }
        if (chips) {
            // Starter phrases: three at a time, rotating every few seconds.
            const int per = 3;
            int n = static_cast<int>(m.chips.size());
            int pages = (n + per - 1) / per;
            int page = (static_cast<int>(gx::now / 4.0f) + m.chipShift) % std::max(1, pages);
            float cy = y + 6;
            text(F::Mono, m.chipNote.empty() ? "TRY ONE OF THESE" : m.chipNote, r.x + 35, cy, 11, C_DIM);
            float tick = std::fmod(gx::now, 4.0f) / 4.0f;   // a thin timer bar until the next rotation
            DrawRectangle(static_cast<int>(r.x + 35), static_cast<int>(cy + 16), static_cast<int>((w - 70) * (1 - tick)), 1, fade(m.accent, 0.5f));
            float cw = (w - 70 - 90 - 3 * 10) / 3;
            for (int k = 0; k < per; ++k) {
                int idx = page * per + k;
                if (idx >= n) break;
                const std::string& s = m.chips[static_cast<size_t>(idx)];
                if (button(Rectangle{r.x + 35 + k * (cw + 10), cy + 24, cw, 34}, s, m.accent, true, s == txt::upper(m.field), 12)) m.field = s;
            }
            if (button(Rectangle{r.x + w - 35 - 90, cy + 24, 90, 34}, "MORE >", C_DIM, pages > 1, false, 11)) m.chipShift += 1;
        }
        if (gx::layer == gx::topLayer) {
            if (enter && picked < 0) picked = 0;
            if (gx::keyPressed(KEY_ESCAPE) && m.buttons.size() > 1 && picked < 0) picked = static_cast<int>(m.buttons.size()) - 1;
        }
        if (picked >= 0) {
            Modal copy = m;
            modals.erase(modals.begin() + static_cast<long>(i));
            if (copy.onPick) copy.onPick(picked, copy.field);
            break;
        }
    }
    gx::layer = 0;
}

void App::drawToasts() {
    float y = H - 70;
    for (int i = static_cast<int>(toasts.size()) - 1; i >= 0; --i) {
        Toast& t = toasts[static_cast<size_t>(i)];
        float age = gx::now - t.born;
        if (age > 4.0f) continue;
        float a = std::min(1.0f, std::min(age * 6, (4.0f - age) * 2));
        auto lines = wrap(F::Mono, t.text, 640, 14);
        float h = 16 + lines.size() * 19.0f;
        float w = 0;
        for (auto& l : lines) w = std::max(w, textWidth(F::Mono, l, 14));
        Rectangle r{(W - w) / 2 - 18, y - h, w + 36, h};
        DrawRectangleRec(r, fade(Color{6, 6, 20, 255}, 0.94f * a));
        DrawRectangleRec(Rectangle{r.x, r.y, 3, r.height}, fade(t.color, a));
        DrawRectangleLinesEx(r, 1, fade(t.color, 0.6f * a));
        for (size_t k = 0; k < lines.size(); ++k) text(F::Mono, lines[k], r.x + 18, r.y + 8 + k * 19, 14, fade(C_TEXT, a));
        y -= h + 8;
    }
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return gx::now - t.born > 4.0f; }), toasts.end());
}
