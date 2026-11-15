// THE RACK and everything you do between runs: the network, the board, the deck,
// the advisor panel, records, help, and the target briefing.
#include <algorithm>
#include <cmath>

#include "app.h"
#include "audio.h"
#include "balance.h"
#include "gfx.h"
#include "rules.h"
#include "save.h"
#include "world.h"

using namespace gx;

namespace {
std::string cr(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return (v < 0 ? "-" : "") + s + " CR";
}

const char* jobLetter(Job j) {
    switch (j) {
        case Job::Relay: return "R";
        case Job::Proxy: return "P";
        case Job::Scout: return "S";
        case Job::Compute: return "C";
    }
    return "?";
}

const char* jobPitch(Job j) {
    switch (j) {
        case Job::Relay: return "download speed";
        case Job::Proxy: return "slower trace, can be the decoy";
        case Job::Scout: return "scouting reports (charges)";
        case Job::Compute: return "one emergency guess per lock";
    }
    return "";
}

Color voteColor(Vote v) { return v == Vote::Go ? C_GREEN : v == Vote::Caution ? C_AMBER : C_RED; }
Color advisorColor(Advisor a) { return a == Advisor::Vesper ? C_RED : a == Advisor::Ledger ? C_AMBER : C_CYAN; }

// A little animated face for each advisor AI.
void advisorPortrait(Vector2 c, float r, Advisor a) {
    Color col = advisorColor(a);
    hexNode(c, r, col, 0.10f, 0);
    float look = std::sin(gx::now * 0.7f + static_cast<int>(a) * 2.0f) * r * 0.18f;
    bool blink = std::fmod(gx::now + static_cast<int>(a) * 1.3f, 4.5f) < 0.12f;
    if (a == Advisor::Vesper) {
        DrawTriangleLines(Vector2{c.x, c.y - r * 0.5f}, Vector2{c.x - r * 0.55f, c.y + r * 0.4f}, Vector2{c.x + r * 0.55f, c.y + r * 0.4f}, col);
        if (!blink) glowDot(Vector2{c.x + look, c.y + r * 0.08f}, r * 0.12f, col);
    } else if (a == Advisor::Ledger) {
        DrawRectangleLinesEx(Rectangle{c.x - r * 0.45f, c.y - r * 0.3f, r * 0.9f, r * 0.6f}, 2, col);
        for (int i = 0; i < 3; ++i)
            DrawRectangleRec(Rectangle{c.x - r * 0.35f + i * r * 0.28f, c.y + r * 0.15f - (i + 1) * r * 0.12f, r * 0.16f, (i + 1) * r * 0.12f}, fade(col, 0.8f));
        if (!blink) glowDot(Vector2{c.x + look, c.y - r * 0.55f}, r * 0.08f, col);
    } else {
        DrawCircleLinesV(Vector2{c.x - r * 0.28f, c.y}, r * 0.26f, col);
        DrawCircleLinesV(Vector2{c.x + r * 0.28f, c.y}, r * 0.26f, col);
        if (!blink) {
            glowDot(Vector2{c.x - r * 0.28f + look * 0.5f, c.y}, r * 0.08f, col);
            glowDot(Vector2{c.x + r * 0.28f + look * 0.5f, c.y}, r * 0.08f, col);
        }
    }
}

struct HelpSection { const char* head; const char* body; };
const HelpSection kHelp[] = {
    {"THE LOOP", "THE RACK -> job board -> connection -> the reckoning. Pick a listing, connect, open the door, take the files, and "
                 "disconnect before the TRACE fills. The trace is HELIOS's ticket on you. Every action costs time: guessing, reading, "
                 "and downloading (which also runs the trace 25% faster)."},
    {"TRACE-OUT", "Accounts seized: the night's haul is lost, a fine comes out of what you have (never more than you have), the bill "
                  "still comes due and your machines wear hard."},
    {"LOCKS", "Limited guesses with feedback. Type on the keyboard and press Enter. GREEN = right in that spot. AMBER = keypad/cipher: "
              "right but elsewhere; ROTOR: one notch away. A known maintenance PASSPHRASE opens a door without breaking it."},
    {"THE DECK", "Tools you buy for yourself: BREAKER (more attempts), UPLINK (faster downloads), SHROUD (quieter: slower trace, "
                 "smaller fines), LENS (faster reading). Permanent, never billed, separate from the network."},
    {"GHOSTNET", "After your first contract: SAINT's salvaged rack and your home server N0. RELAY speeds downloads, PROXY slows the "
                 "trace (and can be the decoy), SCOUT writes reports, COMPUTE holds one emergency guess. Configure it only between runs."},
    {"THE BILL", "Every running machine except N0 bills you after any run that does something, then wears. Can't pay? Nothing is "
                 "taken: everything but N0 goes dark and the decoy is disarmed. No debt. Earn credits, switch them back on."},
    {"THE DECOY", "Arm a running PROXY as the decoy. If the trace fills, HELIOS traces it instead of you: it is destroyed, the fine is "
                  "halved, and you keep the night."},
    {"CAPTURE", "With a scout running, listings marked HW carry detachable hardware. Inside, CAPTURE HARDWARE shows a quote. Take "
                "files first and it arrives more damaged. Corporate iron is tier 3."},
    {"THE DEEP JOB", "A HELIOS archive tuned so no deck alone can finish it. You can try it the moment it appears; if you fall short "
                     "the game tells you exactly how far."},
    {"THE PANEL", "Three advisor AIs - VESPER (risk), LEDGER (money), MOTH (intel) - read your meters and vote before a run. They "
                  "never solve a lock for you. No idle income, no timers, no raids: nothing happens while you're away."},
};
}  // namespace

// ---------------------------------------------------------------------------
void App::drawTopBar() {
    DrawRectangle(0, 0, W, 58, Color{4, 3, 14, 235});
    DrawRectangleGradientH(0, 57, W, 1, fade(C_MAGENTA, 0.0f), C_MAGENTA);
    DrawRectangleGradientH(W / 2, 57, W / 2, 1, C_MAGENTA, fade(C_CYAN, 0.6f));
    glowText(F::Display, "GHOSTNET", 24, 15, 24, C_CYAN, Left, 1.2f);
    text(F::Display, actName(g.act), 210, 22, 12, C_MAGENTA);
    std::string listing = "BOARD " + std::to_string(g.mapIndex + 1) + "  //  " + std::to_string(g.mapRunsLeft) + " RUN(S) TO REFRESH";
    text(F::Mono, listing, 470, 22, 13, C_DIM);
    glowText(F::Display, cr(g.credits), W - 24, 15, 24, C_AMBER, Right, 1.0f);
}

