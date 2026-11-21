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
enum class Job { Relay = 0, Proxy = 1, Scout = 2, Compute = 3, Jammer = 4, Vault = 5, Gateway = 6 };
constexpr int kJobCount = 7;
constexpr int kMaxTier = 5;   // T4 is captured from prototype sites; T5 is a cluster of T4s - or chairman iron (0.9)

// CHAIRMAN IRON (0.9): a tower's own custom hardware, captured from the HELIOS service site its Boardroom's
// maintenance log names. ACCORD grade: tier 5 in a job you choose at the capture, for good. Cheap to run,
// signal 1 - and it can never be the decoy or re-imaged.
// SIGNATURE grade (0.10): the inner chairs' own black-site iron, caged at a HELIOS cage. One fixed job each, busted
// on purpose and priced like it: a heavy flat bill, a louder connection, dear repairs. Never the decoy either.
enum class Grade { None = 0, Accord = 1, Signature = 2 };

// A GATEWAY opens a subnet bay. The bay's role is set between runs.
enum class BayRole { Stealth = 0, Speed = 1, Siege = 2 };
constexpr int kRoleCount = 3;
const char* roleName(BayRole r);     // "STEALTH"
const char* roleText(BayRole r);     // the passive, one line
const char* strikeName(BayRole r);   // "BLACKOUT"
const char* strikeText(BayRole r);   // the once-per-run strike, one line

// Fixed for life the moment a machine joins your rack, decided by where it came
// from. Traits never change or improve (that would be levelling).
enum class Trait { None = 0, Salvaged = 1, OldNet = 2, HotIron = 3, CorpBuild = 4, Unstable = 5 };
constexpr int kTraitCount = 6;
const char* traitName(Trait t);   // "SALVAGED"
const char* traitText(Trait t);   // what it does, both sides of the trade

const char* jobName(Job j);    // "RELAY"
const char* jobLower(Job j);   // "relay"
bool parseJob(const std::string& s, Job& out);

struct Machine {
    int id = 0;
    std::string name;
    Job job = Job::Relay;
    int tier = 1;               // 1..5 (4 only from prototype sites, 5 only chairman iron)
    bool captured = false;
    bool home = false;
    bool active = false;        // in the rack (running) vs cold storage
    int condition = 100;        // 0..100, 0 = dark
    int noise = 1;              // 1..6
    int price = 0;              // what you paid (bought machines)
    Trait trait = Trait::None;  // fixed for life
    std::string origin;         // where it came from
    int bay = -1;               // id of the gateway whose subnet it's plugged into (-1 = main rack / storage)
    BayRole role = BayRole::Speed;   // gateways only: what the bay does
    bool bayOn = true;          // gateways only: the whole subnet's power switch
    bool usedThisRun = false;   // compute favour already given this run
    int answeredThisRun = 0;    // emergency attempts it has held open tonight (an ACCORD compute answers twice)
    // STANDBY (0.8): powered down for tonight's connection only - not billed, not worn, no effect.
    // Set at the briefing, cleared at the reckoning (or on leaving the briefing). Never saved.
    bool standby = false;
    Grade grade = Grade::None;  // 0.9: chairman iron (fixed for life)
    bool accord() const { return grade == Grade::Accord; }
    bool signature() const { return grade == Grade::Signature; }
    bool chairIron() const { return grade != Grade::None; }   // ACCORD or SIGNATURE: tier 5, job fixed for life, never a decoy
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
    // HOUSE POWER (0.8): per tower you own, the gateway whose subnet runs on that tower's power (-1 = none).
    std::vector<int> house;
    // LOADSHED (0.9): the grid is shedding right now - every subnet not on HOUSE POWER is dark. Tonight only, never saved.
    bool shed = false;

