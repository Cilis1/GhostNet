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

// Subnets: a working GATEWAY in the main rack opens a bay of BAY_SLOTS machines (ACCORD_GATE_SLOTS for chairman iron).
bool canPlug(const GameState& g, const Machine& m, const Machine& gw, std::string* why = nullptr);
Result plugIn(GameState& g, Machine& m, Machine& gw);   // from storage or the main rack
Result unplug(GameState& g, Machine& m);                // back to cold storage
Result setRole(GameState& g, Machine& gw, BayRole r);
Result toggleBay(GameState& g, Machine& gw);

// ---- Running lean (0.8) -------------------------------------------------------
// STANDBY: tonight only. A machine on standby is not billed, not worn and does nothing this connection.
// The armed decoy (or anything that would disarm it) can't go on standby. Cleared at the reckoning.
bool canStandby(const GameState& g, const Machine& m, std::string* why = nullptr);
Result setStandby(GameState& g, Machine& m, bool on);
void clearStandby(GameState& g);
// SIGNAL SCRUB: -1 noise for good, paid once. Captured machines stop at SCRUB_FLOOR_CAPTURED, bought ones at 1.
int scrubFloor(const Machine& m);
int scrubSaving(const Machine& m);   // upkeep saved per billed run by the next step (0 at the floor)
int scrubCost(const Machine& m);     // SCRUB_PAYBACK_RUNS x the saving
bool canScrub(const GameState& g, const Machine& m, std::string* why = nullptr);
Result scrubSignal(GameState& g, Machine& m);
// HOUSE POWER: a tower you own powers one subnet of your choice (half its bill). gatewayId -1 clears it.
Result setHousePower(GameState& g, int tower, int gatewayId);
void tidyHousePower(GameState& g);   // drops assignments whose tower or gateway no longer qualifies

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
