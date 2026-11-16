// GhostNet-Setup.exe - downloads GhostNet from the official GitHub releases and installs it safely.
//
// What "safely" means here (see SECURITY.md):
//   * The source is pinned at compile time: github.com/<GHOSTNET_REPO>. No URL can be passed in.
//   * HTTPS only, TLS 1.2+ enforced, Windows validates the certificate chain (never disabled).
//   * Redirects are followed by hand, and every hop must be HTTPS on an allowlisted GitHub host.
//   * The release must have been published by GitHub Actions (asset uploader check).
//   * A manifest lists every file with its SHA-256 and size. Each download is size-capped,
//     hashed while streaming, and compared with the manifest AND with GitHub's own asset digest.
//   * Optional ECDSA P-256 signature over the manifest (required if a public key is compiled in).
//   * Files are staged and only moved into place after *everything* verifies. Nothing runs as admin.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <knownfolders.h>
#include <objbase.h>
#include <objidl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "json.h"
#include "manifest.h"
#include "signing_key.h"

#ifndef GHOSTNET_REPO
#define GHOSTNET_REPO "Cilis1/GhostNet"
#endif
#ifndef GHOSTNET_VERSION
#define GHOSTNET_VERSION "0.1.0"
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace fs = std::filesystem;

namespace {

const char* kRepo = GHOSTNET_REPO;
const char* kManifestName = "ghostnet-manifest.txt";
const char* kSigName = "ghostnet-manifest.sig";
const char* kGameExe = "GhostNet.exe";
const char* kTrustedUploader = "github-actions[bot]";
const char* kAllowedHosts[] = {"api.github.com", "github.com", "objects.githubusercontent.com",
                               "release-assets.githubusercontent.com", "github-releases.githubusercontent.com"};

bool g_color = true;
bool g_yes = false;

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------
const char* C(const char* code) { return g_color ? code : ""; }
#define RESET C("\x1b[0m")
#define GREEN C("\x1b[92m")
#define RED C("\x1b[91m")
#define YELLOW C("\x1b[93m")
#define CYAN C("\x1b[96m")
#define DIM C("\x1b[2m")
#define BOLD C("\x1b[1m")

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string hexOf(const std::vector<uint8_t>& b) {
    static const char* h = "0123456789abcdef";
    std::string o;
    for (uint8_t x : b) { o += h[x >> 4]; o += h[x & 15]; }
    return o;
}
bool unhex(const std::string& s, std::vector<uint8_t>& out) {
    if (s.size() % 2) return false;
    out.clear();
    for (size_t i = 0; i < s.size(); i += 2) {
        auto v = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int a = v(s[i]), b = v(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back(static_cast<uint8_t>(a * 16 + b));
    }
    return true;
}
std::string winErr(DWORD e) {
    if (e == ERROR_WINHTTP_SECURE_FAILURE) return "TLS/certificate validation failed (connection refused for your safety)";
    if (e == ERROR_WINHTTP_TIMEOUT) return "connection timed out";
    if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED) return "could not resolve host (are you online?)";
    if (e == ERROR_WINHTTP_CANNOT_CONNECT) return "could not connect";
    return "Windows error " + std::to_string(e);
}
void step(const std::string& s) { std::cout << "\n" << CYAN << "> " << RESET << BOLD << s << RESET << "\n"; }
void ok(const std::string& s) { std::cout << "  " << GREEN << "[ OK ] " << RESET << s << "\n"; }
void info(const std::string& s) { std::cout << "  " << DIM << s << RESET << "\n"; }
[[noreturn]] void die(const std::string& s) {
    std::cout << "\n  " << RED << "[FAIL] " << RESET << s << "\n  Nothing was installed or changed.\n";
    if (!g_yes) {
        std::cout << "\n  Press Enter to close.";
        std::string x;
        std::getline(std::cin, x);
    }
    std::exit(1);
}
bool ask(const std::string& q, bool def) {
    if (g_yes) return def;
    std::cout << "  " << q << (def ? " [Y/n] " : " [y/N] ") << std::flush;
    std::string a;
    if (!std::getline(std::cin, a)) return def;
    a = lower(a);
    if (a.empty()) return def;
    return a[0] == 'y';
}

// ---------------------------------------------------------------------------
// Hashing & signatures (Windows CNG)
// ---------------------------------------------------------------------------
class Sha256 {
public:
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS ||
            BCryptCreateHash(alg_, &h_, nullptr, 0, nullptr, 0, 0) != STATUS_SUCCESS)
            die("SHA-256 is unavailable on this system");
    }
    ~Sha256() {
        if (h_) BCryptDestroyHash(h_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void update(const uint8_t* p, size_t n) {
        while (n) {
            ULONG chunk = static_cast<ULONG>(std::min<size_t>(n, 1u << 20));
            BCryptHashData(h_, const_cast<PUCHAR>(p), chunk, 0);
            p += chunk;
            n -= chunk;
        }
    }
    std::vector<uint8_t> digest() {
        std::vector<uint8_t> d(32);
        BCryptFinishHash(h_, d.data(), 32, 0);
        return d;
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE h_ = nullptr;
};

std::string sha256Hex(const std::string& data) {
    Sha256 s;
    s.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return hexOf(s.digest());
}

bool sha256File(const fs::path& p, std::string& hex, uint64_t& size) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    Sha256 s;
    std::vector<char> buf(1 << 16);
    size = 0;
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize n = f.gcount();
        if (n > 0) { s.update(reinterpret_cast<uint8_t*>(buf.data()), static_cast<size_t>(n)); size += static_cast<uint64_t>(n); }
    }
    hex = hexOf(s.digest());
    return true;
}

