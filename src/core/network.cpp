// GhostNet - the rules of owning machines.
#include <algorithm>
#include <cctype>

#include "balance.h"
#include "common.h"

const char* jobName(Job j) {
    switch (j) {
        case Job::Relay: return "RELAY";
        case Job::Proxy: return "PROXY";
        case Job::Scout: return "SCOUT";
        case Job::Compute: return "COMPUTE";
        case Job::Jammer: return "JAMMER";
        case Job::Vault: return "VAULT";
        case Job::Gateway: return "GATEWAY";
    }
    return "?";
}
const char* jobLower(Job j) {
    switch (j) {
        case Job::Relay: return "relay";
        case Job::Proxy: return "proxy";
        case Job::Scout: return "scout";
        case Job::Compute: return "compute";
        case Job::Jammer: return "jammer";
        case Job::Vault: return "vault";
        case Job::Gateway: return "gateway";
    }
    return "?";
}
bool parseJob(const std::string& in, Job& out) {
    std::string s;
    for (char c : in) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "relay" || s == "r") { out = Job::Relay; return true; }
    if (s == "proxy" || s == "p") { out = Job::Proxy; return true; }
    if (s == "scout" || s == "s" || s == "scouting") { out = Job::Scout; return true; }
    if (s == "compute" || s == "c") { out = Job::Compute; return true; }
    if (s == "jammer" || s == "j") { out = Job::Jammer; return true; }
    if (s == "vault" || s == "v") { out = Job::Vault; return true; }
    if (s == "gateway" || s == "g") { out = Job::Gateway; return true; }
    return false;
}

const char* traitName(Trait t) {
    switch (t) {
        case Trait::None: return "";
        case Trait::Salvaged: return "SALVAGED";
        case Trait::OldNet: return "OLD NET";
        case Trait::HotIron: return "HOT IRON";
        case Trait::CorpBuild: return "CORPORATE BUILD";
        case Trait::Unstable: return "UNSTABLE";
    }
    return "";
}
const char* traitText(Trait t) {
    switch (t) {
        case Trait::None: return "";
        case Trait::Salvaged: return "Street hardware: repairs cost 1cr a point instead of 2, but it wears 2 more every run.";
        case Trait::OldNet: return "Built before HELIOS: normal wear never takes it below 30 condition, but re-imaging costs double.";
        case Trait::HotIron: return "HELIOS wants it back: if it dies as your decoy the fine is waived entirely, but it bills 10% more.";
        case Trait::CorpBuild: return "Over-engineered: it wears at 60% of the normal rate, but it bills 20% more.";
        case Trait::Unstable: return "HELIOS prototype: tier 4, but it wears double, repairs cost 3cr a point and it bills 40% more.";
    }
    return "";
}

const char* roleName(BayRole r) {
    switch (r) {
        case BayRole::Stealth: return "STEALTH";
        case BayRole::Speed: return "SPEED";
        case BayRole::Siege: return "SIEGE";
    }
    return "?";
}
const char* roleText(BayRole r) {
    switch (r) {
        case BayRole::Stealth: return "The subnet routes your noise: the trace runs slower.";
        case BayRole::Speed: return "The subnet stripes your downloads: they run faster.";
        case BayRole::Siege: return "The subnet works the door: +1 emergency attempt, softer alarm spikes.";
    }
    return "";
}
const char* strikeName(BayRole r) {
    switch (r) {
        case BayRole::Stealth: return "BLACKOUT";
        case BayRole::Speed: return "MIRROR";
        case BayRole::Siege: return "CRACK";
    }
    return "?";
}
const char* strikeText(BayRole r) {
    switch (r) {
        case BayRole::Stealth: return "Once per run: the trace stands still for 8 seconds.";
        case BayRole::Speed: return "Once per run: 12 seconds of the current download land instantly.";
        case BayRole::Siege: return "Once per run: reveals 2 boxes of the lock in front of you.";
    }
    return "";
}

