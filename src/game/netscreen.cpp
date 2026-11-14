#include "netscreen.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>

#include "balance.h"
#include "platform.h"
#include "ui.h"

std::string sanitizeName(const std::string& raw) {
    std::string o;
    for (char c : raw) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == ' ') o += c;
        if (o.size() >= 16) break;
    }
    return ui::trim(o);
}

void unlockNetwork(GameState& g) {
    ui::header("GHOSTNET");
    plat::soundAlert();
    ui::typeOut("  Bad news first: the room's landlord bills by the rack unit now, and somebody has to pay it.", 6);
    ui::typeOut("  Then the good news. Behind the false panel in THE RACK, under SAINT's old courier tags:", 6);
    ui::typeOut("  three slots of salvaged hardware HELIOS logged as melted down. SAINT never melted anything.", 6);
    ui::typeOut("  One machine is still warm. It boots when you touch it.", 6);
    std::cout << "\n  " << col::bold() << "This is your own server mesh." << col::reset()
              << " It is optional infrastructure, and it never replaces your tools.\n"
              << "  " << col::dim() << "Nothing on it earns money. Nothing runs while you're away. It only helps you on runs - and it bills you." << col::reset() << "\n\n";
    std::string name = sanitizeName(ui::prompt("  Name your home server (Enter for 'N0'): "));
    if (name.empty()) name = "N0";
    Machine h;
    h.id = 0;
    h.name = name;
    h.job = Job::Relay;
    h.tier = 1;
    h.home = true;
    h.active = true;
    h.condition = 100;
    h.noise = 1;
    h.origin = "SAINT's last machine. Never billed, never worn, never lost.";
    g.net.machines.insert(g.net.machines.begin(), h);
    g.net.slots = bal::START_SLOTS;
    g.netUnlocked = true;
    g.net.refillScout();
    plat::soundOnline();
    std::cout << "  " << col::green() << name << " comes online." << col::reset()
              << " RELAY, tier 1, free to run, takes one slot. Open GhostNet from THE RACK menu.\n";
}

