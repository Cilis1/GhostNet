#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cctype>

#include "audio.h"
#include "rlgl.h"

const unsigned char* embeddedFont(const char* name, int* size);   // generated (cmake/embed_fonts.cmake)

namespace gx {

float now = 0, dt = 0, wheel = 0;
Vector2 mouse{0, 0};
bool clicked = false;
int layer = 0, topLayer = 0;

Color fade(Color c, float a) {
    a = std::clamp(a, 0.0f, 1.0f);
    c.a = static_cast<unsigned char>(c.a * a);
    return c;
}

Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto l = [t](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x + (y - x) * t); };
    return Color{l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

Color pulse(Color c, float speed, float lo) {
    float s = 0.5f + 0.5f * std::sin(now * speed);
    return fade(c, lo + (1.0f - lo) * s);
}

float uiScale = 1.0f;
ScriptInput* script = nullptr;
std::vector<Widget> widgets;

void fit(int screenW, int screenH, float& scale, float& ox, float& oy) {
    scale = std::min(screenW / static_cast<float>(W), screenH / static_cast<float>(H));
    if (!(scale > 0.01f)) scale = 1.0f;
    ox = (screenW - W * scale) / 2;
    oy = (screenH - H * scale) / 2;
}

Vector2 toVirtual(Vector2 raw, int screenW, int screenH) {
    float s, ox, oy;
    fit(screenW, screenH, s, ox, oy);
    return Vector2{(raw.x - ox) / s, (raw.y - oy) / s};
}

bool keyPressed(int key) {
    if (script) return std::find(script->keys.begin(), script->keys.end(), key) != script->keys.end();
    return IsKeyPressed(key);
}
bool keyRepeat(int key) { return script ? false : IsKeyPressedRepeat(key); }
bool keyDown(int key) { return script ? keyPressed(key) : IsKeyDown(key); }
int charPressed() {
    if (script) {
        if (script->chars.empty()) return 0;
        int c = script->chars.front();
        script->chars.erase(script->chars.begin());
        return c;
    }
    return GetCharPressed();
}

void beginFrame(int modalDepth) {
    now = static_cast<float>(GetTime());
    dt = std::min(GetFrameTime(), 0.1f);
    // Mouse and screen are both in window pixels (the window is not created with
    // FLAG_WINDOW_HIGHDPI, so raylib applies no scaling of its own); map to the
    // 1280x720 canvas with the same letterbox fit main.cpp draws with.
    if (script) {
        mouse = toVirtual(script->raw, script->screenW, script->screenH);
        clicked = script->click;
        wheel = 0;
    } else {
        mouse = toVirtual(GetMousePosition(), GetScreenWidth(), GetScreenHeight());
        clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        wheel = GetMouseWheelMove();
    }
    topLayer = modalDepth;
    layer = 0;
    widgets.clear();
}

// ---------------------------------------------------------------------------
// Fonts: each family is rasterised at a few sizes; text picks the nearest one
// at or above the requested size so small text stays crisp.
// ---------------------------------------------------------------------------
namespace {
struct Sized { float px; Font font; };
std::vector<Sized> families[4];

void loadFamily(F f, const char* name, std::initializer_list<int> sizes) {
    int len = 0;
    const unsigned char* data = embeddedFont(name, &len);
    auto& fam = families[static_cast<int>(f)];
    for (int px : sizes) {
        Font font = data ? LoadFontFromMemory(".ttf", data, len, px, nullptr, 0) : GetFontDefault();
        SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
        fam.push_back({static_cast<float>(px), font});
    }
}

const Font& pick(F f, float size) {
    auto& fam = families[static_cast<int>(f)];
    size *= uiScale;   // rasterised size on screen
    for (auto& s : fam)
        if (s.px >= size - 0.5f) return s.font;
    return fam.back().font;
}

float spacingFor(F f, float size) {
    if (f == F::Display || f == F::Heavy) return size * 0.08f;
    return 0.0f;
}
}  // namespace

void loadFonts() {
    loadFamily(F::Display, "Orbitron-Bold", {16, 24, 32, 40, 64, 96});
    loadFamily(F::Heavy, "Orbitron-Black", {48, 96, 128, 192});
    loadFamily(F::Mono, "JetBrainsMono-Regular", {13, 15, 18, 24, 30, 36});
    loadFamily(F::MonoBold, "JetBrainsMono-Bold", {13, 15, 18, 24, 30, 36, 54});
}

void unloadFonts() {
    for (auto& fam : families) {
        for (auto& s : fam)
            if (s.font.texture.id != GetFontDefault().texture.id) UnloadFont(s.font);
        fam.clear();
    }
}

float textWidth(F f, const std::string& s, float size) {
    return MeasureTextEx(pick(f, size), s.c_str(), size, spacingFor(f, size)).x;
}

void text(F f, const std::string& s, float x, float y, float size, Color c, Align a) {
    if (s.empty()) return;
    float w = (a == Left) ? 0 : textWidth(f, s, size);
    if (a == Center) x -= w / 2;
    if (a == Right) x -= w;
    DrawTextEx(pick(f, size), s.c_str(), Vector2{std::round(x), std::round(y)}, size, spacingFor(f, size), c);
}

void glowText(F f, const std::string& s, float x, float y, float size, Color c, Align a, float strength) {
    float w = (a == Left) ? 0 : textWidth(f, s, size);
    if (a == Center) x -= w / 2;
    if (a == Right) x -= w;
    const Font& font = pick(f, size);
    float sp = spacingFor(f, size);
    BeginBlendMode(BLEND_ADDITIVE);
    float r = std::clamp(size * 0.045f, 1.0f, 3.0f);
    static const float offs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7f, 0.7f}, {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
    for (int ring = 2; ring >= 1; --ring)
        for (auto& o : offs)
            DrawTextEx(font, s.c_str(), Vector2{x + o[0] * r * ring, y + o[1] * r * ring}, size, sp, fade(c, (ring == 1 ? 0.07f : 0.035f) * strength));
    EndBlendMode();
    DrawTextEx(font, s.c_str(), Vector2{std::round(x), std::round(y)}, size, sp, c);
}

void chromaText(F f, const std::string& s, float x, float y, float size, Color c, Align a, float split) {
    float w = (a == Left) ? 0 : textWidth(f, s, size);
    if (a == Center) x -= w / 2;
    if (a == Right) x -= w;
    BeginBlendMode(BLEND_ADDITIVE);
    text(f, s, x - split, y, size, fade(C_RED, 0.55f));
    text(f, s, x + split, y + split * 0.3f, size, fade(C_CYAN, 0.55f));
    EndBlendMode();
    glowText(f, s, x, y, size, c, Left, 1.4f);
}

std::string flow(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        size_t nl = s.find('\n', i);
        std::string line = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        size_t a = line.find_first_not_of(" \t");
        line = a == std::string::npos ? "" : line.substr(a);
        if (line.empty()) {
            if (!out.empty() && out.back() != '\n') out += "\n\n";
        } else {
            if (!out.empty() && out.back() != '\n') out += ' ';
            out += line;
        }
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

std::vector<std::string> wrap(F f, const std::string& s, float width, float size) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && textWidth(f, trial, size) > width) {
                out.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

float para(F f, const std::string& s, float x, float y, float width, float size, Color c, float lineGap, int maxLines) {
    auto lines = wrap(f, s, width, size);
    float lh = size * lineGap;
    int n = 0;
    for (auto& l : lines) {
        if (maxLines > 0 && n >= maxLines) break;
        text(f, l, x, y + n * lh, size, c);
        ++n;
    }
    return n * lh;
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------
void glowRect(Rectangle r, Color c, float thick, int layers) {
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = layers; i >= 1; --i) {
        float g = i * 2.0f;
        DrawRectangleLinesEx(Rectangle{r.x - g, r.y - g, r.width + 2 * g, r.height + 2 * g}, 2.0f, fade(c, 0.06f * (layers + 1 - i)));
    }
    EndBlendMode();
    DrawRectangleLinesEx(r, thick, c);
}

void brackets(Rectangle r, Color c, float len, float th) {
    float x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
    DrawRectangleRec(Rectangle{x0 - 1, y0 - 1, len, th}, c);
    DrawRectangleRec(Rectangle{x0 - 1, y0 - 1, th, len}, c);
    DrawRectangleRec(Rectangle{x1 - len + 1, y0 - 1, len, th}, c);
    DrawRectangleRec(Rectangle{x1 - th + 1, y0 - 1, th, len}, c);
    DrawRectangleRec(Rectangle{x0 - 1, y1 - th + 1, len, th}, c);
    DrawRectangleRec(Rectangle{x0 - 1, y1 - len + 1, th, len}, c);
    DrawRectangleRec(Rectangle{x1 - len + 1, y1 - th + 1, len, th}, c);
    DrawRectangleRec(Rectangle{x1 - th + 1, y1 - len + 1, th, len}, c);
}

void panel(Rectangle r, Color accent, const std::string& title, float alpha) {
    alpha = std::max(alpha, 0.0f);
    DrawRectangleRec(r, fade(Color{8, 9, 26, 255}, alpha));
    DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), 60, fade(accent, 0.10f), fade(accent, 0.0f));
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 3; i >= 1; --i) {
        float g = i * 2.0f;
        DrawRectangleLinesEx(Rectangle{r.x - g, r.y - g, r.width + 2 * g, r.height + 2 * g}, 2.0f, fade(accent, 0.035f * (4 - i)));
    }
    EndBlendMode();
    DrawRectangleLinesEx(r, 1.0f, fade(accent, 0.55f));
    brackets(r, accent, 16, 2);
    if (!title.empty()) {
        float w = textWidth(F::Display, title, 13) + 22;
        DrawRectangleRec(Rectangle{r.x + 18, r.y - 9, w, 18}, Color{8, 9, 26, 255});
        DrawRectangleLinesEx(Rectangle{r.x + 18, r.y - 9, w, 18}, 1.0f, fade(accent, 0.8f));
        text(F::Display, title, r.x + 29, r.y - 7, 13, accent);
    }
}

