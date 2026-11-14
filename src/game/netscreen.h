// GhostNet - the safe moment: managing your machines between runs.
#pragma once
#include "common.h"

void unlockNetwork(GameState& g);
void networkScreen(GameState& g);
std::string sanitizeName(const std::string& raw);
