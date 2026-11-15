// GhostNet - every sound is synthesised at start-up: no audio files ship with the game.
#pragma once

namespace sfx {

enum Id {
    Click, Key, Deny, Granted, Lockout, Alarm, Traced, Tick, Coin, Capture, Sacrifice, Boot, Whoosh, Read, Beat, Honeypot, Count
};

void init();             // safe to call when no audio device exists
void shutdown();
void play(Id id, float volume = 1.0f);
void ambience(bool on, float danger);   // the room hum; call every frame
extern bool enabled;

}  // namespace sfx
