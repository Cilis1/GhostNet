#include "paths.h"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace paths {
namespace {
std::string g_override;
}
void overrideSaveDir(const std::string& dir) { g_override = dir; }

std::string saveDir() {
    fs::path p;
    if (!g_override.empty()) {
        p = g_override;
    } else {
#ifdef _WIN32
        const char* app = std::getenv("APPDATA");
        p = app ? fs::path(app) / "GhostNet" : fs::path(".") / "GhostNetSave";
#else
        const char* home = std::getenv("HOME");
        p = home ? fs::path(home) / ".ghostnet" : fs::path(".") / ".ghostnet";
#endif
    }
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}
}  // namespace paths
