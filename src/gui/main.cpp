// GHOSTNET - a game about a runner who stops renting and starts owning.
// The windowed front end: raylib draws a neon city over the pure game core in src/core.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app.h"
#include "audio.h"
#include "gfx.h"
#include "paths.h"
#include "raylib.h"
#include "rlgl.h"

#if defined(_WIN32)
// <windows.h> clashes with raylib's names, so declare the two calls we need.
extern "C" __declspec(dllimport) int __stdcall AttachConsole(unsigned long pid);
extern "C" __declspec(dllimport) void* __stdcall GetStdHandle(unsigned long which);
extern "C" __declspec(dllimport) int __stdcall MessageBoxA(void* wnd, const char* text, const char* caption, unsigned int type);
#endif

namespace {

// A GUI-subsystem exe has no console. When stdout isn't redirected, borrow the
// parent's console so `GhostNet.exe --version` still prints in a terminal.
void attachConsole() {
#if defined(_WIN32)
    void* h = GetStdHandle(static_cast<unsigned long>(-11));   // STD_OUTPUT_HANDLE
    if (h == nullptr || h == reinterpret_cast<void*>(-1)) {
        if (AttachConsole(static_cast<unsigned long>(-1))) {   // ATTACH_PARENT_PROCESS
            FILE* f = nullptr;
            f = std::freopen("CONOUT$", "w", stdout);
            (void)f;
            std::printf("\n");
        }
    }
#endif
}

void usage() {
    std::printf("GhostNet %s - a game about a runner who stops renting and starts owning.\n"
                "  --version          print the version and exit\n"
                "  --windowed         start in a window (default)\n"
                "  --fullscreen       start borderless fullscreen (F11 toggles in game)\n"
                "  --mute             start with sound off (M on the title screen toggles)\n"
                "  --save-dir DIR     keep the save somewhere else\n"
                "  --shots DIR        render a screenshot tour to DIR and exit (testing)\n"
                "  --autoplay         play the UI with scripted clicks and typing, then exit (testing)\n",
                GHOSTNET_VERSION);
}

void render(App& app) {
    // Mouse and drawing share one letterbox fit (gx::fit), so what you click is what you see.
    float s, ox, oy;
    gx::fit(GetScreenWidth(), GetScreenHeight(), s, ox, oy);
    gx::uiScale = s;
    BeginDrawing();
    ClearBackground(BLACK);
    rlPushMatrix();
    rlTranslatef(ox, oy, 0);
    rlScalef(s, s, 1);
    app.frame();
    rlPopMatrix();
    EndDrawing();
}

// raylib's own log goes to ghostnet.log next to the save, so a player who hits a
// problem has something to send.
FILE* gLog = nullptr;
void logToFile(int level, const char* fmt, va_list args) {
    if (!gLog) return;
    static const char* names[] = {"ALL", "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "NONE"};
    std::fprintf(gLog, "%s: ", names[std::clamp(level, 0, 7)]);
    std::vfprintf(gLog, fmt, args);
    std::fputc('\n', gLog);
    std::fflush(gLog);
}

void fatal(const char* msg) {
    if (gLog) std::fprintf(gLog, "FATAL: %s\n", msg);
#if defined(_WIN32)
    MessageBoxA(nullptr, msg, "GhostNet", 0x10);   // MB_ICONERROR
#else
    std::fprintf(stderr, "%s\n", msg);
#endif
}

// Open the window at a comfortable size for this monitor (about 80% of it,
// keeping 16:9), centred. The canvas scales to whatever size the window ends up.
void sizeWindow() {
    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    if (mw <= 0 || mh <= 0) return;
    float s = std::min(mw * 0.8f / gx::W, mh * 0.8f / gx::H);
    s = std::clamp(s, 0.5f, 3.0f);
    int w = static_cast<int>(gx::W * s), h = static_cast<int>(gx::H * s);
    SetWindowSize(w, h);
    Vector2 mp = GetMonitorPosition(mon);
    SetWindowPosition(static_cast<int>(mp.x) + (mw - w) / 2, static_cast<int>(mp.y) + (mh - h) / 2);
}

}  // namespace

int main(int argc, char** argv) {
    std::string shots;
    bool autoplay = false;
    bool fullscreen = false, mute = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version")) {
            attachConsole();
            std::printf("GhostNet %s\n", GHOSTNET_VERSION);
            std::fflush(stdout);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            attachConsole();
            usage();
            std::fflush(stdout);
            return 0;
        }
        if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!std::strcmp(argv[i], "--autoplay")) autoplay = true;
        else if (!std::strcmp(argv[i], "--save-dir") && i + 1 < argc) paths::overrideSaveDir(argv[++i]);
        else if (!std::strcmp(argv[i], "--fullscreen")) fullscreen = true;
        else if (!std::strcmp(argv[i], "--windowed")) fullscreen = false;
        else if (!std::strcmp(argv[i], "--mute")) mute = true;
    }
#if defined(GHOSTNET_HEADLESS)
    if (shots.empty() && !autoplay) {
        std::printf("This is a headless test build of GhostNet %s: run it with --shots DIR or --autoplay.\n", GHOSTNET_VERSION);
        return 2;
    }
#endif
    bool testing = !shots.empty() || autoplay;
    if (!testing) {
        gLog = std::fopen((paths::saveDir() + "/ghostnet.log").c_str(), "w");
        if (gLog) std::fprintf(gLog, "GhostNet %s\n", GHOSTNET_VERSION);
        SetTraceLogCallback(logToFile);
        SetTraceLogLevel(LOG_INFO);
        // No FLAG_WINDOW_HIGHDPI: with it raylib rescales the mouse behind our back on
        // scaled Windows displays (125%/150%), and clicks miss the buttons. GLFW already
        // makes the process DPI-aware, so the window is in real pixels and we fit the
        // canvas to it ourselves.
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    } else {
        SetTraceLogLevel(LOG_WARNING);
    }
    InitWindow(gx::W, gx::H, "GHOSTNET");
    if (!IsWindowReady()) {
        fatal("GhostNet couldn't open its window.\n\nIt needs a graphics driver with OpenGL 3.3. "
              "Updating your graphics driver usually fixes this.\n\nDetails are in ghostnet.log in %APPDATA%\\GhostNet.");
        return 1;
    }
    if (!testing) sizeWindow();
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    gx::loadFonts();
    gx::initCity(1337);
    if (!testing) sfx::init();
    sfx::enabled = !mute;

    int rc = 0;
    {
        App app;
        if (autoplay) {
            rc = app.autoplay([&app] { render(app); });
        } else if (!shots.empty()) {
            rc = app.screenshotTour(shots, [&app] { render(app); });
        } else {
            if (fullscreen) ToggleBorderlessWindowed();
            while (!WindowShouldClose() && !app.wantsQuit()) {
                if (IsKeyPressed(KEY_F11))
                    ToggleBorderlessWindowed();
                render(app);
            }
            app.shutdown();
        }
    }
    sfx::shutdown();
    gx::unloadFonts();
    CloseWindow();
    if (gLog) std::fclose(gLog);
    return rc;
}
