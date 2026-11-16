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
                   "ONE NOTCH AWAY.";
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
    if (stage == 0)
        return "COMPOSITE, STAGE 1 of 2 - KEYPAD: 4 different digits. Green = right box. Amber = in the code, another box. "
               "A ROTOR waits behind it: each of its wheels sits within 2 notches of this stage's digit in the same box.";
    if (!carried())
        return "COMPOSITE, STAGE 2 of 2 - ROTOR: the phrase opened the keypad without showing its code, so nothing carries. "
               "4 digits, repeats allowed. Green = exact. Amber = ONE NOTCH AWAY. Dark = further off.";
    return "COMPOSITE, STAGE 2 of 2 - ROTOR: every wheel is within 2 notches of the keypad code " + outerRows.back().guess +
           " in the same box. Green = exact. Amber = ONE NOTCH AWAY. Dark = further off.";
}

namespace {
// The lock alphabets are plain ASCII. Checking the ranges directly rejects high-bit bytes
// (which <cctype> would see as negative chars - undefined behaviour) and keeps cipher
// letters inside 'a'..'z' for the counts[26] index.
bool asciiDigit(char c) noexcept { return c >= '0' && c <= '9'; }
bool asciiLower(char c) noexcept { return c >= 'a' && c <= 'z'; }
}  // namespace

bool Lock::guess(const std::string& raw, std::string& err) {
    std::string g = txt::lower(txt::trim(raw));
    Row row;
    if (open) { err = "The lock is already open."; return false; }
    if (sealed()) { err = "The lock has sealed. No guesses remain."; return false; }
    const LockType kind = cur();
    if (kind == LockType::Keypad) {
        if (g.size() != 4 || !std::all_of(g.begin(), g.end(), asciiDigit)) { err = "Keypad wants " + format() + "."; return false; }
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
        if (hit == 4 && !composite()) open = true;
    } else if (kind == LockType::Rotor) {
        if (g.size() != 4 || !std::all_of(g.begin(), g.end(), asciiDigit)) { err = "Rotor wants " + format() + "."; return false; }
        int exact = 0;
        for (int i = 0; i < 4; ++i) {
            int d = std::abs(g[i] - secret[i]);
            if (d == 0) { row.marks.push_back(2); ++exact; }
            else if (d == 1) row.marks.push_back(1);
            else row.marks.push_back(0);
        }
        if (exact == 4) open = true;
    } else if (kind == LockType::Cipher) {
        if (g.size() != 5 || !std::all_of(g.begin(), g.end(), asciiLower)) { err = "Cipher wants " + format() + "."; return false; }
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
        if (g == secret) open = true;
    } else {
        err = "There is no lock.";
        return false;
    }
    ++used;
    row.guess = g;
    history.push_back(row);
    if (composite() && stage == 0 && row.hit == 4) openOuter();   // the inner rotor takes over
    return true;
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
