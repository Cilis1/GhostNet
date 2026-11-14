// GHOSTNET - a game about a runner who stops renting and starts owning.
// Built from the concept document "GHOSTNET: A game about a runner who stops
// renting and starts owning".
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
    ui::header("ACT ONE - RENTING");
    ui::typeOut("  In this city, server space is real estate, and you own none of it.", 8);
    ui::typeOut("  You rent a room the listings call THE RACK: four walls, one socket, the only safe place you have.", 8);
    ui::typeOut("  HELIOS keeps the city's machines running. Every time a runner touches one, HELIOS opens a ticket.", 8);
    ui::typeOut("  The trace meter is just that ticket: how long you've been an open problem.", 8);
    ui::typeOut("  The room used to belong to a courier called SAINT. Nobody says what happened to SAINT.", 8);
    std::cout << "\n  " << col::dim() << "Tip: open the Job board and look for the CONTRACT. Try the Practice range if locks are new to you."
              << col::reset() << "\n";
}

std::string markers(const GameState& g, const Target& t) {
    std::string s;
    if (t.deep) s += std::string(col::red()) + col::bold() + "[THE DEEP JOB] " + col::reset();
    else if (t.kind == Kind::Story) s += std::string(col::magenta()) + "[CONTRACT: " + storyInfo(t.storyIndex).title + "] " + col::reset();
    if (t.kind == Kind::Side) s += std::string(col::magenta()) + "[SIDE ROUTE] " + col::reset();
    // Detachable hardware only shows up when a scouting machine is running.
    if (t.stealable && g.net.bestScoutTier() > 0) s += std::string(col::yellow()) + "[HW] " + col::reset();
    if (g.reports.count(t.id)) s += std::string(col::cyan()) + "[REPORT q" + std::to_string(g.reports.at(t.id)) + "] " + col::reset();
    if (g.knownPhrases.count(t.id)) s += std::string(col::green()) + "[PHRASE] " + col::reset();
    return s;
}

void ending(GameState& g);

void targetMenu(GameState& g, const Target& t) {
    for (;;) {
        ui::header(t.name);
        std::cout << "  " << t.host << "  -  " << kindName(t.kind) << "\n  " << col::dim() << t.blurb << col::reset() << "\n";
        if (t.deep)
            std::cout << "\n" << col::cyan()
                      << "  THE DEEP JOB. Three archives on a HELIOS server that appears on no board.\n"
                      << "  The trace is tuned past anything a deck can do alone; the window is open anyway.\n"
                      << "  Rotor lock (a near miss means one notch off). A maintenance door exists - its phrase\n"
                      << "  is kept at the HELIOS Facilities Annex. Pays 5000cr. It also ends the story." << col::reset() << "\n";
        else if (t.kind == Kind::Story) std::cout << "\n" << col::cyan() << storyInfo(t.storyIndex).briefing << col::reset() << "\n";
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
                std::cout << "  The bill is fixed as you connect: " << g.net.upkeepTotal() << "cr, due after the run if anything happens.\n";
            if (!ui::confirm("  Connect to " + t.host + "?")) continue;
            playRun(g, t);
            autosave(g);
            if (g.deepDone && !g.retired) { ending(g); autosave(g); }
            return;
        } else if (a == "0" || a.empty() || ui::lower(a) == "b") {
            return;
        }
    }
}

