// GhostNet - derived numbers shared by runs, reports and the advisor panel.
#pragma once
#include <algorithm>
#include <cmath>

#include "balance.h"
#include "common.h"
#include "world.h"

inline double effectiveTraceRate(const GameState& g, const Target& t) {
    double r = baseTraceRate(t);
    r *= 1.0 - g.net.proxyBonus() / 100.0;
    r *= 1.0 - bal::SHROUD_TRACE * g.deck.shroud;
    return r;
}

inline double downloadSpeed(const GameState& g) {
    return bal::BASE_SPEED_MBPS * (1.0 + bal::UPLINK_STEP * g.deck.uplink + g.net.relayBonus() / 100.0);
}

inline int secondsToDownload(const GameState& g, int sizeMB) {
    return std::max(1, static_cast<int>(std::ceil(sizeMB / downloadSpeed(g))));
}

inline int baseFine(const GameState& g, const Target& t, bool honeypot, bool anchoring) {
    double f = t.security * bal::FINE_PER_SECURITY;
    f *= 1.0 - bal::SHROUD_FINE * g.deck.shroud;
    double risk = 1.0 + (honeypot ? 0.25 : 0.0) + (anchoring ? 0.25 : 0.0);
    return static_cast<int>(std::round(f * risk));
}

inline int anchorSeconds(const Target& t, bool viaPhrase) {
    int s = 8 + t.security * 5;
    if (viaPhrase) s = static_cast<int>(std::ceil(s * 0.6));
    return s;
}

// Starting condition / noise of the hardware if stolen right now.
inline void captureQuote(const Target& t, bool honeypotTriggered, int& condition, int& noise) {
    HardwareSpec hw = hardwareFor(t.kind);
    int taken = 0;
    for (auto& f : t.files)
        if (f.taken && (f.kind == FileKind::Paydata || f.kind == FileKind::Objective)) ++taken;
    condition = std::max(20, hw.condition - 8 * taken);
    noise = hw.noise;
    if (honeypotTriggered) {
        condition = std::max(15, condition - 15);
        noise = std::min(bal::MAX_NOISE, noise + 1);
    }
}