// Verify an ECDSA P-256 signature (raw r||s, 64 bytes) over SHA-256(data).
bool verifySignature(const std::string& data, const std::vector<uint8_t>& sig, const std::vector<uint8_t>& pubXY) {
    if (sig.size() != 64 || pubXY.size() != 64) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    bool good = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == STATUS_SUCCESS) {
        std::vector<uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
        auto* hdr = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
        hdr->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
        hdr->cbKey = 32;
        std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), pubXY.data(), 64);
        if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), static_cast<ULONG>(blob.size()), 0) ==
            STATUS_SUCCESS) {
            Sha256 h;
            h.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
            std::vector<uint8_t> d = h.digest();
            good = BCryptVerifySignature(key, nullptr, d.data(), 32, const_cast<PUCHAR>(sig.data()), 64, 0) == STATUS_SUCCESS;
            BCryptDestroyKey(key);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return good;
}

// ---------------------------------------------------------------------------
// HTTPS (WinHTTP) with a host allowlist and manual redirect handling
// ---------------------------------------------------------------------------
struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return h; }
};

struct ParsedUrl { std::wstring host, path; INTERNET_PORT port = 0; };

bool hostAllowed(const std::string& host) {
    std::string h = lower(host);
    for (auto* a : kAllowedHosts) if (h == a) return true;
    return false;
}

bool parseUrl(const std::string& url, ParsedUrl& out, std::string& err) {
    if (url.size() > 8192) { err = "URL too long"; return false; }
    std::wstring w = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    std::vector<wchar_t> host(w.size() + 1), path(w.size() + 1), extra(w.size() + 1);
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = static_cast<DWORD>(host.size());
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = static_cast<DWORD>(path.size());
    uc.lpszExtraInfo = extra.data();
    uc.dwExtraInfoLength = static_cast<DWORD>(extra.size());
    if (!WinHttpCrackUrl(w.c_str(), static_cast<DWORD>(w.size()), 0, &uc)) { err = "malformed URL"; return false; }
    if (uc.nScheme != INTERNET_SCHEME_HTTPS || uc.nPort != INTERNET_DEFAULT_HTTPS_PORT) { err = "refusing non-HTTPS URL: " + url.substr(0, 80); return false; }
    out.host.assign(host.data(), uc.dwHostNameLength);
    out.path.assign(path.data(), uc.dwUrlPathLength);
    out.path.append(extra.data(), uc.dwExtraInfoLength);
    out.port = uc.nPort;
    if (out.path.empty()) out.path = L"/";
    if (!hostAllowed(narrow(out.host))) { err = "refusing to talk to non-GitHub host '" + narrow(out.host) + "'"; return false; }
    return true;
}