const char* programName(Program p) {
    switch (p) {
        case Program::None: return "EMPTY";
        case Program::Freeze: return "FREEZE";
        case Program::Peek: return "PEEK";
        case Program::Spoof: return "SPOOF";
        case Program::Burst: return "BURST";
    }
    return "?";
}
const char* programText(Program p) {
    switch (p) {
        case Program::None: return "";
        case Program::Freeze: return "The trace stands still for 4 seconds.";
        case Program::Peek: return "Reveals one position of the lock in front of you.";
        case Program::Spoof: return "The next bait file you take is defused: no alarm, no damage.";
        case Program::Burst: return "Your next 5 download seconds run at double speed, with the trace 50% faster.";
    }
    return "";
}

Machine* Network::find(int id) {
    for (auto& m : machines) if (m.id == id) return &m;
    return nullptr;
}
const Machine* Network::find(int id) const {
    for (auto& m : machines) if (m.id == id) return &m;
    return nullptr;
}
Machine* Network::home() {
    for (auto& m : machines) if (m.home) return &m;
    return nullptr;
}
const Machine* Network::home() const {
    for (auto& m : machines) if (m.home) return &m;
    return nullptr;
}
int Network::externalCount() const {
    int n = 0;
    for (auto& m : machines) if (!m.home) ++n;
    return n;
}
int Network::activeCount() const {
    int n = 0;
    for (auto& m : machines) if (m.active && m.bay < 0) ++n;
    return n;
}

// ---- Subnets -----------------------------------------------------------------
bool Network::bayOpen(const Machine& gw) const {
    if (gw.job != Job::Gateway || gw.bay >= 0 || !gw.working()) return false;
    int before = 0;   // only the first MAX_BAYS working gateways (rack order) open a bay
    for (auto& m : machines) {
        if (&m == &gw) break;
        if (m.job == Job::Gateway && m.bay < 0 && m.working()) ++before;
    }
    return before < bal::MAX_BAYS;
}
int Network::bayPower(int gatewayId) const {
    int p = 0;
    for (auto& m : machines) if (m.bay == gatewayId && m.active && !m.standby) p += bal::POWER_DRAW[std::clamp(m.tier, 1, kMaxTier)];
    return p;
}
int Network::bayBudget(const Machine& gw) const {
    return gw.accord() ? bal::ACCORD_GATE_POWER : bal::GATEWAY_POWER[std::clamp(gw.tier, 1, kMaxTier)];
}
int Network::baySlots(const Machine& gw) const { return gw.accord() ? bal::ACCORD_GATE_SLOTS : bal::BAY_SLOTS; }
// Online = open, switched on, within its power budget and with at least one machine plugged in.
// LOADSHED: while the grid sheds, only a subnet on HOUSE POWER stays lit.
bool Network::bayOnline(const Machine& gw) const {
    if (!bayOpen(gw) || !gw.bayOn || gw.standby || bayPower(gw.id) > bayBudget(gw)) return false;
    if (shed && !housePowered(gw.id)) return false;
    for (auto& m : machines) if (m.bay == gw.id && m.active && !m.dark() && !m.standby) return true;
    return false;
}
std::vector<const Machine*> Network::bayMembers(int gatewayId) const {
    std::vector<const Machine*> v;
    for (auto& m : machines) if (m.bay == gatewayId && m.active) v.push_back(&m);
    return v;
}
std::vector<const Machine*> Network::openBays() const {
    std::vector<const Machine*> v;
    for (auto& m : machines) if (bayOpen(m)) v.push_back(&m);
    return v;
}
bool Network::online(const Machine& m) const {
    if (!m.working() || m.standby) return false;
    if (m.bay < 0) return true;
    const Machine* gw = find(m.bay);
    return gw && bayOnline(*gw);
}
int Network::bayUpkeep(int gatewayId) const {
    int t = 0;
    for (auto* m : bayMembers(gatewayId)) if (online(*m)) t += upkeepOf(*m);
    return t;
}
int Network::houseTower(int gatewayId) const {
    const Machine* gw = find(gatewayId);
    if (!gw || gw->job != Job::Gateway || gw->home) return -1;
    for (size_t k = 0; k < house.size(); ++k) if (house[k] == gatewayId) return static_cast<int>(k);
    return -1;
}
bool Network::housePowered(int gatewayId) const { return houseTower(gatewayId) >= 0; }
int Network::bayBill(int gatewayId) const {
    int raw = bayUpkeep(gatewayId);
    return housePowered(gatewayId) ? (raw + 1) / 2 : raw;
}
int Network::clusterBonus(int gatewayId) const {
    auto v = bayMembers(gatewayId);
    if (static_cast<int>(v.size()) < bal::BAY_SLOTS) return 0;
    for (auto* m : v)
        if (m->job != v[0]->job || m->tier < 3 || m->dark() || m->standby) return 0;
    return 1;
}
int Network::effTier(const Machine& m) const {
    int t = m.tier;
    if (m.bay >= 0 && online(m)) {   // a cluster and an ACCORD gateway don't stack: either lifts the tier once
        const Machine* gw = find(m.bay);
        t += std::max(clusterBonus(m.bay), gw && gw->accord() ? 1 : 0);
    }
    return std::clamp(t, 1, kMaxTier);
}

