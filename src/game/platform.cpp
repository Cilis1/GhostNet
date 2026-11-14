#include "platform.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace plat {

bool soundOn = true;
bool fast = false;
bool colorOn = true;

#ifdef _WIN32
void init() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode) ||
        !SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/)) {
        colorOn = false;   // very old console: fall back to plain text
    }
    SetConsoleTitleW(L"GHOSTNET");
}
bool interactive() { return _isatty(_fileno(stdin)) != 0; }
void beginRealtime() {}
void endRealtime() { while (_kbhit()) _getch(); }
bool keyPressed() {
    if (!interactive()) return false;
    if (_kbhit()) { _getch(); return true; }
    return false;
}
void clearScreen() {
    if (colorOn) std::cout << "\x1b[2J\x1b[H" << std::flush;
    else std::system("cls");
}
static void tone(int f, int ms) { if (soundOn && !fast) Beep(f, ms); }
void soundOnline() { tone(523, 70); tone(659, 70); tone(784, 120); }
void soundBurn()   { tone(220, 160); tone(185, 160); tone(110, 420); }
void soundAlert()  { tone(880, 90); tone(880, 90); }
std::string saveDir() {
    const char* app = std::getenv("APPDATA");
    fs::path p = app ? fs::path(app) / "GhostNet" : fs::path(".") / "GhostNetSave";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}
#else
static termios g_old;
static bool g_raw = false;
void init() {
    const char* t = std::getenv("TERM");
    if (!isatty(STDOUT_FILENO) || (t && std::string(t) == "dumb")) colorOn = false;
}
bool interactive() { return isatty(STDIN_FILENO) != 0; }
void beginRealtime() {
    if (!interactive() || g_raw) return;
    tcgetattr(STDIN_FILENO, &g_old);
    termios raw = g_old;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_raw = true;
}
void endRealtime() {
    if (!g_raw) return;
    tcflush(STDIN_FILENO, TCIFLUSH);
    tcsetattr(STDIN_FILENO, TCSANOW, &g_old);
    g_raw = false;
}
bool keyPressed() {
    if (!g_raw) return false;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeval tv{0, 0};
    if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) return true;
    }
    return false;
}
void clearScreen() {
    if (colorOn) std::cout << "\x1b[2J\x1b[H" << std::flush;
}
void soundOnline() { if (soundOn && !fast && colorOn) std::cout << "\a" << std::flush; }
void soundBurn()   { if (soundOn && !fast && colorOn) std::cout << "\a" << std::flush; }
void soundAlert()  {}
std::string saveDir() {
    const char* home = std::getenv("HOME");
    fs::path p = home ? fs::path(home) / ".ghostnet" : fs::path(".") / ".ghostnet";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}
#endif

void sleepMs(int ms) {
    if (fast || ms <= 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool readLine(std::string& out) {
    std::cout << std::flush;
    if (!std::getline(std::cin, out)) return false;
    if (!out.empty() && out.back() == '\r') out.pop_back();
    return true;
}

}  // namespace plat

namespace col {
#define GN_COL(fn, code) const char* fn() { return plat::colorOn ? code : ""; }
GN_COL(reset, "\x1b[0m")
GN_COL(bold, "\x1b[1m")
GN_COL(dim, "\x1b[2m")
GN_COL(red, "\x1b[91m")
GN_COL(green, "\x1b[92m")
GN_COL(yellow, "\x1b[93m")
GN_COL(blue, "\x1b[94m")
GN_COL(magenta, "\x1b[95m")
GN_COL(cyan, "\x1b[96m")
GN_COL(white, "\x1b[97m")
GN_COL(bgRed, "\x1b[41;97m")
#undef GN_COL
}  // namespace col
