#include "rack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "balance.h"
#include "text.h"
#include "world.h"

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
    static const char* serial[kJobCount] = {"RLY-", "PRX-", "SCT-", "CMP-", "JAM-", "VLT-", "GW-"};
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
    if (m.active && m.bay >= 0) {   // out of a subnet, into the main rack
        if (g.net.activeCount() >= g.net.slots) return {false, "No free main-rack slot. Switch something off or expand the rack."};
        m.bay = -1;
        return {true, m.name + " moves out of the subnet into the main rack."};
    }
    if (m.active) return {false, "Already running."};
    if (m.dark()) return {false, m.name + " is dark (condition 0). Repair it first."};
    if (g.net.activeCount() >= g.net.slots) return {false, "No free slot. Switch something off or expand the rack."};
    m.active = true;
    return {true, m.name + " hums to life. Upkeep " + txt::cr(upkeepOf(m)) + " per run."};
}

Result deactivate(GameState& g, Machine& m) {
    if (m.home) return {false, m.name + " is always running - it's the rack's heart."};
    if (!m.active) return {false, "Already in storage."};
    if (m.bay >= 0) return unplug(g, m);
    m.active = false;
    std::string msg = m.name + " goes to cold storage. Silent, free, no wear.";
    if (g.net.decoyId == m.id) {
        g.net.decoyId = -1;
        msg += " Storing it disarmed the decoy.";
    }
    return {true, msg};
}

Result rejob(GameState& g, Machine& m, Job j) {
    if (m.chairIron()) return {false, m.name + " is chairman iron: it was built for one job and its firmware is signed. It can't be re-imaged."};
    if (g.net.decoyId == m.id) return {false, m.name + " is armed as the decoy. Disarm it before you change its job."};
    if (m.job == j) return {false, "It already does that."};
    if (m.job == Job::Gateway && !g.net.bayMembers(m.id).empty()) return {false, "Unplug " + m.name + "'s subnet before you change its job."};
    if (j == Job::Gateway && m.bay >= 0) return {false, "A gateway can't live inside another subnet. Unplug it first."};
    int fee = rejobFee(m);
    if (g.credits < fee) return {false, "Re-jobbing " + m.name + " costs " + txt::cr(fee) + "."};
    g.credits -= fee;
    m.job = j;
    std::string msg = m.name + " is now a " + jobName(j) + ". Same tier, condition, signal, name and history.";
    if (j == Job::Scout) msg += " (Scouting charges are not refilled.)";
    return {true, msg};
}

Result repairOne(GameState& g, Machine& m) {
    if (m.home) return {false, m.name + " never wears. Nothing to repair."};
    if (m.condition >= 100) return {false, "Already pristine."};
    int need = 100 - m.condition;
    int per = repairPerPoint(m);
    int pts = std::min(need, g.credits / per);
    if (pts <= 0) return {false, "You can't afford a single point."};
    m.condition += pts;
    int cost = pts * per;
    g.credits -= cost;
    g.stats.repairsPaid += cost;
    std::string msg = m.name + ": +" + std::to_string(pts) + " condition for " + txt::cr(cost) + ".";
    if (m.condition < 100) msg += " That's all you could afford.";
    if (!m.active) msg += " It stays in storage until you switch it on.";
    return {true, msg};
}

int repairAllCost(const GameState& g) {
    int c = 0;
    for (auto& m : g.net.machines) c += repairCost(m);
    return c;
}

Result repairAll(GameState& g) {
    int total = 0, count = 0;
    for (auto& m : g.net.machines) {
        if (m.home || m.condition >= 100) continue;
        int before = g.credits;
        if (repairOne(g, m).ok) { total += before - g.credits; ++count; }
    }
    if (count == 0) return {false, repairAllCost(g) > 0 ? "Nothing repaired - you're broke." : "Everything is pristine."};
    return {true, "Repaired " + std::to_string(count) + " machine(s) for " + txt::cr(total) + "."};
}

