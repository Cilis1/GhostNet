#include "locks.h"

#include <algorithm>
#include <cctype>

#include "balance.h"

namespace {
const std::vector<std::string> kWords = {
    "ghost", "cache", "proxy", "relay", "trace", "cable", "laser", "shell", "admin", "patch",
    "crypt", "token", "guard", "vault", "noise", "pulse", "virus", "drone", "flash", "debug",
    "logic", "stack", "queue", "array", "pixel", "modem", "input", "error", "quiet", "black",
    "night", "steel", "glass", "rogue", "agent", "cyber", "nexus", "spike", "sigil", "rivet",
    "ledger", "bytes", "route", "cloak", "smoke", "ember", "flare", "frost", "storm", "tower",
    "harbor", "anchor", "orbit", "radar", "sonar", "lumen", "neon", "prism", "quark", "rumor",
    "cipher", "vapor", "wired", "pivot", "crane", "forge", "grime", "haunt", "index", "joint",
    "knife", "lodge", "metal", "nerve", "omega", "plaza", "quota", "raven", "scope", "thorn",
    "ultra", "vigil", "wrath", "xenon", "yield", "zebra", "audit", "blade", "chord", "delta",
    "drift", "fable", "gamma", "hydra", "irony", "jewel", "karma", "lunar", "mirth", "north",
    "onion", "piano", "relic", "salvo", "tango", "umbra", "venom", "whisk", "alarm", "badge",
    "boost", "cargo", "codec", "depot", "fiber", "gauge", "hatch", "intel", "latch", "mains",
    "angel", "bloom", "brick", "chalk", "civic", "coral", "cover", "crown", "dealt", "dodge",
    "eagle", "fault", "ferry", "ghoul", "grail", "heron", "hound", "inlet", "levee", "lever",
    "magic", "manor", "marsh", "mercy", "mogul", "motel", "oasis", "opera", "panic", "pearl",
    "pilot", "plank", "poker", "pride", "quilt", "radio", "rebel", "ridge", "rival", "river",
    "sable", "saint", "scrap", "shade", "shark", "siren", "slate", "spine", "sting", "sugar",
    "swamp", "sword", "teeth", "tidal", "trust", "vapid", "velvet", "viper", "voice", "wharf"};

}  // namespace

const std::vector<std::string>& cipherWords() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> o;
        for (auto& w : kWords) if (w.size() == 5) o.push_back(w);
        return o;
    }();
    return v;
}

int baseGuesses(LockType t) {
    switch (t) {
        case LockType::Keypad: return 7;
        case LockType::Rotor: return 6;
        case LockType::Cipher: return 6;
        case LockType::Composite: return bal::FORTRESS_GUESSES;
        default: return 0;
    }
}

bool lockHasOddNearRule(LockType t) { return t == LockType::Rotor || t == LockType::Composite; }

std::string lockRuleText(LockType t) {
    switch (t) {
        case LockType::Keypad:
            return "KEYPAD: 4 different digits, judged box by box. Green = right digit, right box. Amber = the digit is in the code, "
                   "in another box. Dark = not in the code.";
        case LockType::Rotor:
            return "ROTOR: 4 digits (repeats allowed), judged per wheel. Green = exact. Amber = the wheel is ONE NOTCH AWAY "
                   "(higher or lower) - NOT misplaced. Dark = further off.";
        case LockType::Cipher:
            return "CIPHER: a 5-letter word. Green = right letter, right place. Amber = the letter is elsewhere in the word. "
                   "Dark = not in the word (duplicates are counted honestly).";
        case LockType::Composite:
            return "COMPOSITE: a KEYPAD, then a ROTOR, on one door, sharing one guess budget. The keypad judges like any keypad. "
                   "Once it opens, every rotor wheel sits within 2 notches of the keypad digit in the same box; amber there means "
                   "ONE NOTCH AWAY, and every wheel that isn't green shows an arrow: UP = the wheel sits higher, DOWN = lower.";
        default: return "No lock. The door is open.";
    }
}

