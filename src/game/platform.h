// GhostNet - tiny platform layer: colours, non-blocking keys, sound, paths.
#pragma once
#include <string>

namespace plat {

void init();                    // enable ANSI colours / UTF-8 where possible
bool interactive();             // stdin is a real terminal
void beginRealtime();           // start polling keys without echo
void endRealtime();
bool keyPressed();              // non-blocking, consumes the key
void sleepMs(int ms);
void clearScreen();
std::string saveDir();          // created on demand
bool readLine(std::string& out);   // false on EOF

// Sounds: "the sound of a machine burning should be unpleasant and specific;
// the sound of a machine coming online should be satisfying."
void soundOnline();
void soundBurn();
void soundAlert();

extern bool soundOn;
extern bool fast;               // no animation delays (tests / --fast)
extern bool colorOn;

}  // namespace plat

// ANSI colour helpers. They return "" when colours are disabled.
namespace col {
const char* reset();
const char* bold();
const char* dim();
const char* red();
const char* green();
const char* yellow();
const char* blue();
const char* magenta();
const char* cyan();
const char* white();
const char* bgRed();
}
