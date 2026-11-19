// GhostNet - the look: palette, fonts, neon drawing helpers and immediate-mode widgets.
// Everything draws in a fixed 1280x720 virtual space; main.cpp scales it to the window.
#pragma once
#include <string>
#include <vector>

#include "raylib.h"

namespace gx {

constexpr int W = 1280;
constexpr int H = 720;

// ---- palette ---------------------------------------------------------------
constexpr Color C_CYAN{0, 240, 255, 255};
constexpr Color C_MAGENTA{255, 43, 214, 255};
constexpr Color C_AMBER{255, 176, 0, 255};
constexpr Color C_GREEN{57, 255, 136, 255};
constexpr Color C_RED{255, 51, 85, 255};
constexpr Color C_PURPLE{150, 100, 255, 255};
constexpr Color C_TEXT{214, 236, 255, 255};
constexpr Color C_DIM{112, 128, 168, 255};
constexpr Color C_DARK{38, 44, 70, 255};
constexpr Color C_INK{6, 4, 18, 255};

Color fade(Color c, float alpha);           // multiply alpha
Color mix(Color a, Color b, float t);
Color pulse(Color c, float speed, float lo = 0.55f);   // breathing alpha

// ---- per-frame state -------------------------------------------------------
extern float now;          // seconds since start
extern float dt;
extern Vector2 mouse;      // virtual coordinates
extern bool clicked;       // left button pressed this frame (and not yet consumed)
extern float wheel;
extern int layer;          // layer currently being drawn
extern int topLayer;       // only widgets on this layer react (modals sit on top)
extern float uiScale;      // window pixels per virtual pixel (fonts rasterise at this scale)
void beginFrame(int modalDepth);

// ---- input -------------------------------------------------------------------
// Every bit of game input goes through these, never straight to raylib, so the
// headless autoplay test (autoplay.cpp) can drive the real UI with scripted
// mouse moves, clicks and typing.
struct ScriptInput {
    Vector2 raw{0, 0};          // mouse in window pixels
    bool click = false;         // left button pressed this frame
    std::vector<int> keys;      // keys pressed this frame
    std::vector<int> chars;     // characters typed this frame
    int screenW = W, screenH = H;
};
extern ScriptInput* script;     // null = real input
// Letterbox fit of the 1280x720 canvas into a window: scale and offset.
void fit(int screenW, int screenH, float& scale, float& ox, float& oy);
Vector2 toVirtual(Vector2 raw, int screenW, int screenH);
bool keyPressed(int key);
bool keyRepeat(int key);
bool keyDown(int key);
int charPressed();
// Clickable things on the top layer this frame (for the autoplay test).
struct Widget {
    Rectangle r;
    std::string label;
};
extern std::vector<Widget> widgets;
// Every string drawn this frame, kept only while recordText is on (the autoplay test reads the screen).
extern bool recordText;
extern std::vector<std::string> drawnText;
bool onScreen(const std::string& needle);   // some string drawn last frame contains needle

// ---- fonts -----------------------------------------------------------------
enum class F { Display, Heavy, Mono, MonoBold };
void loadFonts();
void unloadFonts();
enum Align { Left, Center, Right };
float textWidth(F f, const std::string& s, float size);
void text(F f, const std::string& s, float x, float y, float size, Color c, Align a = Left);
void glowText(F f, const std::string& s, float x, float y, float size, Color c, Align a = Left, float strength = 1.0f);
void chromaText(F f, const std::string& s, float x, float y, float size, Color c, Align a, float split);
// Re-flows terminal-era text: joins hard-wrapped lines, keeps blank-line paragraph breaks.
std::string flow(const std::string& s);
std::vector<std::string> wrap(F f, const std::string& s, float width, float size);
// Draws wrapped text, returns the height used. maxLines <= 0 means no limit.
float para(F f, const std::string& s, float x, float y, float width, float size, Color c, float lineGap = 1.35f, int maxLines = 0);

// ---- shapes ----------------------------------------------------------------
void glowRect(Rectangle r, Color c, float thick = 1.5f, int layers = 4);
void panel(Rectangle r, Color accent, const std::string& title = "", float alpha = 0.93f);
void brackets(Rectangle r, Color c, float len = 14, float thick = 2);
void glowLine(Vector2 a, Vector2 b, Color c, float thick = 2);
void glowDot(Vector2 p, float r, Color c);
void hexNode(Vector2 p, float r, Color c, float fill = 0.18f, float rot = 30);
void pips(float x, float y, int filled, int total, Color c, float size = 9, float gap = 4);
void progress(Rectangle r, float frac, Color c, bool stripes = true);
void tracer(Vector2 a, Vector2 b, float phase, Color c, int dots = 3);   // packets moving a->b
float tag(float x, float y, const std::string& s, Color c, float size = 12);   // pill label, returns width

// ---- widgets ---------------------------------------------------------------
bool hovered(Rectangle r);
bool button(Rectangle r, const std::string& label, Color c, bool enabled = true, bool primary = false, float size = 15);
bool hitArea(Rectangle r, const std::string& label = "");   // invisible clickable region
// Edits `field` from the keyboard. Returns true when Enter is pressed.
bool textInput(std::string& field, size_t maxLen, int mode);   // mode 0: any, 1: digits, 2: letters, 3: name

// ---- the city --------------------------------------------------------------
void initCity(unsigned seed);
void city(float brightness, float danger);   // skyline, rain, neon, grid floor
void shade(float y, float h);                // a dark band so text reads over the city
void crt(float glitch);                      // scanlines, vignette, glitch strips
void ticker(float y, const std::vector<std::string>& items);

// ---- screenshots (headless builds) -----------------------------------------
bool saveScreenshot(const std::string& path);

}  // namespace gx
