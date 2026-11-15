#include "world.h"

#include <algorithm>
#include <cctype>

#include "balance.h"
#include "rng.h"

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Open: return "OPEN";
        case Kind::Hidden: return "HIDDEN";
        case Kind::Guarded: return "WATCHED";
        case Kind::Corporate: return "CORPORATE";
        case Kind::Shrine: return "SHRINE";
        case Kind::Story: return "CONTRACT";
        case Kind::Side: return "SIDE ROUTE";
    }
    return "?";
}
const char* lockName(LockType l) {
    switch (l) {
        case LockType::None: return "NO";
        case LockType::Keypad: return "KEYPAD";
        case LockType::Rotor: return "ROTOR";
        case LockType::Cipher: return "CIPHER";
    }
    return "?";
}

bool Target::hasHoneypot() const {
    for (auto& f : files) if (f.kind == FileKind::Honeypot) return true;
    return false;
}
int Target::objectiveCount() const {
    int n = 0;
    for (auto& f : files) if (f.kind == FileKind::Objective) ++n;
    return n;
}
std::string Target::objectiveDir() const {
    for (auto& f : files) if (f.kind == FileKind::Objective) return f.dir;
    return "";
}

HardwareSpec hardwareFor(Kind k) {
    switch (k) {
        // Captured hardware is always louder than anything you can buy (bought = signal 1).
        case Kind::Open: return {Job::Relay, Job::Proxy, 1, 85, 2};
        case Kind::Hidden: return {Job::Scout, Job::Proxy, 2, 75, 2};
        case Kind::Guarded: return {Job::Proxy, Job::Compute, 2, 60, 3};
        case Kind::Corporate: return {Job::Compute, Job::Relay, 3, 45, 4};
        default: return {Job::Relay, Job::Proxy, 1, 50, 2};
    }
}

Trait traitFor(Kind k) {
    switch (k) {
        case Kind::Open: return Trait::Salvaged;
        case Kind::Hidden: return Trait::OldNet;
        case Kind::Guarded: return Trait::HotIron;
        case Kind::Corporate: return Trait::CorpBuild;
        default: return Trait::None;
    }
}

double baseTraceRate(const Target& t) {
    if (t.traceOverride > 0) return t.traceOverride;
    return bal::TRACE_RATE[std::max(1, std::min(5, t.security))];
}

const char* dangerWord(double r) {
    if (r < 1.3) return "SLOW";
    if (r < 2.1) return "NORMAL";
    if (r < 3.0) return "FAST";
    return "LETHAL";
}

bool isMapLocal(int id) { return id >= 1000 && id < 8000; }

