#include "rack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "balance.h"
#include "text.h"

std::string sanitizeName(const std::string& raw) {
    std::string o;
    for (char c : raw) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == ' ') o += c;
        if (o.size() >= 14) break;
    }
    return txt::trim(o);
}

void unlockNetwork(GameState& g) {
    if (g.netUnlocked) return;
    Machine h;
    h.id = 0;
    h.name = "N0";
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
}

Result buyMachine(GameState& g, Job j) {
    int price = bal::BUY_PRICE[static_cast<int>(j)];
    if (g.net.externalCount() >= bal::MAX_EXTERNAL) return {false, "You already own 10 machines besides N0 - the most anyone can keep. Scrap one first."};
    if (g.credits < price) return {false, "That costs " + txt::cr(price) + ". You have " + txt::cr(g.credits) + "."};
    g.credits -= price;
    Machine m;
    m.id = g.net.nextId++;
    m.job = j;
    m.tier = bal::BUY_TIER;
    m.condition = 100;
    m.noise = 1;
    m.price = price;
    static const char* serial[4] = {"RLY-", "PRX-", "SCT-", "CMP-"};
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d", m.id);
    m.name = std::string(serial[static_cast<int>(j)]) + buf;
    m.origin = "Bought clean for " + txt::cr(price) + ".";
    m.active = g.net.activeCount() < g.net.slots;
    g.net.machines.push_back(m);
    if (m.active) return {true, m.name + " racked and running."};
    return {true, "Your rack is full: " + m.name + " went to COLD STORAGE. It does nothing until you free a slot and switch it on."};
}

Result activate(GameState& g, Machine& m) {
    if (m.active) return {false, "Already running."};
    if (m.dark()) return {false, m.name + " is dark (condition 0). Repair it first."};
    if (g.net.activeCount() >= g.net.slots) return {false, "No free slot. Switch something off or expand the rack."};
    m.active = true;
    return {true, m.name + " hums to life. Upkeep " + txt::cr(upkeepOf(m)) + " per run."};
}

Result deactivate(GameState& g, Machine& m) {
    if (m.home) return {false, m.name + " is always running - it's the rack's heart."};
    if (!m.active) return {false, "Already in storage."};
    m.active = false;
    std::string msg = m.name + " goes to cold storage. Silent, free, no wear.";
    if (g.net.decoyId == m.id) {
        g.net.decoyId = -1;
        msg += " Storing it disarmed the decoy.";
    }
    return {true, msg};
}

Result rejob(GameState& g, Machine& m, Job j) {
    if (g.net.decoyId == m.id) return {false, m.name + " is armed as the decoy. Disarm it before you change its job."};
    if (m.job == j) return {false, "It already does that."};
    if (g.credits < bal::REIMAGINE_FEE) return {false, "Re-jobbing costs " + txt::cr(bal::REIMAGINE_FEE) + "."};
    g.credits -= bal::REIMAGINE_FEE;
    m.job = j;
    std::string msg = m.name + " is now a " + jobName(j) + ". Same tier, condition, signal, name and history.";
    if (j == Job::Scout) msg += " (Scouting charges are not refilled.)";
    return {true, msg};
}

Result repairOne(GameState& g, Machine& m) {
    if (m.home) return {false, m.name + " never wears. Nothing to repair."};
    if (m.condition >= 100) return {false, "Already pristine."};
    int need = 100 - m.condition;
    int pts = std::min(need, g.credits / bal::REPAIR_PER_POINT);
    if (pts <= 0) return {false, "You can't afford a single point."};
    m.condition += pts;
    int cost = pts * bal::REPAIR_PER_POINT;
    g.credits -= cost;
    g.stats.repairsPaid += cost;
    std::string msg = m.name + ": +" + std::to_string(pts) + " condition for " + txt::cr(cost) + ".";
    if (m.condition < 100) msg += " That's all you could afford.";
    if (!m.active) msg += " It stays in storage until you switch it on.";
    return {true, msg};
}

int repairAllCost(const GameState& g) {
    int c = 0;
    for (auto& m : g.net.machines) if (!m.home) c += (100 - m.condition) * bal::REPAIR_PER_POINT;
    return c;
}

Result repairAll(GameState& g) {
    int total = 0, count = 0;
    for (auto& m : g.net.machines) {
        if (m.home || m.condition >= 100) continue;
        int before = g.credits;
        if (repairOne(g, m).ok) { total += before - g.credits; ++count; }
    }
    if (count == 0) return {false, g.credits < bal::REPAIR_PER_POINT ? "Nothing repaired - you're broke." : "Everything is pristine."};
    return {true, "Repaired " + std::to_string(count) + " machine(s) for " + txt::cr(total) + "."};
}