void glowLine(Vector2 a, Vector2 b, Color c, float th) {
    BeginBlendMode(BLEND_ADDITIVE);
    DrawLineEx(a, b, th * 4, fade(c, 0.06f));
    DrawLineEx(a, b, th * 2.2f, fade(c, 0.14f));
    EndBlendMode();
    DrawLineEx(a, b, th, c);
}

void glowDot(Vector2 p, float r, Color c) {
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(p, r * 3.2f, fade(c, 0.35f), fade(c, 0.0f));
    EndBlendMode();
    DrawCircleV(p, r, c);
}

void hexNode(Vector2 p, float r, Color c, float fill, float rot) {
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(p, r * 1.9f, fade(c, 0.20f), fade(c, 0.0f));
    EndBlendMode();
    DrawPoly(p, 6, r, rot, fade(Color{8, 9, 26, 255}, 0.95f));
    DrawPoly(p, 6, r, rot, fade(c, fill));
    DrawPolyLinesEx(p, 6, r + 4, rot, 1.0f, fade(c, 0.35f));
    DrawPolyLinesEx(p, 6, r, rot, 2.2f, c);
}

void pips(float x, float y, int filled, int total, Color c, float size, float gap) {
    for (int i = 0; i < total; ++i) {
        Rectangle r{x + i * (size + gap), y, size, size};
        if (i < filled) {
            DrawRectangleRec(r, c);
            BeginBlendMode(BLEND_ADDITIVE);
            DrawRectangleRec(Rectangle{r.x - 2, r.y - 2, r.width + 4, r.height + 4}, fade(c, 0.15f));
            EndBlendMode();
        } else {
            DrawRectangleLinesEx(r, 1.0f, fade(c, 0.4f));
        }
    }
}