int jobValue(const Machine& m, int tier) {
    int t = std::clamp(tier > 0 ? tier : m.tier, 1, kMaxTier);
    if (m.accord() && m.job == Job::Relay) return bal::ACCORD_RELAY;
    if (m.accord() && m.job == Job::Proxy) return bal::ACCORD_PROXY;
    switch (m.job) {
        case Job::Relay: return bal::RELAY_BONUS[t];
        case Job::Proxy: return bal::PROXY_BONUS[t];
        default: return 1;
    }
}
// Relay and proxy pool per unit: the main rack has its cap, each online subnet has its own cap
// (plus its role bonus when the role matches), and the total sits under a hard ceiling.
// An ACCORD relay or proxy lifts the cap of the pool it runs in (the hard ceiling never moves).
namespace {
int pooledBonus(const Network& n, Job job, int rackCap, int bayCap, int hardCap, int lift, BayRole role, const int (&roleBonus)[kMaxTier + 1]) {
    int rack = 0;
    bool rackAccord = false;
    for (auto& m : n.machines)
        if (m.bay < 0 && m.job == job && n.online(m)) {
            rack += jobValue(m, n.effTier(m));
            rackAccord = rackAccord || m.accord();
        }
    int total = std::min(rack, rackCap + (rackAccord ? lift : 0));
    for (auto* gw : n.openBays()) {
        if (!n.bayOnline(*gw)) continue;
        int bay = gw->role == role ? roleBonus[std::clamp(gw->tier, 1, kMaxTier)] : 0;
        bool bayAccord = false;
        for (auto* m : n.bayMembers(gw->id))
            if (m->job == job && n.online(*m)) {
                bay += jobValue(*m, n.effTier(*m));
                bayAccord = bayAccord || m->accord();
            }
        total += std::min(bay, bayCap + (bayAccord ? lift : 0));
    }
    return std::min(total, hardCap);
}
bool anyAccord(const Network& n, Job job) {
    for (auto& m : n.machines) if (m.accord() && m.job == job && n.online(m)) return true;
    return false;
}
}  // namespace