void jobBoard(GameState& g) {
    for (;;) {
        std::vector<Target> board = buildBoard(g);
        ui::header("JOB BOARD - listing " + std::to_string(g.mapIndex + 1) + " (" + std::to_string(g.mapRunsLeft) +
                   " run(s) until the board refreshes)");
        std::cout << "  " << col::dim() << ui::pad("#", 4) << ui::pad("TARGET", 34) << ui::pad("KIND", 12) << "NOTES" << col::reset() << "\n";
        for (size_t i = 0; i < board.size(); ++i) {
            const Target& t = board[i];
            bool burned = g.burned.count(t.id) > 0;
            std::cout << "  " << ui::pad(std::to_string(i + 1) + ".", 4);
            if (burned) {
                std::cout << col::dim() << ui::pad(t.name, 34) << ui::pad(kindName(t.kind), 12) << "(already hit - gone until the board refreshes)" << col::reset() << "\n";
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
        if (g.burned.count(t.id)) { std::cout << "  That door is burned until the board refreshes.\n"; continue; }
        targetMenu(g, t);
    }
}

void deckUpgrades(GameState& g) {
    for (;;) {
        ui::header("THE DECK - tools you buy for yourself");
        std::cout << "  " << col::dim() << "Permanent. Never billed. Separate from GhostNet: the network is optional, these are yours." << col::reset() << "\n";
        struct Up { const char* name; int* lvl; const int* cost; int max; std::string effect; };
        Up ups[4] = {
            {"BREAKER", &g.deck.breaker, bal::BREAKER_COST, bal::DECK_MAX, "more attempts: +1 guess on every lock"},
            {"UPLINK", &g.deck.uplink, bal::UPLINK_COST, bal::DECK_MAX, "better tools: +15% base download speed"},
            {"SHROUD", &g.deck.shroud, bal::SHROUD_COST, bal::DECK_MAX, "quieter connection: -7% trace, -15% fines"},
            {"LENS", &g.deck.lens, bal::LENS_COST, bal::LENS_MAX, "faster reading: -1s per file read"},
        };
        for (int i = 0; i < 4; ++i) {
            int l = *ups[i].lvl;
            std::cout << "  " << (i + 1) << ") " << ui::pad(ups[i].name, 9) << "lvl " << l << "/" << ups[i].max << "  "
                      << ui::pad(ups[i].effect, 44)
                      << (l < ups[i].max ? "next: " + std::to_string(ups[i].cost[l]) + "cr" : std::string("MAX")) << "\n";
        }
        std::cout << "  Credits: " << col::yellow() << g.credits << "cr" << col::reset() << "\n";
        std::string a = ui::prompt("  Buy which (0 = back): ");
        if (a != "1" && a != "2" && a != "3" && a != "4") return;
        Up& u = ups[a[0] - '1'];
        int l = *u.lvl;
        if (l >= u.max) { std::cout << "  Maxed.\n"; continue; }
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
    static const char* acts[4] = {"", "ACT ONE - RENTING", "ACT TWO - OWNING", "ACT THREE - THE DEEP JOB"};
    std::cout << "  " << col::magenta() << acts[g.act] << col::reset() << (g.deepDone ? "  (deep job done)" : "")
              << "   deep job attempts: " << s.deepAttempts << "\n";
    std::cout << "  Runs " << s.runs << "  |  contracts " << s.jobs << "/" << storyCount() << "  |  clears " << s.robberies
              << "  |  captures " << s.captures << "  |  trace-outs " << s.traceouts << "  |  escapes bought with hardware " << s.sacrifices
              << "  |  walk-aways " << s.walkaways << "\n";
    std::cout << "  Earned " << s.earned << "cr  |  upkeep paid " << s.billsPaid << "cr  |  fines " << s.finesPaid
              << "cr  |  repairs " << s.repairsPaid << "cr  |  unpaid bills " << s.unpaidBills << "\n";
    std::cout << "  Machines burned as decoys, forever: " << col::red() << g.net.burnedCount << col::reset() << "\n";
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
        "  THE LOOP: THE RACK -> Job board -> connection -> the reckoning. Pick a listing, connect,\n"
        "  open the door, take the files, disconnect before the TRACE fills. The trace is HELIOS's\n"
        "  ticket on you, and it fills constantly. Reading costs time. Downloading costs time AND\n"
        "  makes you easier to find (the trace runs x1.25 while bytes move).\n\n"
        "  TRACE-OUT: accounts seized - the night's haul is lost, a fine comes out of what you have\n"
        "  (never more than you have), the bill still comes due, and your machines wear hard.\n\n"
        "  DOORS: break the lock in a limited number of guesses, OR type 'phrase <word>' if you've\n"
        "  read the maintenance passphrase somewhere (shrines hoard them). A phrase breaks nothing.\n"
        "   - KEYPAD: 4 different digits. HIT = right place, NEAR = right digit, wrong place.\n"
        "   - ROTOR : 4 digits, per wheel. '~' = ONE NOTCH AWAY ('close'), not misplaced.\n"
        "   - CIPHER: 5-letter word. [X] right place, (x) elsewhere in the word.\n\n"
        "  THE DECK: tools you buy for yourself - more attempts, better tools, a quieter connection,\n"
        "  faster reading. Permanent, never billed, separate from the network.\n\n"
        "  GHOSTNET (after your first contract): SAINT's salvaged rack and your home server, N0.\n"
        "   - RELAY speeds downloads   - PROXY slows the trace (and can be the DECOY)\n"
        "   - SCOUT gives reports      - COMPUTE holds one emergency extra guess (type 'extra')\n"
        "  Configure everything between runs. Running machines (except N0) bill you after every\n"
        "  run that does something, then wear. Can't pay? Nothing is taken: everything but N0 goes\n"
        "  dark, no debt. Arm a PROXY as the DECOY and if the trace fills, HELIOS traces it instead\n"
        "  of you: it is destroyed, the fine is halved, and you keep the night.\n\n"
        "  CAPTURE: with a scout running, listings marked [HW] carry detachable hardware. Type\n"
        "  'steal' inside for a quote. Take files first and it arrives more damaged. Corporate iron\n"
        "  is tier 3 - the only way past tier 2 besides upgrading N0 - and expensive to run.\n\n"
        "  THE DEEP JOB: a HELIOS archive tuned so no deck alone can finish it. You can try it the\n"
        "  moment it appears; if you fall short, the game tells you by how much.\n\n"
        "  THE PANEL: three advisor AIs (VESPER risk, LEDGER money, MOTH intel) read your meters\n"
        "  and vote before a run. They never solve a lock for you.\n\n"
        "  No idle income, no timers, no raids. Nothing happens while you're away.\n";
    ui::pause();
}

// The two endings. Neither is a loss.
void ending(GameState& g) {
    g.retired = true;
    if (g.deepDone) {
        ui::header("ENDING - AN OWNER");
        std::cout << col::cyan();
        ui::typeOut("  You stop taking listings. Nobody tells you to; there's just nothing left you need to take.", 10);
        int running = g.net.activeCount();
        ui::typeOut("  " + std::to_string(running) + " machine(s) hum in a room you used to rent. N0 is still on relay, the way SAINT left it.", 10);
        ui::typeOut("  HELIOS never closes ticket #48213. It just stops mattering.", 10);
        std::cout << col::reset() << "\n  " << col::bold() << "The rack goes quiet. The machines stay on." << col::reset() << "\n";
    } else {
        ui::header("ENDING - A RUNNER WHO KEPT RENTING");
        std::cout << col::cyan();
        ui::typeOut("  You hand back the key to THE RACK. Somebody else will rent it next month.", 10);
        ui::typeOut("  HELIOS files you the way it files most runners: a closed ticket, a good night or two, nothing owned.", 10);
        ui::typeOut("  The deep archive is still out there. It was always going to need more than a deck.", 10);
        std::cout << col::reset() << "\n  " << col::bold() << "Filed: a runner who kept renting." << col::reset() << "\n";
    }
    const Stats& s = g.stats;
    std::cout << "\n  " << s.runs << " runs, " << s.jobs << " contracts, " << s.captures << " captures, " << s.traceouts << " trace-outs, "
              << g.net.burnedCount << " machine(s) given to HELIOS in your place. " << s.earned << "cr earned.\n";
    std::cout << col::dim() << "  (Your save stays. Continue any time - the city doesn't care that you're done.)" << col::reset() << "\n";
    ui::pause();
}

void hub(GameState& g) {
    for (;;) {
        static const char* acts[4] = {"", "ACT ONE - RENTING", "ACT TWO - OWNING", "ACT THREE - THE DEEP JOB"};
        ui::header(std::string("THE RACK  |  ") + acts[g.act]);
        std::cout << "  Credits " << col::yellow() << g.credits << "cr" << col::reset() << "   Listing " << (g.mapIndex + 1) << " ("
                  << g.mapRunsLeft << " run(s) to refresh)   ";
        if (g.deepDone) std::cout << col::green() << "The deep job is done. You can retire as an owner." << col::reset();
        else if (contractAvailable(g)) std::cout << "Contract: " << col::magenta() << storyInfo(g.story).title << col::reset();
        else if (g.story < storyCount()) std::cout << col::dim() << "The next contract posts when the board refreshes." << col::reset();
        else std::cout << col::magenta() << "Only the deep job is left." << col::reset();
        std::cout << "\n";
        if (g.netUnlocked) {
            int worth = 0;
            for (auto& m : g.net.machines) worth += scrapValue(m);
            std::cout << "  " << col::dim() << "GhostNet: " << g.net.activeCount() << "/" << g.net.slots << " running, worth " << worth
                      << "cr, next run costs " << g.net.upkeepTotal() << "cr, armed to die: "
                      << (g.net.decoy() ? g.net.decoy()->name : std::string("nobody")) << col::reset() << "\n";
        }
        std::cout << "\n  1) Job board\n"
                  << "  2) GhostNet " << (g.netUnlocked ? "" : std::string(col::dim()) + "[locked]" + col::reset()) << "\n"
                  << "  3) The deck (your own tools)\n"
                  << "  4) The Panel (advisor AIs)\n"
                  << "  5) Records & journal\n"
                  << "  6) Practice range\n"
                  << "  7) How to play\n"
                  << "  8) Sound: " << (plat::soundOn ? "on" : "off") << "\n"
                  << "  9) " << (g.deepDone ? "Retire as an owner" : "Walk away (retire)") << "\n"
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
        else if (a == "9") {
            if (!g.deepDone && !ui::confirm("  Walk away without the deep job? You'll be filed as a runner who kept renting.")) continue;
            ending(g);
            autosave(g);
            return;
        }
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
            std::cout << col::dim() << "  stop renting. start owning.    v" << GHOSTNET_VERSION << col::reset() << "\n\n";
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
