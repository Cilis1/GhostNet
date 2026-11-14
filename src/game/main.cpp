// GHOSTNET - a hacking game about owning servers, and what it costs to keep them.
// Built from the design document "GHOSTNET: a plain-language description of
// everything you can do, own, risk, and lose" (extracted from NEON//FALL).
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <string>

#include "balance.h"
#include "common.h"
#include "netscreen.h"
#include "panel.h"
#include "platform.h"
#include "report.h"
#include "run.h"
#include "save.h"
#include "ui.h"
#include "version.h"
#include "world.h"

namespace {

const char* kLogo = R"(
   ________  ______  ____________   ______________
  / ____/ / / / __ \/ ___/_  __/ | / / ____/_  __/
 / / __/ /_/ / / / /\__ \ / / /  |/ / __/   / /
/ /_/ / __  / /_/ /___/ // / / /|  / /___  / /
\____/_/ /_/\____//____//_/ /_/ |_/_____/ /_/
)";

void autosave(const GameState& g) {
    std::string err;
    if (!saveGame(g, &err)) std::cout << "  " << col::red() << "Save failed: " << err << col::reset() << "\n";
}

void newGame(GameState& g, uint64_t seed) {
    g = GameState{};
    g.worldSeed = seed;
    g.credits = bal::START_CREDITS;
    g.mapRunsLeft = bal::RUNS_PER_MAP;
    g.sound = plat::soundOn;
    ui::header("A CLOSET IN MOBILE, ALABAMA");
    ui::typeOut("  Rain on the window. A borrowed deck. Fifty credits and a reputation of zero.", 8);
    ui::typeOut("  Your phone buzzes. A fixer called Mags has a job, if you're the kind of ghost", 8);
    ui::typeOut("  who can get in and out before the trace finds you.", 8);
    std::cout << "\n  " << col::dim() << "Tip: open the Job board and look for the CONTRACT. Try the Practice range if locks are new to you."
              << col::reset() << "\n";
}

std::string markers(const GameState& g, const Target& t) {
    std::string s;
    if (t.kind == Kind::Story) s += std::string(col::magenta()) + "[CONTRACT: " + storyInfo(t.storyIndex).title + "] " + col::reset();
    if (t.kind == Kind::Side) s += std::string(col::magenta()) + "[SIDE ROUTE] " + col::reset();
    if (t.stealable && g.netUnlocked) s += std::string(col::yellow()) + "[HW] " + col::reset();
    if (g.reports.count(t.id)) s += std::string(col::cyan()) + "[REPORT q" + std::to_string(g.reports.at(t.id)) + "] " + col::reset();
    if (g.knownPhrases.count(t.id)) s += std::string(col::green()) + "[PHRASE] " + col::reset();
    return s;
}

void targetMenu(GameState& g, const Target& t) {
    for (;;) {
        ui::header(t.name);
        std::cout << "  " << t.host << "  -  " << kindName(t.kind) << "\n  " << col::dim() << t.blurb << col::reset() << "\n";
        if (t.kind == Kind::Story) std::cout << "\n" << col::cyan() << storyInfo(t.storyIndex).briefing << col::reset() << "\n";
        std::cout << "\n  " << markers(g, t) << "\n";
        bool rep = hasReport(g, t);
        std::cout << "  1) " << (rep ? "Read report (free)" : "Scan target (spend 1 scouting charge)") << "\n"
                  << "  2) Ask the Panel\n"
                  << "  3) " << col::bold() << "CONNECT" << col::reset() << "\n"
                  << "  0) Back\n";
        std::string a = ui::prompt("  > ");
        if (a == "1") {
            if (rep) printReport(g, t);
            else scanTarget(g, t);
            autosave(g);
            ui::pause();
        } else if (a == "2") {
            panelBriefing(g, t);
            ui::pause();
        } else if (a == "3") {
            if (g.netUnlocked && g.net.upkeepTotal() > 0)
                std::cout << "  Network locked in for this run. Upkeep owed when it ends: " << g.net.upkeepTotal() << "cr.\n";
            if (!ui::confirm("  Connect to " + t.host + "?")) continue;
            playRun(g, t);
            autosave(g);
            return;
        } else if (a == "0" || a.empty() || ui::lower(a) == "b") {
            return;
        }
    }
}

