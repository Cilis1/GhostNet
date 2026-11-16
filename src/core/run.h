// GhostNet - one connection to one target, as a pure state machine.
// The front end calls the commands, animates timed actions with step(), and
// shows settle()'s reckoning. Nothing in here draws or prints.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "locks.h"

enum class End { None, Disconnect, TraceOut, Sacrifice, Steal };
enum class Tone { Normal, Good, Warn, Bad, Info, Story, Dim };

struct Line {
    std::string text;
    Tone tone = Tone::Normal;
};

enum class Ev {
    Connected, AccessGranted, PhraseOk, PhraseBad, Guess, Lockout, Extra, MachineDark,
    Read, PhraseLearned, SideFound, DownloadDone, DownloadAborted, Honeypot, Objective,
    ContractDone, Captured, Traced, Sacrifice, Program, Spoofed, Strike,
    Stage, Sweep, MissLogged   // 0.6: a composite's outer stage opens; doctrine pings
};
struct Event {
    Ev type;
    std::string text;
};

struct CaptureQuote {
    bool possible = false;
    std::string reason;          // why not, if impossible
    Job def = Job::Relay, alt = Job::Proxy;
    int tier = 1, condition = 0, noise = 1, upkeep = 0, holdSecs = 0, secondsLeft = 0;
    int leftCr = 0, leftIntel = 0;
    bool trailWarning = false, rackFull = false, honeypot = false, viaPhrase = false;
    Trait trait = Trait::None;   // what it would carry for life
};

struct Shortfall {
    int got = 0, total = 0, mbLeft = 0, secsLeft = 0, secsHeld = 0, trace = 0, pct = 0;
    bool doorOpened = false;
    double speed = 0;
    int uplink = 0, shroud = 0, relay = 0, proxy = 0;
};

struct Reckoning {
    End end = End::None;
    std::vector<Line> lines;
    int credits = 0;
    bool billable = false;
    int bill = 0;
    bool deep = false, deepWon = false;
    Shortfall shortfall;
    std::vector<std::pair<std::string, std::string>> actCards;   // title, text
    std::string storyTitle, storyOutro, infoReward;
    int tower = -1, floorCleared = -1;   // Act IV: the floor this run took (if any)
    bool towerOwned = false, ledgerDone = false, rekeyed = false;
    bool unlockedNetwork = false;
    bool boardRefreshed = false;
    std::string sacrificed, captured;
};

class RunSession {
public:
    RunSession(GameState& g, const Target& t, uint64_t entropy);

    GameState& g;
    Target t;
    Lock lock;
    double trace = 0;
    int secs = 0;
    bool doorOpen = false;
    bool viaPhrase = false;
    int haul = 0;
    bool honeypot = false;
    bool jobDone = false;
    bool anchoring = false;
    int emergLeft = 0;
    std::vector<int> billIds;      // fixed when the run begins
    int startRelay = 0, startProxy = 0;
    int jamDelay = 0, jamCut = 0;  // from working jammers, fixed when the run begins
    int vaultPct = 0;              // best working vault, fixed when the run begins
    std::string vaultName;
    int vaultFiles = 0;            // how many of your best files it keeps
    int bestFile = 0;              // most valuable paydata file downloaded tonight
    std::vector<int> takenFiles;   // every paydata file downloaded tonight (credits)
    // Subnet strikes: the bays that were online when the run began, and which have struck.
    std::vector<int> strikeBays;
    std::vector<int> strikesUsed;
    // Deck programs.
    int freezeLeft = 0;            // seconds the trace stands still
    int burstLeft = 0;             // download seconds at double speed
    bool spoofArmed = false;
    int doorPrograms = 0;          // programs run while the lock was closed (max 1)
    End end = End::None;
    std::string sacrificed, stolen;
    std::vector<std::string> triedPhrases;   // wrong phrases sent this run (upper case)
    std::vector<Line> log;         // the connection's running commentary
    std::vector<Event> events;     // drained by the front end for effects and sound

    enum class Act { None, Download, Hold };
    Act act = Act::None;
    int actFile = -1, actTotal = 0, actDone = 0;
    Job actJob = Job::Relay;

    // Door.
    bool guess(const std::string& raw, std::string& err);
    bool phrase(const std::string& word);
    bool extra(std::string& err);
    // Run the program loaded in a deck slot (free: costs no time). Consumes it.
    bool runProgram(int slot, std::string& err);
    bool programUsable(int slot, std::string* why = nullptr) const;
    // A subnet's once-per-run strike (free: costs no time). Wears the bay after the run.
    bool strike(int gatewayId, std::string& err);
    bool strikeUsable(int gatewayId, std::string* why = nullptr) const;
    int vaultKeeps() const;        // what the vault would save on a trace-out right now
    // Inside.
    bool read(int fileIdx);
    bool startDownload(int fileIdx, std::string& err);
    CaptureQuote quote() const;
    bool startHold(Job j, std::string& err);
    // Timed actions: one game second per step().
    void step();
    void abortAction();
    void disconnect();

    bool over() const { return end != End::None; }
    bool busy() const { return act != Act::None; }
    double rateNow(double mult = 1.0) const;
    int nextSweep() const;           // BIOSENTINEL: seconds until the next sentinel sweep (-1 if none)
    int sweeps = 0, misses = 0;      // doctrine pings tonight
    int secondsLeft() const;
    Reckoning settle();             // call once, after over()

private:
    bool advance(int seconds, double mult = 1.0);
    bool spike(double amount);
    int spikeSize(double base) const;   // after jammers, for the log
    void traced();
    void openDoor(bool phrase);
    void finishDownload();
    void finishCapture();
    void say(const std::string& s, Tone tone = Tone::Normal) { log.push_back({s, tone}); }
    void emit(Ev e, const std::string& s = "") { events.push_back({e, s}); }
    bool settled_ = false;
    bool decoyHot_ = false;        // the burned decoy was HOT IRON: fine waived
    bool subnetBurned_ = false;    // the decoy was a whole subnet: fine waived
};
