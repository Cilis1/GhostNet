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

// Act IV heist prep: intel on a tower floor, bought with credits between runs. Never a second currency,
// never a timer - you pay once and you know it, until a trace re-keys the doors.
// 0.7: TUMBLER SURVEY (FORTRESS only) names a floor's keypad digits, unordered, until that floor re-keys.
enum class Prep { Plan = 0, Phrase = 1, Survey = 2 };
const char* prepName(Prep p);                 // "FLOOR PLAN"
const char* prepText(Prep p);
int prepCost(const Target& t, Prep p);
bool prepAvailable(const GameState& g, const Target& t, Prep p, std::string* why = nullptr);
Result buyPrep(GameState& g, const Target& t, Prep p);