    Machine* find(int id);
    const Machine* find(int id) const;
    Machine* home();
    const Machine* home() const;
    int externalCount() const;
    int activeCount() const;         // main-rack slots in use (subnet members don't take one)
    // Subnets.
    bool online(const Machine& m) const;          // actually doing its job right now
    bool bayOpen(const Machine& gw) const;        // a working gateway within the subnet limit
    bool bayOnline(const Machine& gw) const;      // open, switched on and within its power budget
    std::vector<const Machine*> bayMembers(int gatewayId) const;
    std::vector<const Machine*> openBays() const;
    int bayPower(int gatewayId) const;            // power drawn by its members
    int bayBudget(const Machine& gw) const;
    int baySlots(const Machine& gw) const;        // machines its subnet holds (an ACCORD gateway holds one more)
    int bayUpkeep(int gatewayId) const;           // its members' upkeep, before HOUSE POWER
    bool housePowered(int gatewayId) const;       // an owned tower powers this subnet
    int houseTower(int gatewayId) const;          // which tower (-1 = none)
    int bayBill(int gatewayId) const;             // what the subnet actually bills (half on HOUSE POWER, rounded up)
    int clusterBonus(int gatewayId) const;        // +1 tier for three matching T3+ machines
    int effTier(const Machine& m) const;          // tier it acts at (clusters included)
    int relayBonus() const;          // % (capped)
    int proxyBonus() const;          // % (capped)
    int computeCapacity() const;     // emergency attempts per run (capped)
    int scoutCapacity() const;       // max scout charges right now (capped)
    int usableScoutCharges() const;  // min(stored, capacity)
    int bestScoutTier() const;       // 0 if no working scout
    int jamDelay() const;            // seconds a watchdog's acceleration is held back (capped)
    int jamSpikeCut() const;         // % taken off alarm spikes (capped)
    const Machine* bestVault() const;   // the working vault that keeps the most on a trace-out
    int upkeepTotal() const;         // tonight's bill: online machines, standby excluded, HOUSE POWER applied
    int upkeepAwake() const;         // the same bill with nothing on standby
    int standbyCount() const;
    const Machine* decoy() const;    // armed AND currently valid
    void refillScout();              // stored = capacity at this moment
};

int upkeepOf(const Machine& m);
int scrapValue(const Machine& m);    // what scrapping it would pay, exactly
int repairPerPoint(const Machine& m);
int repairCost(const Machine& m);    // back to 100 condition
int rejobFee(const Machine& m);
int conditionAfterRun(const Machine& m, bool traced);   // wear after a billable run, traits included
int jobValue(const Machine& m, int tier = 0);   // % for relay/proxy, 1 for others (tier 0 = its own)
std::string describeEffect(const Machine& m, int tier = 0);
std::string accordEffect(Job j, bool inSubnet = false);   // 0.9: what ACCORD iron in this job does, one line
std::string signatureEffect(Job j);                       // 0.10: what a SIGNATURE machine does, one line
int signaturesOnline(const Network& n);                    // 0.10: each one adds SIGNATURE_CONNECT_TRACE at the handshake
bool signatureOnline(const Network& n, Job j);

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
enum class Kind { Open, Hidden, Guarded, Corporate, Shrine, Story, Side, Prototype, Tower, Site };   // Site: a HELIOS service site (0.9)
// COMPOSITE (0.6): a keypad and then a rotor on one door, one shared guess budget.
enum class LockType { None, Keypad, Rotor, Cipher, Composite };
// How a corp tower defends itself (Act IV). Always shown at the briefing, never a surprise.
// 0.10 - the inner chairs: LEDGERLOCK audits every download, COUNTERTRACE reads your route back on a rising beat,
// PROVING GROUND runs a different outer doctrine on each floor at HELIOS's own trace.
enum class Doctrine { None = 0, Fortress = 1, Biosentinel = 2, Loadshed = 3, Ledgerlock = 4, Countertrace = 5, Proving = 6 };
enum class FileKind { Paydata, Intel, Objective, Honeypot, Log };

const char* kindName(Kind k);
const char* lockName(LockType l);
const char* doctrineName(Doctrine d);   // "FORTRESS"
const char* doctrineText(Doctrine d);   // one paragraph: how it fights and what beats it

struct File {
    std::string name;
    std::string dir;
    FileKind kind = FileKind::Paydata;
    int size = 1;               // MB
    int credits = 0;
    std::string text;           // readable text (intel / log / objective summary)
    int phraseFor = -1;         // target id whose passphrase this memo leaks
    int revealsSide = -1;       // side-route index this log reveals
    int revealsSite = -1;       // 0.9: the tower whose HELIOS service site this maintenance log names
    bool serviceKey = false;    // 0.10: a cage's SERVICE KEY - read it (with the schematic down) and the capture run opens
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
    int tower = -1, floor = -1; // Act IV: which corp tower and which floor (0-based); a service site has its tower, floor -1
    Doctrine doctrine = Doctrine::None;
    bool hasHoneypot() const;
    int objectiveCount() const;
    std::string objectiveDir() const;
};

// ---------------------------------------------------------------------------
// Player state
// ---------------------------------------------------------------------------
struct Deck { int breaker = 0, uplink = 0, shroud = 0, lens = 0; };

// Deck programs: single-use tricks bought between runs and loaded into the deck.
enum class Program { None = 0, Freeze = 1, Peek = 2, Spoof = 3, Burst = 4 };
constexpr int kProgramCount = 5;
constexpr int kProgramSlots = 2;
const char* programName(Program p);   // "FREEZE"
const char* programText(Program p);   // one line: what it does