class Http {
public:
    Http() {
        std::wstring agent = widen(std::string("GhostNet-Setup/") + GHOSTNET_VERSION + " (+https://github.com/" + kRepo + ")");
        ses_.h = WinHttpOpen(agent.c_str(), 4 /*WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY*/, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses_.h) ses_.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses_.h) die("could not start WinHTTP: " + winErr(GetLastError()));
        DWORD prot = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(ses_, WINHTTP_OPTION_SECURE_PROTOCOLS, &prot, sizeof prot)) {
            prot = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // older Windows without TLS 1.3 support
            if (!WinHttpSetOption(ses_, WINHTTP_OPTION_SECURE_PROTOCOLS, &prot, sizeof prot)) die("TLS 1.2 is not available on this system");
        }
        WinHttpSetTimeouts(ses_, 15000, 15000, 30000, 60000);
    }

    using Sink = std::function<bool(const uint8_t*, size_t)>;
    using Progress = std::function<void(uint64_t done, uint64_t total)>;

    // GET with redirects handled manually so each hop is re-validated.
    bool get(std::string url, const wchar_t* accept, uint64_t maxBytes, const Sink& sink, const Progress& progress, std::string& err) {
        for (int hop = 0; hop < 6; ++hop) {
            ParsedUrl u;
            if (!parseUrl(url, u, err)) return false;
            Handle con(WinHttpConnect(ses_, u.host.c_str(), u.port, 0));
            if (!con.h) { err = winErr(GetLastError()); return false; }
            Handle req(WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
            if (!req.h) { err = winErr(GetLastError()); return false; }
            DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
            WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof noRedirect);
            std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
            if (!WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(req, nullptr)) {
                err = winErr(GetLastError());
                return false;
            }
            DWORD status = 0, sz = sizeof status;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                std::string loc = header(req, WINHTTP_QUERY_LOCATION);
                if (loc.empty()) { err = "redirect without a location"; return false; }
                if (loc.rfind("https://", 0) != 0) { err = "refusing redirect to non-HTTPS location"; return false; }
                url = loc;
                continue;
            }
            if (status != 200) {
                if (status == 403 || status == 429) err = "GitHub refused the request (HTTP " + std::to_string(status) + " - rate limited? try again in a few minutes)";
                else if (status == 404) err = "not found (HTTP 404) - has a release been published yet?";
                else err = "HTTP " + std::to_string(status);
                return false;
            }
            uint64_t total = 0;
            std::string cl = header(req, WINHTTP_QUERY_CONTENT_LENGTH);
            if (!cl.empty()) {
                total = std::strtoull(cl.c_str(), nullptr, 10);
                if (total > maxBytes) { err = "server announced " + std::to_string(total) + " bytes, more than allowed"; return false; }
            }
            uint64_t done = 0;
            std::vector<uint8_t> buf(1 << 16);
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail)) { err = winErr(GetLastError()); return false; }
                if (avail == 0) break;
                DWORD want = std::min<DWORD>(avail, static_cast<DWORD>(buf.size())), got = 0;
                if (!WinHttpReadData(req, buf.data(), want, &got)) { err = winErr(GetLastError()); return false; }
                if (got == 0) break;
                done += got;
                if (done > maxBytes) { err = "download exceeded its size limit"; return false; }
                if (!sink(buf.data(), got)) { err = "could not write data to disk"; return false; }
                if (progress) progress(done, total);
            }
            if (total && done != total) { err = "connection closed early"; return false; }
            return true;
        }
        err = "too many redirects";
        return false;
    }

    bool getString(const std::string& url, const wchar_t* accept, uint64_t maxBytes, std::string& out, std::string& err) {
        out.clear();
        return get(url, accept, maxBytes, [&](const uint8_t* p, size_t n) { out.append(reinterpret_cast<const char*>(p), n); return true; },
                   nullptr, err);
    }

private:
    Handle ses_;
    static std::string header(HINTERNET req, DWORD which) {
        DWORD len = 0;
        WinHttpQueryHeaders(req, which, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &len, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0 || len > 16384) return "";
        std::wstring w(len / sizeof(wchar_t) + 1, L'\0');
        if (!WinHttpQueryHeaders(req, which, WINHTTP_HEADER_NAME_BY_INDEX, &w[0], &len, WINHTTP_NO_HEADER_INDEX)) return "";
        w.resize(len / sizeof(wchar_t));
        return narrow(w);
    }
};

