// GhostNet - deterministic RNG (splitmix64). The world seed decides everything
// that the design doc says must be "deterministic, not random".
#pragma once
#include <cstdint>
#include <string>
#include <vector>

inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
inline uint64_t seedOf(uint64_t a, uint64_t b) { return mix64(a ^ mix64(b + 0x51ED27)); }

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0xC0FFEE) {}
    uint64_t next() { s += 0x9E3779B97F4A7C15ULL; return mix64(s); }
    int range(int lo, int hi) {  // inclusive
        if (hi <= lo) return lo;
        return lo + static_cast<int>(next() % static_cast<uint64_t>(hi - lo + 1));
    }
    double real() { return static_cast<double>(next() >> 11) / static_cast<double>(1ULL << 53); }
    bool chance(double p) { return real() < p; }
    template <class T> const T& pick(const std::vector<T>& v) { return v[static_cast<size_t>(range(0, static_cast<int>(v.size()) - 1))]; }
    template <class T> void shuffle(std::vector<T>& v) {
        for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[static_cast<size_t>(next() % i)]);
    }
};