void progress(Rectangle r, float frac, Color c, bool stripes) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    DrawRectangleRec(r, fade(c, 0.08f));
    Rectangle f{r.x, r.y, r.width * frac, r.height};
    DrawRectangleGradientH(static_cast<int>(f.x), static_cast<int>(f.y), static_cast<int>(f.width), static_cast<int>(f.height), fade(c, 0.45f), c);
    if (stripes && f.width > 4) {
        float off = std::fmod(now * 40.0f, 16.0f);
        for (float x = f.x - 16 + off; x < f.x + f.width; x += 16) {
            float x0 = std::max(x, f.x), x1 = std::min(x + 6, f.x + f.width);
            if (x1 > x0) DrawRectangleRec(Rectangle{x0, f.y, x1 - x0, f.height}, fade(WHITE, 0.10f));
        }
    }
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRectangleRec(Rectangle{f.x, f.y - 3, f.width, f.height + 6}, fade(c, 0.12f));
    EndBlendMode();
    DrawRectangleLinesEx(r, 1.0f, fade(c, 0.6f));
}

void tracer(Vector2 a, Vector2 b, float phase, Color c, int dots) {
    for (int i = 0; i < dots; ++i) {
        float t = std::fmod(phase + static_cast<float>(i) / dots, 1.0f);
        Vector2 p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        glowDot(p, 2.2f, c);
    }
}

