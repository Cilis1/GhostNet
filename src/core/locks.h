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
    // COMPOSITE: stage 0 is a keypad, stage 1 a rotor. `secret` always holds the current
    // stage's answer; the rotor's waits in `inner` until the keypad opens.
    int stage = 0;
    std::string inner;
    std::vector<Row> outerRows;  // the keypad stage's rows, once it has opened

    int left() const { return maxGuesses - used; }
    bool sealed() const { return !open && used >= maxGuesses; }
    // The kind of puzzle in front of you right now (a composite is a keypad, then a rotor).
    LockType cur() const { return type == LockType::Composite ? (stage == 0 ? LockType::Keypad : LockType::Rotor) : type; }
    bool composite() const { return type == LockType::Composite; }
    std::string format() const;          // what a guess looks like
    std::string rule() const;            // how feedback works
    int length() const { return cur() == LockType::Cipher ? 5 : 4; }
    // COMPOSITE: the outer keypad opens and the inner rotor takes over (a FORTRESS phrase does this too).
    void openOuter();
    bool carried() const { return !outerRows.empty() && outerRows.back().hit == 4; }   // the keypad's code is known
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