// ---------------------------------------------------------------------------
// Contracts
// ---------------------------------------------------------------------------
namespace {
struct StoryDef {
    StoryInfo info;
    std::string name, host, blurb, objDir;
    std::vector<std::string> objFiles;
    int security;
    LockType lock;
    bool backdoor, honeypot, watchdog;
    int reward;
    std::string infoReward;
};

const std::vector<StoryDef>& stories() {
    static const std::vector<StoryDef> s = {
        {{"KETTLE STREET",
          "Posted by: a night nurse, paid in cash.\n"
          "Kettle Street Clinic pads every invoice that crosses its billing box and bills the\n"
          "difference to people with no insurance. I need the padded ledger - invoices_padded.csv.\n"
          "It's a cheap keypad on a cheap box. Get in, get it, get out before HELIOS opens a ticket.",
          "The nurse pays. It isn't much. It's the first money this month that didn't come from\n"
          "selling something you'll need later."},
         "Kettle Street Clinic Billing", "billing.kettle-st-clinic.net", "A clinic that bills the people it turns away.",
         "/ledger", {"invoices_padded.csv"}, 1, LockType::Keypad, false, false, false, 300, ""},
        {{"PAPER TRAIL",
          "Posted by: a dockworkers' collective.\n"
          "The Port Customs office is waving HELIOS containers through without inspection. We\n"
          "want both manifests. The cipher lock on that box is mean - but customs offices leave\n"
          "maintenance doors, and somebody always writes the phrase down. Go read first.",
          "The collective sends a thank-you and a transfer. Somebody at Port Customs is going to\n"
          "have a very long week."},
         "Port Customs Manifest Server", "manifests.portcustoms.gov", "Containers that nobody is allowed to open.",
         "/manifests", {"manifest_0414.pdf", "manifest_0415.pdf"}, 2, LockType::Cipher, true, false, false, 600, ""},
        {{"QUIET FLOOR",
          "Posted by: anonymous. Pays mostly in information.\n"
          "Meridian Facilities services HELIOS hardware nobody is supposed to know about. Pull the\n"
          "two sealed work orders. Rotor locks there - on a rotor, a near miss means one notch off,\n"
          "NOT misplaced. And their admin plants bait files. Read before you grab.",
          "The work orders are dull until you read the routing: a HELIOS server that appears on no\n"
          "board, serviced by a facilities annex that appears on no map.\n"
          "The DEEP ARCHIVE. Everything HELIOS has ever ticketed, in one place."},
         "Meridian Facilities Work Orders", "orders.meridian-fac.net", "The contractor that services what HELIOS hides.",
         "/orders/sealed", {"wo_77812.pdf", "wo_77813.pdf"}, 3, LockType::Rotor, false, true, false, 250,
         "Reveals the HELIOS DEEP ARCHIVE and the side route to the Facilities Annex."},
        {{"COLD IRON",
          "Posted by: someone who used to work there.\n"
          "Ferrous Dynamics builds the trace engines HELIOS runs. Their R&D cluster has the source.\n"
          "Three files. Heavy security, heavy hardware. If you have a way in that isn't through\n"
          "the keypad, use it.",
          "You have the source of the thing that hunts you. You read it twice. It is very good at\n"
          "its job. So, increasingly, are you."},
         "Ferrous Dynamics R&D Cluster", "rnd.ferrous-dyn.com", "The people who build the hunters.",
         "/rnd/tracekit", {"tracekit_core.src", "tracekit_heur.src", "tracekit_keys.pem"}, 4, LockType::Keypad, true, true,
         false, 1400, ""},
    };
    return s;
}

const std::vector<std::string> kOpenNames = {"Stairwell Noodle POS", "Level 9 Laundry Grid", "Skybridge Parking Meters",
                                             "Vending Row 44", "Night Market Kiosk", "Pachinko Den Billing",
                                             "Transit Turnstile Hub", "Rooftop Clinic Wi-Fi", "Capsule Hotel Front Desk"};
const std::vector<std::string> kHiddenNames = {"Moth Relay", "Lantern Exchange", "Underpass BBS", "Ghost Choir Forum",
                                               "Sable Courier Drop", "Nightjar Escrow", "Cellar Door Market"};
const std::vector<std::string> kGuardedNames = {"HELIOS Ticket Desk 7", "District Security Logs", "Tower Credit Union",
                                                "Precinct 9 Evidence DB", "Spire Telecom Billing", "Transit Authority Records"};
const std::vector<std::string> kCorpNames = {"HELIOS Edge Cluster", "Kessler-Maru Compute", "Tidewater BioSys Grid",
                                             "Orrin Aerospace Farm", "Aurelian Holdings DC", "Vantablack Logistics Core"};
const std::vector<std::string> kShrineNames = {"The Stacks (public archive)", "Old Net Museum", "Whisper Wiki",
                                               "Dead Drop Library", "Courier's Chapel"};
const std::vector<std::string> kSideNames = {"Blacksite KESTREL", "Undersea Cable Tap 7", "Dark Ledger Annex",
                                             "Orbital Uplink Shadow", "Vault Beneath the Pier", "Project LAMPLIGHT"};
const std::vector<std::string> kPhrases = {"MOTHWING", "LANTERN", "OLDHARBOR", "VELVETROPE", "CINDERFALL", "SALTMARSH",
                                           "BLUEHERON", "IRONCLAD", "GLASSWING", "NIGHTTIDE", "STAIRWELL", "SKYBRIDGE",
                                           "DRIFTWOOD", "STORMCELLAR", "LOWTIDE", "NEONRAIN"};
const std::vector<std::string> kDeepPhrases = {"SAINTKEPTIT", "NEVERMELTED", "OPENTICKET", "LASTCOURIER"};
const std::vector<std::string> kPayNames = {"payroll_q3.db", "escrow.ledger", "wallet_keys.kst", "bonds_cache.dat",
                                            "client_cards.csv", "crypto_cold.wal", "invoices_2026.xls", "bearer_tokens.bin",
                                            "offshore_map.dat", "slush_fund.qbo", "rebates.mdb", "settlements.db"};
const std::vector<std::string> kPayDirs = {"/finance", "/vault", "/home/exec", "/var/backup", "/accounts", "/srv/data"};
const std::vector<std::string> kHoneyNames = {"MASTER_PASSWORDS.txt", "ceo_bitcoin_keys.wal", "DO_NOT_OPEN_bonus.db",
                                              "admin_creds_FINAL.kst"};
const std::vector<std::string> kLore = {
    "HELIOS SLA reminder: unauthorized-connection tickets must be resolved within response window. No exceptions.",
    "IT notice: proxies upstream of us keep slowing our trace pings. Budget request denied again.",
    "Whoever keeps stealing rack hardware from the east wing: we WILL find you. - Facilities",
    "Overheard: corporate hardware runs hot and loud. Great machines, terrible power bills.",
    "Tip from an old runner: name your machines. You fight harder for things with names.",
    "Security memo: rotor locks report one-notch misses differently. Staff keep misreading them too.",
    "Rumor: some vaults plant a juicy-looking file that screams when touched. Read the room first.",
    "Forum post: 'the decoy proxy saved my life. I still miss her. RIP little-sister-3.'",
    "Courier board, archived: 'S. moving rack units again. Says HELIOS logs them as melted. Ask no questions.'",
    "HELIOS ticket #48213 closed: intruder disconnected before response. Escalate tracing budget."};

std::string slug(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!o.empty() && o.back() != '-') o += '-';
    }
    while (!o.empty() && o.back() == '-') o.pop_back();
    if (o.size() > 18) o = o.substr(0, 18);
    while (!o.empty() && o.back() == '-') o.pop_back();
    return o;
}

