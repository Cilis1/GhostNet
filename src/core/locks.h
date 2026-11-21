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
    // dir (0.7, a composite's rotor only): per wheel, +1 = the wheel sits HIGHER than the guess, -1 = LOWER, 0 = exact.
    // logged (0.7, FORTRESS): replayed from an earlier connection - shown, never charged to this one's budget.
    struct Row { std::string guess; std::vector<int> marks; int hit = 0, nearc = 0; std::vector<int> dir; bool logged = false; };
    std::vector<Row> history;
    std::vector<int> peeked;     // positions revealed by a PEEK program
    // COMPOSITE: stage 0 is a keypad, stage 1 a rotor. `secret` always holds the current
    // stage's answer; the rotor's waits in `inner` until the keypad opens.
    int stage = 0;
    std::string inner;
    std::vector<Row> outerRows;  // the keypad stage's rows, once it has opened
    std::string outer;           // COMPOSITE: the keypad's code (kept after the rotor takes over)
    bool surveyed = false;       // FORTRESS: a TUMBLER SURVEY named the keypad's digits (unordered)

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
    // FORTRESS: a guess from an earlier connection, judged and shown as LOGGED. Costs nothing, never opens a stage.
    // Returns false (and adds nothing) if it isn't a well-formed guess for the current stage.
    bool replay(const std::string& guess);
    // COMPOSITE rotor: a non-green wheel also says which way to turn (shown as an arrow).
    bool directional() const { return composite() && stage == 1; }
    bool keypadKnown() const;            // stage 1 not open yet, but a logged row already found the keypad's code
    int liveRows() const;                // rows this connection paid for, on the current stage
    std::string surveyDigits() const;    // the keypad's digits in ascending order ("" unless surveyed)
    // CODES LEFT: how many answers for the current stage still fit everything on screen
    // (rows, logged rows, PEEK/CRACK boxes, the survey, the carry rule). MOTH counts; it never names one.
    long codesLeft() const;
    // Guesses this door usually still needs from here with careful play (composite only; 0 once open).
    int guessesLikely() const;
    // PEEK: reveal the first position you haven't already pinned down. -1 if there's none left.
    int peek();
    bool known(int pos) const;           // exact in some row, or peeked
};

Lock makeLock(LockType t, Rng& r, int bonusGuesses);
// The marks a rotor wheel gives: 2 exact, 1 one notch away, 0 further. dir: +1 the wheel is higher, -1 lower.
int rotorMark(int wheel, int guess);
int rotorDir(int wheel, int guess);
// FORTRESS (0.7): a floor's code is fixed by the world seed, the tower's key and the floor's own re-keys.
uint64_t fortressSeed(const GameState& g, int tower, int floor);
// Typical keypad guesses (careful play, ~90% of codes) when n codes still fit.
int keypadGuessesFor(long n);
int baseGuesses(LockType t);
std::string lockRuleText(LockType t);
bool lockHasOddNearRule(LockType t);
const std::vector<std::string>& cipherWords();   // every word a cipher lock can hold
