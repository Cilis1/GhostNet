#include "ui.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

#include "balance.h"
#include "platform.h"

namespace ui {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.push_back(w);
    return out;
}
bool isNumber(const std::string& s) {
    if (s.empty() || s.size() > 6) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

std::string prompt(const std::string& p) {
    std::cout << col::cyan() << p << col::reset();
    std::string line;
    if (!plat::readLine(line)) throw EofExit{};
    if (!plat::interactive()) std::cout << line << "\n";   // echo scripted input
    return trim(line);
}
bool confirm(const std::string& q) {
    for (;;) {
        std::string a = lower(prompt(q + " [y/n] "));
        if (a == "y" || a == "yes") return true;
        if (a == "n" || a == "no" || a.empty()) return false;
    }
}
void pause() { prompt("  (press Enter) "); }
void header(const std::string& title) {
    std::cout << "\n" << col::bold() << col::green() << "== " << title << " "
              << std::string(title.size() < 66 ? 66 - title.size() : 2, '=') << col::reset() << "\n";
}
void rule() { std::cout << col::dim() << std::string(72, '-') << col::reset() << "\n"; }
void typeOut(const std::string& text, int ms) {
    for (char c : text) {
        std::cout << c << std::flush;
        if (c != ' ') plat::sleepMs(ms);
    }
    std::cout << "\n";
}

std::string bar(double frac, int width, const char* color) {
    frac = std::max(0.0, std::min(1.0, frac));
    int n = static_cast<int>(frac * width + 0.5);
    return std::string("[") + color + std::string(static_cast<size_t>(n), '#') + col::dim() +
           std::string(static_cast<size_t>(width - n), '-') + col::reset() + "]";
}
std::string traceBar(double trace, int width) {
    const char* c = trace < 50 ? col::green() : trace < 80 ? col::yellow() : col::red();
    return bar(trace / 100.0, width, c);
}
std::string conditionWord(int c) {
    if (c <= 0) return "DEAD";
    if (c >= 90) return "pristine";
    if (c >= 70) return "worn";
    if (c >= 45) return "scorched";
    if (c >= 20) return "sagging";
    return "failing";
}
std::string conditionBar(int c, int width) {
    // Condition shown "as scorching": healthy '=' then burnt '~'.
    int n = (c * width + 50) / 100;
    const char* colr = c >= 70 ? col::green() : c >= 45 ? col::yellow() : col::red();
    return std::string(colr) + std::string(static_cast<size_t>(n), '=') + col::dim() +
           std::string(static_cast<size_t>(width - n), '~') + col::reset();
}
std::string noiseStr(int n) {
    const char* c = n <= 1 ? col::dim() : n <= 3 ? col::yellow() : col::red();
    // Signal: how loud the machine is on the wire. Minimum for everything you can buy.
    return std::string(c) + "sig " + (n <= 1 ? "min" : std::string(static_cast<size_t>(n - 1), '+')) + col::reset();
}
std::string pad(const std::string& s, size_t w) {
    if (s.size() >= w) return s.substr(0, w);
    return s + std::string(w - s.size(), ' ');
}
std::string credits(long long c) { return std::to_string(c) + "cr"; }

static std::string light(const Machine& m, bool armed) {
    if (m.dark()) return std::string(col::bgRed()) + "X" + col::reset();
    if (!m.active) return std::string(col::dim()) + "o" + col::reset();
    if (armed) return std::string(col::red()) + col::bold() + "!" + col::reset();
    return std::string(col::green()) + col::bold() + "*" + col::reset();
}

static void rackLine(const Machine& m, const GameState& g) {
    bool armed = g.net.decoyId == m.id;
    std::cout << "  | " << light(m, armed) << " " << col::bold() << pad("[" + std::to_string(m.id) + "]", 5)
              << pad(m.name, 16) << col::reset() << pad(jobName(m.job), 8) << "T" << m.tier << " "
              << pad(m.home ? "home" : m.captured ? "captured" : "bought", 9)
              << (m.home ? std::string(col::cyan()) + "==========" + col::reset() : conditionBar(m.condition))
              << " " << pad(m.home ? "never wears" : std::to_string(m.condition) + "% " + conditionWord(m.condition), 14)
              << noiseStr(m.noise);
    if (!m.home) std::cout << col::dim() << "  " << upkeepOf(m) << "cr/run, worth " << scrapValue(m) << "cr" << col::reset();
    if (armed) std::cout << "  " << col::bgRed() << " DECOY - ARMED TO DIE " << col::reset();
    std::cout << "\n";
    std::cout << "  |     " << col::dim() << describeEffect(m) << " " << m.origin << col::reset() << "\n";
}

void drawRack(const GameState& g) {
    const Network& n = g.net;
    std::cout << "  " << col::dim() << ".-- THE RACK " << std::string(58, '-') << "." << col::reset() << "\n";
    int shown = 0;
    for (auto& m : n.machines)
        if (m.active) { rackLine(m, g); ++shown; }
    for (int i = shown; i < n.slots; ++i)
        std::cout << "  | " << col::dim() << "-  [ ]  empty slot" << col::reset() << "\n";
    for (int i = n.slots; i < bal::MAX_SLOTS; ++i)
        std::cout << "  | " << col::dim() << "#  locked slot (expand)" << col::reset() << "\n";
    std::cout << "  " << col::dim() << "'" << std::string(71, '-') << "'" << col::reset() << "\n";
    std::cout << "  " << col::dim() << "   COLD STORAGE (dark shelf)" << col::reset() << "\n";
    bool any = false;
    for (auto& m : n.machines)
        if (!m.active) { rackLine(m, g); any = true; }
    if (!any) std::cout << "  " << col::dim() << "|     (empty)" << col::reset() << "\n";
}

}  // namespace ui
