// GhostNet - locks: puzzles solved with a limited number of guesses.
#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "rng.h"

struct Lock {
    LockType type = LockType::None;
    std::string secret;
    int maxGuesses = 0;
    int used = 0;
    bool extraUsed = false;      // one emergency attempt per lock, ever
    bool open = false;
    struct Row { std::string guess, feedback; };
    std::vector<Row> history;

    int left() const { return maxGuesses - used; }
    bool sealed() const { return !open && used >= maxGuesses; }
    std::string format() const;          // what a guess looks like
    std::string rule() const;            // how feedback works
    // Returns false with err set if the guess is malformed (costs nothing).
    bool guess(const std::string& raw, std::string& err);
    void print() const;
};

Lock makeLock(LockType t, Rng& r, int bonusGuesses);
int baseGuesses(LockType t);
std::string lockRuleText(LockType t);
bool lockHasOddNearRule(LockType t);
