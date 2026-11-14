// Portable tests for the installer's parsing/validation logic (runs on Linux in CI too).
#include <cstdio>
#include <string>

#include "json.h"
#include "manifest.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
    std::string err;
    json::Value v;

    // JSON: a realistic slice of the GitHub releases API.
    const char* rel = R"({
      "tag_name": "v0.1.0", "draft": false, "prerelease": false,
      "assets": [
        {"name": "GhostNet.exe", "size": 123456, "state": "uploaded",
         "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
         "uploader": {"login": "github-actions[bot]"},
         "browser_download_url": "https://github.com/Cilis1/GhostNet/releases/download/v0.1.0/GhostNet.exe"}
      ],
      "body": "line\nwith \"quotes\" and \u00e9 and \ud83d\udc7b"
    })";
    CHECK(json::parse(rel, v, err));
    CHECK(v.getString("tag_name") == "v0.1.0");
    CHECK(!v.getBool("draft", true));
    CHECK(v.get("assets") && v.get("assets")->arr.size() == 1);
    CHECK(v.get("assets")->arr[0].get("uploader")->getString("login") == "github-actions[bot]");
    CHECK(v.get("assets")->arr[0].getNumber("size") == 123456);
    CHECK(v.getString("body").find("\xF0\x9F\x91\xBB") != std::string::npos);   // ghost emoji via surrogate pair

    CHECK(!json::parse("{\"a\":1,}", v, err));
    CHECK(!json::parse("[1,2", v, err));
    CHECK(!json::parse("{\"a\":\"\\ud83d\"}", v, err));       // lone surrogate
    CHECK(!json::parse(std::string(200, '['), v, err));      // nesting bomb
    CHECK(!json::parse("{\"a\":1} trailing", v, err));
    CHECK(!json::parse("\"raw\ncontrol\"", v, err));

    // File names.
    CHECK(manifest::safeFileName("GhostNet.exe"));
    CHECK(manifest::safeFileName("README.txt"));
    CHECK(!manifest::safeFileName("../GhostNet.exe"));
    CHECK(!manifest::safeFileName("..\\GhostNet.exe"));
    CHECK(!manifest::safeFileName("C:evil.exe"));
    CHECK(!manifest::safeFileName("sub/dir.exe"));
    CHECK(!manifest::safeFileName(".bashrc"));
    CHECK(!manifest::safeFileName("nul"));
    CHECK(!manifest::safeFileName("COM1.exe"));
    CHECK(!manifest::safeFileName("trailingdot."));
    CHECK(!manifest::safeFileName(""));
    CHECK(!manifest::safeFileName(std::string(65, 'a')));

    // Manifests.
    std::string h(64, 'a');
    manifest::Manifest m;
    CHECK(manifest::parse("GHOSTNET-MANIFEST 1\nrepo Cilis1/GhostNet\nversion 0.1.0\nfile " + h + " 100 GhostNet.exe\nfile " + h +
                              " 5 README.txt\n",
                          m, err));
    CHECK(m.repo == "Cilis1/GhostNet" && m.version == "0.1.0" && m.files.size() == 2);
    manifest::Manifest bad;
    CHECK(!manifest::parse("NOPE 1\n", bad, err));
    CHECK(!manifest::parse("GHOSTNET-MANIFEST 1\nrepo a/b\nversion 1\nfile " + h + " 100 a.exe\nfile " + h + " 100 A.EXE\n", bad, err));
    CHECK(!manifest::parse("GHOSTNET-MANIFEST 1\nrepo a/b\nversion 1\nfile XYZ 100 a.exe\n", bad, err));
    CHECK(!manifest::parse("GHOSTNET-MANIFEST 1\nrepo a/b\nversion 1\nfile " + h + " 0 a.exe\n", bad, err));
    CHECK(!manifest::parse("GHOSTNET-MANIFEST 1\nrepo a/b\nversion 1\nfile " + h + " 999999999999 a.exe\n", bad, err));
    CHECK(!manifest::parse("GHOSTNET-MANIFEST 1\nrepo a/b\nversion 1\nfile " + h + " 10 a.exe extra\n", bad, err));
    CHECK(!manifest::parse("GHOSTNET-MANIFEST 1\nrepo a/b\nversion 1;rm\nfile " + h + " 10 a.exe\n", bad, err));
    CHECK(!manifest::parse("GHOSTNET-MANIFEST 1\nrepo a/b\nversion 1\nsurprise x\n", bad, err));

    std::printf(fails ? "%d FAILURE(S)\n" : "all installer logic tests passed\n", fails);
    return fails ? 1 : 0;
}