Lock makeLock(LockType t, Rng& r, int bonus) {
    Lock L;
    L.type = t;
    L.maxGuesses = baseGuesses(t) + bonus;
    if (t == LockType::Keypad) {
        std::vector<char> d = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
        r.shuffle(d);
        L.secret = std::string(d.begin(), d.begin() + 4);
    } else if (t == LockType::Rotor) {
        for (int i = 0; i < 4; ++i) L.secret += static_cast<char>('0' + r.range(0, 9));
    } else if (t == LockType::Cipher) {
        L.secret = r.pick(cipherWords());
    } else if (t == LockType::Composite) {
        std::vector<char> d = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
        r.shuffle(d);
        L.secret = std::string(d.begin(), d.begin() + 4);
        L.outer = L.secret;
        // The carry rule: each inner wheel rests within 2 notches of the outer digit in its box.
        for (int i = 0; i < 4; ++i) {
            int k = L.secret[static_cast<size_t>(i)] - '0';
            L.inner += static_cast<char>('0' + r.range(std::max(0, k - 2), std::min(9, k + 2)));
        }
    } else {
        L.open = true;
    }
    return L;
}

void Lock::openOuter() {
    if (!composite() || stage != 0 || open) return;
    stage = 1;
    outerRows = history;   // opened by a phrase: no full row, so nothing carries into the rotor
    history.clear();
    peeked.clear();
    secret = inner;
}

std::string Lock::format() const {
    switch (cur()) {
        case LockType::Keypad: return "4 different digits, e.g. 0123";
        case LockType::Rotor: return "4 digits, e.g. 1470";
        case LockType::Cipher: return "a 5-letter word, e.g. crane";
        default: return "";
    }
}
std::string Lock::rule() const {
    if (!composite()) return lockRuleText(type);
    if (stage == 0 && keypadKnown())
        return "COMPOSITE, STAGE 1 of 2 - KEYPAD KNOWN: a logged attempt already found this code. Re-entering it costs one "
               "guess and is never a miss. Then the ROTOR: each wheel within 2 notches of that digit, with arrows.";
    if (stage == 0)
        return "COMPOSITE, STAGE 1 of 2 - KEYPAD: 4 different digits. Green = right box. Amber = in the code, another box. "
               "A ROTOR waits behind it: each of its wheels sits within 2 notches of this stage's digit in the same box.";
    if (!carried())
        return "COMPOSITE, STAGE 2 of 2 - ROTOR: the phrase opened the keypad without showing its code, so nothing carries. "
               "4 digits, repeats allowed. Green = exact. Amber = ONE NOTCH AWAY. Arrows: UP = higher, DOWN = lower.";
    return "COMPOSITE, STAGE 2 of 2 - ROTOR: every wheel is within 2 notches of the keypad code " + outerRows.back().guess +
           " in the same box. Green = exact. Amber = ONE NOTCH. Arrow UP = the wheel is higher, DOWN = lower.";
}

namespace {
// The lock alphabets are plain ASCII. Checking the ranges directly rejects high-bit bytes
// (which <cctype> would see as negative chars - undefined behaviour) and keeps cipher
// letters inside 'a'..'z' for the counts[26] index.
bool asciiDigit(char c) noexcept { return c >= '0' && c <= '9'; }
bool asciiLower(char c) noexcept { return c >= 'a' && c <= 'z'; }
}  // namespace

int rotorMark(int wheel, int guess) {
    int d = std::abs(guess - wheel);
    return d == 0 ? 2 : d == 1 ? 1 : 0;
}
int rotorDir(int wheel, int guess) { return wheel > guess ? 1 : wheel < guess ? -1 : 0; }

// Judges a guess against the current stage without touching the lock's budget or state.
// Returns false with err set if the guess is malformed.
static bool judgeGuess(const Lock& L, const std::string& g, Lock::Row& row, std::string& err);

bool Lock::guess(const std::string& raw, std::string& err) {
    std::string g = txt::lower(txt::trim(raw));
    Row row;
    if (open) { err = "The lock is already open."; return false; }
    if (sealed()) { err = "The lock has sealed. No guesses remain."; return false; }
    if (!judgeGuess(*this, g, row, err)) return false;
    if (row.hit == 4 && !composite() && cur() == LockType::Keypad) open = true;
    if (cur() == LockType::Rotor && std::all_of(row.marks.begin(), row.marks.end(), [](int m) { return m == 2; })) open = true;
    if (cur() == LockType::Cipher && g == secret) open = true;
    ++used;
    row.guess = g;
    history.push_back(row);
    if (composite() && stage == 0 && row.hit == 4) openOuter();   // the inner rotor takes over
    return true;
}