namespace {

void summary(const GameState& g) {
    const Network& n = g.net;
    ui::header("THE RACK - GhostNet");
    int worth = 0;
    for (auto& m : n.machines) worth += scrapValue(m);
    std::cout << "  Credits " << col::yellow() << g.credits << "cr" << col::reset() << "   Running " << n.activeCount() << "/" << n.slots
              << " slots   Machines " << n.externalCount() << "/" << bal::MAX_EXTERNAL << " (plus N0)   Worth " << worth
              << "cr scrapped   Burned as decoys: " << n.burnedCount << "\n";
    std::cout << "  Download bonus " << col::green() << "+" << n.relayBonus() << "%" << col::reset() << " (cap " << bal::RELAY_CAP << ")"
              << "   Trace reduction " << col::green() << "-" << n.proxyBonus() << "%" << col::reset() << " (cap " << bal::PROXY_CAP << ")"
              << "   Emergency attempts next run " << n.computeCapacity() << "/" << bal::COMPUTE_MAX << "\n";
    std::cout << "  Scouting charges " << n.usableScoutCharges() << "/" << n.scoutCapacity() << " (best report quality "
              << n.bestScoutTier() << ")   Next run costs " << col::yellow() << n.upkeepTotal() << "cr" << col::reset()
              << "   Armed to die: " << (n.decoy() ? std::string(col::red()) + n.decoy()->name + col::reset() : std::string("nobody")) << "\n\n";
    ui::drawRack(g);
    const Machine* h = n.home();
    std::cout << "\n  " << col::dim() << "Prices: relay " << bal::BUY_PRICE[0] << ", proxy " << bal::BUY_PRICE[1] << ", scout "
              << bal::BUY_PRICE[2] << ", compute " << bal::BUY_PRICE[3] << " (clean T2)  | re-job " << bal::REIMAGINE_FEE
              << " | repair " << bal::REPAIR_PER_POINT << "cr/pt";
    if (n.slots < bal::MAX_SLOTS) std::cout << " | next slot " << bal::SLOT_COST[n.slots - bal::START_SLOTS];
    if (h && h->tier < 3) std::cout << " | " << h->name << " upgrade " << bal::HOME_UPGRADE[h->tier - 1];
    std::cout << col::reset() << "\n";
}

void help() {
    std::cout << "  buy <relay|proxy|scout|compute>   on <id>   off <id> (cold storage)   job <id> <job>   repair <id|all>\n"
              << "  scrap <id>   rename <id> <name>   decoy <id>   disarm   expand   home (upgrade N0)\n"
              << "  back\n";
}

Machine* machineArg(GameState& g, const std::vector<std::string>& w, size_t idx = 1) {
    if (w.size() <= idx || !ui::isNumber(w[idx])) { std::cout << "  Which machine? Use its [id].\n"; return nullptr; }
    Machine* m = g.net.find(std::stoi(w[idx]));
    if (!m) std::cout << "  No machine with id " << w[idx] << ".\n";
    return m;
}

void buy(GameState& g, const std::vector<std::string>& w) {
    Job j;
    if (w.size() < 2 || !parseJob(w[1], j)) { std::cout << "  buy <relay|proxy|scout|compute>\n"; return; }
    int price = bal::BUY_PRICE[static_cast<int>(j)];
    if (g.net.externalCount() >= bal::MAX_EXTERNAL) { std::cout << "  You already own 10 machines besides N0 - the most anyone can keep. Scrap one first.\n"; return; }
    if (g.credits < price) { std::cout << "  That costs " << price << "cr. You have " << g.credits << "cr.\n"; return; }
    g.credits -= price;
    Machine m;
    m.id = g.net.nextId++;
    m.job = j;
    m.tier = bal::BUY_TIER;
    m.condition = 100;
    m.noise = 1;
    m.price = price;
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d", m.id);
    static const char* serial[4] = {"RLY-", "PRX-", "SCT-", "CMP-"};
    m.name = std::string(serial[static_cast<int>(j)]) + buf;
    m.origin = "Bought clean for " + std::to_string(price) + "cr.";
    m.active = g.net.activeCount() < g.net.slots;
    g.net.machines.push_back(m);
    if (m.active) {
        plat::soundOnline();
        std::cout << "  " << col::green() << m.name << " racked and running." << col::reset() << "\n";
    } else {
        std::cout << "  " << col::yellow() << "Your rack is full: " << m.name
                  << " went to COLD STORAGE. It is not running and does nothing until you free a slot and switch it on." << col::reset() << "\n";
    }
}

void activate(GameState& g, Machine& m) {
    if (m.active) { std::cout << "  Already running.\n"; return; }
    if (m.dark()) { std::cout << "  " << m.name << " is dark (condition 0). Repair it first.\n"; return; }
    if (g.net.activeCount() >= g.net.slots) { std::cout << "  No free slot (" << g.net.slots << "). Switch something off or expand.\n"; return; }
    m.active = true;
    plat::soundOnline();
    std::cout << "  " << col::green() << m.name << " hums to life." << col::reset() << " Upkeep " << upkeepOf(m) << "cr/run.\n";
}

void deactivate(GameState& g, Machine& m) {
    if (m.home) { std::cout << "  " << m.name << " is always running - it's the rack's heart.\n"; return; }
    if (!m.active) { std::cout << "  Already in storage.\n"; return; }
    m.active = false;
    std::cout << "  " << m.name << " goes to cold storage. Silent, free, no wear.\n";
    if (g.net.decoyId == m.id) { g.net.decoyId = -1; std::cout << "  Storing it disarmed the decoy.\n"; }
}

void reimagine(GameState& g, Machine& m, const std::vector<std::string>& w) {
    Job j;
    if (w.size() < 3 || !parseJob(w[2], j)) { std::cout << "  job <id> <relay|proxy|scout|compute>\n"; return; }
    if (g.net.decoyId == m.id) { std::cout << "  " << m.name << " is armed as the decoy. Disarm it before you change its job.\n"; return; }
    if (m.job == j) { std::cout << "  It already does that.\n"; return; }
    if (g.credits < bal::REIMAGINE_FEE) { std::cout << "  Re-jobbing costs " << bal::REIMAGINE_FEE << "cr.\n"; return; }
    g.credits -= bal::REIMAGINE_FEE;
    m.job = j;
    std::cout << "  " << m.name << " is now a " << jobName(j) << ". Same tier, condition, signal, name and history. -" << bal::REIMAGINE_FEE << "cr.\n";
    if (j == Job::Scout) std::cout << col::dim() << "  (Capacity, not ammunition: scouting charges are not refilled.)" << col::reset() << "\n";
}

int repairOne(GameState& g, Machine& m) {
    if (m.home) return 0;
    int need = 100 - m.condition;
    int afford = g.credits / bal::REPAIR_PER_POINT;
    int pts = std::min(need, afford);
    if (pts <= 0) return 0;
    m.condition += pts;
    int cost = pts * bal::REPAIR_PER_POINT;
    g.credits -= cost;
    g.stats.repairsPaid += cost;
    std::cout << "  " << m.name << ": +" << pts << " condition (" << cost << "cr) -> " << m.condition << "%"
              << (m.condition < 100 ? " (that's all you could afford)" : "") << "\n";
    if (!m.active && m.condition > 0) std::cout << col::dim() << "    It stays in storage until you switch it on." << col::reset() << "\n";
    return cost;
}

void repair(GameState& g, const std::vector<std::string>& w) {
    if (w.size() >= 2 && ui::lower(w[1]) == "all") {
        int total = 0;
        for (auto& m : g.net.machines) if (!m.home && m.condition < 100) total += repairOne(g, m);
        if (total == 0) std::cout << "  Nothing repaired" << (g.credits <= 0 ? " - you're broke." : " - everything is pristine.") << "\n";
        return;
    }
    Machine* m = machineArg(g, w);
    if (!m) return;
    if (m->home) { std::cout << "  The home server never takes damage.\n"; return; }
    if (m->condition >= 100) { std::cout << "  Already pristine.\n"; return; }
    if (repairOne(g, *m) == 0) std::cout << "  You can't afford a single point.\n";
}

void scrap(GameState& g, Machine& m) {
    if (m.home) { std::cout << "  " << m.name << " can never be scrapped.\n"; return; }
    int value = scrapValue(m);
    std::cout << "  Scrapping " << col::bold() << m.name << col::reset() << " destroys it permanently for " << col::yellow() << value
              << "cr" << col::reset() << (m.captured ? " (flat rate for captured hardware)." : " (a fraction of the " + std::to_string(m.price) + "cr you paid).") << "\n";
    std::string a = ui::prompt("  Type its name to confirm: ");
    if (a != m.name) { std::cout << "  Kept.\n"; return; }
    if (g.net.decoyId == m.id) g.net.decoyId = -1;
    int id = m.id;
    std::string name = m.name;
    auto& v = g.net.machines;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const Machine& x) { return x.id == id; }), v.end());
    g.credits += value;
    std::cout << "  " << name << " is stripped for parts. +" << value << "cr. A small funeral.\n";
}