void jobBoard(GameState& g) {
    for (;;) {
        std::vector<Target> board = buildBoard(g);
        ui::header("JOB BOARD - map " + std::to_string(g.mapIndex + 1) + " (" + std::to_string(g.mapRunsLeft) +
                   " run(s) until the city refreshes)");
        std::cout << "  " << col::dim() << ui::pad("#", 4) << ui::pad("TARGET", 34) << ui::pad("KIND", 12) << "NOTES" << col::reset() << "\n";
        for (size_t i = 0; i < board.size(); ++i) {
            const Target& t = board[i];
            bool burned = g.burned.count(t.id) > 0;
            std::cout << "  " << ui::pad(std::to_string(i + 1) + ".", 4);
            if (burned) {
                std::cout << col::dim() << ui::pad(t.name, 34) << ui::pad(kindName(t.kind), 12) << "(already hit this cycle)" << col::reset() << "\n";
                continue;
            }
            std::cout << ui::pad(t.name, 34) << ui::pad(kindName(t.kind), 12) << markers(g, t) << "\n";
        }
        std::cout << "  " << col::dim() << "Scouting charges: " << (g.netUnlocked ? std::to_string(g.net.usableScoutCharges()) : std::string("-"))
                  << "   Credits: " << g.credits << "cr" << col::reset() << "\n";
        std::string a = ui::prompt("  Pick a target number (0 = back): ");
        if (!ui::isNumber(a) || a == "0") return;
        size_t i = static_cast<size_t>(std::stoi(a));
        if (i < 1 || i > board.size()) continue;
        const Target& t = board[i - 1];
        if (g.burned.count(t.id)) { std::cout << "  That door is burned until the map refreshes.\n"; continue; }
        targetMenu(g, t);
    }
}

void deckUpgrades(GameState& g) {
    for (;;) {
        ui::header("YOUR DECK - tools you buy for yourself");
        std::cout << "  " << col::dim() << "Permanent. Never billed. GhostNet is optional; these are not." << col::reset() << "\n";
        struct Up { const char* name; int* lvl; const int* cost; std::string effect; };
        Up ups[3] = {
            {"BREAKER", &g.deck.breaker, bal::BREAKER_COST, "+1 guess on every lock per level"},
            {"UPLINK", &g.deck.uplink, bal::UPLINK_COST, "+15% base download speed per level"},
            {"SHROUD", &g.deck.shroud, bal::SHROUD_COST, "-7% trace rate and -15% fines per level"},
        };
        for (int i = 0; i < 3; ++i) {
            int l = *ups[i].lvl;
            std::cout << "  " << (i + 1) << ") " << ui::pad(ups[i].name, 9) << "lvl " << l << "/" << bal::DECK_MAX << "  "
                      << ui::pad(ups[i].effect, 42)
                      << (l < bal::DECK_MAX ? "next: " + std::to_string(ups[i].cost[l]) + "cr" : std::string("MAX")) << "\n";
        }
        std::cout << "  Credits: " << col::yellow() << g.credits << "cr" << col::reset() << "\n";
        std::string a = ui::prompt("  Buy which (0 = back): ");
        if (a != "1" && a != "2" && a != "3") return;
        Up& u = ups[a[0] - '1'];
        int l = *u.lvl;
        if (l >= bal::DECK_MAX) { std::cout << "  Maxed.\n"; continue; }
        if (g.credits < u.cost[l]) { std::cout << "  Not enough credits.\n"; continue; }
        g.credits -= u.cost[l];
        *u.lvl += 1;
        std::cout << "  " << col::green() << u.name << " upgraded to level " << *u.lvl << "." << col::reset() << "\n";
        autosave(g);
    }
}