int payValue(int sec, Rng& r) {
    return static_cast<int>(bal::PAY_BY_SECURITY[std::max(1, std::min(5, sec))] * (0.7 + 0.6 * r.real()));
}

void addPaydata(Target& t, Rng& r, int n, double mult, int minSize, int maxSize) {
    std::vector<std::string> names = kPayNames;
    r.shuffle(names);
    for (int i = 0; i < n && i < static_cast<int>(names.size()); ++i) {
        File f;
        f.name = names[static_cast<size_t>(i)];
        f.dir = r.pick(kPayDirs);
        f.kind = FileKind::Paydata;
        f.size = r.range(minSize, maxSize);
        f.credits = std::max(10, static_cast<int>(payValue(t.security, r) * mult));
        t.files.push_back(f);
    }
}

void addHoneypot(Target& t, Rng& r) {
    File f;
    f.name = r.pick(kHoneyNames);
    f.dir = r.pick(kPayDirs);
    f.kind = FileKind::Honeypot;
    f.size = r.range(4, 10);
    f.credits = payValue(t.security, r) * 2;   // looks irresistible
    t.files.push_back(f);
}

void addLore(Target& t, Rng& r) {
    File f;
    f.name = "memo_" + std::to_string(r.range(100, 999)) + ".txt";
    f.dir = "/home/staff";
    f.kind = FileKind::Intel;
    f.size = 1;
    f.text = r.pick(kLore);
    t.files.push_back(f);
}

int sizeMin(int sec) { return 4 + sec * 2; }
int sizeMax(int sec) { return 10 + sec * 7; }