float tag(float x, float y, const std::string& s, Color c, float size) {
    float w = textWidth(F::MonoBold, s, size) + 12;
    Rectangle r{x, y, w, size + 8};
    DrawRectangleRec(r, fade(c, 0.14f));
    DrawRectangleLinesEx(r, 1.0f, fade(c, 0.85f));
    text(F::MonoBold, s, x + 6, y + 4, size, c);
    return w;
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
bool hovered(Rectangle r) { return layer == topLayer && CheckCollisionPointRec(mouse, r); }

bool hitArea(Rectangle r, const std::string& label) {
    if (layer == topLayer) widgets.push_back({r, label});
    if (clicked && hovered(r)) {
        clicked = false;
        return true;
    }
    return false;
}

bool button(Rectangle r, const std::string& label, Color c, bool enabled, bool primary, float size) {
    bool hov = enabled && hovered(r);
    if (enabled && layer == topLayer) widgets.push_back({r, label});
    Color base = enabled ? c : C_DARK;
    float fill = primary ? 0.22f : 0.08f;
    if (hov) fill += 0.16f;
    DrawRectangleRec(r, fade(Color{8, 9, 26, 255}, 0.9f));
    DrawRectangleRec(r, fade(base, fill));
    if (hov || primary) {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangleRec(Rectangle{r.x - 3, r.y - 3, r.width + 6, r.height + 6}, fade(base, hov ? 0.12f : 0.06f));
        EndBlendMode();
    }
    DrawRectangleLinesEx(r, hov ? 2.0f : 1.0f, fade(base, enabled ? 0.95f : 0.6f));
    // clipped corner accent
    DrawTriangle(Vector2{r.x + r.width - 10, r.y}, Vector2{r.x + r.width, r.y + 10}, Vector2{r.x + r.width, r.y}, fade(base, 0.9f));
    Color tc = enabled ? (hov ? WHITE : mix(base, WHITE, 0.25f)) : C_DIM;
    float tw = textWidth(F::Display, label, size);
    float sz = size;
    if (tw > r.width - 16) sz = size * (r.width - 16) / tw;
    if (hov) glowText(F::Display, label, r.x + r.width / 2, r.y + (r.height - sz) / 2, sz, tc, Center, 0.8f);
    else text(F::Display, label, r.x + r.width / 2, r.y + (r.height - sz) / 2, sz, tc, Center);
    if (enabled && clicked && hov) {
        clicked = false;
        sfx::play(sfx::Click);
        return true;
    }
    return false;
}

bool textInput(std::string& field, size_t maxLen, int mode) {
    if (layer != topLayer) return false;
    int ch;
    while ((ch = charPressed()) > 0) {
        if (ch > 126) continue;
        char c = static_cast<char>(ch);
        bool ok = false;
        if (mode == 1) ok = c >= '0' && c <= '9';
        else if (mode == 2) ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        else if (mode == 3) ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ' ';
        else ok = c >= 32;
        if (ok && field.size() < maxLen) {
            field += c;
            sfx::play(sfx::Key);
        }
    }
    if ((keyPressed(KEY_BACKSPACE) || keyRepeat(KEY_BACKSPACE)) && !field.empty()) {
        field.pop_back();
        sfx::play(sfx::Key);
    }
    return keyPressed(KEY_ENTER) || keyPressed(KEY_KP_ENTER);
}

// ---------------------------------------------------------------------------
// The city behind everything
// ---------------------------------------------------------------------------
namespace {
struct Building {
    float x, w, h;
    int layerIdx;
    unsigned seed;
    int sign;   // -1 none, else index into signs
};
std::vector<Building> buildings;
const char* kSigns[] = {"RENT", "HOTEL", "24H", "NOODLE", "PAWN", "OPEN", "LOAN"};
constexpr float HORIZON = 540.0f;

unsigned hashU(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float hashF(unsigned x) { return (hashU(x) & 0xFFFFFF) / static_cast<float>(0xFFFFFF); }
}  // namespace

void initCity(unsigned seed) {
    buildings.clear();
    for (int L = 0; L < 3; ++L) {
        float x = -40;
        unsigned s = seed * 7919u + L * 104729u;
        while (x < W + 40) {
            Building b;
            b.layerIdx = L;
            b.seed = hashU(s++);
            b.w = 40 + hashF(b.seed) * (L == 2 ? 110 : 80);
            float base = L == 0 ? 210 : L == 1 ? 150 : 90;
            b.h = base + hashF(b.seed ^ 0xabc) * (L == 0 ? 190 : L == 1 ? 170 : 150);
            b.x = x;
            b.sign = (L == 2 && hashF(b.seed ^ 0x51) < 0.35f) ? static_cast<int>(hashU(b.seed) % 7) : -1;
            x += b.w + (L == 2 ? 6 + hashF(b.seed ^ 3) * 30 : 2);
            buildings.push_back(b);
        }
    }
}

void city(float bright, float danger) {
    // Sky.
    Color top = mix(Color{4, 2, 14, 255}, Color{22, 2, 10, 255}, danger);
    Color bottom = mix(Color{34, 6, 58, 255}, Color{70, 6, 24, 255}, danger);
    DrawRectangleGradientV(0, 0, W, static_cast<int>(HORIZON), top, bottom);
    // Haze sun behind the towers.
    BeginBlendMode(BLEND_ADDITIVE);
    Color sun = mix(C_MAGENTA, C_RED, danger);
    DrawCircleGradient(Vector2{640, HORIZON - 60}, 330, fade(sun, 0.16f * bright), fade(sun, 0.0f));
    DrawCircleGradient(Vector2{640, HORIZON - 40}, 150, fade(C_AMBER, 0.10f * bright), fade(C_AMBER, 0.0f));
    EndBlendMode();
    // Searchlights from HELIOS.
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 2; ++i) {
        float a = std::sin(now * 0.23f + i * 2.1f) * 0.6f - 1.5708f;
        Vector2 o{1141.0f, HORIZON - 380};
        Vector2 p1{o.x + std::cos(a - 0.05f) * 900, o.y + std::sin(a - 0.05f) * 900};
        Vector2 p2{o.x + std::cos(a + 0.05f) * 900, o.y + std::sin(a + 0.05f) * 900};
        DrawTriangle(o, p2, p1, fade(mix(C_CYAN, C_RED, danger), 0.035f * bright));
    }
    EndBlendMode();

    // Buildings, back to front.
    for (int L = 0; L < 3; ++L) {
        Color body = L == 0 ? Color{24, 10, 46, 255} : L == 1 ? Color{14, 7, 30, 255} : Color{7, 4, 16, 255};
        float par = std::sin(now * 0.05f) * (L + 1) * 3.0f;
        for (auto& b : buildings) {
            if (b.layerIdx != L) continue;
            float x = b.x + par, y = HORIZON - b.h;
            DrawRectangleRec(Rectangle{x, y, b.w, b.h}, body);
            if (L == 2) DrawRectangleRec(Rectangle{x, y, b.w, 2}, fade(mix(C_MAGENTA, C_RED, danger), 0.35f * bright));
            // Windows.
            float cw = L == 2 ? 7 : 5, ch = L == 2 ? 9 : 6, gx = L == 2 ? 6 : 4, gy = L == 2 ? 8 : 6;
            int cols = static_cast<int>((b.w - 8) / (cw + gx));
            int rows = static_cast<int>((b.h - 16) / (ch + gy));
            unsigned tbucket = static_cast<unsigned>(now * 0.7f);
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c) {
                    unsigned h = hashU(b.seed + r * 131u + c * 7u);
                    float lit = (h & 255) / 255.0f;
                    if (lit > 0.20f) continue;
                    if ((hashU(h + tbucket) & 63) == 0) continue;   // flicker
                    Color wc = (h >> 9) % 5 == 0 ? C_AMBER : (h >> 9) % 5 == 1 ? C_MAGENTA : C_CYAN;
                    float a = (0.2f + 0.5f * lit / 0.20f) * bright * (L == 2 ? 0.7f : L == 1 ? 0.45f : 0.3f);
                    DrawRectangleRec(Rectangle{x + 6 + c * (cw + gx), y + 10 + r * (ch + gy), cw, ch}, fade(wc, a));
                }
            if (b.sign >= 0 && (b.x + b.w < 420 || b.x > 860) && textWidth(F::Display, kSigns[b.sign], 15) < b.w - 8) {
                const char* s = kSigns[b.sign];
                Color sc = (b.seed & 1) ? C_MAGENTA : C_CYAN;
                bool on = (hashU(b.seed + static_cast<unsigned>(now * 6)) & 31) != 0;
                if (on) glowText(F::Display, s, x + b.w / 2, y + 20, 15, fade(sc, bright), Center, 1.2f);
            }
        }
    }
    // The HELIOS tower.
    {
        float x = 1110, w = 62, h = 400, y = HORIZON - h;
        DrawRectangleRec(Rectangle{x, y, w, h}, Color{5, 3, 12, 255});
        DrawTriangle(Vector2{x, y}, Vector2{x + w / 2, y - 50}, Vector2{x + w, y}, Color{5, 3, 12, 255});
        DrawLineEx(Vector2{x + w / 2, y - 50}, Vector2{x + w / 2, y - 100}, 2, Color{40, 20, 60, 255});
        bool beacon = std::fmod(now, 1.6f) < 0.25f;
        if (beacon) glowDot(Vector2{x + w / 2, y - 100}, 4, C_RED);
        const char* name = "HELIOS";
        Color hc = mix(C_AMBER, C_RED, danger);
        for (int i = 0; i < 6; ++i) {
            char cs[2] = {name[i], 0};
            glowText(F::Display, cs, x + w / 2, y + 30 + i * 30, 22, fade(hc, bright), Center, 1.5f);
        }
    }
    // Ground and perspective grid.
    DrawRectangleGradientV(0, static_cast<int>(HORIZON), W, H - static_cast<int>(HORIZON), Color{12, 3, 24, 255}, Color{3, 1, 8, 255});
    Color grid = mix(C_MAGENTA, C_RED, danger);
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRectangle(0, static_cast<int>(HORIZON) - 1, W, 2, fade(grid, 0.6f * bright));
    for (int i = -14; i <= 14; ++i) {
        Vector2 a{640.0f + i * 22.0f, HORIZON};
        Vector2 b{640.0f + i * 150.0f, static_cast<float>(H)};
        DrawLineEx(a, b, 1.0f, fade(grid, 0.22f * bright));
    }
    float scroll = std::fmod(now * 0.35f, 1.0f);
    for (int i = 0; i < 10; ++i) {
        float f = (i + scroll) / 10.0f;
        float y = HORIZON + (H - HORIZON) * f * f;
        DrawLineEx(Vector2{0, y}, Vector2{static_cast<float>(W), y}, 1.0f, fade(grid, 0.08f + 0.25f * f * bright));
    }
    EndBlendMode();
    // Rain.
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 140; ++i) {
        float sp = 500 + hashF(i * 13u) * 400;
        float x0 = hashF(i * 7u + 1) * (W + 200);
        float y = std::fmod(hashF(i * 3u + 2) * H + now * sp, static_cast<float>(H + 60)) - 30;
        float x = std::fmod(x0 - (y + 30) * 0.18f + W + 200, static_cast<float>(W + 200)) - 100;
        DrawLineEx(Vector2{x, y}, Vector2{x - 3, y + 16}, 1.0f, fade(Color{150, 200, 255, 255}, 0.10f + 0.08f * hashF(i)));
    }
    EndBlendMode();
}

