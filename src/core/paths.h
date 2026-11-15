// GhostNet - where saves live. %APPDATA%\GhostNet on Windows, ~/.ghostnet elsewhere.
#pragma once
#include <string>

namespace paths {
std::string saveDir();                 // created on demand
void overrideSaveDir(const std::string& dir);   // tests and tools
}