std::string deepPassphrase(uint64_t seed) {
    Rng r(seedOf(seed, 0xDEE9));
    return r.pick(kDeepPhrases);
}

Target makeStory(uint64_t seed, int idx) {
    const StoryDef& d = stories()[static_cast<size_t>(idx)];
    Rng r(seedOf(seed, 0x5700 + static_cast<uint64_t>(idx)));
    Target t;
    t.id = 8000 + idx;
    t.name = d.name;
    t.host = d.host;
    t.blurb = d.blurb;
    t.kind = Kind::Story;
    t.security = d.security;
    t.lock = d.lock;
    t.backdoor = d.backdoor;
    t.watchdog = d.watchdog;
    t.storyIndex = idx;
    t.reward = d.reward;
    t.infoReward = d.infoReward;
    if (t.backdoor) t.passphrase = r.pick(kPhrases);
    for (auto& n : d.objFiles) {
        File f;
        f.name = n;
        f.dir = d.objDir;
        f.kind = FileKind::Objective;
        f.size = r.range(sizeMin(t.security), sizeMin(t.security) + 6);
        t.files.push_back(f);
    }
    addPaydata(t, r, idx == 0 ? 1 : 2, 0.8, sizeMin(t.security), sizeMax(t.security));
    if (d.honeypot) addHoneypot(t, r);
    addLore(t, r);
    return t;
}

Target makeDeep(uint64_t seed) {
    Rng r(seedOf(seed, 0xDEE7));
    Target t;
    t.id = kDeepId;
    t.name = "HELIOS DEEP ARCHIVE";
    t.host = "deep.archive.helios.int";
    t.blurb = "Every ticket HELIOS ever opened. No runner with a deck alone finishes inside the window.";
    t.kind = Kind::Story;
    t.deep = true;
    t.security = 5;
    t.traceOverride = bal::DEEP_TRACE_RATE;
    t.lock = LockType::Rotor;
    t.backdoor = true;
    t.passphrase = deepPassphrase(seed);
    t.watchdog = true;
    t.reward = 5000;
    const char* names[3] = {"ticket_index.arc", "runner_dossiers.arc", "response_policy.arc"};
    const int sizes[3] = {34, 36, 38};
    for (int i = 0; i < 3; ++i) {
        File f;
        f.name = names[i];
        f.dir = "/archive/root";
        f.kind = FileKind::Objective;
        f.size = sizes[i];
        t.files.push_back(f);
    }
    File memo;
    memo.name = "ticket_48213.txt";
    memo.dir = "/archive/open";
    memo.kind = FileKind::Intel;
    memo.size = 1;
    memo.text = "Ticket #48213 - status: OPEN. Subject: unknown runner, operating owned hardware.\n"
                "Hardware registered to no known entity. Origin: courier inventory logged as destroyed.";
    t.files.push_back(memo);
    (void)r;
    return t;
}

Target makeSide(uint64_t seed, int idx) {
    Rng r(seedOf(seed, 0x51DE00 + static_cast<uint64_t>(idx)));
    Target t;
    t.id = 9000 + idx;
    t.sideIndex = idx;
    t.kind = Kind::Side;
    t.watchdog = true;
    t.blurb = "Found through a paper trail. Rich haul. Real watchdog.";
    std::vector<LockType> locks = {LockType::Keypad, LockType::Rotor, LockType::Cipher};
    if (idx == kAnnexSide) {
        t.name = "HELIOS Facilities Annex";
        t.host = "annex.facilities.helios.int";
        t.security = 3;
        t.lock = LockType::Keypad;
        addPaydata(t, r, 3, 1.6, sizeMin(t.security), sizeMax(t.security));
        File memo;
        memo.name = "deep_maint.txt";
        memo.dir = "/maint/deep";
        memo.kind = FileKind::Intel;
        memo.size = 1;
        memo.phraseFor = kDeepId;
        memo.text = "From: facilities@annex\nThe maintenance door at deep.archive.helios.int still answers to '" +
                    deepPassphrase(seed) + "'.\nDo not rotate it - the archive refuses re-keying mid-cycle. - K.";
        t.files.push_back(memo);
        addLore(t, r);
        return t;
    }
    t.name = kSideNames[static_cast<size_t>(idx) % kSideNames.size()];
    if (idx >= static_cast<int>(kSideNames.size())) t.name += " " + std::to_string(idx / kSideNames.size() + 1);
    t.host = slug(t.name) + ".dark";
    t.security = std::min(5, 3 + idx % 3);
    t.lock = r.pick(locks);
    addPaydata(t, r, r.range(3, 5), 2.0, sizeMin(t.security), sizeMax(t.security));
    if (r.chance(0.5)) addHoneypot(t, r);
    addLore(t, r);
    return t;
}
}  // namespace