bool Lock::replay(const std::string& raw) {
    std::string g = txt::lower(txt::trim(raw)), err;
    Row row;
    if (open || !judgeGuess(*this, g, row, err)) return false;
    row.guess = g;
    row.logged = true;
    history.push_back(row);
    return true;
}

static bool judgeGuess(const Lock& L, const std::string& g, Lock::Row& row, std::string& err) {
    const std::string& secret = L.secret;
    const LockType kind = L.cur();
    if (kind == LockType::Keypad) {
        if (g.size() != 4 || !std::all_of(g.begin(), g.end(), asciiDigit)) { err = "Keypad wants " + L.format() + "."; return false; }
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (g[i] == g[j]) { err = "Keypad digits must all be different."; return false; }
        int hit = 0, nearc = 0;
        for (int i = 0; i < 4; ++i) {
            if (g[i] == secret[i]) { ++hit; row.marks.push_back(2); }
            else if (secret.find(g[i]) != std::string::npos) { ++nearc; row.marks.push_back(1); }
            else row.marks.push_back(0);
        }
        row.hit = hit;
        row.nearc = nearc;
    } else if (kind == LockType::Rotor) {
        if (g.size() != 4 || !std::all_of(g.begin(), g.end(), asciiDigit)) { err = "Rotor wants " + L.format() + "."; return false; }
        for (int i = 0; i < 4; ++i) {
            int m = rotorMark(secret[i] - '0', g[i] - '0');
            row.marks.push_back(m);
            if (m == 2) ++row.hit;
            if (L.directional()) row.dir.push_back(rotorDir(secret[i] - '0', g[i] - '0'));
        }
    } else if (kind == LockType::Cipher) {
        if (g.size() != 5 || !std::all_of(g.begin(), g.end(), asciiLower)) { err = "Cipher wants " + L.format() + "."; return false; }
        std::string res(5, '.');
        int counts[26] = {0};
        for (int i = 0; i < 5; ++i) {
            if (g[i] == secret[i]) res[i] = 'G';
            else counts[secret[i] - 'a']++;
        }
        for (int i = 0; i < 5; ++i) {
            if (res[i] == 'G') continue;
            int k = g[i] - 'a';
            if (counts[k] > 0) { res[i] = 'Y'; counts[k]--; }
        }
        for (int i = 0; i < 5; ++i) row.marks.push_back(res[i] == 'G' ? 2 : res[i] == 'Y' ? 1 : 0);
    } else {
        err = "There is no lock.";
        return false;
    }
    return true;
}

bool Lock::keypadKnown() const {
    if (!composite() || stage != 0 || open) return false;
    for (auto& r : history) if (r.hit == 4) return true;
    return false;
}

int Lock::liveRows() const {
    int n = 0;
    for (auto& r : history) if (!r.logged) ++n;
    return n;
}

std::string Lock::surveyDigits() const {
    if (!surveyed || outer.size() != 4) return "";
    std::string d = outer;
    std::sort(d.begin(), d.end());
    return d;
}

namespace {
const std::vector<std::string>& allKeypadCodes() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> o;
        for (int a = 0; a < 10; ++a)
            for (int b = 0; b < 10; ++b)
                for (int c = 0; c < 10; ++c)
                    for (int d = 0; d < 10; ++d)
                        if (a != b && a != c && a != d && b != c && b != d && c != d)
                            o.push_back({char('0' + a), char('0' + b), char('0' + c), char('0' + d)});
        return o;
    }();
    return v;
}
}  // namespace

