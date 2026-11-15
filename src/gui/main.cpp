// GHOSTNET - a game about a runner who stops renting and starts owning.
// The windowed front end: raylib draws a neon city over the pure game core in src/core.
#include <algorithm>
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
                "  --shots DIR        render a screenshot tour to DIR and exit (testing)\n",
                GHOSTNET_VERSION);
}

void render(App& app) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    float s = std::min(sw / static_cast<float>(gx::W), sh / static_cast<float>(gx::H));
    if (s <= 0) s = 1;
    float ox = (sw - gx::W * s) / 2, oy = (sh - gx::H * s) / 2;
    SetMouseOffset(static_cast<int>(-ox), static_cast<int>(-oy));
    SetMouseScale(1.0f / s, 1.0f / s);
    BeginDrawing();
    ClearBackground(BLACK);
    rlPushMatrix();
    rlTranslatef(ox, oy, 0);
    rlScalef(s, s, 1);
    app.frame();
    rlPopMatrix();
    EndDrawing();
}

}  // namespace

int main(int argc, char** argv) {
    std::string shots;
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
        else if (!std::strcmp(argv[i], "--save-dir") && i + 1 < argc) paths::overrideSaveDir(argv[++i]);
        else if (!std::strcmp(argv[i], "--fullscreen")) fullscreen = true;
        else if (!std::strcmp(argv[i], "--windowed")) fullscreen = false;
        else if (!std::strcmp(argv[i], "--mute")) mute = true;
    }
#if defined(GHOSTNET_HEADLESS)
    if (shots.empty()) {
        std::printf("This is a headless test build of GhostNet %s: run it with --shots DIR.\n", GHOSTNET_VERSION);
        return 2;
    }
#endif
    SetTraceLogLevel(LOG_WARNING);
    if (shots.empty()) SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(gx::W, gx::H, "GHOSTNET");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    gx::loadFonts();
    gx::initCity(1337);
    if (shots.empty()) sfx::init();
    sfx::enabled = !mute;

    int rc = 0;
    {
        App app;
        if (!shots.empty()) {
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
    return rc;
}