void records(const GameState& g) {
    const Stats& s = g.stats;
    ui::header("RECORDS");
    std::cout << "  Runs " << s.runs << "  |  contracts " << s.jobs << "/" << storyCount() << "  |  robberies " << s.robberies
              << "  |  steals " << s.steals << "  |  trace-outs " << s.traceouts << "  |  sacrifices " << s.sacrifices
              << "  |  walk-aways " << s.walkaways << "\n";
    std::cout << "  Earned " << s.earned << "cr  |  upkeep paid " << s.billsPaid << "cr  |  fines " << s.finesPaid
              << "cr  |  repairs " << s.repairsPaid << "cr  |  unpaid bills " << s.unpaidBills << "\n";
    std::cout << "  Machines burned as bait, forever: " << col::red() << g.net.burnedCount << col::reset() << "\n";
    std::cout << "\n  " << col::bold() << "Recent runs" << col::reset() << "\n";
    size_t from = s.log.size() > 10 ? s.log.size() - 10 : 0;
    for (size_t i = from; i < s.log.size(); ++i) std::cout << "   " << s.log[i] << "\n";
    if (s.log.empty()) std::cout << "   (none yet)\n";
    std::cout << "\n  " << col::bold() << "Journal" << col::reset() << "\n";
    size_t jf = g.journal.size() > 15 ? g.journal.size() - 15 : 0;
    for (size_t i = jf; i < g.journal.size(); ++i) std::cout << "   - " << g.journal[i] << "\n";
    if (g.journal.empty()) std::cout << "   (empty - read memos inside servers to fill it)\n";
    ui::pause();
}

void howToPlay() {
    ui::header("HOW TO PLAY");
    std::cout <<
        "  THE LOOP: pick a target from the Job board -> connect -> open the door -> download\n"
        "  files -> disconnect before the TRACE meter hits 100%. Every action costs connection\n"
        "  time; the trace rises with every second.\n\n"
        "  DOORS: break the lock in a limited number of guesses, OR type 'phrase <word>' if you\n"
        "  have read the maintenance passphrase in a memo somewhere (shrines hoard them).\n"
        "   - KEYPAD: 4 different digits. HIT = right place, NEAR = right digit, wrong place.\n"
        "   - ROTOR : 4 digits, per wheel. '~' = ONE NOTCH AWAY, not misplaced.\n"
        "   - CIPHER: 5-letter word. [X] right place, (x) elsewhere in the word.\n\n"
        "  GHOSTNET (after your first contract): servers you own. Each does one job:\n"
        "   - RELAY speeds downloads   - PROXY slows the trace   - SCOUT gives reports\n"
        "   - COMPUTE holds one emergency extra guess per run (type 'extra' at a lock)\n"
        "  You configure everything BEFORE a run. Running machines bill you after every run\n"
        "  and wear down. Can't pay? Everything goes dark (no debt). Arm a PROXY as BAIT and,\n"
        "  if you get traced, it is destroyed instead of you - and the fine is halved.\n\n"
        "  STEALING: targets marked [HW] carry detachable hardware. Type 'steal' inside for a\n"
        "  quote. Rob first and the machine arrives damaged; steal first and you leave the\n"
        "  money behind. Corporate iron is top tier - and ruinous to keep running.\n\n"
        "  THE PANEL: three advisor AIs (VESPER risk, LEDGER money, MOTH intel) read your\n"
        "  meters and vote before a run. They never solve a lock for you.\n\n"
        "  There is no idle income, no timers, no raids. It only matters while you play.\n";
    ui::pause();
}