void App::drawHub() {
    drawTopBar();
    struct T { Tab t; const char* label; };
    const T tabs[] = {{Tab::Rack, "THE RACK"}, {Tab::Board, "JOB BOARD"}, {Tab::Deck, "THE DECK"},
                      {Tab::Panel, "THE PANEL"}, {Tab::Records, "RECORDS"}, {Tab::Help, "HOW TO PLAY"}};
    for (int i = 0; i < 6; ++i) {
        Rectangle r{24.0f + i * 156, 70, 146, 34};
        bool on = tab == tabs[i].t;
        if (button(r, tabs[i].label, on ? C_CYAN : C_DIM, true, on, 13)) {
            tab = tabs[i].t;
            if (tab == Tab::Board) refreshBoard();
        }
        if (on) DrawRectangle(static_cast<int>(r.x), static_cast<int>(r.y + r.height + 3), static_cast<int>(r.width), 2, C_CYAN);
    }
    if (modals.empty() && !gx::keyDown(KEY_LEFT_CONTROL))
        for (int i = 0; i < 6; ++i)
            if (gx::keyPressed(KEY_ONE + i)) {
                tab = tabs[i].t;
                if (tab == Tab::Board) refreshBoard();
            }
    if (button(Rectangle{1000, 70, 120, 34}, sfx::enabled ? "SOUND ON" : "SOUND OFF", C_DIM, true, false, 12)) {
        sfx::enabled = !sfx::enabled;
        g.sound = sfx::enabled;
        autosave();
    }
    if (button(Rectangle{1132, 70, 124, 34}, "MENU", C_DIM, true, false, 12)) {
        Modal m;
        m.title = "THE RACK";
        m.body = g.deepDone ? "The deep job is done. You can retire as an owner, or keep running - the city doesn't mind."
                            : "Save and step out, or walk away for good. Walking away without the deep job files you as a runner "
                              "who kept renting. (Your save stays either way.)";
        m.buttons = {"SAVE & QUIT", "PRACTICE RANGE", g.deepDone ? "RETIRE" : "WALK AWAY", "CANCEL"};
        m.accent = C_CYAN;
        m.onPick = [this](int c, const std::string&) {
            if (c == 0) {
                autosave();
                haveSave = true;
                go(Scr::Title);
            } else if (c == 1) {
                practiceOn = false;
                practiceFromHub = true;
                go(Scr::Practice);
            } else if (c == 2) {
                auto end = [this] {
                    g.retired = true;
                    autosave();
                    go(Scr::Ending);
                };
                if (g.deepDone) end();
                else confirm("WALK AWAY", "Walk away without the deep job? You'll be filed as a runner who kept renting.", "WALK AWAY", C_AMBER, end);
            }
        };
        pushModal(m);
    }

    Rectangle area{24, 122, 1232, 566};
    switch (tab) {
        case Tab::Rack: g.netUnlocked ? drawRack(area) : drawLockedRack(area); break;
        case Tab::Board: drawBoard(area); break;
        case Tab::Deck: drawDeck(area); break;
        case Tab::Panel: drawPanelTab(area); break;
        case Tab::Records: drawRecords(area); break;
        case Tab::Help:
            panel(area, C_PURPLE, "HOW TO PLAY");
            drawHelp(Rectangle{area.x + 20, area.y + 24, area.width - 40, area.height - 40});
            break;
    }
}

// ---------------------------------------------------------------------------
// The rack: a live graph of your machines
// ---------------------------------------------------------------------------
void App::drawLockedRack(Rectangle area) {
    panel(area, C_MAGENTA, "THE RACK");
    // The rented room: an empty rack frame behind a false panel.
    Rectangle frame{area.x + 80, area.y + 60, 300, 440};
    DrawRectangleRec(frame, Color{10, 8, 20, 255});
    DrawRectangleLinesEx(frame, 3, Color{60, 50, 90, 255});
    for (int i = 0; i < 3; ++i) {
        Rectangle bay{frame.x + 24, frame.y + 40 + i * 130.0f, frame.width - 48, 100};
        DrawRectangleRec(bay, Color{16, 12, 30, 255});
        for (float x = bay.x; x < bay.x + bay.width - 10; x += 22)
            DrawTriangle(Vector2{x, bay.y + bay.height}, Vector2{x + 11, bay.y + bay.height}, Vector2{x + 22, bay.y + bay.height - 10},
                         fade(C_AMBER, 0.25f));
        bool blink = std::fmod(gx::now + i * 0.7f, 2.4f) < 0.1f;
        DrawCircle(static_cast<int>(bay.x + bay.width - 16), static_cast<int>(bay.y + 14), 3, blink ? C_GREEN : Color{30, 40, 40, 255});
        text(F::Mono, "BAY " + std::to_string(i + 1) + "  //  NO SIGNAL", bay.x + 12, bay.y + 12, 13, C_DIM);
    }
    // SAINT's tags, sprayed over the panel.
    glowText(F::Heavy, "SAINT", frame.x + frame.width / 2, frame.y + 190, 48, fade(C_MAGENTA, 0.55f), Center, 1.0f);
    text(F::Display, "THE RACK", area.x + 440, area.y + 60, 30, WHITE);
    float y = area.y + 110;
    y += para(F::Mono,
              "A rented room the listings call THE RACK: four walls, one socket, the only safe place you have. The rack frame "
              "in the corner is empty. There's a false panel behind it with old courier tags sprayed across it - SAINT's.",
              area.x + 440, y, 720, 16, C_TEXT) + 16;
    y += para(F::Mono, "Finish your first CONTRACT. Then see what's behind the panel.", area.x + 440, y, 720, 16, C_MAGENTA) + 30;
    text(F::Display, "YOUR DECK", area.x + 440, y, 16, C_CYAN);
    y += 30;
    const char* names[4] = {"BREAKER", "UPLINK", "SHROUD", "LENS"};
    int lv[4] = {g.deck.breaker, g.deck.uplink, g.deck.shroud, g.deck.lens};
    int mx[4] = {bal::DECK_MAX, bal::DECK_MAX, bal::DECK_MAX, bal::LENS_MAX};
    for (int i = 0; i < 4; ++i) {
        text(F::Mono, names[i], area.x + 440, y + i * 26, 14, C_TEXT);
        pips(area.x + 540, y + i * 26 + 3, lv[i], mx[i], C_CYAN);
    }
    if (button(Rectangle{area.x + 440, area.y + area.height - 80, 260, 46}, "OPEN THE JOB BOARD", C_MAGENTA, true, true, 15)) {
        tab = Tab::Board;
        refreshBoard();
    }
}

