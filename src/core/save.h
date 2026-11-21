// GhostNet - plain-text save files (atomic write, validated load).
#pragma once
#include <string>

#include "common.h"

std::string savePath();
std::string saveTempPath();   // where a save is written before it replaces the old one
bool saveGame(const GameState& g, std::string* err = nullptr);
bool loadGame(GameState& g, std::string* err = nullptr);
bool saveExists();
