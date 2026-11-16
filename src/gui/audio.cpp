#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "raylib.h"

namespace sfx {

bool enabled = true;

namespace {
constexpr int RATE = 44100;
constexpr float TAU = 6.2831853f;
bool ready = false;
Sound sounds[Count];
Sound hum;
uint32_t noiseState = 0x1234567u;

float noise() {
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (noiseState & 0xFFFF) / 32768.0f - 1.0f;
}

Sound make(float seconds, const std::function<float(float t, float u)>& fn, float gain = 0.5f) {
    int n = static_cast<int>(seconds * RATE);
    std::vector<short> buf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / RATE;
        float v = fn(t, t / seconds) * gain;
        // tiny fade in/out against clicks
        float edge = std::min(1.0f, std::min(i / 60.0f, (n - i) / 200.0f));
        buf[static_cast<size_t>(i)] = static_cast<short>(std::clamp(v * edge, -1.0f, 1.0f) * 32000);
    }
    Wave w;
    w.frameCount = static_cast<unsigned int>(n);
    w.sampleRate = RATE;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = buf.data();
    return LoadSoundFromWave(w);
}

float sq(float ph) { return std::fmod(ph, 1.0f) < 0.5f ? 1.0f : -1.0f; }
float tri(float ph) { float f = std::fmod(ph, 1.0f); return 4.0f * std::fabs(f - 0.5f) - 1.0f; }
float env(float u, float attack, float k) { return (u < attack ? u / attack : 1.0f) * std::exp(-k * u); }
}  // namespace

void init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    ready = true;
    sounds[Click] = make(0.05f, [](float t, float u) { return sq(t * 1500) * 0.4f * env(u, 0.02f, 6); }, 0.35f);
    sounds[Key] = make(0.03f, [](float t, float u) { return (noise() * 0.5f + sq(t * 2600) * 0.3f) * env(u, 0.01f, 7); }, 0.25f);
    sounds[Deny] = make(0.24f, [](float t, float u) { float f = u < 0.5f ? 240.0f : 170.0f; return sq(t * f) * 0.5f * env(u, 0.02f, 2.5f); }, 0.35f);
    sounds[Granted] = make(0.45f, [](float t, float u) {
        float f = u < 0.33f ? 660.0f : u < 0.66f ? 880.0f : 1320.0f;
        return (std::sin(TAU * f * t) * 0.6f + tri(t * f * 2) * 0.2f) * env(u, 0.02f, 1.6f);
    });
    sounds[Lockout] = make(0.6f, [](float t, float u) {
        float trem = 0.6f + 0.4f * sq(t * 14);
        return (tri(t * 110) * 0.6f + sq(t * 55) * 0.3f) * trem * env(u, 0.02f, 1.8f);
    }, 0.5f);
    sounds[Alarm] = make(0.7f, [](float t, float u) {
        float f = 620 + 280 * std::sin(TAU * 2.8f * t);
        return sq(t * f) * 0.35f * (1.0f - u * 0.5f);
    }, 0.3f);
    sounds[Traced] = make(1.5f, [](float, float u) {
        float f = 900 * std::exp(-3.0f * u) + 40;
        static float ph = 0;
        ph += f / RATE;
        return (sq(ph) * 0.4f + noise() * 0.35f * u) * (1.0f - u);
    }, 0.45f);
    sounds[Tick] = make(0.09f, [](float t, float u) { return std::sin(TAU * (u < 0.5f ? 1800.0f : 2400.0f) * t) * env(u, 0.02f, 4); }, 0.3f);
    sounds[Coin] = make(0.35f, [](float t, float u) {
        float f = u < 0.25f ? 988.0f : 1319.0f;
        return (sq(t * f) * 0.3f + std::sin(TAU * f * t) * 0.4f) * env(u, 0.01f, 2.5f);
    }, 0.35f);
    sounds[Capture] = make(1.1f, [](float t, float u) {
        float f = 160 + 900 * u;
        static float ph = 0;
        ph += f / RATE;
        float chord = u > 0.6f ? (std::sin(TAU * 440 * t) + std::sin(TAU * 554 * t) + std::sin(TAU * 659 * t)) * 0.2f : 0.0f;
        return (tri(ph) * 0.4f + chord) * env(u, 0.05f, 1.2f);
    }, 0.45f);
    sounds[Sacrifice] = make(1.4f, [](float t, float u) {
        static float lp = 0;
        lp += (noise() - lp) * (0.25f * (1.0f - u) + 0.02f);
        return (lp * 1.6f + std::sin(TAU * 48 * t) * 0.5f) * std::exp(-2.5f * u);
    }, 0.6f);
    sounds[Boot] = make(1.3f, [](float t, float u) {
        float f = 110 * (1.0f + u);
        return (std::sin(TAU * f * t) * 0.4f + std::sin(TAU * f * 1.5f * t) * 0.25f + std::sin(TAU * f * 2 * t) * 0.15f) *
               env(u, 0.3f, 1.0f);
    }, 0.45f);
    sounds[Whoosh] = make(0.35f, [](float, float u) {
        static float lp = 0;
        lp += (noise() - lp) * (0.05f + 0.3f * u);
        return lp * 1.4f * std::sin(3.14159f * u);
    }, 0.4f);
    sounds[Read] = make(0.18f, [](float t, float u) { return std::sin(TAU * 1200 * t) * (std::fmod(u * 3, 1.0f) < 0.35f ? 1.0f : 0.0f) * 0.4f; }, 0.3f);
    sounds[Beat] = make(0.12f, [](float t, float u) { return std::sin(TAU * (70 - 30 * u) * t) * std::exp(-5 * u); }, 0.7f);
    sounds[Honeypot] = make(0.8f, [](float t, float u) { return (sq(t * (300 + 200 * sq(t * 8))) * 0.4f + noise() * 0.1f) * (1 - u); }, 0.35f);
    // The room: a 4-second seamless hum (whole cycles of every partial).
    hum = make(4.0f, [](float t, float) {
        float lfo = 0.6f + 0.4f * std::sin(TAU * 0.25f * t);
        return (std::sin(TAU * 55 * t) * 0.5f + std::sin(TAU * 82.5f * t) * 0.25f * lfo + std::sin(TAU * 110 * t) * 0.1f) * 0.6f;
    }, 0.25f);
}

void shutdown() {
    if (!ready) return;
    for (auto& s : sounds) UnloadSound(s);
    UnloadSound(hum);
    CloseAudioDevice();
    ready = false;
}

void play(Id id, float volume) {
    if (!ready || !enabled || id < 0 || id >= Count) return;
    SetSoundVolume(sounds[id], volume);
    PlaySound(sounds[id]);
}

void ambience(bool on, float danger) {
    if (!ready) return;
    if (!on || !enabled) {
        if (IsSoundPlaying(hum)) StopSound(hum);
        return;
    }
    SetSoundPitch(hum, 1.0f + danger * 0.25f);
    SetSoundVolume(hum, 0.55f + danger * 0.4f);
    if (!IsSoundPlaying(hum)) PlaySound(hum);
}

}  // namespace sfx
