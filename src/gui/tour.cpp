// A scripted tour through every screen, rendered to PNG files. CI runs it on the
// headless (software-rendered) build so the whole front end is exercised on
// every push, and the pictures double as up-to-date screenshots.
#include <cstdio>
#include <filesystem>

#include "app.h"
#include "balance.h"
#include "gfx.h"
#include "paths.h"
#include "rack.h"
#include "save.h"
#include "world.h"

int App::screenshotTour(const std::string& dir, const std::function<void()>& render) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path saves = fs::temp_directory_path() / "ghostnet-tour-saves";
    fs::remove_all(saves, ec);
    paths::overrideSaveDir(saves.string());
    haveSave = false;

    int failures = 0, count = 0;
    auto shot = [&](const std::string& name, float age = 30.0f) {
        scrBorn = gx::now - age;
        toasts.clear();                       // stale toasts would cover the next screen
        for (int i = 0; i < 3; ++i) {
            render();
            scrBorn = static_cast<float>(GetTime()) - age;
            logBorn = static_cast<float>(GetTime()) - 100.0f;   // run log fully typed
            rowBorn = static_cast<float>(GetTime()) - 100.0f;   // lock rows fully revealed
        }
        std::string path = (fs::path(dir) / (name + ".png")).string();
        if (!gx::saveScreenshot(path)) {
            std::fprintf(stderr, "screenshot failed: %s\n", path.c_str());
            ++failures;
        }
        ++count;
    };
    auto dismiss = [&] {
        while (!modals.empty()) {
            Modal m = modals.back();
            modals.pop_back();
            if (m.onPick) m.onPick(0, m.field);
        }
    };
    auto findTarget = [&](auto pred) -> const Target* {
        refreshBoard();
        for (auto& t : board)
            if (pred(t)) return &t;
        return nullptr;
    };

    // Title, intro.
    shot("01-title");
    newGame(7);
    shot("02-intro");
    autosave();
    tab = Tab::Board;
    go(Scr::Hub);
    refreshBoard();
    shot("03-board");
    tab = Tab::Rack;
    shot("04-rack-locked");

    // First contract: briefing, keypad lock, inside, reckoning, the network unlocks.
    const Target* c0 = findTarget([](const Target& t) { return t.kind == Kind::Story; });
    if (!c0) { std::fprintf(stderr, "no contract on the board\n"); return failures + 1; }
    brief = *c0;
    go(Scr::Brief);
    shot("05-briefing");
    connect(brief);
    std::string err;
    const std::string tries[] = {"0123", "4567", "8901"};
    for (auto& s : tries)
        if (run->lock.left() > 1 && !run->doorOpen) run->guess(s, err);
    input = "52";
    handleEvents();
    shot("06-lock-keypad", 1.0f);
    run->guess(run->lock.secret, err);
    handleEvents();
    for (size_t i = 0; i < run->t.files.size(); ++i)
        if (run->t.files[i].readable()) {
            run->read(static_cast<int>(i));
            readFile = static_cast<int>(i);
            break;
        }
    for (size_t i = 0; i < run->t.files.size(); ++i)
        if (run->t.files[i].kind == FileKind::Objective && run->startDownload(static_cast<int>(i), err)) {
            while (run->busy() && !run->over()) run->step();
        }
    for (size_t i = 0; i < run->t.files.size(); ++i)
        if (run->t.files[i].kind == FileKind::Paydata && run->startDownload(static_cast<int>(i), err)) {
            for (int k = 0; k < 3 && run->busy(); ++k) run->step();
            break;
        }
    handleEvents();
    floats.clear();
    shot("07-inside", 1.0f);
    while (run->busy() && !run->over()) run->step();
    run->disconnect();
    handleEvents();
    endedAt = gx::now - 0.3f;
    shot("08-jacked-out", 0.3f);
    finishRun();
    shot("09-reckoning");
    leaveReckoning();
    shot("10-network-unlocked");
    dismiss();

    // A lived-in rack: bought and captured machines, storage, a decoy.
    g.credits += 6000;
    buyMachine(g, Job::Proxy);
    buyMachine(g, Job::Scout);
    expandRack(g);
    upgradeHome(g);
    Machine cap;
    cap.id = g.net.nextId++;
    cap.name = "kessler-7";
    cap.job = Job::Proxy;
    cap.tier = 3;
    cap.captured = true;
    cap.condition = 62;
    cap.noise = 4;
    cap.active = true;
    cap.origin = "Captured from Kessler-Maru Compute (board 4).";
    cap.trait = Trait::HotIron;
    g.net.machines.push_back(cap);
    armDecoy(g, g.net.machines.back(), true);
    Machine st = cap;
    st.id = g.net.nextId++;
    st.name = "turnstile-9";
    st.job = Job::Relay;
    st.tier = 1;
    st.condition = 0;
    st.active = false;
    st.origin = "Captured from Transit Turnstile Hub (board 2).";
    st.trait = Trait::Salvaged;
    g.net.machines.push_back(st);
    buyMachine(g, Job::Vault);    // rack full: it lands in cold storage
    buyMachine(g, Job::Jammer);
    g.programs[0] = Program::Freeze;
    g.programs[1] = Program::Peek;
    Memorial plate;
    plate.name = "PRX-3";
    plate.job = Job::Proxy;
    plate.tier = 2;
    plate.run = 14;
    plate.where = "Tidewater BioSys Grid";
    plate.origin = "Bought from a back-room dealer.";
    g.wall.push_back(plate);
    plate.name = "vending-12";
    plate.tier = 1;
    plate.run = 19;
    plate.where = "Halcyon Freight Ledger";
    g.wall.push_back(plate);
    g.act = 2;
    g.deck.breaker = 1;
    g.deck.uplink = 2;
    g.deck.shroud = 1;
    g.story = 1;
    g.mapIndex = 1;
    autosave();
    selMachine = -1;
    tab = Tab::Rack;
    shot("11-rack");
    selMachine = cap.id;
    shot("12-machine");
    selMachine = -1;
    tab = Tab::Deck;
    shot("13-deck");
    tab = Tab::Panel;
    shot("14-panel");
    tab = Tab::Records;
    shot("15-records");
    tab = Tab::Help;
    shot("16-help");
    tab = Tab::Board;
    refreshBoard();
    shot("17-board-later");

    // Cipher and rotor locks, a hot trace, a trace-out.
    const Target* any = findTarget([&](const Target& t) { return !g.burned.count(t.id) && t.lock != LockType::None; });
    if (!any) { std::fprintf(stderr, "no locked target\n"); return failures + 1; }
    Target cipherT = *any;
    cipherT.lock = LockType::Cipher;
    connect(cipherT);
    for (const char* w : {"crane", "spine", "ghost"})
        if (run->lock.left() > 1) run->guess(w, err);
    run->runProgram(1, err);   // PEEK: one letter, ghosted into the input row
    input.clear();
    handleEvents();
    floats.clear();
    shot("18-lock-cipher", 1.0f);
    openPhraseModal();
    shot("25-phrase-starters", 1.0f);
    modals.clear();
    run->disconnect();
    run.reset();

    Target rotorT = *any;
    rotorT.lock = LockType::Rotor;
    connect(rotorT);
    for (const char* w : {"1234", "5555", "9080"})
        if (run->lock.left() > 1) run->guess(w, err);
    run->trace = 86;
    traceShown = 86;
    handleEvents();
    shot("19-lock-rotor-hot-trace", 1.0f);
    g.net.decoyId = -1;   // no decoy: a real trace-out
    run->trace = 99.5;
    while (!run->over() && !run->lock.sealed()) run->guess("0000", err);
    if (!run->over()) run->phrase("WRONG");
    handleEvents();
    endedAt = gx::now - 0.4f;
    shot("20-traced", 0.4f);
    finishRun();
    shot("21-traceout-reckoning");
    leaveReckoning();
    dismiss();

    // The deep job's shortfall readout.
    Target deep;
    deep.name = "HELIOS DEEP ARCHIVE";
    rk = Reckoning{};
    rk.end = End::TraceOut;
    rk.deep = true;
    rk.lines = {{"Fine: -550cr", Tone::Bad}, {"The bill: -95cr", Tone::Warn}, {"Wear: HEAVY (traced with no decoy) on 3 machine(s)", Tone::Bad}};
    rk.shortfall = Shortfall{1, 3, 74, 21, 24, 100, 43, true, 3.4, 2, 1, 15, 9};
    rk.credits = g.credits;
    rkCreditsBefore = g.credits + 645;
    go(Scr::Reckon);
    shot("22-deep-shortfall");

    // Endings and the practice range.
    g.deepDone = true;
    go(Scr::Ending);
    shot("23-ending-owner");
    practice = makeLock(LockType::Cipher, practiceRng, 0);
    practiceOn = true;
    practice.guess("crane", err);
    practice.guess("ghost", err);
    input.clear();
    go(Scr::Practice);
    shot("24-practice");

    fs::remove_all(saves, ec);
    std::printf("screenshot tour: %d screens, %d failures -> %s\n", count, failures, dir.c_str());
    return failures;
}
