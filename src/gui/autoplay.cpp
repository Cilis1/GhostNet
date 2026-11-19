// Headless autoplay: plays the real UI the way a person does, by moving the
// mouse onto buttons, clicking and typing. Nothing here calls game logic
// directly; it only reads state to decide what to click and to check that the
// click did what it should. CI runs it on every push, so a dead button, a
// screen that never lets you leave, or mouse mapping that misses the buttons
// fails the build.
//
// The simulated window is 1600x1000 (not 16:9), so every click also goes
// through the letterbox mapping from window pixels to the 1280x720 canvas.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app.h"
#include "balance.h"
#include "gfx.h"
#include "paths.h"
#include "world.h"

namespace {

struct Driver {
    App& app;
    const std::function<void()>& render;
    gx::ScriptInput in;
    int frames = 0;
    std::string step;

    Driver(App& a, const std::function<void()>& r) : app(a), render(r) {
        in.screenW = 1600;
        in.screenH = 1000;
    }

    void frame() {
        gx::script = &in;
        render();
        gx::script = nullptr;
        in.click = false;
        in.keys.clear();
        ++frames;
    }

    Vector2 toRaw(Vector2 v) const {
        float s, ox, oy;
        gx::fit(in.screenW, in.screenH, s, ox, oy);
        return Vector2{ox + v.x * s, oy + v.y * s};
    }

    std::vector<gx::Widget> find(const std::string& label) const {
        std::vector<gx::Widget> out;
        for (auto& w : gx::widgets)
            if (w.label == label) out.push_back(w);
        return out;
    }

    void dumpLabels() const {
        std::fprintf(stderr, "  clickable now:");
        for (auto& w : gx::widgets) std::fprintf(stderr, " [%s]", w.label.c_str());
        std::fprintf(stderr, "\n");
    }

    bool fail(const std::string& why) {
        std::fprintf(stderr, "autoplay FAILED at '%s': %s (after %d frames)\n", step.c_str(), why.c_str(), frames);
        dumpLabels();
        return false;
    }

    // Move onto the nth widget with this label and click it, like a person:
    // one frame hovering, one frame pressing.
    // Patience is in real seconds, not frames: CI machines render much faster
    // than a laptop running the software renderer, and the UI's animations are timed.
    bool click(const std::string& label, size_t nth = 0, double patience = 8.0) {
        double until = GetTime() + patience;
        while (GetTime() < until) {
            auto ws = find(label);
            if (ws.size() > nth) {
                Rectangle r = ws[nth].r;
                in.raw = toRaw(Vector2{r.x + r.width / 2, r.y + r.height / 2});
                frame();
                in.click = true;
                frame();
                return true;
            }
            frame();
        }
        return fail("no clickable '" + label + "'");
    }

    template <class Pred>
    bool waitFor(Pred pred, const std::string& what, double patience = 10.0) {
        double until = GetTime() + patience;
        while (GetTime() < until) {
            if (pred()) return true;
            frame();
        }
        return pred() || fail("timed out waiting for " + what);
    }

    void type(const std::string& s) {
        for (char c : s) in.chars.push_back(static_cast<unsigned char>(c));
        frame();
    }

    void key(int k) {
        in.keys.push_back(k);
        frame();
    }

    // Click through whatever dialogs are up (story beats, the network unlock).
    bool clearModals(int limit = 12) {
        for (int i = 0; i < limit && !app.modals.empty(); ++i) {
            const Modal& m = app.modals.back();
            if (m.input) {
                for (int k = 0; k < 20 && !app.modals.empty() && !app.modals.back().field.empty(); ++k) key(KEY_BACKSPACE);
                type("kettle");
            }
            if (!click(m.buttons.front())) return false;
        }
        return app.modals.empty() || fail("dialogs never cleared");
    }
};

std::string wrongGuess(const Lock& lock) {
    // A keypad-legal guess (4 distinct digits) that isn't the answer.
    for (const char* g : {"0123", "4567", "8901", "2345"})
        if (lock.secret != g) return lock.cur() == LockType::Cipher ? std::string("crane") : std::string(g);
    return "1234";
}

}  // namespace