void App::drawRack(Rectangle area) {
    Rectangle left{area.x, area.y, 780, area.height};
    Rectangle right{area.x + 796, area.y, area.width - 796, area.height};
    panel(left, C_CYAN, "GHOSTNET // YOUR SERVER MESH");
    Network& n = g.net;

    Vector2 c{left.x + 390, left.y + 245};
    float rx = 250, ry = 155;
    std::vector<Machine*> active, stored;
    for (auto& m : n.machines) {
        if (m.home) continue;
        (m.active ? active : stored).push_back(&m);
    }
    int ring = std::max(1, n.slots - 1);
    std::vector<Vector2> pos(static_cast<size_t>(ring));
    for (int i = 0; i < ring; ++i) {
        float a = -1.5708f + 6.2832f * i / ring + 0.3f;
        pos[static_cast<size_t>(i)] = Vector2{c.x + std::cos(a) * rx, c.y + std::sin(a) * ry};
    }
    // The city uplink.
    Vector2 up{left.x + 60, left.y + 50};
    text(F::Mono, "CITY GRID", up.x + 14, up.y - 7, 12, C_DIM);
    glowDot(up, 4, C_PURPLE);
    glowLine(up, c, fade(C_PURPLE, 0.35f), 1);
    tracer(up, c, gx::now * 0.4f, C_PURPLE, 2);
    // Slot ring.
    for (int i = 0; i < ring; ++i) {
        Vector2 p = pos[static_cast<size_t>(i)];
        if (i < static_cast<int>(active.size())) {
            Machine& m = *active[static_cast<size_t>(i)];
            Color col = m.dark() ? C_DARK : jobColor(m.job);
            glowLine(c, p, fade(col, 0.45f), 1.4f);
            if (!m.dark()) tracer(c, p, gx::now * (0.5f + 0.1f * m.tier) + i * 0.17f, col, 3);
        } else {
            for (float t = 0; t < 1; t += 0.06f) {
                Vector2 a{c.x + (p.x - c.x) * t, c.y + (p.y - c.y) * t};
                Vector2 b{c.x + (p.x - c.x) * (t + 0.03f), c.y + (p.y - c.y) * (t + 0.03f)};
                DrawLineEx(a, b, 1, fade(C_DIM, 0.4f));
            }
            DrawPolyLinesEx(p, 6, 26, 30, 1, fade(C_DIM, 0.5f));
            text(F::Mono, "EMPTY SLOT", p.x, p.y + 34, 12, fade(C_DIM, 0.7f), Center);
        }
    }
    auto drawMachine = [&](Machine& m, Vector2 p, float r) {
        Color col = m.dark() ? C_DARK : jobColor(m.job);
        bool sel = selMachine == m.id;
        if (m.home) {
            DrawRing(p, r + 10, r + 13, 0, 360, 48, fade(C_AMBER, 0.35f));
            float a = gx::now * 40;
            DrawRing(p, r + 16, r + 18, a, a + 60, 16, fade(C_AMBER, 0.8f));
            DrawRing(p, r + 16, r + 18, a + 180, a + 240, 16, fade(C_AMBER, 0.8f));
        }
        hexNode(p, r, col, m.dark() ? 0.05f : 0.16f);
        glowText(F::Display, m.dark() ? "X" : jobLetter(m.job), p.x, p.y - 12, 22, m.dark() ? C_DIM : WHITE, Center, 0.8f);
        text(F::MonoBold, "T" + std::to_string(m.tier), p.x, p.y + 10, 12, fade(col, 0.95f), Center);
        if (!m.home) {
            float cond = m.condition / 100.0f;
            DrawRing(p, r + 5, r + 8, -90, -90 + 360 * cond, 48, mix(C_RED, C_GREEN, cond));
        }
        text(F::MonoBold, m.name, p.x, p.y + r + 12, 13, sel ? WHITE : C_TEXT, Center);
        std::string sub = m.home ? "HOME  //  " + std::string(jobName(m.job)) : m.dark() ? "DARK" : std::string(jobName(m.job));
        text(F::Mono, sub, p.x, p.y + r + 28, 11, m.dark() ? C_RED : fade(col, 0.9f), Center);
        if (n.decoyId == m.id) {
            float a = gx::now * 90;
            for (int k = 0; k < 4; ++k) DrawRing(p, r + 14, r + 17, a + k * 90, a + k * 90 + 50, 12, C_RED);
            glowText(F::Display, "ARMED TO DIE", p.x, p.y - r - 34, 11, pulse(C_RED, 5), Center);
        }
        if (sel) brackets(Rectangle{p.x - r - 22, p.y - r - 22, 2 * r + 44, 2 * r + 76}, pulse(WHITE, 4, 0.5f), 12, 2);
        if (hitArea(Rectangle{p.x - r, p.y - r, 2 * r, 2 * r + 36}, m.name)) {
            selMachine = m.id;
            sfx::play(sfx::Click);
        }
    };
    for (size_t i = 0; i < active.size() && i < pos.size(); ++i) drawMachine(*active[i], pos[i], 28);
    if (Machine* h = n.home()) drawMachine(*h, c, 38);

    // Cold storage tray.
    float ty = left.y + left.height - 112;
    DrawRectangleGradientV(static_cast<int>(left.x + 1), static_cast<int>(ty - 10), static_cast<int>(left.width - 2), 2, fade(C_CYAN, 0.3f), fade(C_CYAN, 0));
    text(F::Display, "COLD STORAGE", left.x + 20, ty, 12, C_DIM);
    text(F::Mono, "free, no wear, counts toward the 10-machine limit", left.x + 150, ty + 1, 12, fade(C_DIM, 0.8f));
    if (stored.empty()) text(F::Mono, "(empty)", left.x + 20, ty + 34, 13, fade(C_DIM, 0.7f));
    for (size_t i = 0; i < stored.size() && i < 6; ++i) {
        Machine& m = *stored[i];
        Rectangle r{left.x + 20 + i * 124.0f, ty + 24, 114, 64};
        Color col = m.dark() ? C_DARK : jobColor(m.job);
        bool sel = selMachine == m.id;
        DrawRectangleRec(r, fade(col, sel ? 0.22f : 0.08f));
        DrawRectangleLinesEx(r, sel ? 2.0f : 1.0f, fade(col, 0.8f));
        text(F::MonoBold, m.name, r.x + 8, r.y + 8, 13, C_TEXT);
        text(F::Mono, std::string(jobName(m.job)) + " T" + std::to_string(m.tier), r.x + 8, r.y + 26, 11, col);
        progress(Rectangle{r.x + 8, r.y + 46, r.width - 16, 6}, m.condition / 100.0f, mix(C_RED, C_GREEN, m.condition / 100.0f), false);
        if (hitArea(r, m.name)) {
            selMachine = m.id;
            sfx::play(sfx::Click);
        }
    }
    if (stored.size() > 6) text(F::Mono, "+" + std::to_string(stored.size() - 6) + " more", left.x + 760, ty + 50, 13, C_DIM, Right);
    text(F::Mono, "click a machine to manage it", left.x + left.width - 16, left.y + 14, 12, fade(C_DIM, 0.8f), Right);
    if (hitArea(left, "mesh")) selMachine = -1;

    // Right column: what the doc asks the rack screen to show at a glance.
    panel(right, C_MAGENTA, "STATUS");
    int worth = 0;
    for (auto& m : n.machines) worth += scrapValue(m);
    const Machine* decoy = n.decoy();
    struct Row { const char* k; std::string v; Color c; };
    Row rows[4] = {{"MACHINES RUNNING", std::to_string(n.activeCount()) + " / " + std::to_string(n.slots) + " slots", C_CYAN},
                   {"WORTH (SCRAP)", cr(worth), C_AMBER},
                   {"NEXT RUN COSTS", cr(n.upkeepTotal()), n.upkeepTotal() > g.credits ? C_RED : C_TEXT},
                   {"ARMED TO DIE", decoy ? decoy->name : std::string("nobody"), decoy ? C_RED : C_DIM}};
    for (int i = 0; i < 4; ++i) {
        float y = right.y + 22 + i * 30;
        text(F::Mono, rows[i].k, right.x + 20, y, 13, C_DIM);
        text(F::MonoBold, rows[i].v, right.x + right.width - 20, y, 14, rows[i].c, Right);
    }
    std::string fx = "DL +" + std::to_string(n.relayBonus()) + "%   TRACE -" + std::to_string(n.proxyBonus()) + "%   SCOUT " +
                     std::to_string(n.usableScoutCharges()) + "   EMERG " + std::to_string(n.computeCapacity());
    text(F::Mono, fx, right.x + 20, right.y + 146, 12, fade(C_CYAN, 0.9f));
    DrawRectangle(static_cast<int>(right.x + 16), static_cast<int>(right.y + 168), static_cast<int>(right.width - 32), 1, fade(C_MAGENTA, 0.4f));
    Rectangle body{right.x + 16, right.y + 180, right.width - 32, right.height - 192};
    Machine* sel = n.find(selMachine);
    if (sel) drawMachinePanel(body, *sel);
    else {
        selMachine = -1;
        drawShop(body);
    }
}

