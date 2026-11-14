// GhostNet - the wider world: the job board, story contracts, side routes.
#pragma once
#include <string>
#include <vector>

#include "common.h"

struct StoryInfo {
    std::string title;
    std::string briefing;
    std::string outro;
};

const StoryInfo& storyInfo(int idx);
bool contractAvailable(const GameState& g);   // one new contract per board refresh
bool deepListed(const GameState& g);          // the deep job, once HELIOS's secret is out
std::vector<Target> buildBoard(const GameState& g);    // everything you can hit right now
Target* findTarget(std::vector<Target>& board, int id);
bool isMapLocal(int id);                                // regular + shrine targets
double baseTraceRate(const Target& t);
const char* dangerWord(double ratePerSec);
// What kind of machine a place yields: default job, alternative job, tier, base condition, base noise.
struct HardwareSpec { Job def, alt; int tier, condition, noise; };
HardwareSpec hardwareFor(Kind k);
