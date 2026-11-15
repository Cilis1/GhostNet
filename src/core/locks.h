// GhostNet - locks: puzzles solved with a limited number of guesses.
#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "text.h"
#include "rng.h"

struct Lock {
    LockType type = LockType::None;
    std::string secret;
    int maxGuesses = 0;
    int used = 0;
    bool extraUsed = false;      // one emergency attempt per lock, ever
    bool open = false;
    // marks per position: 2 = exact, 1 = near (rotor: one notch / keypad + cipher: elsewhere), 0 = no.
    // hit/nearc are the same marks counted (kept for the advisors and old tools).
    struct Row { std::string guess; std::vector<int> marks; int hit = 0, nearc = 0; };
    std::vector<Row> history;
    std::vector<int> peeked;     // positions revealed by a PEEK program

    int left() const { return maxGuesses - used; }
    bool sealed() const { return !open && used >= maxGuesses; }
    std::string format() const;          // what a guess looks like
    std::string rule() const;            // how feedback works
    int length() const { return type == LockType::Cipher ? 5 : 4; }
    // Returns false with err set if the guess is malformed (costs nothing).
    bool guess(const std::string& raw, std::string& err);
    // PEEK: reveal the first position you haven't already pinned down. -1 if there's none left.
    int peek();
    bool known(int pos) const;           // exact in some row, or peeked
};

Lock makeLock(LockType t, Rng& r, int bonusGuesses);
int baseGuesses(LockType t);
std::string lockRuleText(LockType t);
bool lockHasOddNearRule(LockType t);
const std::vector<std::string>& cipherWords();   // every word a cipher lock can hold
