#include "locks.h"

#include <algorithm>
#include <cctype>
#include <iostream>

#include "platform.h"
#include "ui.h"

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

std::vector<std::string> fiveLetterWords() {
    std::vector<std::string> v;
    for (auto& w : kWords) if (w.size() == 5) v.push_back(w);
    return v;
}
}  // namespace

int baseGuesses(LockType t) {
    switch (t) {
        case LockType::Keypad: return 7;
        case LockType::Rotor: return 6;
        case LockType::Cipher: return 6;
        default: return 0;
    }
}

bool lockHasOddNearRule(LockType t) { return t == LockType::Rotor; }

std::string lockRuleText(LockType t) {
    switch (t) {
        case LockType::Keypad:
            return "KEYPAD: 4 different digits. HIT = right digit, right place. NEAR = right digit, wrong place.";
        case LockType::Rotor:
            return "ROTOR: 4 digits (repeats allowed), judged per wheel. '=' exact. '~' means the wheel is ONE AWAY "
                   "(higher or lower) - NOT misplaced. '.' means further off.";
        case LockType::Cipher:
            return "CIPHER: a 5-letter word. [X] right letter, right place. (x) letter is in the word elsewhere. "
                   "' x ' letter not present (duplicates are counted honestly).";
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
        L.secret = r.pick(fiveLetterWords());
    } else {
        L.open = true;
    }
    return L;
}

std::string Lock::format() const {
    switch (type) {
        case LockType::Keypad: return "4 different digits, e.g. 0123";
        case LockType::Rotor: return "4 digits, e.g. 1470";
        case LockType::Cipher: return "a 5-letter word, e.g. crane";
        default: return "";
    }
}
std::string Lock::rule() const { return lockRuleText(type); }

bool Lock::guess(const std::string& raw, std::string& err) {
    std::string g = ui::lower(ui::trim(raw));
    if (open) { err = "The lock is already open."; return false; }
    if (sealed()) { err = "The lock has sealed. No guesses remain."; return false; }
    std::string fb;
    if (type == LockType::Keypad) {
        if (g.size() != 4 || !std::all_of(g.begin(), g.end(), ::isdigit)) { err = "Keypad wants " + format() + "."; return false; }
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (g[i] == g[j]) { err = "Keypad digits must all be different."; return false; }
        int hit = 0, nearc = 0;
        for (int i = 0; i < 4; ++i) {
            if (g[i] == secret[i]) ++hit;
            else if (secret.find(g[i]) != std::string::npos) ++nearc;
        }
        fb = std::string(col::green()) + "HIT " + std::to_string(hit) + col::reset() + "  " + col::yellow() +
             "NEAR " + std::to_string(nearc) + col::reset();
        if (hit == 4) open = true;
    } else if (type == LockType::Rotor) {
        if (g.size() != 4 || !std::all_of(g.begin(), g.end(), ::isdigit)) { err = "Rotor wants " + format() + "."; return false; }
        int exact = 0;
        for (int i = 0; i < 4; ++i) {
            int d = std::abs(g[i] - secret[i]);
            if (d == 0) { fb += std::string(col::green()) + " = " + col::reset(); ++exact; }
            else if (d == 1) fb += std::string(col::yellow()) + " ~ " + col::reset();
            else fb += std::string(col::dim()) + " . " + col::reset();
        }
        if (exact == 4) open = true;
    } else if (type == LockType::Cipher) {
        if (g.size() != 5 || !std::all_of(g.begin(), g.end(), ::isalpha)) { err = "Cipher wants " + format() + "."; return false; }
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
        for (int i = 0; i < 5; ++i) {
            char up = static_cast<char>(std::toupper(static_cast<unsigned char>(g[i])));
            if (res[i] == 'G') fb += std::string(col::green()) + "[" + up + "]" + col::reset();
            else if (res[i] == 'Y') fb += std::string(col::yellow()) + "(" + g[i] + ")" + col::reset();
            else fb += std::string(col::dim()) + " " + g[i] + " " + col::reset();
        }
        if (g == secret) open = true;
    } else {
        err = "There is no lock.";
        return false;
    }
    ++used;
    history.push_back({g, fb});
    return true;
}

void Lock::print() const {
    std::cout << "  " << col::bold() << lockName(type) << " LOCK" << col::reset() << "   guesses left: "
              << (left() > 1 ? col::green() : col::red()) << left() << col::reset() << "/" << maxGuesses
              << (extraUsed ? col::dim() + std::string("   emergency attempt: spent") + col::reset() : "") << "\n";
    for (size_t i = 0; i < history.size(); ++i)
        std::cout << "   " << col::dim() << (i + 1) << "." << col::reset() << " " << ui::pad(history[i].guess, 7)
                  << history[i].feedback << "\n";
}
