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
    Stage, Sweep, MissLogged,  // 0.6: a composite's outer stage opens; doctrine pings
    Shed, SiteFound, AccordFreeze   // 0.9: LOADSHED drops the subnets; a service site is pinned; ACCORD iron freezes the trace
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
    // CHAIRMAN IRON (0.9): a service site's ACCORD unit takes any job - chosen at the hold, for good.
    bool accord = false;
    std::string ironName;
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
    int standbySaved = 0;          // bill avoided by STANDBY tonight (shown, never refunded)
    int houseSaved = 0;            // bill avoided by HOUSE POWER tonight
    bool deep = false, deepWon = false;
    Shortfall shortfall;
    std::vector<std::pair<std::string, std::string>> actCards;   // title, text
    std::string storyTitle, storyOutro, infoReward;
    int tower = -1, floorCleared = -1;   // Act IV: the floor this run took (if any)
    bool towerOwned = false, ledgerDone = false, rekeyed = false;
    int sitePinned = -1;                 // 0.9: the tower whose HELIOS service site went on the board tonight
    bool siteRekeyed = false;            // 0.9: traced at a service site - it re-keyed, nothing else lost
    bool accordCaptured = false;         // 0.9: tonight's capture was chairman iron
    bool alarmRekeyed = false;           // FORTRESS: the alarm log filled and this floor re-keyed
    int alarms = -1;                     // FORTRESS: misses on the floor's alarm log after the run (-1: not a FORTRESS floor)
    bool unlockedNetwork = false;
    bool boardRefreshed = false;
    std::string sacrificed, captured;
};

// FORTRESS (0.7): what a floor's record says about its door right now, for the briefing and MOTH.
struct FortressOutlook {
    bool active = false;          // a Kessler-Maru floor with a persistent code
    int alarms = 0;               // misses on the alarm log (of bal::FORTRESS_ALARM_LOG)
    int logged = 0;               // logged attempts
    bool survey = false;
    std::string surveyDigits;     // ascending, when surveyed
    bool keypadKnown = false;
    long codes = 0;               // keypad codes that still fit
    long rotorCodes = -1;         // rotor settings that still fit (-1 until the keypad is known)
    int likely = 0;               // guesses this door usually still needs
    int budget = 0;               // your guesses per connection (1 + BREAKER)
    int emergency = 0;            // + compute boxes / SIEGE subnets that can answer
};
bool isFortressFloor(const Target& t);
int fortressMissesOn(const GameState& g, const Target& t);   // the alarm log: logged rows that weren't a stage's code
FortressOutlook fortressOutlook(const GameState& g, const Target& t);
Lock fortressLock(const GameState& g, const Target& t);   // the floor's door as it stands, logged rows replayed

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
    std::vector<int> billIds;      // fixed when the run begins (standby machines are left out)
    std::vector<int> houseBays;    // subnets on HOUSE POWER when the run began: they bill at half
    int standbyCount = 0;          // machines sitting tonight out
    int standbySaved = 0;          // what standby took off tonight's bill
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
    // LOADSHED (0.9): every LOADSHED_EVERY seconds the grid drops every subnet not on HOUSE POWER for LOADSHED_SECS.
    bool shedding() const;           // the grid is shedding right now
    int nextShed() const;            // seconds until the next shed (-1 if none; 0 while shedding)
    int shedLeft() const;            // seconds of the current shed left (0 if not shedding)
    int sheds = 0;
    // CHAIRMAN IRON (0.9), fixed when the run begins.
    bool accordFreeze = false;       // an ACCORD jammer will freeze the trace once, at ACCORD_FREEZE_AT
    std::string accordFreezeName;
    bool accordVault = false;        // an ACCORD vault keeps one tower objective through a trace
    std::string heldObjective;       // the objective an ACCORD vault kept from an earlier trace (already taken)
    int sweeps = 0, misses = 0;      // doctrine pings tonight
    // FORTRESS (0.7): the floor's code persists; attempts are logged to its record as they happen.
    bool fortress = false;
    bool alarmSealed = false;        // the alarm log overflowed tonight: the door is sealed and the floor re-keyed
    int alarms() const;              // misses on this floor's alarm log right now
    int secondsLeft() const;
    Reckoning settle();             // call once, after over()

private:
    bool advance(int seconds, double mult = 1.0);
    bool spike(double amount);
    int spikeSize(double base) const;   // after jammers, for the log
    void traced();
    void endShed();                  // the run is over: the grid's lights come back on at home
    bool shedAt(int s) const;
    double dlSpeed_ = 0, dlCarry_ = 0;   // LOADSHED: a download slows while the grid sheds its relays
    FortressRecord* record();
    void logRow(int stage, const std::string& guess);
    void logPeek(int pos);
    void openDoor(bool phrase);
    void finishDownload();
    void finishCapture();
    void say(const std::string& s, Tone tone = Tone::Normal) { log.push_back({s, tone}); }
    void emit(Ev e, const std::string& s = "") { events.push_back({e, s}); }
    bool settled_ = false;
    bool decoyHot_ = false;        // the burned decoy was HOT IRON: fine waived
    bool subnetBurned_ = false;    // the decoy was a whole subnet: fine waived
};