Result scrap(GameState& g, int id) {
    Machine* m = g.net.find(id);
    if (!m) return {false, "No such machine."};
    if (m->home) return {false, m->name + " can never be scrapped."};
    int value = scrapValue(*m);
    std::string name = m->name;
    if (g.net.decoyId == id) g.net.decoyId = -1;
    auto& v = g.net.machines;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const Machine& x) { return x.id == id; }), v.end());
    g.credits += value;
    return {true, name + " is stripped for parts. +" + txt::cr(value) + "."};
}

bool decoyNeedsConfirm(const Machine& m) { return !m.captured; }

Result armDecoy(GameState& g, Machine& m, bool confirmed) {
    if (m.home) return {false, "Never " + m.name + ". There has to be one thing you cannot lose."};
    if (g.net.decoyId == m.id) return {false, m.name + " is already the decoy."};
    if (m.job != Job::Proxy) return {false, "Only a PROXY can be a decoy."};
    if (!m.active) return {false, "It has to be in the rack, running."};
    if (m.dark()) return {false, "It's dark. A dead machine can't fool HELIOS."};
    if (decoyNeedsConfirm(m) && !confirmed) return {false, "confirm"};
    std::string msg;
    if (g.net.decoyId >= 0)
        if (const Machine* old = g.net.find(g.net.decoyId)) msg = old->name + " is no longer the decoy. ";
    g.net.decoyId = m.id;
    return {true, msg + m.name + " is armed. It keeps slowing the trace until the moment it dies."};
}

Result disarmDecoy(GameState& g) {
    if (g.net.decoyId < 0) return {false, "Nothing is armed."};
    g.net.decoyId = -1;
    return {true, "Decoy disarmed."};
}

Result expandRack(GameState& g) {
    if (g.net.slots >= bal::MAX_SLOTS) return {false, "The rack is at its maximum of " + std::to_string(bal::MAX_SLOTS) + " slots."};
    int cost = bal::SLOT_COST[g.net.slots - bal::START_SLOTS];
    if (g.credits < cost) return {false, "The next slot costs " + txt::cr(cost) + "."};
    g.credits -= cost;
    g.net.slots += 1;
    return {true, "Slot " + std::to_string(g.net.slots) + " bolted in. -" + txt::cr(cost) + "."};
}

Result upgradeHome(GameState& g) {
    Machine* h = g.net.home();
    if (!h) return {false, "No home server."};
    if (h->tier >= 3) return {false, h->name + " is already top tier."};
    int cost = bal::HOME_UPGRADE[h->tier - 1];
    if (g.credits < cost) return {false, "Upgrading " + h->name + " costs " + txt::cr(cost) + "."};
    g.credits -= cost;
    h->tier += 1;
    return {true, h->name + " is now tier " + std::to_string(h->tier) + ". It never bills you and it never wears."};
}

Result renameMachine(GameState&, Machine& m, const std::string& name) {
    std::string n = sanitizeName(name);
    if (n.empty()) return {false, "Names are letters, digits, space - _ . (max 14)."};
    std::string old = m.name;
    m.name = n;
    return {true, old + " is now called " + n + "."};
}

ToolInfo toolInfo(const GameState& g, Tool t) {
    switch (t) {
        case Tool::Breaker:
            return {"BREAKER", "More attempts: +1 guess on every lock", g.deck.breaker, bal::DECK_MAX,
                    g.deck.breaker < bal::DECK_MAX ? bal::BREAKER_COST[g.deck.breaker] : -1};
        case Tool::Uplink:
            return {"UPLINK", "Better tools: +15% base download speed", g.deck.uplink, bal::DECK_MAX,
                    g.deck.uplink < bal::DECK_MAX ? bal::UPLINK_COST[g.deck.uplink] : -1};
        case Tool::Shroud:
            return {"SHROUD", "Quieter connection: -7% trace, -15% fines", g.deck.shroud, bal::DECK_MAX,
                    g.deck.shroud < bal::DECK_MAX ? bal::SHROUD_COST[g.deck.shroud] : -1};
        case Tool::Lens:
            return {"LENS", "Faster reading: -1s per file read", g.deck.lens, bal::LENS_MAX,
                    g.deck.lens < bal::LENS_MAX ? bal::LENS_COST[g.deck.lens] : -1};
    }
    return {"?", "", 0, 0, -1};
}

Result buyTool(GameState& g, Tool t) {
    ToolInfo ti = toolInfo(g, t);
    if (ti.nextCost < 0) return {false, std::string(ti.name) + " is maxed."};
    if (g.credits < ti.nextCost) return {false, std::string(ti.name) + " costs " + txt::cr(ti.nextCost) + "."};
    g.credits -= ti.nextCost;
    int* lvl = t == Tool::Breaker ? &g.deck.breaker : t == Tool::Uplink ? &g.deck.uplink : t == Tool::Shroud ? &g.deck.shroud : &g.deck.lens;
    *lvl += 1;
    return {true, std::string(ti.name) + " upgraded to level " + std::to_string(*lvl) + "."};
}