void App::drawMachinePanel(Rectangle a, Machine& m) {
    Color col = m.dark() ? C_DIM : jobColor(m.job);
    glowText(F::Display, m.name, a.x + 4, a.y, 22, col, Left, 0.9f);
    std::string kind = std::string(jobName(m.job)) + "  //  TIER " + std::to_string(m.tier) + "  //  " +
                       (m.home ? "HOME" : m.captured ? "CAPTURED" : "BOUGHT") + (m.active ? "" : "  //  STORED");
    text(F::Mono, kind, a.x + 4, a.y + 30, 12, C_TEXT);
    float y = a.y + 50;
    y += para(F::Mono, m.origin, a.x + 4, y, a.width - 8, 12, C_DIM, 1.3f, 2) + 6;
    if (!m.home) {
        text(F::Mono, "CONDITION " + std::to_string(m.condition), a.x + 4, y, 12, C_TEXT);
        progress(Rectangle{a.x + 120, y + 2, a.width - 124, 10}, m.condition / 100.0f, mix(C_RED, C_GREEN, m.condition / 100.0f), false);
        y += 20;
        text(F::Mono, "NOISE", a.x + 4, y, 12, C_TEXT);
        pips(a.x + 120, y + 1, m.noise, bal::MAX_NOISE, C_AMBER, 9, 3);
        text(F::Mono, "UPKEEP " + std::to_string(upkeepOf(m)) + "cr/run", a.x + a.width - 4, y, 12, C_AMBER, Right);
        y += 22;
    }
    y += para(F::Mono, describeEffect(m), a.x + 4, y, a.width - 8, 12, fade(col, 0.95f), 1.3f, 3) + 10;

    float bw = (a.width - 10) / 2, bh = 32;
    auto at = [&](int i) { return Rectangle{a.x + (i % 2) * (bw + 10), y + (i / 2) * (bh + 8), bw, bh}; };
    int i = 0;
    if (!m.home) {
        if (m.active) {
            if (button(at(i++), "STORE", C_DIM)) result(deactivate(g, m));
        } else if (button(at(i++), "SWITCH ON", C_GREEN, !m.dark())) {
            result(activate(g, m));
        }
        int cost = (100 - m.condition) * bal::REPAIR_PER_POINT;
        if (button(at(i++), m.condition >= 100 ? "PRISTINE" : "REPAIR " + std::to_string(cost) + "cr", C_CYAN, m.condition < 100))
            result(repairOne(g, m));
        if (g.net.decoyId == m.id) {
            if (button(at(i++), "DISARM DECOY", C_RED)) result(disarmDecoy(g));
        } else if (button(at(i++), "ARM AS DECOY", C_RED, m.job == Job::Proxy && m.working())) {
            Result r = armDecoy(g, m, false);
            if (r.msg == "confirm") {
                int id = m.id;
                confirm("ARM " + m.name + " AS THE DECOY?",
                        "You bought this machine. If the trace fills on a run, HELIOS traces " + m.name +
                            " instead of you: it is DESTROYED, for good. You keep the night and the fine is halved.",
                        "ARM IT", C_RED, [this, id] {
                            if (Machine* mm = g.net.find(id)) result(armDecoy(g, *mm, true));
                        });
            } else {
                result(r);
            }
        }
        int value = scrapValue(m);
        if (button(at(i++), "SCRAP +" + std::to_string(value) + "cr", C_AMBER, g.net.decoyId != m.id)) {
            int id = m.id;
            confirm("SCRAP " + m.name + "?",
                    "Strip it for parts: +" + std::to_string(value) + "cr, exactly. It is gone for good - this is the only way you lose a "
                    "machine besides a decoy.",
                    "SCRAP IT", C_AMBER, [this, id] { result(scrap(g, id)); });
        }
    } else {
        Machine* h = g.net.home();
        bool can = h && h->tier < 3;
        std::string lbl = can ? "UPGRADE T" + std::to_string(h->tier + 1) + " " + std::to_string(bal::HOME_UPGRADE[h->tier - 1]) + "cr" : "TOP TIER";
        if (button(at(i++), lbl, C_AMBER, can)) result(upgradeHome(g));
    }
    if (button(at(i++), "RENAME", C_PURPLE)) {
        Modal md;
        md.title = "RENAME " + m.name;
        md.body = "Short and plain: letters, digits, space - _ . (max 14). The name is used everywhere.";
        md.buttons = {"RENAME", "CANCEL"};
        md.input = true;
        md.field = m.name;
        md.accent = C_PURPLE;
        int id = m.id;
        md.onPick = [this, id](int c, const std::string& f) {
            if (c != 0) return;
            if (Machine* mm = g.net.find(id)) result(renameMachine(g, *mm, f));
        };
        pushModal(md);
    }
    y += ((i + 1) / 2) * (bh + 8) + 8;
    text(F::Display, "RE-JOB  " + std::to_string(bal::REIMAGINE_FEE) + "cr", a.x + 4, y, 12, C_DIM);
    text(F::Mono, "keeps tier, condition, noise, name", a.x + a.width - 4, y + 1, 11, fade(C_DIM, 0.8f), Right);
    y += 20;
    float jw = (a.width - 18) / 4;
    for (int j = 0; j < 4; ++j) {
        Job job = static_cast<Job>(j);
        bool ok = job != m.job && g.net.decoyId != m.id;
        if (button(Rectangle{a.x + j * (jw + 6), y, jw, 30}, jobName(job), jobColor(job), ok, false, 11)) result(rejob(g, m, job));
    }
    if (button(Rectangle{a.x, a.y + a.height - 34, a.width, 32}, "BACK TO HARDWARE", C_DIM, true, false, 12)) selMachine = -1;
}

