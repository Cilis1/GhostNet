#include "world.h"

#include <algorithm>
#include <cctype>

#include "balance.h"
#include "rng.h"

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Open: return "OPEN";
        case Kind::Hidden: return "HIDDEN";
        case Kind::Guarded: return "GUARDED";
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
        case Kind::Open: return {Job::Relay, Job::Proxy, 1, 85, 1};
        case Kind::Hidden: return {Job::Scout, Job::Proxy, 2, 75, 2};
        case Kind::Guarded: return {Job::Proxy, Job::Compute, 2, 60, 3};
        case Kind::Corporate: return {Job::Compute, Job::Relay, 3, 45, 5};
        default: return {Job::Relay, Job::Proxy, 1, 50, 1};
    }
}

double baseTraceRate(const Target& t) { return bal::TRACE_RATE[std::max(1, std::min(5, t.security))]; }

const char* dangerWord(double r) {
    if (r < 1.3) return "SLOW";
    if (r < 2.1) return "NORMAL";
    if (r < 3.0) return "FAST";
    return "LETHAL";
}

bool isMapLocal(int id) { return id >= 1000 && id < 8000; }

// ---------------------------------------------------------------------------
// Story contracts
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
};

const std::vector<StoryDef>& stories() {
    static const std::vector<StoryDef> s = {
        {{"FIRST LIGHT",
          "Mags here. Easy one to start. Brightwater Laundromat runs a skim for the dock union\n"
          "and keeps the receipts on a billing box with a cheap keypad. Pull skim_records.csv\n"
          "and get out. Do this clean and I'll make it worth your while - I have hardware\n"
          "the corp thinks was destroyed.",
          "Mags: Clean work. As promised - a salvaged rack. Corporate wrote it off as melted.\n"
          "It's yours. One machine inside. Treat it like home."},
         "Brightwater Laundromat Billing", "bwl-billing.net", "A laundromat with a suspicious amount of revenue.",
         "/ledger", {"skim_records.csv"}, 1, LockType::Keypad, false, false, false, 120},
        {{"PAPER TRAIL",
          "Halvorsen Shipping is moving something off the books. I need both cargo manifests\n"
          "from their customs office. Their cipher lock is mean, but customs offices leave\n"
          "maintenance doors open. Somebody, somewhere, wrote the phrase down. Go read.",
          "Mags: Both manifests. Somebody's going to have a bad week at Halvorsen."},
         "Halvorsen Shipping Customs", "customs.halvorsen-ship.com", "Customs paperwork for a shipping line with secrets.",
         "/manifests", {"manifest_0414.pdf", "manifest_0415.pdf"}, 2, LockType::Cipher, true, false, false, 250},
        {{"THE QUIET FLOOR",
          "Meridian Insurance denied a claim on a fire they started. The evidence is in their\n"
          "claims vault. Rotor locks there - remember, on a rotor NEAR means one notch off,\n"
          "not misplaced. And their sysadmin loves planting bait files. Read before you grab.",
          "Mags: That's the proof. A family gets their house back. Not bad for a ghost."},
         "Meridian Insurance Claims Vault", "vault.meridian-ins.net", "Claims that were never meant to be paid.",
         "/claims/denied", {"claim_88231.pdf", "adjuster_notes.txt"}, 3, LockType::Rotor, false, true, false, 450},
        {{"COLD IRON",
          "Ferrous Dynamics built the trace engine that hunts people like us. Their R&D\n"
          "cluster has the source. Three files. Heavy security, heavy hardware. If you have\n"
          "a way in that isn't through the keypad, use it.",
          "Mags: You have their trace engine. Now we know how the hunter thinks."},
         "Ferrous Dynamics R&D Cluster", "rnd.ferrous-dyn.com", "The people who build the hunters.",
         "/rnd/tracekit", {"tracekit_core.src", "tracekit_heur.src", "tracekit_keys.pem"}, 4, LockType::Keypad, true, true,
         false, 800},
        {{"GLASS HOUSE",
          "This is it. Aurelian Tower core. The board minutes that tie every job you've done\n"
          "to one boardroom. There's a watchdog on this box - the longer you stay, the faster\n"
          "it hunts. Plan. Prepare. Then go.",
          "Mags: It's everywhere. Every feed, every paper. Aurelian is finished.\n"
          "You started with one machine in a closet. Look at what you built.\n"
          "The board stays open - the city always has more doors."},
         "Aurelian Tower Core", "core.aurelian-holdings.com", "The glass tower that owns the skyline.",
         "/board/minutes", {"minutes_q1.enc", "minutes_q2.enc", "minutes_q3.enc"}, 5, LockType::Cipher, false, true, true,
         1500},
    };
    return s;
}

const std::vector<std::string> kOpenNames = {"Kwik-Print Kiosk", "Harbor Parking Meters", "St. Ives Clinic Wi-Fi",
                                             "Dockside Vending Grid", "Lucky 8 Arcade", "Bayline Ferry Ticketing",
                                             "Noodle Bar POS", "Gulfport Car Wash", "Azalea Motel Front Desk"};
const std::vector<std::string> kHiddenNames = {"Moth Relay", "Lantern Exchange", "Underpass BBS", "Blackwater Mirror",
                                               "Sable Courier Drop", "Nightjar Escrow", "Cellar Door Market"};
