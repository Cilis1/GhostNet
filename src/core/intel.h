// GhostNet - scouting reports and the advisor panel, as data.
#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "rack.h"

struct ReportView {
    bool exists = false;
    int quality = 0;              // 1..3 as shown right now
    std::string lockName, lockRule;
    bool lockWarning = false;     // rotor: near means one notch
    std::string danger;           // SLOW / NORMAL / FAST / LETHAL
    bool watchdog = false;
    int security = 0;             // quality >= 2
    int objectives = 0;
    std::string objectiveDir;
    int payFiles = 0;
    int secondsToTrace = 0;       // quality >= 3
    bool backdoor = false;
    std::vector<std::string> facts;
    std::vector<std::string> warnings;
};

bool hasReport(const GameState& g, const Target& t);
Result scanTarget(GameState& g, const Target& t);   // spends a charge (re-reading is free)
ReportView reportView(const GameState& g, const Target& t);

enum class Vote { Go, Caution, Abort };
enum class Advisor { Vesper = 0, Ledger = 1, Moth = 2 };
struct PanelView {
    struct Say {
        Advisor who;
        std::string text;
    };
    std::vector<Say> lines;
    Vote votes[3] = {Vote::Go, Vote::Go, Vote::Go};
    std::string verdict;
};
const char* advisorName(Advisor a);
const char* advisorRole(Advisor a);
const char* voteName(Vote v);
PanelView panelBriefing(const GameState& g, const Target& t);
PanelView panelReview(const GameState& g);

// STANDBY SUGGESTED (0.8): LEDGER's picks for tonight's briefing. The briefing starts with all of them OFF;
// the player opts in. Each pick says what it saves and, in amber, what tonight loses if things go wrong.
struct StandbyPick {
    int id = -1;
    int saving = 0;               // cr off tonight's bill (a gateway counts its whole subnet)
    std::string why;              // "past the relay cap - adds nothing"
    std::vector<std::string> risks;   // amber lines: vault, decoy cover, subnet, scans...
};
std::vector<StandbyPick> standbyPicks(const GameState& g, const Target& t);
// What standing this machine down tonight costs, in plain words (empty = nothing it does tonight).
std::vector<std::string> standbyRisks(const GameState& g, const Target& t, const Machine& m);
// Everything tonight loses to what is on standby right now, against the fully awake rack (the amber lines).
std::vector<std::string> standbyLosses(const GameState& g);

// Act IV heist prep: intel on a tower floor, bought with credits between runs. Never a second currency,
// never a timer - you pay once and you know it, until a trace re-keys the doors.
// 0.7: TUMBLER SURVEY (FORTRESS only) names a floor's keypad digits, unordered, until that floor re-keys.
enum class Prep { Plan = 0, Phrase = 1, Survey = 2 };
const char* prepName(Prep p);                 // "FLOOR PLAN"
const char* prepText(Prep p);
int prepCost(const Target& t, Prep p);
bool prepAvailable(const GameState& g, const Target& t, Prep p, std::string* why = nullptr);
Result buyPrep(GameState& g, const Target& t, Prep p);
