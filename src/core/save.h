// GhostNet - plain-text save files (atomic write, validated load).
#pragma once
#include <string>

#include "common.h"

std::string savePath();
bool saveGame(const GameState& g, std::string* err = nullptr);
bool loadGame(GameState& g, std::string* err = nullptr);
bool saveExists();