const std::vector<std::string> kGuardedNames = {"Port Authority Records", "Vance Security Logs", "Gulfline Credit Union",
                                                "Precinct 9 Evidence DB", "Coastal Telecom Billing", "Mobile Bay Customs"};
const std::vector<std::string> kCorpNames = {"Kessler-Maru Compute", "Tidewater BioSys Grid", "Orrin Aerospace Farm",
                                             "Aurelian Holdings DC-2", "Ferrous Dynamics Edge", "Halcyon Pharma Core"};
const std::vector<std::string> kShrineNames = {"The Stacks (public archive)", "Old Net Museum", "Whisper Wiki",
                                               "Dead Drop Library", "Saint Ada Reading Room"};
const std::vector<std::string> kSideNames = {"Blacksite KESTREL", "Undersea Cable Tap 7", "Dark Ledger Annex",
                                             "Orbital Uplink Shadow", "Vault Beneath the Pier", "Project LAMPLIGHT"};
const std::vector<std::string> kPhrases = {"MOTHWING", "LANTERN", "OLDHARBOR", "VELVETROPE", "CINDERFALL", "SALTMARSH",
                                           "BLUEHERON", "IRONCLAD", "GLASSWING", "NIGHTTIDE", "AZALEA", "PELICAN",
                                           "DRIFTWOOD", "STORMCELLAR", "LOWTIDE", "MAGNOLIA"};
const std::vector<std::string> kPayNames = {"payroll_q3.db", "escrow.ledger", "wallet_keys.kst", "bonds_cache.dat",
                                            "client_cards.csv", "crypto_cold.wal", "invoices_2026.xls", "bearer_tokens.bin",
                                            "offshore_map.dat", "slush_fund.qbo", "rebates.mdb", "settlements.db"};
const std::vector<std::string> kPayDirs = {"/finance", "/vault", "/home/exec", "/var/backup", "/accounts", "/srv/data"};
const std::vector<std::string> kHoneyNames = {"MASTER_PASSWORDS.txt", "ceo_bitcoin_keys.wal", "DO_NOT_OPEN_bonus.db",
                                              "admin_creds_FINAL.kst"};
const std::vector<std::string> kLore = {
    "Reminder: the trace engine logs connection time, not bytes. Short visits are safe visits.",
    "IT notice: proxies upstream of us keep slowing our trace pings. Budget request denied again.",
    "Whoever keeps stealing rack hardware from the east wing: we WILL find you. - Facilities",
    "Overheard: corporate hardware runs hot and loud. Great machines, terrible electric bills.",
    "Tip from an old ghost: name your machines. You fight harder for things with names.",
    "Security memo: rotor locks report one-notch misses differently. Staff keep misreading them too.",
    "Rumor: some vaults plant a juicy-looking file that screams when touched. Read the room first.",
    "Old forum post: 'the bait proxy saved my life. I still miss her. RIP little-sister-3.'"};

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
    static const int base[6] = {0, 22, 38, 58, 84, 115};
    return static_cast<int>(base[std::max(1, std::min(5, sec))] * (0.7 + 0.6 * r.real()));
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
        f.credits = std::max(5, static_cast<int>(payValue(t.security, r) * mult));
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

Target makeSide(uint64_t seed, int idx) {
    Rng r(seedOf(seed, 0x51DE00 + static_cast<uint64_t>(idx)));
    Target t;
    t.id = 9000 + idx;
    t.sideIndex = idx;
    t.name = kSideNames[static_cast<size_t>(idx) % kSideNames.size()];
    if (idx >= static_cast<int>(kSideNames.size())) t.name += " " + std::to_string(idx / kSideNames.size() + 1);
    t.host = slug(t.name) + ".onion-ish";
    t.blurb = "Found through a paper trail. Rich haul. Real watchdog.";
    t.kind = Kind::Side;
    t.security = std::min(5, 3 + idx % 3);
    std::vector<LockType> locks = {LockType::Keypad, LockType::Rotor, LockType::Cipher};
    t.lock = r.pick(locks);
    t.watchdog = true;
    addPaydata(t, r, r.range(3, 5), 2.0, sizeMin(t.security), sizeMax(t.security));
    if (r.chance(0.5)) addHoneypot(t, r);
    addLore(t, r);
    return t;
}
}  // namespace

int storyCount() { return static_cast<int>(stories().size()); }
bool contractAvailable(const GameState& g) { return g.story < storyCount() && g.mapIndex >= g.story; }
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
                    : t.kind == Kind::Guarded ? "Watched and guarded. Real money."
                                              : "Industrial iron. Best hardware in the city.";
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
    shrine.host = slug(shrine.name) + ".org";
    shrine.blurb = "Poor in money, rich in information. Never stealable.";
    shrine.security = 1;
    shrine.lock = r.chance(0.5) ? LockType::None : LockType::Keypad;
    addPaydata(shrine, r, 1, 0.5, 4, 8);
    addLore(shrine, r);

    // Current story contract.
    // One new contract per city refresh: Mags doesn't call every night.
    if (contractAvailable(g)) board.push_back(makeStory(g.worldSeed, g.story));
    // Revealed side routes.
    for (int s : g.sidesRevealed)
        if (!g.sidesBurned.count(s)) board.push_back(makeSide(g.worldSeed, s));

    // Leak every backdoor passphrase somewhere readable on this map.
    for (auto& t : board) {
        if (!t.backdoor) continue;
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
