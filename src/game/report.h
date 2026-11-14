// GhostNet - scouting reports.
#pragma once
#include "common.h"

// Scan a target (spends a charge only the first time). Prints the result.
void scanTarget(GameState& g, const Target& t);
// Print an existing report at the quality you are entitled to right now.
void printReport(const GameState& g, const Target& t);
bool hasReport(const GameState& g, const Target& t);