int storyCount() { return static_cast<int>(stories().size()); }
// One new contract per board refresh: nobody posts work for you every night.
bool contractAvailable(const GameState& g) { return g.story < storyCount() && g.mapIndex >= g.story; }
// The deep job surfaces once QUIET FLOOR exposes it, or once Act Three begins - whichever comes first.
// Nothing stops you trying it the moment it appears.
bool deepListed(const GameState& g) { return (g.story >= 3 || g.act >= 3) && !g.deepDone; }
const StoryInfo& storyInfo(int idx) { return stories()[static_cast<size_t>(idx)].info; }

std::vector<Target> buildBoard(const GameState& g) {
    std::vector<Target> board;
    Rng r(seedOf(g.worldSeed, 0xB0A2D00 + static_cast<uint64_t>(g.mapIndex)));

    // Five regular targets: one of each kind, plus one extra.
    std::vector<Kind> kinds = {Kind::Open, Kind::Hidden, Kind::Guarded, Kind::Corporate};
    std::vector<Kind> extra = {Kind::Open, Kind::Open, Kind::Hidden, Kind::Guarded, Kind::Corporate};
    kinds.push_back(r.pick(extra));
    r.shuffle(kinds);
    std::set<std::string> usedNames;
    for (size_t i = 0; i < kinds.size(); ++i) {
        Target t;
        t.id = 1000 + g.mapIndex * 10 + static_cast<int>(i);
        t.kind = kinds[i];
        const std::vector<std::string>* pool = &kOpenNames;
        switch (t.kind) {
            case Kind::Open: pool = &kOpenNames; t.security = 1; break;
            case Kind::Hidden: pool = &kHiddenNames; t.security = 2; break;
            case Kind::Guarded: pool = &kGuardedNames; t.security = 3; break;
            case Kind::Corporate: pool = &kCorpNames; t.security = 4; break;
            default: break;
        }
        if (t.kind != Kind::Open && r.chance(0.3)) t.security = std::min(5, t.security + 1);
        do { t.name = r.pick(*pool); } while (usedNames.count(t.name) && usedNames.size() < 20);
        usedNames.insert(t.name);
        static const char* tlds[] = {".net", ".com", ".biz", ".io", ".local"};
        t.host = slug(t.name) + tlds[r.range(0, 4)];
        if (t.kind == Kind::Open) {
            t.lock = r.chance(0.5) ? LockType::None : LockType::Keypad;
            t.blurb = "Cheap, quiet, barely watched.";
        } else {
            std::vector<LockType> locks = {LockType::Keypad, LockType::Rotor, LockType::Cipher};
            t.lock = r.pick(locks);
            t.blurb = t.kind == Kind::Hidden ? "Secretive. Trades in information."
                    : t.kind == Kind::Guarded ? "Watched by HELIOS response teams. Real money."
                                              : "Corporate iron. The best hardware in the city.";
        }
        t.backdoor = t.lock != LockType::None && r.chance(0.4);
        if (t.backdoor) t.passphrase = r.pick(kPhrases);
        int n = t.kind == Kind::Open ? r.range(2, 3) : t.kind == Kind::Corporate ? r.range(3, 5) : r.range(2, 4);
        addPaydata(t, r, n, t.kind == Kind::Hidden ? 0.8 : 1.0, sizeMin(t.security), sizeMax(t.security));
        if ((t.kind == Kind::Guarded && r.chance(0.4)) || (t.kind == Kind::Corporate && r.chance(0.6))) addHoneypot(t, r);
        if (t.kind == Kind::Hidden || r.chance(0.4)) addLore(t, r);
        board.push_back(t);
    }

    // Exactly two carry detachable hardware - picked by the world seed.
    std::vector<size_t> idx = {0, 1, 2, 3, 4};
    r.shuffle(idx);
    board[idx[0]].stealable = true;
    board[idx[1]].stealable = true;

    // One carries a paper trail to a side route.
    size_t trailIdx = static_cast<size_t>(r.range(0, 4));
    {
        Target& t = board[trailIdx];
        t.trail = true;
        t.trailSide = g.mapIndex;
        Target side = makeSide(g.worldSeed, g.mapIndex);
        File f;
        f.name = "maint.log";
        f.dir = "/var/log";
        f.kind = FileKind::Log;
        f.size = 1;
        f.revealsSide = g.mapIndex;
        f.text = "[maint] nightly backup pushed via undocumented route -> " + side.host +
                 "\n[maint] reminder: that box is NOT on the asset register. do not mention it.";
        t.files.push_back(f);
    }

    // Maybe one jackpot: storage absurdly dense for its hardware.
    if (r.chance(0.25)) {
        Target& t = board[static_cast<size_t>(r.range(0, 4))];
        t.jackpot = true;
        for (auto& f : t.files) if (f.kind == FileKind::Paydata) f.credits *= 3;
    }

    // The shrine: information-rich, money-poor, never stealable.
    Target shrine;
    shrine.id = 1000 + g.mapIndex * 10 + 5;
    shrine.kind = Kind::Shrine;
    shrine.name = kShrineNames[static_cast<size_t>(g.mapIndex) % kShrineNames.size()];
    shrine.host = slug(shrine.name) + ".free";
    shrine.blurb = "Poor in money, rich in information. Never stealable.";
    shrine.security = 1;
    shrine.lock = r.chance(0.5) ? LockType::None : LockType::Keypad;
    addPaydata(shrine, r, 1, 0.5, 4, 8);
    addLore(shrine, r);

    // Current story contract.
    if (contractAvailable(g)) board.push_back(makeStory(g.worldSeed, g.story));
    if (deepListed(g)) board.push_back(makeDeep(g.worldSeed));
    // Revealed side routes.
    for (int s : g.sidesRevealed)
        if (!g.sidesBurned.count(s)) board.push_back(makeSide(g.worldSeed, s));

    // Leak every backdoor passphrase somewhere readable on this map.
    for (auto& t : board) {
        if (!t.backdoor || t.deep) continue;   // the deep job's phrase lives only in the Facilities Annex
        File memo;
        memo.name = "facilities_" + std::to_string(r.range(10, 99)) + ".txt";
        memo.dir = "/home/facilities";
        memo.kind = FileKind::Intel;
        memo.size = 1;
        memo.phraseFor = t.id;
        memo.text = "From: facilities@" + t.host + "\nThe maintenance door at " + t.host + " still answers to '" +
                    t.passphrase + "'.\nChange it after the audit. - K.";
        bool inShrine = t.kind == Kind::Story || r.chance(0.7);
        if (inShrine) {
            shrine.files.push_back(memo);
        } else {
            size_t other = static_cast<size_t>(r.range(0, 4));
            if (board[other].id == t.id) other = (other + 1) % 5;
            board[other].files.push_back(memo);
        }
    }
    board.insert(board.begin() + 5, shrine);
    return board;
}

Target* findTarget(std::vector<Target>& board, int id) {
    for (auto& t : board) if (t.id == id) return &t;
    return nullptr;
}

const std::vector<std::string>& starterPhrases() { return kPhrases; }
