// GhostNet - text UI helpers.
#pragma once
#include <string>
#include <vector>

#include "common.h"

struct EofExit {};   // thrown when stdin closes; main() saves and exits

namespace ui {
std::string lower(std::string s);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s);
bool isNumber(const std::string& s);

std::string prompt(const std::string& p);          // throws EofExit on EOF
bool confirm(const std::string& question);         // y/n
void pause();
void header(const std::string& title);
void rule();
void typeOut(const std::string& text, int msPerChar = 6);

std::string bar(double frac, int width, const char* color);
std::string traceBar(double trace, int width = 20);
std::string conditionWord(int c);
std::string conditionBar(int c, int width = 10);
std::string noiseStr(int n);
std::string pad(const std::string& s, size_t w);
std::string credits(long long c);

void drawRack(const GameState& g);                 // the physical rack picture
}
