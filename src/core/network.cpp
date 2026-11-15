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
    }
    return "?";
}
const char* jobLower(Job j) {
    switch (j) {
        case Job::Relay: return "relay";
        case Job::Proxy: return "proxy";
        case Job::Scout: return "scout";
        case Job::Compute: return "compute";
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
    return false;
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
int upkeepOf(const Machine& m) {
    if (m.home) return 0;
    return bal::TIER_UPKEEP[m.tier] + (m.noise - 1) * bal::NOISE_UPKEEP;
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
    }
    return s;
}
