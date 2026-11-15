// GhostNet - shared data types.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Machines & the network
// ---------------------------------------------------------------------------
enum class Job { Relay = 0, Proxy = 1, Scout = 2, Compute = 3 };

const char* jobName(Job j);    // "RELAY"
const char* jobLower(Job j);   // "relay"
bool parseJob(const std::string& s, Job& out);

struct Machine {
    int id = 0;
    std::string name;
    Job job = Job::Relay;
    int tier = 1;               // 1..3
    bool captured = false;
    bool home = false;
    bool active = false;        // in the rack (running) vs cold storage
    int condition = 100;        // 0..100, 0 = dark
    int noise = 1;              // 1..6
    int price = 0;              // what you paid (bought machines)
    std::string origin;         // where it came from
    bool usedThisRun = false;   // compute favour already given this run
    bool dark() const { return condition <= 0; }
    bool working() const { return active && !dark(); }
};

struct Network {
    std::vector<Machine> machines;   // machines[0] is always the home server once unlocked
    int slots = 3;
    int decoyId = -1;
    int scoutCharges = 0;            // stored charges ("ammunition")
    int nextId = 1;
    int burnedCount = 0;             // machines lost as decoys, forever

    Machine* find(int id);
    const Machine* find(int id) const;
    Machine* home();
    const Machine* home() const;
    int externalCount() const;
    int activeCount() const;
    int relayBonus() const;          // % (capped)
    int proxyBonus() const;          // % (capped)
    int computeCapacity() const;     // emergency attempts per run (capped)
    int scoutCapacity() const;       // max scout charges right now (capped)
    int usableScoutCharges() const;  // min(stored, capacity)
    int bestScoutTier() const;       // 0 if no working scout
    int upkeepTotal() const;
    const Machine* decoy() const;    // armed AND currently valid
    void refillScout();              // stored = capacity at this moment
};

int upkeepOf(const Machine& m);
int scrapValue(const Machine& m);    // what scrapping it would pay, exactly
int jobValue(const Machine& m);      // % for relay/proxy, 1 for scout/compute
std::string describeEffect(const Machine& m);

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
enum class Kind { Open, Hidden, Guarded, Corporate, Shrine, Story, Side };
enum class LockType { None, Keypad, Rotor, Cipher };
enum class FileKind { Paydata, Intel, Objective, Honeypot, Log };

const char* kindName(Kind k);
const char* lockName(LockType l);

struct File {
    std::string name;
    std::string dir;
    FileKind kind = FileKind::Paydata;
    int size = 1;               // MB
    int credits = 0;
    std::string text;           // readable text (intel / log / objective summary)
    int phraseFor = -1;         // target id whose passphrase this memo leaks
    int revealsSide = -1;       // side-route index this log reveals
    bool taken = false;
    bool read = false;
    bool downloadable() const { return kind != FileKind::Intel && kind != FileKind::Log; }
    bool readable() const { return kind == FileKind::Intel || kind == FileKind::Log; }
};

struct Target {
    int id = 0;
    std::string name, host, blurb;
    Kind kind = Kind::Open;
    int security = 1;           // 1..5
    LockType lock = LockType::None;
    bool backdoor = false;      // maintenance way in exists
    std::string passphrase;
    bool stealable = false;     // carries detachable hardware
    bool trail = false;         // carries the paper trail to a side route
    int trailSide = -1;
    bool jackpot = false;
    bool watchdog = false;      // trace accelerates the longer you stay
    std::vector<File> files;
    int storyIndex = -1;
    int sideIndex = -1;
    int reward = 0;             // contract payment (story)
    bool deep = false;          // THE DEEP JOB
    double traceOverride = 0;   // if > 0, replaces the security-based trace rate
    std::string infoReward;     // contracts that pay in information
    bool hasHoneypot() const;
    int objectiveCount() const;
    std::string objectiveDir() const;
};

// ---------------------------------------------------------------------------
// Player state
// ---------------------------------------------------------------------------
struct Deck { int breaker = 0, uplink = 0, shroud = 0, lens = 0; };

struct Stats {
    int runs = 0, jobs = 0, robberies = 0, traceouts = 0, sacrifices = 0;
    int captures = 0, walkaways = 0, unpaidBills = 0, deepAttempts = 0;
    long long earned = 0, billsPaid = 0, finesPaid = 0, repairsPaid = 0;
    std::vector<std::string> log;    // short run history, newest last
};

struct GameState {
    uint64_t worldSeed = 0;
    int credits = 0;
    int mapIndex = 0;
    int mapRunsLeft = 3;
    int story = 0;                   // contracts completed (= next contract index)
    int act = 1;                     // 1 RENTING, 2 OWNING, 3 THE DEEP JOB
    bool deepDone = false;           // retired as an owner
    bool retired = false;            // walked away (either ending)
    bool netUnlocked = false;
    Network net;
    Deck deck;
    Stats stats;
    std::set<int> burned;            // map-local targets already hit this cycle
    std::map<int, int> reports;      // target id -> report quality (1..3)
    std::set<int> knownPhrases;      // target ids whose passphrase you have read
    std::vector<int> sidesRevealed;
    std::set<int> sidesBurned;
    uint64_t runCounter = 0;
    bool sound = true;
    std::vector<std::string> journal;
};

int storyCount();
constexpr int kDeepId = 8100;
constexpr int kAnnexSide = 1000;   // the side route that holds the deep job's maintenance phrase