int Network::relayBonus() const {
    return pooledBonus(*this, Job::Relay, bal::RELAY_CAP, bal::BAY_RELAY_CAP, bal::RELAY_HARD_CAP, bal::ACCORD_RELAY_LIFT, BayRole::Speed,
                       bal::SPEED_BONUS);
}
int Network::proxyBonus() const {
    return pooledBonus(*this, Job::Proxy, bal::PROXY_CAP, bal::BAY_PROXY_CAP, bal::PROXY_HARD_CAP, bal::ACCORD_PROXY_LIFT, BayRole::Stealth,
                       bal::STEALTH_BONUS);
}
int Network::computeCapacity() const {
    int t = 0, siege = 0;
    for (auto& m : machines) if (online(m) && m.job == Job::Compute) t += m.accord() ? bal::ACCORD_COMPUTE : 1;
    for (auto* gw : openBays()) if (bayOnline(*gw) && gw->role == BayRole::Siege) ++siege;
    return std::min(t, bal::COMPUTE_MAX + (anyAccord(*this, Job::Compute) ? 1 : 0)) + siege;
}
int Network::scoutCapacity() const {
    int t = 0;
    for (auto& m : machines)
        if (online(m) && m.job == Job::Scout) t += m.accord() ? bal::ACCORD_SCOUT_CHARGES : bal::SCOUT_CHARGES[effTier(m)];
    return std::min(t, bal::SCOUT_MAX_CHARGES);
}
int Network::usableScoutCharges() const { return std::min(scoutCharges, scoutCapacity()); }
int Network::bestScoutTier() const {
    // A report is read, not scouted: a scout on standby still vouches for the reports it wrote.
    if (standbyCount() > 0) {
        Network awake = *this;
        for (auto& m : awake.machines) m.standby = false;
        return awake.bestScoutTier();
    }
    int best = 0;
    for (auto& m : machines) if (online(m) && m.job == Job::Scout) best = std::max(best, std::min(3, effTier(m)));
    return best;
}
int Network::jamDelay() const {
    int t = 0;
    for (auto& m : machines) if (online(m) && m.job == Job::Jammer) t += m.accord() ? bal::ACCORD_JAM_DELAY : bal::JAM_DELAY[effTier(m)];
    return std::min(t, anyAccord(*this, Job::Jammer) ? bal::ACCORD_JAM_DELAY : bal::JAM_DELAY_CAP);
}
int Network::jamSpikeCut() const {
    int t = 0;
    for (auto& m : machines) if (online(m) && m.job == Job::Jammer) t += m.accord() ? bal::ACCORD_JAM_SPIKE : bal::JAM_SPIKE[effTier(m)];
    for (auto* gw : openBays())
        if (bayOnline(*gw) && gw->role == BayRole::Siege) t += bal::SIEGE_SPIKE[std::clamp(gw->tier, 1, kMaxTier)];
    return std::min(t, anyAccord(*this, Job::Jammer) ? bal::ACCORD_JAM_SPIKE : bal::JAM_SPIKE_CAP);
}
const Machine* Network::bestVault() const {
    const Machine* best = nullptr;
    for (auto& m : machines)
        if (online(m) && m.job == Job::Vault && (!best || (m.accord() && !best->accord()) || (m.accord() == best->accord() && effTier(m) > effTier(*best))))
            best = &m;
    return best;
}
int upkeepOf(const Machine& m) {
    if (m.home) return 0;
    if (m.accord()) return bal::ACCORD_UPKEEP;   // chairman iron: signal 1, no trait, one flat number
    int base = bal::TIER_UPKEEP[std::clamp(m.tier, 1, kMaxTier)] + (m.noise - 1) * bal::NOISE_UPKEEP;
    int pct = m.trait == Trait::CorpBuild ? bal::CORP_UPKEEP_PCT : m.trait == Trait::HotIron ? bal::HOTIRON_UPKEEP_PCT
            : m.trait == Trait::Unstable ? bal::UNSTABLE_UPKEEP_PCT : 0;
    return base + (base * pct + 99) / 100;
}
int repairPerPoint(const Machine& m) {
    return m.trait == Trait::Salvaged ? bal::SALVAGED_REPAIR : m.trait == Trait::Unstable ? bal::UNSTABLE_REPAIR : bal::REPAIR_PER_POINT;
}
int repairCost(const Machine& m) { return m.home ? 0 : (100 - m.condition) * repairPerPoint(m); }
int rejobFee(const Machine& m) { return bal::REIMAGINE_FEE * (m.trait == Trait::OldNet ? bal::OLDNET_REJOB_MULT : 1); }
int conditionAfterRun(const Machine& m, bool traced) {
    if (m.home) return m.condition;
    int w = m.captured ? bal::WEAR_NORMAL_CAPTURED : bal::WEAR_NORMAL_BOUGHT;
    if (traced) w += m.captured ? bal::WEAR_TRACED_EXTRA_CAPTURED : bal::WEAR_TRACED_EXTRA_BOUGHT;
    if (m.trait == Trait::Salvaged) w += bal::SALVAGED_EXTRA_WEAR;
    if (m.trait == Trait::CorpBuild) w = (w * bal::CORP_WEAR_PCT + 99) / 100;
    if (m.trait == Trait::Unstable) w *= bal::UNSTABLE_WEAR_MULT;
    int after = std::max(0, m.condition - w);
    // Old Net iron shrugs off ordinary nights; a trace-out still hurts it.
    if (m.trait == Trait::OldNet && !traced) after = std::max(after, std::min(m.condition, bal::OLDNET_FLOOR));
    return after;
}
int scrapValue(const Machine& m) {
    if (m.home) return 0;
    return m.captured ? bal::SCRAP_CAPTURED[std::clamp(m.tier, 1, kMaxTier)] : m.price * bal::SCRAP_BOUGHT_PCT / 100;
}
int Network::upkeepTotal() const {
    int t = 0;
    for (auto& m : machines) if (online(m) && !m.home && m.bay < 0) t += upkeepOf(m);   // the main rack
    for (auto& m : machines)
        if (m.job == Job::Gateway && m.bay < 0) t += bayBill(m.id);   // each subnet bills on its own
    return t;
}
int Network::upkeepAwake() const {
    Network n = *this;
    for (auto& m : n.machines) m.standby = false;
    return n.upkeepTotal();
}
int Network::standbyCount() const {
    int c = 0;
    for (auto& m : machines) c += m.standby ? 1 : 0;
    return c;
}
const Machine* Network::decoy() const {
    const Machine* m = find(decoyId);
    if (!m || m->home || !online(*m)) return nullptr;
    if (m->accord()) return nullptr;   // chairman iron never takes a ticket: HELIOS would know whose it was
    if (m->job == Job::Gateway) return bayOnline(*m) ? m : nullptr;   // the whole subnet takes the ticket
    if (m->job != Job::Proxy) return nullptr;
    return m;
}
void Network::refillScout() { scoutCharges = scoutCapacity(); }

