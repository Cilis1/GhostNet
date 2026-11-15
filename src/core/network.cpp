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
    return false;
}

const char* traitName(Trait t) {
    switch (t) {
        case Trait::None: return "";
        case Trait::Salvaged: return "SALVAGED";
        case Trait::OldNet: return "OLD NET";
        case Trait::HotIron: return "HOT IRON";
        case Trait::CorpBuild: return "CORPORATE BUILD";
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
    for (auto& m : machines) if (m.active) ++n;
    return n;
}
int jobValue(const Machine& m) {
    switch (m.job) {
        case Job::Relay: return bal::RELAY_BONUS[m.tier];
        case Job::Proxy: return bal::PROXY_BONUS[m.tier];
        default: return 1;
    }
}
int Network::relayBonus() const {
    int t = 0;
    for (auto& m : machines) if (m.working() && m.job == Job::Relay) t += jobValue(m);
    return std::min(t, bal::RELAY_CAP);
}
int Network::proxyBonus() const {
    int t = 0;
    for (auto& m : machines) if (m.working() && m.job == Job::Proxy) t += jobValue(m);
    return std::min(t, bal::PROXY_CAP);
}
int Network::computeCapacity() const {
    int t = 0;
    for (auto& m : machines) if (m.working() && m.job == Job::Compute) ++t;
    return std::min(t, bal::COMPUTE_MAX);
}
int Network::scoutCapacity() const {
    int t = 0;
    for (auto& m : machines) if (m.working() && m.job == Job::Scout) ++t;
    return std::min(t, bal::SCOUT_MAX_CHARGES);
}
int Network::usableScoutCharges() const { return std::min(scoutCharges, scoutCapacity()); }
int Network::bestScoutTier() const {
    int best = 0;
    for (auto& m : machines) if (m.working() && m.job == Job::Scout) best = std::max(best, m.tier);
    return best;
}
int Network::jamDelay() const {
    int t = 0;
    for (auto& m : machines) if (m.working() && m.job == Job::Jammer) t += bal::JAM_DELAY[m.tier];
    return std::min(t, bal::JAM_DELAY_CAP);
}
int Network::jamSpikeCut() const {
    int t = 0;
    for (auto& m : machines) if (m.working() && m.job == Job::Jammer) t += bal::JAM_SPIKE[m.tier];
    return std::min(t, bal::JAM_SPIKE_CAP);
}
const Machine* Network::bestVault() const {
    const Machine* best = nullptr;
    for (auto& m : machines)
        if (m.working() && m.job == Job::Vault && (!best || m.tier > best->tier)) best = &m;
    return best;
}
int upkeepOf(const Machine& m) {
    if (m.home) return 0;
    int base = bal::TIER_UPKEEP[m.tier] + (m.noise - 1) * bal::NOISE_UPKEEP;
    int pct = m.trait == Trait::CorpBuild ? bal::CORP_UPKEEP_PCT : m.trait == Trait::HotIron ? bal::HOTIRON_UPKEEP_PCT : 0;
    return base + (base * pct + 99) / 100;
}
int repairPerPoint(const Machine& m) { return m.trait == Trait::Salvaged ? bal::SALVAGED_REPAIR : bal::REPAIR_PER_POINT; }
int repairCost(const Machine& m) { return m.home ? 0 : (100 - m.condition) * repairPerPoint(m); }
int rejobFee(const Machine& m) { return bal::REIMAGINE_FEE * (m.trait == Trait::OldNet ? bal::OLDNET_REJOB_MULT : 1); }
int conditionAfterRun(const Machine& m, bool traced) {
    if (m.home) return m.condition;
    int w = m.captured ? bal::WEAR_NORMAL_CAPTURED : bal::WEAR_NORMAL_BOUGHT;
    if (traced) w += m.captured ? bal::WEAR_TRACED_EXTRA_CAPTURED : bal::WEAR_TRACED_EXTRA_BOUGHT;
    if (m.trait == Trait::Salvaged) w += bal::SALVAGED_EXTRA_WEAR;
    if (m.trait == Trait::CorpBuild) w = (w * bal::CORP_WEAR_PCT + 99) / 100;
    int after = std::max(0, m.condition - w);
    // Old Net iron shrugs off ordinary nights; a trace-out still hurts it.
    if (m.trait == Trait::OldNet && !traced) after = std::max(after, std::min(m.condition, bal::OLDNET_FLOOR));
    return after;
}
int scrapValue(const Machine& m) {
    if (m.home) return 0;
    return m.captured ? bal::SCRAP_CAPTURED[m.tier] : m.price * bal::SCRAP_BOUGHT_PCT / 100;
}
int Network::upkeepTotal() const {
    int t = 0;
    for (auto& m : machines) if (m.working() && !m.home) t += upkeepOf(m);
    return t;
}
const Machine* Network::decoy() const {
    const Machine* m = find(decoyId);
    if (!m || !m->working() || m->job != Job::Proxy || m->home) return nullptr;
    return m;
}
void Network::refillScout() { scoutCharges = scoutCapacity(); }

std::string describeEffect(const Machine& m) {
    std::string s;
    switch (m.job) {
        case Job::Relay:
            s = "Speeds downloads by " + std::to_string(jobValue(m)) + "% (network cap " +
                std::to_string(bal::RELAY_CAP) + "%).";
            break;
        case Job::Proxy:
            s = "Slows the trace by " + std::to_string(jobValue(m)) + "% (network cap " +
                std::to_string(bal::PROXY_CAP) + "%). The only job that can be armed as a decoy.";
            break;
        case Job::Scout:
            s = "Adds 1 scouting charge; tier " + std::to_string(m.tier) + " reports" +
                (m.tier >= 3 ? " (full detail)." : m.tier == 2 ? " (security, objective, files)." : " (lock + trace speed only).");
            break;
        case Job::Compute:
            s = "Holds 1 emergency lock attempt per run" +
                std::string(m.home ? " (free, never wears)." : " (wears " + std::to_string(bal::COMPUTE_WEAR[m.tier]) + " when used).");
            break;
        case Job::Jammer:
            s = "Holds a watchdog's acceleration back " + std::to_string(bal::JAM_DELAY[m.tier]) + "s and softens alarm spikes by " +
                std::to_string(bal::JAM_SPIKE[m.tier]) + "% (network caps " + std::to_string(bal::JAM_DELAY_CAP) + "s / " +
                std::to_string(bal::JAM_SPIKE_CAP) + "%).";
            break;
        case Job::Vault:
            s = "If you're traced out, keeps " + std::to_string(bal::VAULT_KEEP[m.tier]) +
                "% of your single most valuable downloaded file. Only your best vault counts.";
            break;
    }
    return s;
}