void App::drawShop(Rectangle a) {
    text(F::Display, "BUY HARDWARE", a.x + 4, a.y, 14, C_CYAN);
    text(F::Mono, "arrives tier 2, perfect condition, quiet", a.x + a.width - 4, a.y + 2, 11, C_DIM, Right);
    float y = a.y + 26;
    for (int j = 0; j < 4; ++j) {
        Job job = static_cast<Job>(j);
        Color col = jobColor(job);
        hexNode(Vector2{a.x + 18, y + 18}, 13, col, 0.2f);
        text(F::Display, jobLetter(job), a.x + 18, y + 11, 13, WHITE, Center);
        text(F::MonoBold, jobName(job), a.x + 42, y + 4, 14, col);
        text(F::Mono, jobPitch(job), a.x + 42, y + 22, 11, C_DIM);
        int price = bal::BUY_PRICE[j];
        if (button(Rectangle{a.x + a.width - 120, y + 2, 120, 32}, std::to_string(price) + " CR", col, g.credits >= price, false, 13)) {
            Result r = buyMachine(g, job);
            result(r);
            if (r.ok) sfx::play(sfx::Boot, 0.6f);
        }
        y += 46;
    }
    y += 8;
    DrawRectangle(static_cast<int>(a.x), static_cast<int>(y), static_cast<int>(a.width), 1, fade(C_MAGENTA, 0.3f));
    y += 14;
    text(F::Display, "THE ROOM", a.x + 4, y, 14, C_MAGENTA);
    y += 26;
    Network& n = g.net;
    bool canSlot = n.slots < bal::MAX_SLOTS;
    std::string sl = canSlot ? "EXPAND RACK  " + std::to_string(bal::SLOT_COST[n.slots - bal::START_SLOTS]) + "cr" : "RACK MAXED (6)";
    if (button(Rectangle{a.x, y, a.width, 34}, sl, C_MAGENTA, canSlot)) result(expandRack(g));
    y += 42;
    Machine* h = n.home();
    bool canUp = h && h->tier < 3;
    std::string ul = canUp ? "UPGRADE " + h->name + " TO T" + std::to_string(h->tier + 1) + "  " + std::to_string(bal::HOME_UPGRADE[h->tier - 1]) + "cr"
                           : (h ? h->name : std::string("N0")) + " IS TOP TIER";
    if (button(Rectangle{a.x, y, a.width, 34}, ul, C_AMBER, canUp)) result(upgradeHome(g));
    y += 42;
    int rc = repairAllCost(g);
    if (button(Rectangle{a.x, y, a.width, 34}, rc > 0 ? "REPAIR ALL  " + std::to_string(rc) + "cr" : "EVERYTHING PRISTINE", C_CYAN, rc > 0))
        result(repairAll(g));
}

// ---------------------------------------------------------------------------
// The job board
// ---------------------------------------------------------------------------
void App::refreshBoard() { board = buildBoard(g); }