std::string describeEffect(const Machine& m, int tier) {
    const int t = std::clamp(tier > 0 ? tier : m.tier, 1, kMaxTier);
    const std::string cap = m.bay >= 0 ? "subnet cap " : "rack cap ";
    if (m.accord()) return accordEffect(m.job, m.bay >= 0);
    std::string s;
    switch (m.job) {
        case Job::Relay:
            s = "Speeds downloads by " + std::to_string(jobValue(m, t)) + "% (" + cap +
                std::to_string(m.bay >= 0 ? bal::BAY_RELAY_CAP : bal::RELAY_CAP) + "%).";
            break;
        case Job::Proxy:
            s = "Slows the trace by " + std::to_string(jobValue(m, t)) + "% (" + cap +
                std::to_string(m.bay >= 0 ? bal::BAY_PROXY_CAP : bal::PROXY_CAP) + "%). Can be armed as a decoy.";
            break;
        case Job::Scout:
            s = "Adds " + std::to_string(bal::SCOUT_CHARGES[t]) + " scouting charge(s); tier " + std::to_string(std::min(t, 3)) + " reports" +
                (t >= 3 ? " (full detail)." : t == 2 ? " (security, objective, files)." : " (lock + trace speed only).");
            break;
        case Job::Compute:
            s = "Holds 1 emergency lock attempt per run" +
                std::string(m.home ? " (free, never wears)." : " (wears " + std::to_string(bal::COMPUTE_WEAR[t]) + " when used).");
            break;
        case Job::Jammer:
            s = "Holds a watchdog's acceleration back " + std::to_string(bal::JAM_DELAY[t]) + "s and softens alarm spikes by " +
                std::to_string(bal::JAM_SPIKE[t]) + "% (network caps " + std::to_string(bal::JAM_DELAY_CAP) + "s / " +
                std::to_string(bal::JAM_SPIKE_CAP) + "%).";
            break;
        case Job::Vault:
            s = "If you're traced out, keeps " + std::to_string(bal::VAULT_KEEP[t]) + "% of your " +
                (bal::VAULT_FILES[t] > 1 ? std::to_string(bal::VAULT_FILES[t]) + " most valuable downloaded files." : "single most valuable downloaded file.") +
                " Only your best vault counts.";
            break;
        case Job::Gateway:
            s = "Opens a SUBNET: " + std::to_string(bal::BAY_SLOTS) + " extra slots with " + std::to_string(bal::GATEWAY_POWER[std::clamp(m.tier, 1, kMaxTier)]) +
                " power, its own role, its own bill and its own caps. Up to " + std::to_string(bal::MAX_BAYS) + " subnets.";
            break;
    }
    if (tier > m.tier) s += " Acting T" + std::to_string(t) + " in a cluster.";
    return s;
}