void shade(float y, float h) {
    DrawRectangleGradientV(0, static_cast<int>(y - 40), W, 40, Color{2, 1, 8, 0}, Color{2, 1, 8, 170});
    DrawRectangle(0, static_cast<int>(y), W, static_cast<int>(h), Color{2, 1, 8, 170});
    DrawRectangleGradientV(0, static_cast<int>(y + h), W, 40, Color{2, 1, 8, 170}, Color{2, 1, 8, 0});
}

void crt(float glitch) {
    for (int y = 0; y < H; y += 3) DrawRectangle(0, y, W, 1, Color{0, 0, 0, 46});
    float band = std::fmod(now * 70.0f, H + 200.0f) - 100;
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRectangleGradientV(0, static_cast<int>(band), W, 90, fade(C_CYAN, 0.0f), fade(C_CYAN, 0.025f));
    EndBlendMode();
    // Vignette.
    DrawRectangleGradientV(0, 0, W, 110, Color{0, 0, 0, 140}, Color{0, 0, 0, 0});
    DrawRectangleGradientV(0, H - 120, W, 120, Color{0, 0, 0, 0}, Color{0, 0, 0, 160});
    DrawRectangleGradientH(0, 0, 150, H, Color{0, 0, 0, 140}, Color{0, 0, 0, 0});
    DrawRectangleGradientH(W - 150, 0, 150, H, Color{0, 0, 0, 0}, Color{0, 0, 0, 140});
    // Glitch: a random burst every few seconds, or forced by the game.
    float cycle = std::fmod(now, 7.3f);
    float g = std::max(glitch, cycle < 0.12f ? 0.6f : 0.0f);
    if (g > 0.01f) {
        unsigned seed = static_cast<unsigned>(now * 30);
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < static_cast<int>(2 + g * 6); ++i) {
            float y = hashF(seed + i * 17u) * H;
            float h = 2 + hashF(seed + i * 31u) * 14 * g;
            float off = (hashF(seed + i * 5u) - 0.5f) * 40 * g;
            DrawRectangleRec(Rectangle{off, y, static_cast<float>(W), h}, fade(i % 2 ? C_CYAN : C_MAGENTA, 0.10f * g));
        }
        EndBlendMode();
    }
}