void armDecoy(GameState& g, Machine& m) {
    if (m.home) { std::cout << "  Never " << m.name << ". There has to be one thing you cannot lose.\n"; return; }
    if (g.net.decoyId == m.id) { std::cout << "  " << m.name << " is already the decoy.\n"; return; }
    if (m.job != Job::Proxy) { std::cout << "  Only a PROXY can be a decoy.\n"; return; }
    if (!m.active) { std::cout << "  It has to be in the rack, running.\n"; return; }
    if (m.dark()) { std::cout << "  It's dark. A dead machine can't fool HELIOS.\n"; return; }
    if (!m.captured) {
        std::cout << "  " << col::red() << "You paid " << m.price << "cr for " << m.name << "." << col::reset()
                  << " If the trace fills, HELIOS traces it instead of you and it is DESTROYED -\n"
                  << "  not stored, not repairable, gone. The fine is halved and you keep the night.\n";
        std::string a = ui::prompt("  Type 'sacrifice' to arm it: ");
        if (ui::lower(a) != "sacrifice") { std::cout << "  Not armed.\n"; return; }
    }
    if (g.net.decoyId >= 0)
        if (const Machine* old = g.net.find(g.net.decoyId)) std::cout << "  " << old->name << " is no longer the decoy.\n";
    g.net.decoyId = m.id;
    std::cout << "  " << col::bgRed() << " DECOY ARMED " << col::reset() << " " << m.name << " keeps slowing the trace until the moment it dies.\n";
}

}  // namespace