// ---------------------------------------------------------------------------
// Install locations
// ---------------------------------------------------------------------------
fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}
fs::path defaultInstallDir() {
    fs::path base = knownFolder(FOLDERID_LocalAppData);
    if (base.empty()) die("could not find your AppData folder");
    return base / "Programs" / "GhostNet";
}
fs::path startMenuLink() { return knownFolder(FOLDERID_Programs) / "GhostNet.lnk"; }
fs::path desktopLink() { return knownFolder(FOLDERID_Desktop) / "GhostNet.lnk"; }

bool makeShortcut(const fs::path& link, const fs::path& target, const fs::path& workdir) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(workdir.c_str());
    sl->SetDescription(L"GHOSTNET - own the network, pay the bill, lose what you name");
    IPersistFile* pf = nullptr;
    bool good = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))) {
        good = SUCCEEDED(pf->Save(link.c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    return good;
}

struct Installed { std::string version; std::vector<std::string> files; };
bool readInstalled(const fs::path& dir, Installed& out) {
    std::ifstream f(dir / "installed.txt");
    if (!f) return false;
    std::string k, v;
    while (f >> k >> v) {
        if (k == "version" && manifest::safeVersion(v)) out.version = v;
        else if (k == "file" && manifest::safeFileName(v)) out.files.push_back(v);
    }
    return !out.version.empty();
}

void printBanner() {
    std::cout << GREEN << BOLD << "\n   GHOSTNET SETUP" << RESET << DIM << "   v" << GHOSTNET_VERSION << "\n"
              << "   Official source: https://github.com/" << kRepo << "/releases" << RESET << "\n";
}

void uninstall(const fs::path& dir) {
    step("Uninstalling GhostNet from " + narrow(dir.wstring()));
    Installed inst;
    if (!readInstalled(dir, inst)) die("no GhostNet installation found there (installed.txt missing)");
    if (!ask("Remove GhostNet " + inst.version + "? Your saves in %APPDATA%\\GhostNet are kept.", true)) die("cancelled");
    std::error_code ec;
    for (auto& f : inst.files) fs::remove(dir / f, ec);
    fs::remove(dir / "installed.txt", ec);
    fs::remove_all(dir / ".staging", ec);
    fs::remove(dir, ec);   // only succeeds if empty
    for (auto& l : {startMenuLink(), desktopLink()}) {
        if (!l.empty() && fs::exists(l, ec)) fs::remove(l, ec);
    }
    ok("GhostNet removed.");
}

// ---------------------------------------------------------------------------
// Self-test (run in CI): exercises hashing, parsing and validation offline.
// ---------------------------------------------------------------------------
int selfTest() {
    int fails = 0;
    auto check = [&](bool c, const char* what) {
        std::cout << (c ? "  pass  " : "  FAIL  ") << what << "\n";
        if (!c) ++fails;
    };
    check(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known answer");
    check(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty input");
    json::Value v;
    std::string err;
    check(json::parse(R"({"tag_name":"v1.0","draft":false,"assets":[{"name":"a\u00e9.exe","size":12}]})", v, err) &&
              v.getString("tag_name") == "v1.0" && v.get("assets")->arr.size() == 1, "JSON parse");
    check(!json::parse(R"({"a":1,})", v, err), "JSON rejects trailing comma");
    check(!json::parse(std::string(100, '['), v, err), "JSON rejects deep nesting");
    check(manifest::safeFileName("GhostNet.exe") && !manifest::safeFileName("..\\evil.exe") && !manifest::safeFileName("a/b") &&
              !manifest::safeFileName("CON.txt") && !manifest::safeFileName(".hidden") && !manifest::safeFileName("x.exe."),
          "file name validation");
    manifest::Manifest m;
    std::string good = "GHOSTNET-MANIFEST 1\nrepo Cilis1/GhostNet\nversion 1.2.3\nfile " + std::string(64, 'a') + " 10 GhostNet.exe\n";
    check(manifest::parse(good, m, err) && m.files.size() == 1, "manifest parse");
    manifest::Manifest m2;
    check(!manifest::parse("GHOSTNET-MANIFEST 1\nrepo x/y\nversion 1\nfile " + std::string(64, 'a') + " 10 ../x.exe\n", m2, err),
          "manifest rejects traversal");
    ParsedUrl pu;
    check(parseUrl("https://github.com/" + std::string(kRepo) + "/releases", pu, err), "URL allowlist accepts github.com");
    check(!parseUrl("http://github.com/x", pu, err), "URL rejects plain HTTP");
    check(!parseUrl("https://evil.example.com/x", pu, err), "URL rejects foreign host");
    check(!parseUrl("https://github.com:8443/x", pu, err), "URL rejects odd port");
    check(!parseUrl("https://github.com.evil.io/x", pu, err), "URL rejects look-alike host");
    std::vector<uint8_t> zero(64, 0);
    check(!verifySignature("data", zero, zero), "signature verify rejects garbage");
    std::cout << (fails ? "SELFTEST FAILED\n" : "SELFTEST OK\n");
    return fails ? 1 : 0;
}

struct Asset { std::string name, url, digest, uploader, state; uint64_t size = 0; };

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (!GetConsoleMode(h, &mode) || !SetConsoleMode(h, mode | 0x0004)) g_color = false;
    }
    SetConsoleTitleW(L"GhostNet Setup");

    fs::path dir;
    bool doUninstall = false, noShortcut = false, noLaunch = false, checkOnly = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--yes" || a == L"-y") g_yes = true;
        else if (a == L"--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == L"--uninstall") doUninstall = true;
        else if (a == L"--no-shortcut") noShortcut = true;
        else if (a == L"--no-launch") noLaunch = true;
        else if (a == L"--check") checkOnly = true;
        else if (a == L"--no-color") g_color = false;
        else if (a == L"--selftest") return selfTest();
        else if (a == L"--version") { std::cout << "GhostNet-Setup " << GHOSTNET_VERSION << " (" << kRepo << ")\n"; return 0; }
        else {
            std::cout << "GhostNet-Setup " << GHOSTNET_VERSION << "\n"
                      << "  (no options)   interactive install / update\n"
                      << "  --dir <path>   install somewhere other than %LOCALAPPDATA%\\Programs\\GhostNet\n"
                      << "  --yes          don't ask questions (accept defaults)\n"
                      << "  --check        download and verify only; install nothing\n"
                      << "  --no-shortcut  don't create a Start Menu shortcut\n"
                      << "  --no-launch    don't offer to start the game afterwards\n"
                      << "  --uninstall    remove GhostNet (keeps your saves)\n"
                      << "  --selftest     run the built-in security self-test\n";
            return a == L"--help" || a == L"-h" || a == L"/?" ? 0 : 2;
        }
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    printBanner();
    if (dir.empty()) dir = defaultInstallDir();
    if (!dir.is_absolute()) die("--dir must be an absolute path");
    if (dir == dir.root_path()) die("refusing to install into the root of a drive");
    if (doUninstall) { uninstall(dir); return 0; }

    // 1. Ask GitHub for the latest release.
    step("Contacting GitHub over HTTPS (TLS 1.2+, certificate checked by Windows)");
    Http http;
    std::string body, err;
    std::string api = std::string("https://api.github.com/repos/") + kRepo + "/releases/latest";
    if (!http.getString(api, L"application/vnd.github+json", 4u << 20, body, err)) die("could not read release info: " + err);
    json::Value rel;
    if (!json::parse(body, rel, err) || rel.type != json::Value::Type::Object) die("GitHub sent something that isn't valid JSON: " + err);
    std::string tag = rel.getString("tag_name");
    if (rel.getBool("draft") || rel.getBool("prerelease")) die("latest release is a draft/prerelease; refusing");
    if (tag.empty()) die("release has no tag");
    ok("Latest release: " + tag + "  (" + rel.getString("published_at", "?") + ")");

    std::map<std::string, Asset> assets;
    if (const json::Value* list = rel.get("assets"); list && list->type == json::Value::Type::Array) {
        for (auto& a : list->arr) {
            Asset x;
            x.name = a.getString("name");
            x.url = a.getString("browser_download_url");
            x.digest = lower(a.getString("digest"));
            x.state = a.getString("state", "uploaded");
            x.size = static_cast<uint64_t>(std::max(0.0, a.getNumber("size", 0)));
            if (const json::Value* up = a.get("uploader")) x.uploader = up->getString("login");
            if (!x.name.empty()) assets[x.name] = x;
        }
    }
    const std::string urlPrefix = std::string("https://github.com/") + kRepo + "/releases/download/";
    auto checkAsset = [&](const Asset& a) {
        if (a.state != "uploaded") die("asset " + a.name + " is not fully uploaded");
        if (a.uploader != kTrustedUploader)
            die("asset " + a.name + " was uploaded by '" + a.uploader + "', not by the official build pipeline (" + kTrustedUploader + ")");
        if (a.url.rfind(urlPrefix, 0) != 0) die("asset " + a.name + " points outside the official repository");
    };

    // 2. Manifest (+ optional signature).
    step("Verifying the release manifest");
    auto mit = assets.find(kManifestName);
    if (mit == assets.end()) die(std::string("release is missing ") + kManifestName);
    checkAsset(mit->second);
    std::string mtext;
    if (!http.getString(mit->second.url, L"application/octet-stream", 64 * 1024, mtext, err)) die("could not download manifest: " + err);
    if (!mit->second.digest.empty() && mit->second.digest != "sha256:" + sha256Hex(mtext)) die("manifest does not match GitHub's recorded digest");
    manifest::Manifest man;
    if (!manifest::parse(mtext, man, err)) die("manifest rejected: " + err);
    if (lower(man.repo) != lower(kRepo)) die("manifest belongs to a different repository (" + man.repo + ")");
    if (tag != "v" + man.version) die("manifest version " + man.version + " does not match release tag " + tag);
    ok("Manifest for " + man.repo + " " + man.version + ": " + std::to_string(man.files.size()) + " file(s)");

    std::string pubHex = GHOSTNET_SIGNING_PUBKEY_HEX;
    if (!pubHex.empty()) {
        std::vector<uint8_t> pub, sig;
        if (!unhex(pubHex, pub) || pub.size() != 64) die("installer was built with a malformed signing key");
        auto sit = assets.find(kSigName);
        if (sit == assets.end()) die("release is not signed, and this installer requires a signature");
        checkAsset(sit->second);
        std::string stext;
        if (!http.getString(sit->second.url, L"application/octet-stream", 1024, stext, err)) die("could not download signature: " + err);
        while (!stext.empty() && (stext.back() == '\n' || stext.back() == '\r' || stext.back() == ' ')) stext.pop_back();
        if (!unhex(stext, sig) || !verifySignature(mtext, sig, pub)) die("manifest signature is INVALID - this release may have been tampered with");
        ok("Manifest signature valid (ECDSA P-256)");
    } else {
        info("No signing key compiled in: trusting HTTPS + pinned repo + build-pipeline uploader + SHA-256.");
    }

    bool hasGame = false;
    for (auto& e : man.files) {
        if (e.name == kGameExe) hasGame = true;
        auto it = assets.find(e.name);
        if (it == assets.end()) die("manifest lists " + e.name + " but the release does not contain it");
        checkAsset(it->second);
        if (it->second.size != e.size) die("size of " + e.name + " on GitHub does not match the manifest");
        if (!it->second.digest.empty() && it->second.digest != "sha256:" + e.sha256) die("GitHub's digest for " + e.name + " does not match the manifest");
    }
    if (!hasGame) die(std::string("manifest does not include ") + kGameExe);

    // 3. Existing install?
    Installed prev;
    bool had = readInstalled(dir, prev);
    if (!checkOnly) {
        std::cout << "\n  Install folder: " << BOLD << narrow(dir.wstring()) << RESET << "\n";
        if (had && prev.version == man.version) {
            if (!ask("GhostNet " + prev.version + " is already installed. Verify and repair it?", true)) return 0;
        } else if (had) {
            if (!ask("Update GhostNet " + prev.version + " -> " + man.version + "?", true)) return 0;
        } else if (!ask("Install GhostNet " + man.version + " here?", true)) {
            return 0;
        }
    }

    // 4. Download every file into a staging folder, hashing as we go.
    step("Downloading and verifying " + std::to_string(man.files.size()) + " file(s)");
    std::error_code ec;
    fs::path staging = checkOnly ? fs::temp_directory_path(ec) / ("ghostnet-check-" + std::to_string(GetCurrentProcessId())) : dir / ".staging";
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    if (ec) die("cannot create " + narrow(staging.wstring()) + ": " + ec.message());
    for (auto& e : man.files) {
        fs::path part = staging / (e.name + ".part");
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) die("cannot write " + narrow(part.wstring()));
        Sha256 hasher;
        int lastPct = -1;
        bool okDl = http.get(assets[e.name].url, L"application/octet-stream", e.size,
            [&](const uint8_t* p, size_t n) {
                hasher.update(p, n);
                out.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n));
                return static_cast<bool>(out);
            },
            [&](uint64_t done, uint64_t) {
                int pct = static_cast<int>(done * 100 / e.size);
                if (pct != lastPct) {
                    lastPct = pct;
                    int bars = pct / 4;
                    std::cout << "\r  " << e.name << " [" << std::string(static_cast<size_t>(bars), '#') << std::string(static_cast<size_t>(25 - bars), '-')
                              << "] " << pct << "%   " << std::flush;
                }
            },
            err);
        out.close();
        std::cout << "\r" << std::string(70, ' ') << "\r";
        if (!okDl) { fs::remove_all(staging, ec); die("download of " + e.name + " failed: " + err); }
        std::string got = hexOf(hasher.digest());
        uint64_t size = fs::file_size(part, ec);
        if (size != e.size || got != e.sha256) {
            fs::remove_all(staging, ec);
            die("VERIFICATION FAILED for " + e.name + "\n         expected " + e.sha256 + "\n         got      " + got +
                "\n  The file was deleted. Do not run anything from this download.");
        }
        ok(e.name + "  " + std::to_string(e.size) + " bytes  sha256 " + got.substr(0, 16) + "...");
    }

    if (checkOnly) {
        fs::remove_all(staging, ec);
        ok("Every file verified. (--check: nothing installed)");
        return 0;
    }

    // 5. Move into place only after everything verified.
    step("Installing");
    for (auto& e : man.files) {
        fs::path from = staging / (e.name + ".part"), to = dir / e.name;
        if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DWORD le = GetLastError();
            fs::remove_all(staging, ec);
            die("could not place " + e.name + (le == ERROR_SHARING_VIOLATION || le == ERROR_ACCESS_DENIED ? " - is GhostNet still running? Close it and retry." : ": " + winErr(le)));
        }
        std::string hex;
        uint64_t size = 0;
        if (!sha256File(to, hex, size) || hex != e.sha256 || size != e.size) die("post-install check failed for " + e.name);
    }
    fs::remove_all(staging, ec);
    // Remove files from an older version that are no longer part of the game.
    for (auto& old : prev.files) {
        bool still = std::any_of(man.files.begin(), man.files.end(), [&](const manifest::Entry& x) { return x.name == old; });
        if (!still) fs::remove(dir / old, ec);
    }
    {
        std::ofstream f(dir / "installed.txt", std::ios::trunc);
        f << "version " << man.version << "\n";
        for (auto& e : man.files) f << "file " << e.name << "\n";
    }
    ok("Installed GhostNet " + man.version + " to " + narrow(dir.wstring()));

    fs::path exe = dir / kGameExe;
    if (!noShortcut) {
        if (makeShortcut(startMenuLink(), exe, dir)) ok("Start Menu shortcut created");
        else info("Could not create a Start Menu shortcut (not critical).");
        if (!g_yes && ask("Also put a shortcut on your Desktop?", false)) {
            if (makeShortcut(desktopLink(), exe, dir)) ok("Desktop shortcut created");
        }
    }
    std::cout << "\n  " << GREEN << BOLD << "Done." << RESET << " Run this setup again any time to update or repair.\n";
    if (!noLaunch && !g_yes && ask("Launch GhostNet now?", true)) {
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + exe.wstring() + L"\"";
        if (CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            info("Could not start the game automatically; use the Start Menu shortcut.");
        }
    }
    CoUninitialize();
    return 0;
}