void App::drawBoard(Rectangle area) {
    if (board.empty()) refreshBoard();
    std::string head = "LISTING " + std::to_string(g.mapIndex + 1) + "   //   " + std::to_string(g.mapRunsLeft) +
                       " run(s) until the board refreshes (and old listings are forgotten)";
    text(F::Mono, head, area.x, area.y - 2, 13, C_DIM);
    std::string sc = g.netUnlocked ? "SCOUTING CHARGES " + std::to_string(g.net.usableScoutCharges()) : "NO NETWORK YET";
    text(F::MonoBold, sc, area.x + area.width, area.y - 2, 13, C_AMBER, Right);
    float cw = 400, ch = 122, gap = 16;
    int rows = (static_cast<int>(board.size()) + 2) / 3;
    float contentH = rows * (ch + gap);
    float viewH = area.height - 24;
    if (modals.empty() && gx::wheel != 0) boardScroll -= gx::wheel * 60;
    boardScroll = std::clamp(boardScroll, 0.0f, std::max(0.0f, contentH - viewH));
    for (size_t i = 0; i < board.size(); ++i) {
        const Target& t = board[i];
        float x = area.x + (i % 3) * (cw + gap);
        float y = area.y + 22 + (i / 3) * (ch + gap) - boardScroll;
        if (y + ch < area.y + 10 || y > area.y + area.height) continue;
        Rectangle r{x, y, cw, ch};
        bool burned = g.burned.count(t.id) > 0;
        Color kc = t.deep ? C_RED : kindColor(t.kind);
        bool hov = !burned && hovered(r);
        DrawRectangleRec(r, fade(Color{8, 9, 26, 255}, 0.92f));
        DrawRectangleGradientH(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height), fade(kc, hov ? 0.20f : 0.09f), fade(kc, 0.0f));
        DrawRectangleRec(Rectangle{r.x, r.y, 4, r.height}, kc);
        if (hov) glowRect(r, kc, 1.5f, 3);
        else DrawRectangleLinesEx(r, 1, fade(kc, 0.45f));
        if (t.deep) brackets(r, pulse(C_RED, 3), 14, 2);
        text(F::Display, t.name, x + 18, y + 12, 15, burned ? C_DIM : WHITE);
        text(F::Mono, t.host, x + 18, y + 34, 12, C_DIM);
        text(F::MonoBold, t.deep ? "THE DEEP JOB" : kindName(t.kind), x + cw - 14, y + 13, 12, kc, Right);
        text(F::Mono, "SEC", x + 18, y + 60, 12, C_DIM);
        pips(x + 50, y + 61, t.security, 5, mix(C_GREEN, C_RED, (t.security - 1) / 4.0f), 9, 3);
        text(F::MonoBold, std::string(lockName(t.lock)) + " LOCK", x + 130, y + 59, 12, C_TEXT);
        const char* dw = dangerWord(baseTraceRate(t));
        std::string dws = dw;
        Color dc = dws == "SLOW" ? C_GREEN : dws == "NORMAL" ? C_CYAN : dws == "FAST" ? C_AMBER : C_RED;
        text(F::MonoBold, "TRACE " + dws, x + cw - 14, y + 59, 12, dc, Right);
        float tx = x + 18;
        float ty = y + 86;
        if (t.kind == Kind::Story && !t.deep) tx += tag(tx, ty, storyInfo(t.storyIndex).title + "  " + std::to_string(t.reward) + "cr", C_MAGENTA, 11) + 6;
        if (t.deep) tx += tag(tx, ty, "5000cr  //  ENDS THE STORY", C_RED, 11) + 6;
        if (t.kind == Kind::Side) tx += tag(tx, ty, "SIDE ROUTE", C_PURPLE, 11) + 6;
        if (t.stealable && g.net.bestScoutTier() > 0) tx += tag(tx, ty, "HW", C_AMBER, 11) + 6;
        if (g.reports.count(t.id)) tx += tag(tx, ty, "REPORT Q" + std::to_string(g.reports.at(t.id)), C_CYAN, 11) + 6;
        if (g.knownPhrases.count(t.id)) tx += tag(tx, ty, "PHRASE", C_GREEN, 11) + 6;
        if (t.watchdog) tag(tx, ty, "WATCHDOG", C_RED, 11);
        if (burned) {
            DrawRectangleRec(r, Color{4, 3, 10, 170});
            text(F::Display, "BURNED", x + cw / 2, y + 40, 20, fade(C_RED, 0.8f), Center);
            text(F::Mono, "already hit - gone until the board refreshes", x + cw / 2, y + 68, 12, C_DIM, Center);
        } else if (y >= area.y && hitArea(r, t.name)) {
            brief = t;
            go(Scr::Brief);
        }
    }
    if (contentH > viewH) text(F::Mono, "scroll for more", area.x + area.width, area.y + area.height - 2, 12, C_DIM, Right);
}

// ---------------------------------------------------------------------------
// The deck
// ---------------------------------------------------------------------------
void App::drawDeck(Rectangle area) {
    text(F::Mono, "Tools you buy for yourself. Permanent. Never billed. Separate from GhostNet: the network is optional, these are yours.",
         area.x, area.y - 2, 13, C_DIM);
    const Tool tools[4] = {Tool::Breaker, Tool::Uplink, Tool::Shroud, Tool::Lens};
    const Color cols[4] = {C_GREEN, C_CYAN, C_MAGENTA, C_AMBER};
    float cw = 296, gap = 16;
    for (int i = 0; i < 4; ++i) {
        ToolInfo ti = toolInfo(g, tools[i]);
        Rectangle r{area.x + i * (cw + gap), area.y + 30, cw, 470};
        Color c = cols[i];
        panel(r, c, "");
        Vector2 ic{r.x + cw / 2, r.y + 120};
        float spin = gx::now * (20 + i * 7);
        for (int k = 0; k < 3; ++k) DrawRing(ic, 62 + k * 10, 64 + k * 10, spin * (k % 2 ? -1 : 1) + k * 40, spin * (k % 2 ? -1 : 1) + k * 40 + 120 + 40 * ti.level, 32, fade(c, 0.6f - k * 0.15f));
        hexNode(ic, 44, c, 0.15f, spin * 0.2f);
        // glyphs
        if (i == 0) {   // breaker: a key
            DrawCircleLinesV(Vector2{ic.x - 12, ic.y}, 10, c);
            DrawLineEx(Vector2{ic.x - 2, ic.y}, Vector2{ic.x + 22, ic.y}, 3, c);
            DrawLineEx(Vector2{ic.x + 16, ic.y}, Vector2{ic.x + 16, ic.y + 9}, 3, c);
        } else if (i == 1) {   // uplink: arrows
            for (int k = 0; k < 2; ++k) {
                float yy = ic.y + 8 - k * 14;
                DrawTriangle(Vector2{ic.x, yy - 12}, Vector2{ic.x - 14, yy + 2}, Vector2{ic.x + 14, yy + 2}, fade(c, 0.6f + 0.4f * k));
            }
        } else if (i == 2) {   // shroud: waves fading out
            for (int k = 0; k < 3; ++k) DrawRing(ic, 6 + k * 9, 8 + k * 9, 200, 340, 16, fade(c, 1.0f - k * 0.3f));
        } else {   // lens: an eye
            DrawCircleLinesV(ic, 18, c);
            glowDot(Vector2{ic.x + std::sin(gx::now) * 6, ic.y}, 6, c);
        }
        glowText(F::Display, ti.name, r.x + cw / 2, r.y + 210, 24, c, Center);
        para(F::Mono, ti.effect, r.x + 24, r.y + 250, cw - 48, 14, C_TEXT);
        text(F::Mono, "LEVEL " + std::to_string(ti.level) + " / " + std::to_string(ti.max), r.x + 24, r.y + 320, 13, C_DIM);
        pips(r.x + 24, r.y + 342, ti.level, ti.max, c, 22, 6);
        bool maxed = ti.nextCost < 0;
        if (button(Rectangle{r.x + 24, r.y + r.height - 70, cw - 48, 44}, maxed ? "MAXED" : "UPGRADE  " + std::to_string(ti.nextCost) + " CR", c,
                   !maxed && g.credits >= ti.nextCost, !maxed && g.credits >= ti.nextCost, 15)) {
            Result res = buyTool(g, tools[i]);
            result(res);
            if (res.ok) flash = 0.3f, flashColor = c;
        }
    }
}