int App::autoplay(const std::function<void()>& render) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path saves = fs::temp_directory_path() / "ghostnet-autoplay-saves";
    fs::remove_all(saves, ec);
    paths::overrideSaveDir(saves.string());
    haveSave = false;

    Driver d(*this, render);
    auto bad = [&] { return 1; };
    gx::recordText = true;
    struct TextOff { ~TextOff() { gx::recordText = false; } } textOff;

    // ---- title -> practice range: type a guess, click keys, leave -------------
    d.step = "practice range";
    if (!d.click("PRACTICE RANGE")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Practice; }, "the practice screen")) return bad();
    if (!d.click("KEYPAD")) return bad();
    if (!d.waitFor([&] { return practiceOn; }, "a practice lock")) return bad();
    d.type(wrongGuess(practice));
    d.key(KEY_ENTER);
    if (!d.waitFor([&] { return practice.history.size() == 1; }, "the typed guess to register")) return bad();
    for (char c : std::string("5678"))
        if (!d.click(std::string(1, c))) return bad();
    if (!d.click("OK")) return bad();
    if (!d.waitFor([&] { return practice.history.size() == 2; }, "the clicked guess to register")) return bad();
    d.step = "fortress drill";   // the real Kessler budget; a reconnect keeps the code and replays the row for free
    if (!d.click("FORTRESS DRILL")) return bad();
    if (!d.waitFor([&] { return drillOn && practice.composite() && practice.maxGuesses == drillBudget(); }, "the drill door")) return bad();
    {
        const std::string drillCode = practice.outer;
        d.type(wrongGuess(practice));
        d.key(KEY_ENTER);
        if (!d.waitFor([&] { return practice.history.size() == 1 && practice.sealed(); }, "the drill guess")) return bad();
        if (!d.click("RECONNECT")) return bad();
        if (!d.waitFor([&] { return drillConnections == 2; }, "the drill reconnect")) return bad();
        if (practice.outer != drillCode || practice.used != 0 || practice.history.size() != 1 || !practice.history[0].logged)
            return d.fail("the drill didn't keep its code and log"), bad();
    }
    if (!d.click("BACK")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Title; }, "the title screen")) return bad();

    // ---- new game -> intro -> hub ------------------------------------------------
    d.step = "new game";
    if (!d.click("NEW GAME")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Intro; }, "the intro")) return bad();
    if (!d.find("SKIP").empty() ? !d.click("SKIP") : !d.click("BEGIN")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Hub; }, "the hub")) return bad();
    if (!d.clearModals()) return bad();

    // ---- every hub tab ---------------------------------------------------------
    d.step = "hub tabs";
    const std::pair<const char*, Tab> tabs[] = {{"THE RACK", Tab::Rack},     {"THE DECK", Tab::Deck}, {"THE PANEL", Tab::Panel},
                                                {"RECORDS", Tab::Records},   {"HOW TO PLAY", Tab::Help},
                                                {"JOB BOARD", Tab::Board}};
    for (auto& t : tabs) {
        if (!d.click(t.first)) return bad();
        if (!d.waitFor([&] { return tab == t.second; }, std::string("the ") + t.first + " tab")) return bad();
    }

    // ---- the first contract, all by mouse and keyboard ---------------------------
    d.step = "pick the contract";
    std::string contract;
    for (auto& t : board)
        if (t.kind == Kind::Story) contract = t.name;
    if (contract.empty()) return d.fail("no contract on the board"), bad();
    if (!d.click(contract)) return bad();
    if (!d.waitFor([&] { return scr == Scr::Brief; }, "the briefing")) return bad();
    if (!d.click("CONNECT")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Run && run; }, "the connection")) return bad();

    d.step = "break the lock";
    if (run->lock.type != LockType::None && !run->doorOpen) {
        size_t before = run->lock.history.size();
        d.type(wrongGuess(run->lock));
        d.key(KEY_ENTER);
        if (!d.waitFor([&] { return run->lock.history.size() == before + 1; }, "the typed guess to register")) return bad();
        std::string answer = run->lock.secret;
        if (run->lock.type == LockType::Cipher) {
            d.type(answer);
            if (!d.click("SUBMIT")) return bad();
        } else {
            for (char c : answer)
                if (!d.click(std::string(1, c))) return bad();
            if (!d.click("OK")) return bad();
        }
        if (!d.waitFor([&] { return run->doorOpen; }, "the door to open")) return bad();
    }

    d.step = "inside";
    if (!d.waitFor([&] { return !d.find("READ").empty() || !d.find("OPEN").empty(); }, "the file list")) return bad();
    if (!d.click(d.find("READ").empty() ? "OPEN" : "READ")) return bad();
    if (!d.waitFor([&] { return readFile >= 0 || !run->busy(); }, "the file to open", 30.0)) return bad();
    for (int i = 0; i < 4; ++i) {
        if (d.find("GET").empty()) break;
        if (!d.click("GET")) return bad();
        if (!d.waitFor([&] { return !run->busy(); }, "the download", 30.0)) return bad();
    }
    if (!d.waitFor([&] { return run->jobDone; }, "the objective")) return bad();
    if (!d.click("JACK OUT - KEEP IT")) return bad();
    if (!d.waitFor([&] { return run && run->over(); }, "the disconnect")) return bad();
    if (!d.waitFor([&] { return gx::now - endedAt > 0.6f || scr == Scr::Reckon; }, "the overlay")) return bad();
    if (scr == Scr::Run) {   // click through the JACKED OUT card
        d.in.click = true;
        d.frame();
    }
    if (!d.waitFor([&] { return scr == Scr::Reckon; }, "the reckoning")) return bad();

    d.step = "reckoning";
    for (double until = GetTime() + 10.0; GetTime() < until && scr == Scr::Reckon;) {
        if (!d.find("CONTINUE").empty()) d.click("CONTINUE");
        else if (!d.find("SKIP").empty()) d.click("SKIP");
        else d.frame();
    }
    if (!d.waitFor([&] { return scr == Scr::Hub; }, "the hub after the run")) return bad();
    if (!d.clearModals()) return bad();
    if (!g.netUnlocked) return d.fail("the network didn't unlock after the first contract"), bad();
    if (g.net.home() && g.net.home()->name != "kettle") return d.fail("N0's typed name didn't stick: " + g.net.home()->name), bad();

    // ---- the rack: buy a machine, select it, store it, switch it on -------------
    d.step = "the rack";
    if (!d.click("THE RACK")) return bad();
    if (!d.find("BACK TO HARDWARE").empty() && !d.click("BACK TO HARDWARE")) return bad();
    if (!d.waitFor([&] { return selMachine == -1; }, "the shop view")) return bad();
    size_t machinesBefore = g.net.machines.size();
    if (!d.click("400 CR")) return bad();
    if (!d.waitFor([&] { return g.net.machines.size() == machinesBefore + 1; }, "the bought relay")) return bad();
    if (!d.clearModals()) return bad();
    std::string bought = g.net.machines.back().name;
    if (!d.click(bought)) return bad();
    if (!d.waitFor([&] { return selMachine == g.net.machines.back().id; }, "the machine to be selected")) return bad();
    if (!d.click("STORE")) return bad();
    if (!d.waitFor([&] { return !g.net.machines.back().active; }, "the machine to go into storage")) return bad();
    if (!d.click("SWITCH ON")) return bad();
    if (!d.waitFor([&] { return g.net.machines.back().active; }, "the machine to switch back on")) return bad();
    if (!d.click("BACK TO HARDWARE")) return bad();
    if (!d.waitFor([&] { return selMachine == -1; }, "the machine to be deselected")) return bad();

    // ---- the menu: save & quit ---------------------------------------------------
    d.step = "save and quit";
    if (!d.click("MENU")) return bad();
    if (!d.click("SAVE & QUIT")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Title; }, "the title after save & quit")) return bad();
    d.step = "continue";
    if (!d.click("CONTINUE")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Hub && g.netUnlocked; }, "the saved game to load")) return bad();

    // ---- Act IV: fast-forward to an owner (as a finished save would be), then climb by hand -
    d.step = "the ledger";
    g.deepDone = true;
    g.retired = true;
    g.act = 3;
    g.credits = 6000;
    g.deck.breaker = bal::DECK_MAX;
    if (!d.waitFor([&] { return g.act == 4 && !modals.empty(); }, "the Act IV card")) return bad();
    if (!d.click("OPEN THE BOARD")) return bad();
    if (!d.waitFor([&] { return tab == Tab::Board && !board.empty() && board.front().kind == Kind::Tower; }, "the towers on the board")) return bad();
    Target tower = board.front();
    if (!d.click(tower.name)) return bad();
    if (!d.waitFor([&] { return scr == Scr::Brief; }, "the tower briefing")) return bad();
    if (!d.click("FLOOR PLAN  " + std::to_string(bal::PREP_PLAN_COST) + "cr")) return bad();
    if (!d.waitFor([&] { return g.reports.count(tower.id) && g.reports.at(tower.id) == 3; }, "the bought floor plan")) return bad();
    if (!d.clearModals()) return bad();
    d.step = "the tumbler survey";
    if (!d.click("TUMBLER SURVEY  " + std::to_string(bal::PREP_SURVEY_COST[0]) + "cr")) return bad();
    if (!d.waitFor([&] { return g.fortress.count(tower.id) && g.fortress.at(tower.id).survey; }, "the bought survey")) return bad();
    if (!d.clearModals()) return bad();
    if (!d.click("CONNECT")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Run && run; }, "the tower connection")) return bad();
    d.step = "the composite lock";
    if (!run->lock.composite()) return d.fail("Kessler-Maru's door isn't a composite"), bad();
    if (!d.waitFor([&] { return gx::onScreen("ALARM LOG  0/" + std::to_string(bal::FORTRESS_ALARM_LOG)); }, "the ALARM LOG meter")) return bad();
    if (!run->lock.surveyed || !gx::onScreen("SURVEY") || run->lock.codesLeft() != 24 || !gx::onScreen("CODES LEFT 24"))
        return d.fail("the survey isn't on the lock"), bad();
    const std::string outer = run->lock.outer, inner = run->lock.inner;
    {   // a keypad miss by mouse: the digits in the wrong order
        std::string miss = outer.substr(1) + outer.substr(0, 1);
        for (char c : miss)
            if (!d.click(std::string(1, c))) return bad();
        if (!d.click("OK")) return bad();
        if (!d.waitFor([&] { return run->lock.history.size() == 1 && run->alarms() == 1; }, "the logged miss")) return bad();
        if (!d.waitFor([&] { return gx::onScreen("ALARM LOG  1/" + std::to_string(bal::FORTRESS_ALARM_LOG)); }, "the ALARM LOG to count the miss"))
            return bad();
    }
    for (char c : outer)   // stage 1, the keypad, by mouse
        if (!d.click(std::string(1, c))) return bad();
    if (!d.click("OK")) return bad();
    if (!d.waitFor([&] { return run->lock.stage == 1 && run->lock.carried(); }, "the rotor to wake")) return bad();
    std::string rmiss = inner;
    rmiss[0] = rmiss[0] == '9' ? '8' : static_cast<char>(rmiss[0] + 1);
    d.type(rmiss);   // a rotor miss by keyboard: arrows on the screen
    d.key(KEY_ENTER);
    if (!d.waitFor([&] { return run->lock.history.size() == 1 && run->lock.history.back().dir.size() == 4; }, "a rotor row with arrows")) return bad();
    if (!d.waitFor([&] { return gx::onScreen("CODES LEFT"); }, "the rotor's CODES LEFT")) return bad();
    if (!d.click("JACK OUT")) return bad();
    if (!d.waitFor([&] { return run && run->over(); }, "the disconnect")) return bad();
    d.in.click = true;
    d.frame();
    if (!d.waitFor([&] { return scr == Scr::Reckon; }, "the tower reckoning", 10.0)) return bad();
    if (g.act != 4 || g.floorsCleared(tower.tower) != 0) return d.fail("a walk-away changed the tower"), bad();
    for (double until = GetTime() + 10.0; GetTime() < until && scr == Scr::Reckon;) {
        if (!d.find("CONTINUE").empty()) d.click("CONTINUE");
        else if (!d.find("SKIP").empty()) d.click("SKIP");
        else d.frame();
    }
    if (!d.waitFor([&] { return scr == Scr::Hub; }, "the hub after the tower")) return bad();
    if (!d.clearModals()) return bad();

    d.step = "reconnect: the same code";
    if (tab != Tab::Board && !d.click("JOB BOARD")) return bad();
    if (!d.waitFor([&] { return tab == Tab::Board; }, "the board")) return bad();
    if (!d.click(tower.name)) return bad();
    if (!d.waitFor([&] { return scr == Scr::Brief; }, "the tower briefing again")) return bad();
    if (!d.click("CONNECT")) return bad();
    if (!d.waitFor([&] { return scr == Scr::Run && run; }, "the second tower connection")) return bad();
    if (run->lock.outer != outer || run->lock.inner != inner) return d.fail("Kessler-Maru re-keyed between connections"), bad();
    if (!run->lock.keypadKnown() || run->lock.used != 0 || run->lock.history.size() != 2 || !run->lock.history[0].logged)
        return d.fail("the logged rows didn't come back free"), bad();
    if (!d.waitFor([&] { return gx::onScreen("KEYPAD KNOWN") && gx::onScreen("LOGGED"); }, "KEYPAD KNOWN and the LOGGED rows")) return bad();
    for (char c : outer)
        if (!d.click(std::string(1, c))) return bad();
    if (!d.click("OK")) return bad();
    if (!d.waitFor([&] { return run->lock.stage == 1; }, "the known keypad")) return bad();
    if (run->lock.used != 1 || run->alarms() != 2 || run->lock.history.size() != 1 || !run->lock.history[0].logged)
        return d.fail("the known keypad cost more than one guess, or the rotor rows didn't replay"), bad();
    d.type(inner);
    d.key(KEY_ENTER);
    if (!d.waitFor([&] { return run->doorOpen; }, "the composite to open")) return bad();
    if (!d.click("JACK OUT")) return bad();
    if (!d.waitFor([&] { return run && run->over(); }, "the disconnect")) return bad();
    d.in.click = true;
    d.frame();
    if (!d.waitFor([&] { return scr == Scr::Reckon; }, "the tower reckoning", 10.0)) return bad();

    std::printf("autoplay: played title -> practice -> new game -> contract -> reckoning -> rack -> save & quit -> continue "
                "-> the ledger -> survey -> composite lock -> reconnect to the same code with %d frames of clicks and typing, all OK\n",
                d.frames);
    fs::remove_all(saves, ec);
    return 0;
}