void hub(GameState& g) {
    for (;;) {
        ui::header("YOUR DECK");
        std::cout << "  Credits " << col::yellow() << g.credits << "cr" << col::reset() << "   Map " << (g.mapIndex + 1) << " ("
                  << g.mapRunsLeft << " run(s) to refresh)   ";
        if (contractAvailable(g)) std::cout << "Contract: " << col::magenta() << storyInfo(g.story).title << col::reset();
        else if (g.story < storyCount()) std::cout << col::dim() << "Mags will call with the next contract when the city refreshes." << col::reset();
        else std::cout << col::green() << "Campaign complete - the city is yours to work." << col::reset();
        std::cout << "\n";
        if (g.netUnlocked)
            std::cout << "  " << col::dim() << "GhostNet: " << g.net.activeCount() << "/" << g.net.slots << " running, bill "
                      << g.net.upkeepTotal() << "cr/run, bait " << (g.net.bait() ? g.net.bait()->name : std::string("none"))
                      << col::reset() << "\n";
        std::cout << "\n  1) Job board\n"
                  << "  2) GhostNet " << (g.netUnlocked ? "" : std::string(col::dim()) + "[locked]" + col::reset()) << "\n"
                  << "  3) Deck upgrades\n"
                  << "  4) The Panel (advisor AIs)\n"
                  << "  5) Records & journal\n"
                  << "  6) Practice range\n"
                  << "  7) How to play\n"
                  << "  8) Sound: " << (plat::soundOn ? "on" : "off") << "\n"
                  << "  0) Save & quit to title\n";
        std::string a = ui::prompt("  > ");
        if (a == "1") jobBoard(g);
        else if (a == "2") { networkScreen(g); autosave(g); }
        else if (a == "3") deckUpgrades(g);
        else if (a == "4") { panelReview(g); ui::pause(); }
        else if (a == "5") records(g);
        else if (a == "6") practiceRange(g);
        else if (a == "7") howToPlay();
        else if (a == "8") { plat::soundOn = !plat::soundOn; g.sound = plat::soundOn; autosave(g); }
        else if (a == "0" || ui::lower(a) == "q") { autosave(g); return; }
    }
}

}  // namespace

int main(int argc, char** argv) {
    uint64_t seed = 0;
    bool seedGiven = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--fast")) plat::fast = true;
        else if (!std::strcmp(argv[i], "--no-color")) plat::colorOn = false;
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) { seed = std::strtoull(argv[++i], nullptr, 10); seedGiven = true; }
        else if (!std::strcmp(argv[i], "--version")) { std::cout << "GhostNet " << GHOSTNET_VERSION << "\n"; return 0; }
        else if (!std::strcmp(argv[i], "--help")) {
            std::cout << "GhostNet " << GHOSTNET_VERSION << "\n  --seed N    world seed for a new game\n  --fast      no animations\n"
                         "  --no-color  plain text\n  --version\n";
            return 0;
        }
    }
    bool wantColor = plat::colorOn;
    plat::init();
    if (!wantColor) plat::colorOn = false;

    GameState g;
    try {
        for (;;) {
            std::cout << col::green() << col::bold() << kLogo << col::reset();
            std::cout << col::dim() << "  own the network. pay the bill. lose what you name.    v" << GHOSTNET_VERSION << col::reset() << "\n\n";
            bool has = saveExists();
            if (has) std::cout << "  1) Continue\n";
            std::cout << "  2) New game\n  3) Practice range\n  4) How to play\n  0) Quit\n";
            std::string a = ui::prompt("  > ");
            if (a == "1" && has) {
                std::string err;
                if (!loadGame(g, &err)) { std::cout << "  " << col::red() << "Could not load: " << err << col::reset() << "\n"; continue; }
                plat::soundOn = g.sound;
                hub(g);
            } else if (a == "2") {
                if (has && !ui::confirm("  This overwrites your current save. Continue?")) continue;
                uint64_t s = seedGiven ? seed
                                       : (static_cast<uint64_t>(std::random_device{}()) << 32) ^
                                             static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
                newGame(g, s);
                autosave(g);
                hub(g);
            } else if (a == "3") {
                practiceRange(g);
            } else if (a == "4") {
                howToPlay();
            } else if (a == "0" || ui::lower(a) == "q" || ui::lower(a) == "quit") {
                break;
            }
        }
    } catch (const EofExit&) {
        if (g.worldSeed) saveGame(g);
        std::cout << "\n";
    }
    std::cout << col::reset() << "  Jacking out. Your machines wait in the dark.\n";
    return 0;
}