void ticker(float y, const std::vector<std::string>& items) {
    DrawRectangle(0, static_cast<int>(y), W, 22, Color{4, 3, 12, 230});
    DrawRectangle(0, static_cast<int>(y), W, 1, fade(C_MAGENTA, 0.5f));
    std::string all;
    for (auto& s : items) all += s + "      ///      ";
    float w = textWidth(F::Mono, all, 13);
    if (w <= 0) return;
    float x = -std::fmod(now * 60.0f, w);
    for (int k = 0; k < 3 && x < W; ++k, x += w) text(F::Mono, all, x, y + 4, 13, fade(C_DIM, 0.9f));
    DrawRectangle(0, static_cast<int>(y), 92, 22, Color{40, 4, 30, 255});
    text(F::MonoBold, "CITYWIRE", 10, y + 4, 13, C_MAGENTA);
}

bool saveScreenshot(const std::string& path) {
    Image im = LoadImageFromScreen();
    if (!im.data) return false;
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
#if defined(GHOSTNET_HEADLESS)
    // The software framebuffer reads back bottom-up and BGR.
    ImageFlipVertical(&im);
    auto* px = static_cast<unsigned char*>(im.data);
    for (int i = 0; i < im.width * im.height; ++i) std::swap(px[i * 4], px[i * 4 + 2]);
#endif
    bool ok = ExportImage(im, path.c_str());
    UnloadImage(im);
    return ok;
}

}  // namespace gx