// ---------------------------------------------------------------------------
// The advisor panel
// ---------------------------------------------------------------------------
void App::drawPanelTab(Rectangle area) {
    PanelView pv = panelReview(g);
    text(F::Mono, "Three advisor AIs read your meters. They argue so you don't have to - and they never solve a lock for you.", area.x,
         area.y - 2, 13, C_DIM);
    float cw = 400, gap = 16;
    for (int a = 0; a < 3; ++a) {
        Advisor who = static_cast<Advisor>(a);
        Color c = advisorColor(who);
        Rectangle r{area.x + a * (cw + gap), area.y + 30, cw, 400};
        panel(r, c, "");
        advisorPortrait(Vector2{r.x + 60, r.y + 64}, 38, who);
        glowText(F::Display, advisorName(who), r.x + 116, r.y + 38, 24, c);
        text(F::Mono, std::string("advises on ") + advisorRole(who), r.x + 118, r.y + 70, 13, C_DIM);
        float y = r.y + 124;
        for (auto& s : pv.lines) {
            if (s.who != who) continue;
            DrawRectangle(static_cast<int>(r.x + 22), static_cast<int>(y + 4), 3, 12, c);
            y += para(F::Mono, s.text, r.x + 34, y, cw - 56, 14, C_TEXT) + 12;
            if (y > r.y + r.height - 30) break;
        }
    }
    Rectangle v{area.x, area.y + 450, area.width, 110};
    panel(v, C_MAGENTA, "CONSENSUS");
    para(F::Mono, pv.verdict, v.x + 24, v.y + 26, v.width - 48, 15, WHITE);
}

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------
void App::drawRecords(Rectangle area) {
    const Stats& s = g.stats;
    Rectangle l{area.x, area.y, 600, area.height};
    Rectangle r{area.x + 616, area.y, area.width - 616, area.height};
    panel(l, C_CYAN, "RECORDS");
    glowText(F::Display, actName(g.act), l.x + 24, l.y + 24, 18, C_MAGENTA);
    if (g.deepDone) text(F::MonoBold, "DEEP JOB DONE", l.x + l.width - 24, l.y + 27, 13, C_GREEN, Right);
    struct KV { const char* k; std::string v; Color c; };
    KV kv[] = {{"runs", std::to_string(s.runs), C_TEXT},
               {"contracts", std::to_string(s.jobs) + " / " + std::to_string(storyCount()), C_MAGENTA},
               {"clears", std::to_string(s.robberies), C_TEXT},
               {"captures", std::to_string(s.captures), C_AMBER},
               {"trace-outs", std::to_string(s.traceouts), C_RED},
               {"escapes bought with hardware", std::to_string(s.sacrifices), C_RED},
               {"walk-aways", std::to_string(s.walkaways), C_DIM},
               {"deep job attempts", std::to_string(s.deepAttempts), C_RED},
               {"earned", cr(s.earned), C_GREEN},
               {"upkeep paid", cr(s.billsPaid), C_AMBER},
               {"fines", cr(s.finesPaid), C_RED},
               {"repairs", cr(s.repairsPaid), C_CYAN},
               {"unpaid bills", std::to_string(s.unpaidBills), C_AMBER},
               {"machines burned as decoys, forever", std::to_string(g.net.burnedCount), C_RED}};
    for (size_t i = 0; i < sizeof kv / sizeof kv[0]; ++i) {
        float y = l.y + 64 + i * 26.0f;
        text(F::Mono, kv[i].k, l.x + 24, y, 14, C_DIM);
        text(F::MonoBold, kv[i].v, l.x + l.width - 24, y, 14, kv[i].c, Right);
        DrawRectangle(static_cast<int>(l.x + 24), static_cast<int>(y + 20), static_cast<int>(l.width - 48), 1, fade(C_DIM, 0.12f));
    }
    panel(r, C_MAGENTA, "RECENT RUNS & JOURNAL");
    float y = r.y + 24;
    size_t from = s.log.size() > 8 ? s.log.size() - 8 : 0;
    if (s.log.empty()) text(F::Mono, "(no runs yet)", r.x + 24, y, 13, C_DIM), y += 22;
    for (size_t i = from; i < s.log.size(); ++i) y += para(F::Mono, s.log[i], r.x + 24, y, r.width - 48, 12, C_TEXT, 1.3f, 2) + 4;
    y += 10;
    DrawRectangle(static_cast<int>(r.x + 24), static_cast<int>(y), static_cast<int>(r.width - 48), 1, fade(C_MAGENTA, 0.4f));
    y += 12;
    text(F::Display, "JOURNAL", r.x + 24, y, 13, C_MAGENTA);
    y += 24;
    size_t jf = g.journal.size() > 9 ? g.journal.size() - 9 : 0;
    if (g.journal.empty()) text(F::Mono, "(empty)", r.x + 24, y, 13, C_DIM);
    for (size_t i = jf; i < g.journal.size() && y < r.y + r.height - 20; ++i)
        y += para(F::Mono, "- " + g.journal[i], r.x + 24, y, r.width - 48, 12, fade(C_GREEN, 0.9f), 1.3f, 2) + 3;
}

void App::drawHelp(Rectangle a) {
    float colW = (a.width - 30) / 2;
    float y[2] = {a.y, a.y};
    int n = static_cast<int>(sizeof kHelp / sizeof kHelp[0]);
    for (int i = 0; i < n; ++i) {
        int c = i < (n + 1) / 2 ? 0 : 1;
        float x = a.x + c * (colW + 30);
        glowText(F::Display, kHelp[i].head, x, y[c], 14, i % 2 ? C_MAGENTA : C_CYAN, Left, 0.7f);
        y[c] += 22;
        y[c] += para(F::Mono, kHelp[i].body, x, y[c], colW, 13, C_TEXT, 1.32f) + 14;
    }
}