// A nameplate on the memorial wall: every machine that died as a decoy.
struct Memorial {
    std::string name, origin, where;
    Job job = Job::Proxy;
    int tier = 1;
    int run = 0;                     // the run it died on
};

struct Stats {
    int runs = 0, jobs = 0, robberies = 0, traceouts = 0, sacrifices = 0;
    int captures = 0, walkaways = 0, unpaidBills = 0, deepAttempts = 0;
    long long earned = 0, billsPaid = 0, finesPaid = 0, repairsPaid = 0;
    std::vector<std::string> log;    // short run history, newest last
};

// A FORTRESS floor's memory between connections (0.7). Only guesses are stored - the marks are
// recomputed from the code, which comes from the world seed, so nothing here can lie about the lock.
struct FortressRecord {
    struct Row { int stage = 0; std::string guess; };   // stage 0 = the keypad, 1 = the rotor
    int rekeys = 0;                  // times this floor's alarm log filled (part of its code's seed)
    bool survey = false;             // TUMBLER SURVEY bought for the current code
    std::vector<Row> rows;           // logged attempts on the current code, oldest first
    std::vector<std::pair<int, int>> peeks;   // (stage, box) shown by PEEK / CRACK on the current code
    bool blank() const { return rekeys == 0 && !survey && rows.empty() && peeks.empty(); }
};

struct GameState {
    uint64_t worldSeed = 0;
    int credits = 0;
    int mapIndex = 0;
    int mapRunsLeft = 3;
    int story = 0;                   // contracts completed (= next contract index)
    int act = 1;                     // 1 RENTING, 2 OWNING, 3 THE DEEP JOB, 4 THE LEDGER
    bool deepDone = false;           // retired as an owner
    bool retired = false;            // walked away (either ending)
    bool netUnlocked = false;
    Network net;
    Deck deck;
    Stats stats;
    Program programs[kProgramSlots] = {Program::None, Program::None};
    std::vector<Memorial> wall;      // decoys that died for you, oldest first
    std::set<int> burned;            // map-local targets already hit this cycle
    std::map<int, int> reports;      // target id -> report quality (1..3)
    std::set<int> knownPhrases;      // target ids whose passphrase you have read
    std::vector<int> sidesRevealed;
    std::set<int> sidesBurned;
    uint64_t runCounter = 0;
    bool sound = true;
    std::vector<std::string> journal;
    // Act IV - THE LEDGER (0.6): corp towers you start yourself, after the owner ending.
    std::vector<int> towerFloor;     // floors cleared per tower (== kFloors when owned)
    std::vector<int> towerKey;       // how many times each tower re-keyed its doors (after a trace)
    // 0.7 - a FORTRESS floor keeps its code until it re-keys: what you tried there stays logged.
    std::map<int, FortressRecord> fortress;   // floor id -> record (Kessler-Maru floors not yet taken)
    // 0.9 - CHAIRMAN IRON: per tower, what you know of its HELIOS service site (0 nothing, 1 the maintenance log, 2 taken).
    // 0.10 - an inner chair's site is a CAGE taken in two runs: 1 = the cage run, 3 = SERVICE KEY held (the capture run).
    std::vector<int> siteState;
    // 0.9: an ACCORD vault kept one tower objective offline through a trace: floor id -> objective file name.
    std::map<int, std::string> vaultHeld;
    int siteOf(int tower) const { return tower >= 0 && tower < static_cast<int>(siteState.size()) ? siteState[static_cast<size_t>(tower)] : 0; }
    int floorsCleared(int tower) const { return tower >= 0 && tower < static_cast<int>(towerFloor.size()) ? towerFloor[static_cast<size_t>(tower)] : 0; }
    int keyOf(int tower) const { return tower >= 0 && tower < static_cast<int>(towerKey.size()) ? towerKey[static_cast<size_t>(tower)] : 0; }
};

int storyCount();
constexpr int kDeepId = 8100;
constexpr int kAnnexSide = 1000;   // the side route that holds the deep job's maintenance phrase
constexpr int kTowerCount = 6;     // corp towers on the Ledger: 3 outer (0.6-0.9), then the 3 inner chairs (0.10)
constexpr int kOuterTowers = 3;    // towers 0..2 are the outer ring; 3..5 the inner chairs
constexpr int kInnerUnlock = 2;    // outer towers you must own before the inner chairs list
constexpr int kFloors = 3;         // floors per tower; the last one is the Boardroom
constexpr int kTowerIdBase = 8200; // tower floor ids: 8200 + tower * 10 + floor
constexpr int kSiteIdBase = 8300;  // 0.9: HELIOS service site ids: 8300 + tower
constexpr int kCageDoorBase = 8320; // 0.10: an inner chair's cage, its capture run (a different door): 8320 + tower
constexpr int kMaxAct = 4;
