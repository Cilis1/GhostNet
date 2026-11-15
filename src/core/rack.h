// GhostNet - everything you can do to your rack between runs. Pure rules:
// each action returns whether it happened and a plain-language message.
#pragma once
#include <string>

#include "common.h"

struct Result {
    bool ok = false;
    std::string msg;
};

void unlockNetwork(GameState& g);                    // SAINT's rack: N0 comes online
std::string sanitizeName(const std::string& raw);    // short, plain names

Result buyMachine(GameState& g, Job j);
Result activate(GameState& g, Machine& m);
Result deactivate(GameState& g, Machine& m);
Result rejob(GameState& g, Machine& m, Job j);
Result repairOne(GameState& g, Machine& m);
Result repairAll(GameState& g);
int repairAllCost(const GameState& g);
Result scrap(GameState& g, int machineId);           // the UI must confirm first; see scrapValue()
// Arming a machine you BOUGHT needs explicit confirmation; pass confirmed=true once the player agreed.
bool decoyNeedsConfirm(const Machine& m);
Result armDecoy(GameState& g, Machine& m, bool confirmed);
Result disarmDecoy(GameState& g);
Result expandRack(GameState& g);
Result upgradeHome(GameState& g);
Result renameMachine(GameState& g, Machine& m, const std::string& name);

// The deck: tools you buy for yourself.
enum class Tool { Breaker = 0, Uplink = 1, Shroud = 2, Lens = 3 };
struct ToolInfo {
    const char* name;
    const char* effect;
    int level, max, nextCost;   // nextCost < 0 when maxed
};
ToolInfo toolInfo(const GameState& g, Tool t);
Result buyTool(GameState& g, Tool t);

// Deck programs: single use, two slots, bought with credits between runs.
int programCost(Program p);
int freeProgramSlot(const GameState& g);    // -1 when both are loaded
Result buyProgram(GameState& g, Program p);  // loads into the first free slot
Result unloadProgram(GameState& g, int slot);   // unused programs refund in full
