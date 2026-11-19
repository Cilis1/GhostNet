// GhostNet - the windowed game: screens, modals and the glue between the core rules and the look.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "intel.h"
#include "locks.h"
#include "rack.h"
#include "raylib.h"
#include "rng.h"
#include "run.h"

enum class Scr { Title, Intro, Hub, Brief, Run, Reckon, Ending, Practice };
enum class Tab { Rack, Board, Deck, Panel, Records, Help };

struct Modal {
    std::string title, body;
    std::vector<std::string> buttons;   // first is the primary action
    Color accent{0, 240, 255, 255};
    bool cinematic = false;             // act cards / story beats: big, slow
    bool input = false;                 // shows a text field (names)
    std::string field;
    // Input helpers: clickable suggestions that fill the field, shown a few at a time and
    // rotating on their own (MORE skips ahead). They are drawn after the buttons, so the
    // primary action stays buttons.front().
    std::vector<std::string> chips;
    std::string chipNote;
    int chipShift = 0;
    std::function<void(int choice, const std::string& field)> onPick;
};

struct Toast {
    std::string text;
    Color color;
    float born;
};

struct FloatText {
    std::string text;
    Color color;
    Vector2 at;
    float born;
};

class App {
public:
    App();
    void frame();            // one frame of input + drawing in the 1280x720 space
    bool wantsQuit() const { return quit_; }
    void shutdown();

    // Headless screenshot tour (CI and docs). Returns the number of failures.
    int screenshotTour(const std::string& dir, const std::function<void()>& render);
    // Headless autoplay: plays the real UI with scripted mouse clicks and typing only.
    // Returns the number of failures.
    int autoplay(const std::function<void()>& render);

    // ---- state -----------------------------------------------------------
    GameState g;
    bool haveSave = false;
    bool titleHelp = false;
    int helpPage = 0;
    Scr scr = Scr::Title;
    Tab tab = Tab::Rack;
    float scrBorn = 0;

    // hub
    int selMachine = -1;      // machine id, -1 = shop view
    int storagePage = 0;      // cold storage tray page (it shows a handful at a time)
    std::vector<Target> board;
    float boardScroll = 0;
    Target brief;

    // run
    std::unique_ptr<RunSession> run;
    std::string input;
    float stepClock = 0;
    float traceShown = 0;
    std::string hoverHint;     // drawn last on the run screen, next to the pointer
    int readFile = -1;
    float endedAt = -1;
    size_t logSeen = 0;
    float flash = 0;
    Color flashColor{255, 255, 255, 255};
    float shake = 0;
    float glitch = 0;
    float beatClock = 0;
    std::vector<FloatText> floats;

    // reckoning
    Reckoning rk;
    int rkCreditsBefore = 0;
    bool rkUnlocked = false;

    // practice
    Lock practice;
    bool practiceOn = false;
    // FORTRESS DRILL (0.7): a composite on your real Kessler budget; RECONNECT replays your rows as LOGGED.
    bool drillOn = false;
    Lock drillBase;                                     // the drill's code (until its alarm log fills)
    std::vector<std::pair<int, std::string>> drillRows; // (stage, guess) logged so far
    int drillConnections = 0, drillRekeys = 0;
    int drillBudget() const;
    int drillAlarms() const;
    void drillConnect(bool fresh);
    bool practiceFromHub = false;
    size_t rowsSeen = 0;
    float rowBorn = 0;
    float logBorn = 0;
    Rng practiceRng{1};

    std::vector<Modal> modals;
    std::vector<Toast> toasts;

private:
    bool quit_ = false;

    void go(Scr s);
    void toast(const std::string& s, Color c);
    void result(const Result& r);
    void autosave();
    void pushModal(Modal m) { modals.push_back(std::move(m)); }
    void confirm(const std::string& title, const std::string& body, const std::string& yes, Color accent, std::function<void()> fn);

    // screens (app.cpp / hub.cpp / connect.cpp)
    void drawTitle();
    void drawIntro();
    void drawHub();
    void drawTopBar();
    void drawRack(Rectangle area);
    void drawMachinePanel(Rectangle area, Machine& m);
    void drawHoverHint();   // shared by the hub and the run screen
    void drawShop(Rectangle area);
    void drawLockedRack(Rectangle area);
    void drawBoard(Rectangle area);
    void drawDeck(Rectangle area);
    void drawPanelTab(Rectangle area);
    void drawRecords(Rectangle area);
    void drawHelp(Rectangle area);
    void drawBrief();
    void drawRun();
    void drawRoute(Rectangle area);
    void drawTrace(Vector2 c, float radius);
    void drawLog(Rectangle area);
    void drawFiles(Rectangle area);
    bool drawLock(Rectangle area, Lock& lock, std::string& field, bool interactive, bool live);
    void drawReckon();
    void drawEnding();
    void drawPractice();
    void drawModals();
    void drawToasts();
    void drawOverlayFx();

    void newGame(uint64_t seed);
    void loadOrNew();
    void connect(const Target& t);
    void submitGuess();
    void handleEvents();
    void finishRun();
    void leaveReckoning();
    void showLedgerCard();
    void showNetworkUnlock();
    void startCapture();
    void openPhraseModal();
    void refreshBoard();
};

const char* actName(int act);
Color jobColor(Job j);
Color kindColor(Kind k);
Color roleColor(BayRole r);
Color toneColor(Tone t);