void networkScreen(GameState& g) {
    if (!g.netUnlocked) {
        std::cout << "  " << col::dim() << "[LOCKED] The rack is just a rented room for now. Finish a contract." << col::reset() << "\n";
        ui::pause();
        return;
    }
    summary(g);
    help();
    for (;;) {
        std::string line = ui::prompt("  ghostnet> ");
        auto w = ui::split(line);
        if (w.empty()) { summary(g); continue; }
        std::string c = ui::lower(w[0]);
        if (c == "back" || c == "b" || c == "q" || c == "0" || c == "exit") return;
        if (c == "help" || c == "?") { help(); continue; }
        if (c == "ls" || c == "show" || c == "inspect") { summary(g); continue; }
        if (c == "buy") buy(g, w);
        else if (c == "on" || c == "activate") { if (Machine* m = machineArg(g, w)) activate(g, *m); }
        else if (c == "off" || c == "deactivate" || c == "store") { if (Machine* m = machineArg(g, w)) deactivate(g, *m); }
        else if (c == "job" || c == "reimagine") { if (Machine* m = machineArg(g, w)) reimagine(g, *m, w); }
        else if (c == "repair") repair(g, w);
        else if (c == "scrap" || c == "release") { if (Machine* m = machineArg(g, w)) scrap(g, *m); }
        else if (c == "rename") {
            Machine* m = machineArg(g, w);
            if (!m) continue;
            std::string rest;
            for (size_t i = 2; i < w.size(); ++i) rest += (i > 2 ? " " : "") + w[i];
            std::string n = sanitizeName(rest);
            if (n.empty()) { std::cout << "  rename <id> <name>  (letters, digits, space - _ . ; max 16)\n"; continue; }
            std::cout << "  " << m->name << " is now called " << col::bold() << n << col::reset() << ".\n";
            m->name = n;
        } else if (c == "decoy" || c == "arm") { if (Machine* m = machineArg(g, w)) armDecoy(g, *m); }
        else if (c == "disarm" || c == "undecoy") {
            if (g.net.decoyId < 0) std::cout << "  Nothing is armed.\n";
            else { g.net.decoyId = -1; std::cout << "  Decoy disarmed.\n"; }
        } else if (c == "expand") {
            if (g.net.slots >= bal::MAX_SLOTS) { std::cout << "  The rack is at its maximum of " << bal::MAX_SLOTS << " slots.\n"; continue; }
            int cost = bal::SLOT_COST[g.net.slots - bal::START_SLOTS];
            if (g.credits < cost) { std::cout << "  The next slot costs " << cost << "cr.\n"; continue; }
            g.credits -= cost;
            g.net.slots += 1;
            std::cout << "  Slot " << g.net.slots << " bolted in. -" << cost << "cr.\n";
        } else if (c == "home" || c == "upgrade") {
            Machine* h = g.net.home();
            if (!h) continue;
            if (h->tier >= 3) { std::cout << "  " << h->name << " is already top tier.\n"; continue; }
            int cost = bal::HOME_UPGRADE[h->tier - 1];
            if (g.credits < cost) { std::cout << "  Upgrading " << h->name << " costs " << cost << "cr.\n"; continue; }
            g.credits -= cost;
            h->tier += 1;
            plat::soundOnline();
            std::cout << "  " << h->name << " is now tier " << h->tier << ". It never bills you and it never wears.\n";
        } else std::cout << "  Unknown command. Type 'help'.\n";
    }
}