std::string accordEffect(Job j, bool inSubnet) {
    const std::string pool = inSubnet ? "its subnet's" : "the rack's";
    switch (j) {
        case Job::Relay:
            return "ACCORD. Speeds downloads by " + std::to_string(bal::ACCORD_RELAY) + "% and lifts " + pool + " relay cap by " +
                   std::to_string(bal::ACCORD_RELAY_LIFT) + " (rack " + std::to_string(bal::RELAY_CAP + bal::ACCORD_RELAY_LIFT) + "%, hard ceiling " +
                   std::to_string(bal::RELAY_HARD_CAP) + "%).";
        case Job::Proxy:
            return "ACCORD. Slows the trace by " + std::to_string(bal::ACCORD_PROXY) + "% and lifts " + pool + " proxy cap by " +
                   std::to_string(bal::ACCORD_PROXY_LIFT) + " (rack " + std::to_string(bal::PROXY_CAP + bal::ACCORD_PROXY_LIFT) + "%, hard ceiling " +
                   std::to_string(bal::PROXY_HARD_CAP) + "%). Never a decoy.";
        case Job::Scout:
            return "ACCORD. " + std::to_string(bal::ACCORD_SCOUT_CHARGES) + " scouting charges, full-detail reports - and every new report on a "
                   "tower floor or service site gives up one doctrine secret: its maintenance phrase, or one FORTRESS keypad box.";
        case Job::Compute:
            return "ACCORD. Holds " + std::to_string(bal::ACCORD_COMPUTE) + " emergency attempts per run and lifts the compute cap to " +
                   std::to_string(bal::COMPUTE_MAX + 1) + ".";
        case Job::Jammer:
            return "ACCORD. Holds a watchdog back " + std::to_string(bal::ACCORD_JAM_DELAY) + "s, softens alarm spikes by " +
                   std::to_string(bal::ACCORD_JAM_SPIKE) + "% (the caps rise to match) - and once a run, when the trace reaches " +
                   std::to_string(bal::ACCORD_FREEZE_AT) + "%, it FREEZES it for " + std::to_string(bal::FREEZE_SECS) + "s on its own.";
        case Job::Vault:
            return "ACCORD. If you're traced out, keeps 100% of your " + std::to_string(bal::ACCORD_VAULT_FILES) +
                   " most valuable files - and one tower objective you took stays taken, offline, for next time.";
        case Job::Gateway:
            return "ACCORD. Opens a SUBNET of " + std::to_string(bal::ACCORD_GATE_SLOTS) + " slots with " + std::to_string(bal::ACCORD_GATE_POWER) +
                   " power, and every machine in it acts a tier higher (a cluster adds nothing on top).";
    }
    return "";
}