// ---------------------------------------------------------------------------
// The briefing: report, advisor votes, connect
// ---------------------------------------------------------------------------
void App::drawBrief() {
    drawTopBar();
    const Target& t = brief;
    Color kc = t.deep ? C_RED : kindColor(t.kind);
    Rectangle l{24, 76, 560, 612};
    panel(l, kc, t.deep ? "THE DEEP JOB" : kindName(t.kind));
    chromaText(F::Display, t.name, l.x + 24, l.y + 28, t.name.size() > 26 ? 20 : 26, WHITE, Left, 1.2f);
    text(F::Mono, t.host, l.x + 24, l.y + 66, 14, kc);
    float y = l.y + 96;
    y += para(F::Mono, t.blurb, l.x + 24, y, l.width - 48, 14, C_DIM) + 12;
    std::string story;
    if (t.deep)
        story = "Three archives on a HELIOS server that appears on no board. The trace is tuned past anything a deck can do alone; the "
                "window is open anyway. Rotor lock (a near miss means one notch off). A maintenance door exists - its phrase is kept "
                "at the HELIOS Facilities Annex. Pays 5000cr. It also ends the story.";
    else if (t.kind == Kind::Story)
        story = storyInfo(t.storyIndex).briefing;
    if (!story.empty()) {
        std::string clean = flow(story);
        y += para(F::Mono, clean, l.x + 24, y, l.width - 48, 14, C_MAGENTA, 1.35f, 9) + 14;
    }
    text(F::Mono, "SECURITY", l.x + 24, y, 13, C_DIM);
    pips(l.x + 130, y + 1, t.security, 5, mix(C_GREEN, C_RED, (t.security - 1) / 4.0f), 12, 4);
    y += 24;
    text(F::Mono, "LOCK", l.x + 24, y, 13, C_DIM);
    text(F::MonoBold, lockName(t.lock), l.x + 130, y, 13, C_TEXT);
    y += 22;
    text(F::Mono, "TRACE", l.x + 24, y, 13, C_DIM);
    text(F::MonoBold, std::string(dangerWord(effectiveTraceRate(g, t))) + (t.watchdog ? "  + WATCHDOG" : ""), l.x + 130, y, 13,
         t.watchdog ? C_RED : C_AMBER);
    y += 22;
    if (g.knownPhrases.count(t.id)) {
        text(F::Mono, "PHRASE", l.x + 24, y, 13, C_DIM);
        text(F::MonoBold, "known - one click opens the door", l.x + 130, y, 13, C_GREEN);
        y += 22;
    }

    bool burned = g.burned.count(t.id) > 0;
    float by = l.y + l.height - 74;
    if (g.netUnlocked && g.net.upkeepTotal() > 0)
        text(F::Mono, "The bill is fixed as you connect: " + std::to_string(g.net.upkeepTotal()) + "cr if anything happens.", l.x + 24, by - 26, 12, C_AMBER);
    if (const Machine* d = g.net.decoy()) text(F::Mono, "Armed to die tonight: " + d->name, l.x + 24, by - 44, 12, C_RED);
    if (button(Rectangle{l.x + 24, by, 300, 50}, "CONNECT", t.deep ? C_RED : C_MAGENTA, !burned, true, 20) ||
        (modals.empty() && !burned && gx::keyPressed(KEY_ENTER))) {
        connect(t);
        return;
    }
    if (button(Rectangle{l.x + 340, by, 196, 50}, "BACK", C_DIM, true, false, 16) || (modals.empty() && gx::keyPressed(KEY_ESCAPE))) {
        tab = Tab::Board;
        go(Scr::Hub);
        return;
    }

    // Report.
    Rectangle rp{600, 76, 656, 300};
    panel(rp, C_CYAN, "SCOUTING REPORT");
    ReportView rv = reportView(g, t);
    if (!rv.exists) {
        float yy = rp.y + 30;
        yy += para(F::Mono, "No report on this target. A scouting machine can reach out quietly and write one (it costs a charge "
                            "and wears the scout slightly; re-reading is free).",
                   rp.x + 24, yy, rp.width - 48, 14, C_TEXT) + 16;
        bool can = g.netUnlocked && g.net.scoutCapacity() > 0 && g.net.usableScoutCharges() > 0;
        std::string why = !g.netUnlocked ? "You have no network yet." : g.net.scoutCapacity() == 0 ? "No scout is running in your rack."
                          : g.net.usableScoutCharges() <= 0 ? "No charges left: they refill on a contract or a board refresh." : "";
        if (button(Rectangle{rp.x + 24, yy, 280, 42}, "SCAN  (" + std::to_string(g.netUnlocked ? g.net.usableScoutCharges() : 0) + " charges)", C_CYAN, can, can)) {
            Result r = scanTarget(g, t);
            result(r);
        }
        if (!why.empty()) text(F::Mono, why, rp.x + 24, yy + 54, 13, C_AMBER);
    } else {
        float yy = rp.y + 26;
        text(F::Mono, "QUALITY", rp.x + 24, yy, 13, C_DIM);
        pips(rp.x + 110, yy + 1, rv.quality, 3, C_CYAN, 12, 4);
        text(F::MonoBold, rv.lockName + " LOCK", rp.x + 240, yy, 13, C_TEXT);
        text(F::MonoBold, "TRACE " + rv.danger + (rv.watchdog ? " + WATCHDOG" : ""), rp.x + rp.width - 24, yy, 13, C_AMBER, Right);
        yy += 24;
        yy += para(F::Mono, rv.lockRule, rp.x + 24, yy, rp.width - 48, 12, rv.lockWarning ? C_AMBER : C_DIM, 1.3f, 2) + 6;
        if (rv.quality >= 2) {
            std::string l2 = "security " + std::to_string(rv.security) + "   objectives " + std::to_string(rv.objectives) +
                             (rv.objectiveDir.empty() ? "" : " in " + rv.objectiveDir) + "   pay files " + std::to_string(rv.payFiles);
            text(F::Mono, l2, rp.x + 24, yy, 13, C_TEXT);
            yy += 20;
        }
        if (rv.quality >= 3) {
            text(F::Mono, "about " + std::to_string(rv.secondsToTrace) + "s to a full trace" + (rv.backdoor ? "   //   maintenance door: YES" : ""),
                 rp.x + 24, yy, 13, C_TEXT);
            yy += 20;
        }
        for (auto& f : rv.facts) {
            if (yy > rp.y + rp.height - 24) break;
            yy += para(F::Mono, "- " + f, rp.x + 24, yy, rp.width - 48, 12, C_CYAN, 1.3f, 2);
        }
        for (auto& w : rv.warnings) {
            if (yy > rp.y + rp.height - 24) break;
            yy += para(F::Mono, "! " + w, rp.x + 24, yy, rp.width - 48, 12, C_RED, 1.3f, 2);
        }
    }

    // Panel briefing.
    Rectangle pp{600, 392, 656, 296};
    panel(pp, C_MAGENTA, "THE PANEL VOTES");
    PanelView pv = panelBriefing(g, t);
    float yy = pp.y + 22;
    for (int a = 0; a < 3; ++a) {
        Advisor who = static_cast<Advisor>(a);
        Color c = advisorColor(who);
        advisorPortrait(Vector2{pp.x + 44, yy + 30}, 22, who);
        text(F::Display, advisorName(who), pp.x + 80, yy + 6, 14, c);
        Vote v = pv.votes[a];
        tag(pp.x + 170, yy + 3, voteName(v), voteColor(v), 12);
        std::string said;
        for (auto& s : pv.lines)
            if (s.who == who) { said = s.text; break; }
        para(F::Mono, said, pp.x + 80, yy + 28, pp.width - 104, 12, C_TEXT, 1.3f, 3);
        yy += 76;
    }
    para(F::MonoBold, pv.verdict, pp.x + 24, pp.y + pp.height - 42, pp.width - 48, 13, WHITE, 1.3f, 2);
}
