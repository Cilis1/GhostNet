// Release manifest: the signed-off list of files that make up the game.
//
//   GHOSTNET-MANIFEST 1
//   repo Cilis1/GhostNet
//   version 0.1.0
//   file <sha256-hex> <size-bytes> <name>
//
// Platform-neutral so it can be unit-tested anywhere.
#pragma once
#include <cctype>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace manifest {

constexpr uint64_t kMaxFileBytes = 64ull * 1024 * 1024;   // no game file is anywhere near this
constexpr size_t kMaxFiles = 32;

struct Entry { std::string sha256, name; uint64_t size = 0; };
struct Manifest { std::string repo, version; std::vector<Entry> files; };

// Only plain file names: no paths, no traversal, no hidden files, no device tricks.
inline bool safeFileName(const std::string& n) {
    if (n.empty() || n.size() > 64 || n[0] == '.' || n.back() == '.' || n.back() == ' ') return false;
    for (char c : n) {
        unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '.' || c == '-' || c == '_')) return false;
    }
    if (n.find("..") != std::string::npos) return false;
    std::string base = n.substr(0, n.find('.'));
    for (auto& ch : base) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                     "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (auto* r : reserved) if (base == r) return false;
    return true;
}

inline bool isHex64(const std::string& h) {
    if (h.size() != 64) return false;
    for (char c : h) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline bool safeVersion(const std::string& v) {
    if (v.empty() || v.size() > 32) return false;
    for (char c : v) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+')) return false;
    return true;
}

inline bool parse(const std::string& text, Manifest& m, std::string& err) {
    if (text.size() > 64 * 1024) { err = "manifest too large"; return false; }
    std::istringstream is(text);
    std::string line;
    bool header = false;
    std::set<std::string> seen;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string k;
        ls >> k;
        if (!header) {
            int v = 0;
            ls >> v;
            if (k != "GHOSTNET-MANIFEST" || v != 1) { err = "not a GhostNet manifest"; return false; }
            header = true;
            continue;
        }
        if (k == "repo") ls >> m.repo;
        else if (k == "version") ls >> m.version;
        else if (k == "file") {
            Entry e;
            std::string extra;
            if (!(ls >> e.sha256 >> e.size >> e.name) || (ls >> extra)) { err = "malformed file line"; return false; }
            if (!isHex64(e.sha256)) { err = "bad hash for " + e.name; return false; }
            if (!safeFileName(e.name)) { err = "unsafe file name in manifest: " + e.name; return false; }
            if (e.size == 0 || e.size > kMaxFileBytes) { err = "bad size for " + e.name; return false; }
            std::string low = e.name;
            for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (!seen.insert(low).second) { err = "duplicate file " + e.name; return false; }
            m.files.push_back(e);
            if (m.files.size() > kMaxFiles) { err = "too many files"; return false; }
        } else {
            err = "unknown manifest field '" + k + "'";
            return false;
        }
    }
    if (!header) { err = "empty manifest"; return false; }
    if (m.repo.empty() || !safeVersion(m.version) || m.files.empty()) { err = "manifest missing repo/version/files"; return false; }
    return true;
}

}  // namespace manifest
