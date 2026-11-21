// A scripted tour through every screen, rendered to PNG files. CI runs it on the
// headless (software-rendered) build so the whole front end is exercised on
// every push, and the pictures double as up-to-date screenshots.
#include <cstdio>
#include <filesystem>
#include <optional>

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

    // Subnets: a T3 SPEED subnet with a cluster of three T3 relays, and a STEALTH subnet holding a prototype.
    g.net.slots = bal::MAX_SLOTS;
    for (auto& m : g.net.machines)   // keep the main rack to four so both gateways fit
        if (!m.home && m.active && (m.job == Job::Jammer || m.job == Job::Scout)) m.active = false;
    auto add = [&](Job j, int tier, const std::string& name, Trait tr, bool active, int bay, int cond) -> int {
        Machine m;
        m.id = g.net.nextId++;
        m.job = j;
        m.tier = tier;
        m.name = name;
        m.trait = tr;
        m.captured = true;
        m.active = active;
        m.bay = bay;
        m.condition = cond;
        m.noise = tr == Trait::Unstable ? 4 : 1;
        m.origin = tr == Trait::Unstable ? "Captured from HELIOS Testbed LAZARUS (board 3)." : "Captured from Kessler-Maru Compute (board 2).";
        g.net.machines.push_back(m);
        return m.id;
    };
    int gwA = add(Job::Gateway, 3, "SPINE-GW", Trait::CorpBuild, true, -1, 88);
    int gwB = add(Job::Gateway, 2, "GW-07", Trait::None, true, -1, 100);
    if (Machine* G = g.net.find(gwA)) G->role = BayRole::Speed;
    if (Machine* G = g.net.find(gwB)) G->role = BayRole::Stealth;
    add(Job::Relay, 3, "kessler-31", Trait::CorpBuild, true, gwA, 92);
    add(Job::Relay, 3, "kessler-38", Trait::CorpBuild, true, gwA, 74);
    add(Job::Relay, 3, "aurelia-44", Trait::CorpBuild, true, gwA, 81);
    int proto = add(Job::Proxy, 4, "lazarus-52", Trait::Unstable, true, gwB, 63);
    add(Job::Scout, 1, "kiosk-12", Trait::Salvaged, true, gwB, 70);
    add(Job::Compute, 2, "vault-dc-3", Trait::CorpBuild, false, -1, 55);   // more than six in storage: the tray pages
    add(Job::Relay, 1, "pachinko-8", Trait::Salvaged, false, -1, 40);
    add(Job::Jammer, 2, "spire-19", Trait::OldNet, false, -1, 90);
    g.credits += 2500;
    tab = Tab::Rack;
    selMachine = -1;
    shot("26-rack-subnets");
    selMachine = gwA;
    shot("27-gateway");
    selMachine = proto;
    shot("28-prototype-in-subnet");
    selMachine = -1;
    // 0.8 RUNNING LEAN: TONIGHT'S RACK at a briefing, LEDGER's picks taken (they start OFF), the amber lines showing.
    if (const Target* lt = findTarget([&](const Target& t) { return !t.deep && t.kind != Kind::Story && !g.burned.count(t.id) && t.security <= 2 && !t.watchdog; })) {
        brief = *lt;
        for (auto& m : g.net.machines)   // the scout back in the rack: LEDGER knows a scout does nothing during a run
            if (m.job == Job::Scout && m.bay < 0 && !m.home && !m.dark()) { m.active = true; m.noise = 3; m.tier = 3; m.captured = true; }
        go(Scr::Brief);
        shot("38-brief-votes-lean");
        briefStandby = true;
        for (auto& p : standbyPicks(g, brief)) {
            if (Machine* m = g.net.find(p.id)) setStandby(g, *m, true);
        }
        shot("39-brief-standby");
        tab = Tab::Rack;
        go(Scr::Hub);   // leaving the briefing wakes everything
        if (g.net.standbyCount() != 0) { std::fprintf(stderr, "standby outlived the briefing\n"); ++failures; }
    }
    {
        int keep = g.mapIndex, keepAct = g.act;
        g.act = 2;
        for (int mi = 2; mi < 40; ++mi) {
            g.mapIndex = mi;
            bool found = false;
            for (auto& b : buildBoard(g)) if (b.kind == Kind::Prototype) found = true;
            if (found) break;
        }
        tab = Tab::Board;
        refreshBoard();
        shot("29-board-prototype");
        const Target* pt = findTarget([](const Target& t) { return t.kind == Kind::Prototype; });
        if (!pt) { std::fprintf(stderr, "no prototype listing\n"); ++failures; }
        else {
            Target protoT = *pt;
            connect(protoT);
            run->guess(protoT.lock == LockType::Rotor ? "1234" : "crane", err);
            run->strike(gwB, err);      // BLACKOUT
            handleEvents();
            shot("30-run-strike", 0.5f);
            floats.clear();
            run->disconnect();
            run.reset();
        }
        g.mapIndex = keep;
        g.act = keepAct;
        refreshBoard();
    }

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

    // Act IV - THE LEDGER: the card, the towers on the board, a doctrine briefing, both doctrines live.
    g.retired = true;
    g.deck.breaker = bal::DECK_MAX;
    g.credits += 4000;
    tab = Tab::Rack;
    go(Scr::Hub);
    shot("31-ledger-card");
    dismiss();
    tab = Tab::Board;
    refreshBoard();
    boardScroll = 0;
    shot("32-board-ledger");
    // findTarget rebuilds the board, so copy each hit before the next lookup.
    std::optional<Target> fort, bio;
    if (const Target* f = findTarget([](const Target& t) { return t.kind == Kind::Tower && t.doctrine == Doctrine::Fortress; })) fort = *f;
    if (const Target* b = findTarget([](const Target& t) { return t.kind == Kind::Tower && t.doctrine == Doctrine::Biosentinel; })) bio = *b;
    if (!fort || !bio) { std::fprintf(stderr, "no towers on the Ledger\n"); ++failures; }
    else {
        Target fortT = *fort, bioT = *bio;
        buyPrep(g, fortT, Prep::Plan);
        buyPrep(g, fortT, Prep::Survey);
        connect(fortT);   // the first connection: two keypad misses, then the code; a rotor miss with arrows
        std::string k = run->lock.secret;
        run->guess(k.substr(1) + k.substr(0, 1), err);
        run->guess(k.substr(2) + k.substr(0, 2), err);
        run->guess(k, err);
        std::string near = run->lock.secret;
        near[0] = near[0] == '9' ? '8' : static_cast<char>(near[0] + 1);
        near[2] = near[2] < '2' ? '9' : static_cast<char>(near[2] - 2);
        run->guess(near, err);
        input = "4";
        handleEvents();
        floats.clear();
        shot("34-lock-composite", 1.0f);
        run->disconnect();
        run->settle();
        run.reset();
        brief = fortT;   // the briefing now reads the floor's record: alarm log, KEYPAD KNOWN, the estimate
        go(Scr::Brief);
        shot("33-brief-fortress");
        connect(fortT);   // the second connection: the same code, every row LOGGED for free
        input.clear();
        handleEvents();
        floats.clear();
        shot("36-lock-logged", 1.0f);
        run->disconnect();
        run.reset();
        buyPrep(g, bioT, Prep::Phrase);
        connect(bioT);
        run->phrase(bioT.passphrase);
        for (size_t i = 0; i < run->t.files.size(); ++i)
            if (run->t.files[i].kind == FileKind::Objective && run->startDownload(static_cast<int>(i), err)) {
                for (int s = 0; s < 14 && run->busy() && !run->over(); ++s) run->step();
                break;
            }
        traceShown = static_cast<float>(run->trace);
        handleEvents();
        floats.clear();
        shot("35-run-biosentinel", 1.0f);
        run->disconnect();
        run.reset();
    }
    {   // HOUSE POWER: own Tidewater's Boardroom, and its building feeds SPINE-GW's subnet at half the bill
        if (g.towerFloor.size() < static_cast<size_t>(kTowerCount)) g.towerFloor.resize(kTowerCount, 0);
        int keepFloor = g.towerFloor[1];
        g.towerFloor[1] = kFloors;
        if (!setHousePower(g, 1, gwA).ok) { std::fprintf(stderr, "house power refused\n"); ++failures; }
        tab = Tab::Rack;
        go(Scr::Hub);
        dismiss();
        selMachine = gwA;
        shot("40-gateway-house-power");
        selMachine = -1;
        g.towerFloor[1] = keepFloor;
        tidyHousePower(g);
    }
    // 0.9 CHAIRMAN IRON: Halden Grid's LOADSHED, a pinned service site, the ACCORD picker, the gold plate.
    if (const Target* h = findTarget([](const Target& t) { return t.kind == Kind::Tower && t.doctrine == Doctrine::Loadshed; })) {
        Target ht = *h;
        brief = ht;
        go(Scr::Brief);
        shot("41-brief-loadshed");
        buyPrep(g, ht, Prep::Phrase);
        connect(ht);
        run->phrase(ht.passphrase);
        for (size_t i = 0; i < run->t.files.size(); ++i)
            if (run->t.files[i].kind == FileKind::Objective && run->startDownload(static_cast<int>(i), err)) break;
        for (int s = 0; s < 40 && !run->over() && !run->shedding(); ++s) {
            if (!run->busy())
                for (size_t i = 0; i < run->t.files.size(); ++i)
                    if (!run->t.files[i].taken && run->startDownload(static_cast<int>(i), err)) break;
            run->step();
            run->trace = std::min(run->trace, 45.0);   // staging: keep the line open until the grid sheds
        }
        if (!run->shedding()) { std::fprintf(stderr, "no shed on Halden Grid\n"); ++failures; }
        traceShown = static_cast<float>(run->trace);
        handleEvents();
        floats.clear();
        shot("42-run-loadshed", 1.0f);
        run->disconnect();
        run.reset();
    } else {
        std::fprintf(stderr, "no Halden Grid on the Ledger\n");
        ++failures;
    }
    {
        if (g.towerFloor.size() < static_cast<size_t>(kTowerCount)) g.towerFloor.resize(kTowerCount, 0);
        if (g.siteState.size() < static_cast<size_t>(kTowerCount)) g.siteState.resize(kTowerCount, 0);
        std::vector<int> keepFloors = g.towerFloor;
        g.towerFloor[1] = kFloors;
        g.siteState[1] = 1;   // Tidewater owned, its Boardroom maintenance log read: OSSUARY is pinned
        tab = Tab::Board;
        go(Scr::Hub);
        dismiss();
        refreshBoard();
        boardScroll = 1e6f;   // the sites sit at the end of the board
        shot("43-board-site");
        boardScroll = 0;
        if (const Target* st = findTarget([](const Target& t) { return t.kind == Kind::Site; })) {
            Target site = *st;
            while (g.net.externalCount() >= bal::MAX_EXTERNAL) {   // make room: the rack is at its limit of owned machines
                int victim = -1;
                for (auto& m : g.net.machines)
                    if (!m.home && m.job != Job::Gateway && m.bay < 0 && g.net.decoyId != m.id) victim = m.id;
                if (victim < 0 || !scrap(g, victim).ok) break;
            }
            brief = site;
            go(Scr::Brief);
            shot("44-brief-site");
            connect(site);
            run->phrase(site.passphrase);
            handleEvents();
            floats.clear();
            startCapture();
            if (modals.empty() || modals.back().picks.empty()) { std::fprintf(stderr, "no ACCORD picker\n"); ++failures; }
            else modals.back().pick = static_cast<int>(Job::Jammer);
            shot("45-accord-picker", 1.0f);
            modals.clear();
            if (!run->startHold(Job::Jammer, err)) { std::fprintf(stderr, "ACCORD hold refused: %s\n", err.c_str()); ++failures; }
            for (int s = 0; s < 200 && run->busy() && !run->over(); ++s) {
                run->step();
                run->trace = std::min(run->trace, 60.0);
            }
            run->events.clear();
            run->settle();
            run.reset();
            int iron = -1;
            for (auto& m : g.net.machines) if (m.accord()) iron = m.id;
            if (iron < 0) { std::fprintf(stderr, "no ACCORD machine after the hold\n"); ++failures; }
            if (Machine* m = g.net.find(iron); m && !m->active) activate(g, *m);
            tab = Tab::Rack;
            go(Scr::Hub);
            dismiss();
            selMachine = iron;
            shot("46-rack-accord");
            selMachine = -1;
        } else {
            std::fprintf(stderr, "no service site on the board\n");
            ++failures;
        }
        // 0.10 THE INNER CHAIRS: two outer towers open them; LEDGERLOCK, COUNTERTRACE, a cage's inner door and SIGNATURE iron.
        g.towerFloor[0] = g.towerFloor[1] = kFloors;
        g.siteState[0] = 1;
        tab = Tab::Board;
        go(Scr::Hub);
        dismiss();
        refreshBoard();
        shot("47-board-inner-chairs");
        if (const Target* a = findTarget([](const Target& t) { return t.kind == Kind::Tower && t.doctrine == Doctrine::Ledgerlock; })) {
            brief = *a;
            go(Scr::Brief);
            shot("48-brief-ledgerlock");
        } else {
            std::fprintf(stderr, "no Aurex on the board\n");
            ++failures;
        }
        if (const Target* sw = findTarget([](const Target& t) { return t.kind == Kind::Tower && t.doctrine == Doctrine::Countertrace; })) {
            Target st = *sw;
            buyPrep(g, st, Prep::Phrase);
            connect(st);
            run->phrase(st.passphrase);
            int landed = 0;
            for (int s = 0; s < 80 && !run->over() && landed < 2; ++s) {
                if (!run->busy())
                    for (size_t i = 0; i < run->t.files.size(); ++i)
                        if (run->t.files[i].kind == FileKind::Objective && !run->t.files[i].taken && run->startDownload(static_cast<int>(i), err)) break;
                run->step();
                run->trace = std::min(run->trace, 55.0);   // staging: keep the line open until Sable Wren reads it back
                landed = run->counters;
                if (run->nextCounter() == 2 && landed >= 1) break;
            }
            if (run->counters < 1) { std::fprintf(stderr, "no countertrace on Sable Wren\n"); ++failures; }
            traceShown = static_cast<float>(run->trace);
            handleEvents();
            floats.clear();
            shot("49-run-countertrace", 1.0f);
            run->disconnect();
            run.reset();
        } else {
            std::fprintf(stderr, "no Sable Wren on the board\n");
            ++failures;
        }
        g.towerFloor[3] = kFloors;
        g.siteState[3] = 3;   // the cage run done: OUBLIETTE's inner door is on the board, with the service key's phrase
        g.knownPhrases.insert(cageDoorId(3));
        while (g.net.externalCount() >= bal::MAX_EXTERNAL) {
            int victim = -1;
            for (auto& m : g.net.machines)
                if (!m.home && !m.chairIron() && m.job != Job::Gateway && m.bay < 0 && g.net.decoyId != m.id) victim = m.id;
            if (victim < 0 || !scrap(g, victim).ok) break;
        }
        refreshBoard();
        if (const Target* cg = findTarget([](const Target& t) { return isCageCapture(t); })) {
            Target door = *cg;
            brief = door;
            go(Scr::Brief);
            shot("50-brief-cage-door");
            connect(door);
            run->phrase(door.passphrase);
            handleEvents();
            floats.clear();
            startCapture();
            if (modals.empty() || modals.back().title.find("SIGNATURE") == std::string::npos) { std::fprintf(stderr, "no SIGNATURE hold\n"); ++failures; }
            shot("51-signature-hold", 1.0f);
            modals.clear();
            if (!run->startHold(Job::Vault, err)) { std::fprintf(stderr, "SIGNATURE hold refused: %s\n", err.c_str()); ++failures; }
            for (int s = 0; s < 200 && run->busy() && !run->over(); ++s) {
                run->step();
                run->trace = std::min(run->trace, 60.0);
            }
            run->events.clear();
            run->settle();
            run.reset();
            int sig = -1;
            for (auto& m : g.net.machines) if (m.signature()) sig = m.id;
            if (sig < 0) { std::fprintf(stderr, "no SIGNATURE machine after the hold\n"); ++failures; }
            if (Machine* m = g.net.find(sig); m && !m->active) activate(g, *m);
            tab = Tab::Rack;
            go(Scr::Hub);
            dismiss();
            selMachine = sig;
            shot("52-rack-signature");
            selMachine = -1;
        } else {
            std::fprintf(stderr, "no cage door on the board\n");
            ++failures;
        }
        g.towerFloor = keepFloors;
        tidyHousePower(g);
    }
    drillConnect(true);   // FORTRESS DRILL on the real budget, one reconnect in
    {
        std::string dc = practice.outer;
        practice.guess(dc.substr(1) + dc.substr(0, 1), err);
        drillRows.push_back({0, practice.history.back().guess});
        drillConnect(false);
    }
    go(Scr::Practice);
    shot("37-practice-drill");
    drillOn = false;
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