long Lock::codesLeft() const {
    if (open) return 1;
    const LockType kind = cur();
    if (kind == LockType::Keypad) {
        const std::string digits = surveyDigits();
        long n = 0;
        for (auto& c : allKeypadCodes()) {
            bool ok = true;
            if (!digits.empty()) {
                std::string s = c;
                std::sort(s.begin(), s.end());
                ok = s == digits;
            }
            for (int p : peeked) if (ok && p >= 0 && p < 4) ok = c[static_cast<size_t>(p)] == secret[static_cast<size_t>(p)];
            for (size_t r = 0; ok && r < history.size(); ++r) {
                const Row& row = history[r];
                for (size_t i = 0; ok && i < 4 && i < row.marks.size(); ++i) {
                    int mk = row.guess[i] == c[i] ? 2 : c.find(row.guess[i]) != std::string::npos ? 1 : 0;
                    ok = mk == row.marks[i];
                }
            }
            if (ok) ++n;
        }
        return n;
    }
    if (kind == LockType::Rotor) {
        long n = 1;
        for (size_t i = 0; i < 4; ++i) {
            int k = composite() && carried() ? outerRows.back().guess[i] - '0' : -1;
            int fit = 0;
            for (int v = 0; v <= 9; ++v) {
                bool ok = k < 0 || std::abs(v - k) <= 2;
                if (std::find(peeked.begin(), peeked.end(), static_cast<int>(i)) != peeked.end()) ok = ok && v == secret[i] - '0';
                for (size_t r = 0; ok && r < history.size(); ++r) {
                    const Row& row = history[r];
                    int gv = row.guess[i] - '0';
                    ok = rotorMark(v, gv) == row.marks[i] && (row.dir.size() != 4 || rotorDir(v, gv) == row.dir[i]);
                }
                if (ok) ++fit;
            }
            n *= fit;
        }
        return n;
    }
    if (kind == LockType::Cipher) {
        long n = 0;
        for (auto& w : cipherWords()) {
            Lock probe = *this;
            probe.secret = w;
            bool ok = true;
            for (int p : peeked) if (ok && p >= 0 && p < 5) ok = w[static_cast<size_t>(p)] == secret[static_cast<size_t>(p)];
            for (size_t r = 0; ok && r < history.size(); ++r) {
                Row again;
                std::string err;
                ok = judgeGuess(probe, history[r].guess, again, err) && again.marks == history[r].marks;
            }
            if (ok) ++n;
        }
        return n;
    }
    return 1;
}

int keypadGuessesFor(long n) { return n <= 1 ? 1 : n <= 3 ? 2 : n <= 40 ? 3 : 4; }

int Lock::guessesLikely() const {
    if (open || !composite()) return 0;
    if (stage == 0) return keypadGuessesFor(codesLeft()) + 2;   // the rotor: probe the middle, then answer
    long n = codesLeft();
    if (n <= 1) return 1;
    int worst = 0;   // the widest wheel decides whether one probe is enough
    for (size_t i = 0; i < 4; ++i) {
        int k = carried() ? outerRows.back().guess[i] - '0' : -1;
        int fit = 0;
        for (int v = 0; v <= 9; ++v) {
            bool ok = k < 0 || std::abs(v - k) <= 2;
            for (auto& row : history) ok = ok && rotorMark(v, row.guess[i] - '0') == row.marks[i] &&
                                          (row.dir.size() != 4 || rotorDir(v, row.guess[i] - '0') == row.dir[i]);
            if (ok) ++fit;
        }
        worst = std::max(worst, fit);
    }
    return worst <= 5 ? 2 : 3;
}

uint64_t fortressSeed(const GameState& g, int tower, int floor) {
    int rekeys = 0;
    auto it = g.fortress.find(kTowerIdBase + tower * 10 + floor);
    if (it != g.fortress.end()) rekeys = it->second.rekeys;
    uint64_t s = seedOf(g.worldSeed, 0xF0E7000 + static_cast<uint64_t>(tower) * 16 + static_cast<uint64_t>(floor));
    s = seedOf(s, 0x4B3700 + static_cast<uint64_t>(g.keyOf(tower)));
    return seedOf(s, 0xA1A200 + static_cast<uint64_t>(rekeys));
}


bool Lock::known(int pos) const {
    if (std::find(peeked.begin(), peeked.end(), pos) != peeked.end()) return true;
    for (auto& r : history)
        if (pos < static_cast<int>(r.marks.size()) && r.marks[static_cast<size_t>(pos)] == 2) return true;
    return false;
}

int Lock::peek() {
    if (open || type == LockType::None) return -1;
    for (int i = 0; i < length(); ++i)
        if (!known(i)) {
            peeked.push_back(i);
            return i;
        }
    return -1;
}
