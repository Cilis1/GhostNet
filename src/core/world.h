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
HardwareSpec hardwareFor(const Target& t);   // per listing (prototype labs vary)
// The stock maintenance phrases HELIOS techs reuse across the city (never the deep job's).
const std::vector<std::string>& starterPhrases();
Trait traitFor(Kind k);                      // what a captured machine carries for life
// Act IV - THE LEDGER (0.6): corp towers, opened by the owner ending.
bool ledgerOpen(const GameState& g);
bool enterLedger(GameState& g);               // act 4; true the first time (show the card)
bool towerOwned(const GameState& g, int tower);
int towerOfId(int id);                        // -1 if the id isn't a tower floor
int floorId(int tower, int floor);
const char* towerCorp(int tower);             // "Kessler-Maru"
const char* towerTitle(int tower);            // "KESSLER-MARU TOWER"
Doctrine towerDoctrine(int tower);
std::string floorOutro(int tower, int floor);
std::string towerPassphrase(const GameState& g, int tower, int floor);   // at the tower's current key
Target makeTowerFloor(const GameState& g, int tower, int floor);