Result scrap(GameState& g, int id) {
    Machine* m = g.net.find(id);
    if (!m) return {false, "No such machine."};
    if (m->home) return {false, m->name + " can never be scrapped."};
    if (m->chairIron()) return {false, m->name + " is chairman iron. Nobody in the city would touch its parts - store it instead."};
    if (m->job == Job::Gateway && !g.net.bayMembers(id).empty()) return {false, "Unplug " + m->name + "'s subnet first."};
    int value = scrapValue(*m);
    std::string name = m->name;
    if (g.net.decoyId == id) g.net.decoyId = -1;
    auto& v = g.net.machines;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const Machine& x) { return x.id == id; }), v.end());
    if (g.net.decoyId >= 0 && !g.net.decoy()) g.net.decoyId = -1;
    g.credits += value;
    return {true, name + " is stripped for parts. +" + txt::cr(value) + "."};
}

bool decoyNeedsConfirm(const Machine& m) { return !m.captured || m.job == Job::Gateway; }   // a gateway takes its subnet with it

Result armDecoy(GameState& g, Machine& m, bool confirmed) {
    if (m.home) return {false, "Never " + m.name + ". There has to be one thing you cannot lose."};
    if (g.net.decoyId == m.id) return {false, m.name + " is already the decoy."};
    if (m.chairIron()) return {false, m.name + " is chairman iron. HELIOS would know exactly whose it was - it can never be the decoy."};
    if (m.job != Job::Proxy && m.job != Job::Gateway) return {false, "Only a PROXY - or a whole subnet, by its GATEWAY - can be a decoy."};
    if (!m.active) return {false, "It has to be in the rack, running."};
    if (m.dark()) return {false, "It's dark. A dead machine can't fool HELIOS."};
    if (!g.net.online(m)) return {false, m.name + " isn't online (its subnet is off, browned out or closed)."};
    if (m.job == Job::Gateway && !g.net.bayOnline(m)) return {false, m.name + "'s subnet has to be open and switched on."};
    if (decoyNeedsConfirm(m) && !confirmed) return {false, "confirm"};
    std::string msg;
    if (g.net.decoyId >= 0)
        if (const Machine* old = g.net.find(g.net.decoyId)) msg = old->name + " is no longer the decoy. ";
    g.net.decoyId = m.id;
    if (m.job == Job::Gateway)
        return {true, msg + m.name + " and its whole subnet are armed. If HELIOS closes the ticket, every machine in it burns with the gateway."};
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

// ---------------------------------------------------------------------------
// Deck programs
// ---------------------------------------------------------------------------
int programCost(Program p) { return bal::PROGRAM_COST[static_cast<int>(p)]; }

int freeProgramSlot(const GameState& g) {
    for (int i = 0; i < kProgramSlots; ++i)
        if (g.programs[i] == Program::None) return i;
    return -1;
}

Result buyProgram(GameState& g, Program p) {
    if (p == Program::None) return {false, "Nothing to load."};
    int slot = freeProgramSlot(g);
    if (slot < 0) return {false, "Both program slots are loaded. Unload one first."};
    int cost = programCost(p);
    if (g.credits < cost) return {false, std::string(programName(p)) + " costs " + txt::cr(cost) + "."};
    g.credits -= cost;
    g.programs[slot] = p;
    return {true, std::string(programName(p)) + " loaded into slot " + std::to_string(slot + 1) + ". It runs once, then it's gone."};
}

Result unloadProgram(GameState& g, int slot) {
    if (slot < 0 || slot >= kProgramSlots || g.programs[slot] == Program::None) return {false, "That slot is empty."};
    Program p = g.programs[slot];
    int refund = programCost(p);
    g.programs[slot] = Program::None;
    g.credits += refund;
    return {true, std::string(programName(p)) + " unloaded. Unused, so it's a full refund: +" + txt::cr(refund) + "."};
}

// ---------------------------------------------------------------------------
// Subnets
// ---------------------------------------------------------------------------
bool canPlug(const GameState& g, const Machine& m, const Machine& gw, std::string* why) {
    auto no = [&](const std::string& s) { if (why) *why = s; return false; };
    if (gw.job != Job::Gateway) return no(gw.name + " isn't a gateway.");
    if (!g.net.bayOpen(gw)) return no(gw.name + " has no open subnet (it must be running, and only " + std::to_string(bal::MAX_BAYS) + " subnets open).");
    if (m.home) return no("N0 stays the heart of the main rack.");
    if (m.job == Job::Gateway) return no("A gateway can't sit inside another subnet.");
    if (m.dark()) return no(m.name + " is dark. Repair it first.");
    if (m.bay == gw.id && m.active) return no(m.name + " is already in that subnet.");
    if (static_cast<int>(g.net.bayMembers(gw.id).size()) >= g.net.baySlots(gw)) return no(gw.name + "'s subnet is full.");
    int draw = bal::POWER_DRAW[std::clamp(m.tier, 1, kMaxTier)];
    if (g.net.bayPower(gw.id) + draw > g.net.bayBudget(gw))
        return no(m.name + " draws " + std::to_string(draw) + " power; " + gw.name + " has " +
                  std::to_string(g.net.bayBudget(gw) - g.net.bayPower(gw.id)) + " left.");
    return true;
}

Result plugIn(GameState& g, Machine& m, Machine& gw) {
    std::string why;
    if (!canPlug(g, m, gw, &why)) return {false, why};
    bool fromRack = m.active && m.bay < 0;
    m.bay = gw.id;
    m.active = true;
    std::string msg = m.name + " plugs into " + gw.name + "'s subnet" + (fromRack ? ", freeing a main-rack slot." : ".");
    if (g.net.clusterBonus(gw.id)) msg += " CLUSTER: three matching machines - they all act one tier higher.";
    return {true, msg};
}

Result unplug(GameState& g, Machine& m) {
    if (m.bay < 0) return {false, m.name + " isn't in a subnet."};
    m.bay = -1;
    m.active = false;
    std::string msg = m.name + " is unplugged and goes to cold storage.";
    if (g.net.decoyId >= 0 && !g.net.decoy()) {   // the subnet it armed is now empty
        g.net.decoyId = -1;
        msg += " That disarmed the decoy.";
    }
    return {true, msg};
}

Result setRole(GameState& g, Machine& gw, BayRole r) {
    (void)g;
    if (gw.job != Job::Gateway) return {false, "Only a gateway's subnet has a role."};
    if (gw.role == r) return {false, "It already runs " + std::string(roleName(r)) + "."};
    gw.role = r;
    return {true, gw.name + "'s subnet now runs " + roleName(r) + ". " + roleText(r)};
}

Result toggleBay(GameState& g, Machine& gw) {
    if (gw.job != Job::Gateway) return {false, "Not a gateway."};
    gw.bayOn = !gw.bayOn;
    if (g.net.decoyId >= 0 && !g.net.decoy()) g.net.decoyId = -1;
    return {true, gw.name + "'s subnet is " + (gw.bayOn ? "ON. It bills and wears with the rest." : "OFF. No bill, no wear, no effect.")};
}

// ---------------------------------------------------------------------------
// Running lean (0.8): STANDBY, SIGNAL SCRUB, HOUSE POWER
// ---------------------------------------------------------------------------
bool canStandby(const GameState& g, const Machine& m, std::string* why) {
    auto no = [&](const std::string& s) { if (why) *why = s; return false; };
    if (m.home) return no("N0 is your home server: it never bills, so there's nothing to save.");
    if (!m.working()) return no(m.name + " isn't running.");
    if (g.net.decoyId == m.id) return no(m.name + " is the armed decoy. Disarm it first - a decoy on standby can't take a trace.");
    if (g.net.decoy()) {   // would this standby quietly disarm the decoy (e.g. the last member of a decoy subnet)?
        Network probe = g.net;
        if (Machine* pm = probe.find(m.id)) pm->standby = true;
        if (!probe.decoy()) return no("That would leave the armed decoy subnet empty. Disarm it first.");
    }
    return true;
}

Result setStandby(GameState& g, Machine& m, bool on) {
    if (m.standby == on) return {false, m.name + (on ? " is already on standby." : " is already awake.")};
    if (on) {
        std::string why;
        if (!canStandby(g, m, &why)) return {false, why};
    }
    m.standby = on;
    return {true, on ? m.name + " goes on STANDBY for tonight: not billed, not worn, no effect. Back on at the reckoning."
                     : m.name + " is awake again for tonight."};
}

void clearStandby(GameState& g) {
    for (auto& m : g.net.machines) m.standby = false;
}

int scrubFloor(const Machine& m) { return m.captured ? bal::SCRUB_FLOOR_CAPTURED : 1; }

int scrubSaving(const Machine& m) {
    if (m.home || m.noise <= scrubFloor(m)) return 0;
    Machine q = m;
    q.noise -= 1;
    return upkeepOf(m) - upkeepOf(q);
}

int scrubCost(const Machine& m) { return bal::SCRUB_PAYBACK_RUNS * scrubSaving(m); }

bool canScrub(const GameState& g, const Machine& m, std::string* why) {
    auto no = [&](const std::string& s) { if (why) *why = s; return false; };
    if (m.home) return no("N0 is already silent.");
    if (m.noise <= scrubFloor(m))
        return no(m.captured ? "Captured hardware never gets quieter than noise " + std::to_string(bal::SCRUB_FLOOR_CAPTURED) + "."
                             : "Bought hardware is already as quiet as it gets.");
    if (g.credits < scrubCost(m)) return no("A scrub costs " + std::to_string(scrubCost(m)) + "cr.");
    return true;
}

Result scrubSignal(GameState& g, Machine& m) {
    std::string why;
    if (!canScrub(g, m, &why)) return {false, why};
    int cost = scrubCost(m), save = scrubSaving(m);
    g.credits -= cost;
    m.noise -= 1;
    return {true, m.name + " is scrubbed to noise " + std::to_string(m.noise) + ": -" + std::to_string(cost) + "cr once, " + std::to_string(save) +
                      "cr less on every bill. Upkeep now " + std::to_string(upkeepOf(m)) + "cr/run."};
}

void tidyHousePower(GameState& g) {
    auto& h = g.net.house;
    if (h.size() > static_cast<size_t>(kTowerCount)) h.resize(kTowerCount);
    for (size_t k = 0; k < h.size(); ++k) {
        const Machine* gw = h[k] >= 0 ? g.net.find(h[k]) : nullptr;
        bool dup = false;
        for (size_t j = 0; j < k; ++j) dup = dup || (h[j] == h[k] && h[k] >= 0);
        if (h[k] >= 0 && (!towerOwned(g, static_cast<int>(k)) || !gw || gw->job != Job::Gateway || gw->home || dup)) h[k] = -1;
    }
    while (!h.empty() && h.back() < 0) h.pop_back();
}

Result setHousePower(GameState& g, int tower, int gatewayId) {
    if (!towerOwned(g, tower)) return {false, "Own the tower first: HOUSE POWER comes with its Boardroom."};
    auto& h = g.net.house;
    if (h.size() < static_cast<size_t>(kTowerCount)) h.resize(kTowerCount, -1);
    if (gatewayId < 0) {
        h[static_cast<size_t>(tower)] = -1;
        tidyHousePower(g);
        return {true, std::string("The tower's house power is unassigned.")};
    }
    const Machine* gw = g.net.find(gatewayId);
    if (!gw || gw->job != Job::Gateway || gw->home) return {false, "HOUSE POWER feeds a subnet: pick a gateway."};
    for (auto& x : h) if (x == gatewayId) x = -1;   // a subnet takes one tower's power at most
    h[static_cast<size_t>(tower)] = gatewayId;
    tidyHousePower(g);
    return {true, gw->name + "'s subnet now runs on HOUSE POWER: its bill is halved."};
}
