// GhostNet - THE PANEL: three advisor AIs who argue about your plans.
// They read your meters out loud. They never solve a lock for you.
#pragma once
#include "common.h"

void panelBriefing(const GameState& g, const Target& t);  // before connecting
void panelReview(const GameState& g);                      // between runs, about your network
