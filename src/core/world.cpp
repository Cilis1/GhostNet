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
        case Kind::Prototype: return "PROTOTYPE";
        case Kind::Tower: return "CORP TOWER";
        case Kind::Site: return "SERVICE SITE";
    }
    return "?";
}
const char* lockName(LockType l) {
    switch (l) {
        case LockType::None: return "NO";
        case LockType::Keypad: return "KEYPAD";
        case LockType::Rotor: return "ROTOR";
        case LockType::Cipher: return "CIPHER";
        case LockType::Composite: return "COMPOSITE";
    }
    return "?";
}
const char* doctrineName(Doctrine d) {
    switch (d) {
        case Doctrine::Fortress: return "FORTRESS";
        case Doctrine::Biosentinel: return "BIOSENTINEL";
        case Doctrine::Loadshed: return "LOADSHED";
        case Doctrine::Ledgerlock: return "LEDGERLOCK";
        case Doctrine::Countertrace: return "COUNTERTRACE";
        case Doctrine::Proving: return "PROVING GROUND";
        default: return "NONE";
    }
}
const char* doctrineText(Doctrine d) {
    switch (d) {
        case Doctrine::Fortress:
            return "FORTRESS. Every door is a COMPOSITE lock (a keypad, then a rotor) on a tiny shared budget, there are no "
                   "maintenance ports, and every miss is logged as an alarm ping. Every compute box - and a SIEGE subnet - can "
                   "answer once at each door. Beat it with SIEGE (CRACK), compute, BREAKER and PEEK.";
        case Doctrine::Biosentinel:
            return "BIOSENTINEL. A watchdog on every floor that hunts twice as hungrily, and a sentinel sweep every 12s "
                   "that pings the trace (a frozen trace lets it pass). Big files: this one is a clock. Beat it with JAMMERS "
                   "(they hold the hunt back and soften the sweeps), a STEALTH subnet (BLACKOUT), proxies, FREEZE and BURST.";
        case Doctrine::Loadshed:
            return "LOADSHED. Every 15s the grid sheds load for 3s, and every subnet you run goes dark for it: no pooled relay or "
                   "proxy bonus, no role, no cluster, no strike. The main rack and your deck never notice, and a subnet on HOUSE "
                   "POWER from a tower you own stays lit. Beat it with a strong main rack, BREAKER and UPLINK, and strike between sheds.";
        case Doctrine::Ledgerlock:
            return "LEDGERLOCK. Every file you download is AUDITED the moment it lands: +9% for an objective, +4% for anything else, "
                   "and no jammer softens an audit. A working VAULT escrows files first - every file it would keep on a trace-out "
                   "spares one audit. Beat it with VAULTS (a vault subnet escrows a whole floor), proxies, and by taking only what you need.";
        case Doctrine::Countertrace:
            return "COUNTERTRACE. Every 10s Sable Wren reads your route back: +6%, and 2% more each time one lands. Jammers can't "
                   "soften it and FREEZE doesn't stop it - it follows hops, not the clock. Each working PROXY dead-ends one countertrace "
                   "a night. Beat it with PROXIES (a STEALTH subnet of them), speed, and short visits.";
        case Doctrine::Proving:
            return "PROVING GROUND. HELIOS tests its own doctrines on you, one a floor, at its own trace: FORTRESS on the first, "
                   "BIOSENTINEL on the second, LOADSHED in the Boardroom. Every floor's doctrine is printed on its listing. Bring "
                   "the kit that beat each one - and change it between floors.";
        default: return "";
    }
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
        case Kind::Prototype: return {Job::Gateway, Job::Relay, 4, 70, 4};
        case Kind::Site: return {Job::Relay, Job::Proxy, kMaxTier, 100, 1};   // chairman iron: any job, chosen at the hold (a cage's is fixed)
        default: return {Job::Relay, Job::Proxy, 1, 50, 2};
    }
}

HardwareSpec hardwareFor(const Target& t) {
    HardwareSpec hw = hardwareFor(t.kind);
    if (t.kind == Kind::Prototype) {   // which experiment sits in this lab, fixed per listing
        static const Job pairs[3][2] = {{Job::Gateway, Job::Relay}, {Job::Proxy, Job::Jammer}, {Job::Compute, Job::Vault}};
        int k = ((t.id / 10) % 3 + 3) % 3;
        hw.def = pairs[k][0];
        hw.alt = pairs[k][1];
    }
    return hw;
}

Trait traitFor(Kind k) {
    switch (k) {
        case Kind::Open: return Trait::Salvaged;
        case Kind::Hidden: return Trait::OldNet;
        case Kind::Guarded: return Trait::HotIron;
        case Kind::Corporate: return Trait::CorpBuild;
        case Kind::Prototype: return Trait::Unstable;
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
const std::vector<std::string> kProtoNames = {"HELIOS Testbed LAZARUS", "Black-site ORACLE", "Skunkworks Node VANTA",
                                              "Cold Lab PALISADE", "Prototype Bay 3", "Darkroom MERIDIAN"};
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
// ---------------------------------------------------------------------------
// Act IV - THE LEDGER: corp towers
// ---------------------------------------------------------------------------
struct FloorDef {
    std::string name, dir, blurb;
    std::vector<std::string> objectives;
    LockType lock;
    bool watchdog, honeypot;
    std::string outro;
};
struct TowerDef {
    std::string corp, title, host;
    Doctrine doctrine;
    FloorDef floors[kFloors];
    std::string boardroomMemo;
};

const std::vector<TowerDef>& towers() {
    static const std::vector<TowerDef> v = {
        {"Kessler-Maru", "KESSLER-MARU TOWER", "tower.kessler-maru.corp", Doctrine::Fortress,
         {{"Kessler-Maru: Intake Floor", "/intake", "Where the city's compute contracts are signed. Every door is two doors.",
           {"render_queue.db", "client_roster.enc", "power_contracts.pdf"}, LockType::Composite, false, false,
           "The intake floor goes quiet. Somewhere above you, a door you haven't met yet just learned your name."},
          {"Kessler-Maru: Compute Halls", "/halls", "Rows of rented iron, billing by the minute. Locked like a bank.",
           {"rate_card_2027.xls", "lease_ledger.db", "throttle_policy.pdf"}, LockType::Composite, false, true,
           "The halls hum on without you. You know now what they charge the city for every second of compute."},
          {"Kessler-Maru: The Boardroom", "/board", "The room that sets the rent. It has never been opened from outside.",
           {"board_minutes.enc", "landlord_accord.pdf", "price_floor.db"}, LockType::Composite, true, true,
           "KESSLER-MARU, OWNED. Not the building - the record of what it charges. You read the minutes where they "
           "agreed that prices rise whenever runners succeed. You were on the agenda twice."}},
         "From: chair@kessler-maru\nMotion carried: compute rates track 'unauthorized market activity'.\nWhen runners win, "
         "everyone's rent goes up. Nobody has to say it out loud."},
        {"Tidewater BioSys", "TIDEWATER BIOSYS TOWER", "tower.tidewater-biosys.corp", Doctrine::Biosentinel,
         {{"Tidewater: Wet Labs", "/labs", "Cold rooms and warm servers. Something in the walls is listening.",
           {"trial_cohort.csv", "consent_waivers.pdf", "lab_telemetry.db"}, LockType::Keypad, true, false,
           "The wet labs settle. The sentinels keep sweeping a room you already left."},
          {"Tidewater: Sample Vault", "/vault", "Where the samples go, and the patents that grow from them.",
           {"sample_registry.db", "patent_chain.enc", "insurer_feed.csv"}, LockType::Cipher, true, true,
           "The vault seals behind you. The insurer feed explains every claim the clinics on Kettle Street ever denied."},
          {"Tidewater: The Boardroom", "/board", "The room that decides who gets treated. The sentinels never sleep here.",
           {"board_minutes.enc", "coverage_map.db", "landlord_accord.pdf"}, LockType::Rotor, true, true,
           "TIDEWATER BIOSYS, OWNED. The coverage map is a city map with the poor parts greyed out. The landlord accord "
           "is signed by the same names as Kessler-Maru's. It was always one table."}},
         "From: chair@tidewater-biosys\nCoverage follows rent. Rent follows the accord.\nThe accord has six signatures. "
         "Keep the other four out of the minutes."},
        {"Halden Grid", "HALDEN GRID TOWER", "tower.halden-grid.corp", Doctrine::Loadshed,
         {{"Halden Grid: Dispatch Floor", "/dispatch", "Where the city's power is scheduled - and rationed. The lights flicker on purpose.",
           {"load_schedule.db", "brownout_roster.csv", "meter_override.enc"}, LockType::Keypad, false, true,
           "Dispatch blinks out for three seconds and comes back. You know now which streets they dim first, and why."},
          {"Halden Grid: Substation Control", "/substation", "Breaker halls, a rotor on every door, and a grid that sheds the moment anything strains it.",
           {"shed_priority.db", "tariff_curve.xls", "feeder_map.enc"}, LockType::Rotor, true, true,
           "The substation hums behind you. The feeder map puts every clinic on Kettle Street on the circuit that sheds first - "
           "and every data hall on the one that never does."},
          {"Halden Grid: The Boardroom", "/board", "The room that decides who gets power when there isn't enough. The answer never changes.",
           {"board_minutes.enc", "priority_accord.pdf", "landlord_accord.pdf"}, LockType::Cipher, true, true,
           "HALDEN GRID, OWNED. The priority accord ranks the city by rent paid. The landlord accord is in here too - the third "
           "copy you've read, the same six signatures. The lights at the top table have never once gone out."}},
         "From: chair@halden-grid\nWhen the grid strains, shed the tenants. Never the towers.\nThe accord keeps the top table lit. "
         "So does HELIOS - ask facilities what they keep in the service sites."},
        // ---- the inner chairs (0.10): they list once you own two of the outer three ----
        {"Aurex", "AUREX TOWER", "tower.aurex.corp", Doctrine::Ledgerlock,
         {{"Aurex: Settlement Floor", "/settle", "Every rent in the city clears through this floor. Every byte that leaves it is counted.",
           {"settlement_batch.db", "margin_calls.csv", "dark_pool.enc"}, LockType::Keypad, false, true,
           "Settlement closes its books on you. The margin calls are rent arrears, bundled and sold to the towers you already own."},
          {"Aurex: Clearing House", "/clearing", "Where the city's debts are netted, and nothing is ever written off.",
           {"netting_ledger.db", "collateral_map.enc", "haircut_table.xls"}, LockType::Cipher, true, true,
           "The clearing house balances without you. The collateral map is Kettle Street - every block pledged twice."},
          {"Aurex: The Boardroom", "/board", "The fourth chair. It audits the other three.",
           {"board_minutes.enc", "rent_index.db", "landlord_accord.pdf"}, LockType::Rotor, true, true,
           "AUREX, OWNED. The rent index is the number every landlord in the city raises their rent to. Aurex sets it on a "
           "Tuesday, by vote, and the accord's fourth signature is the chair's own."}},
         "From: chair@aurex\nThe index rises 4% this quarter. It always rises 4%.\nHELIOS keeps our iron in a cage nobody audits. "
         "Keep it that way."},
        {"Sable Wren", "SABLE WREN TOWER", "tower.sable-wren.corp", Doctrine::Countertrace,
         {{"Sable Wren: Listening Post", "/listen", "Private security for the top table. They don't guard doors; they follow footprints home.",
           {"intercept_index.db", "runner_dossiers.enc", "tap_orders.pdf"}, LockType::Cipher, false, true,
           "The listening post keeps listening. One of the dossiers is yours; it's thinner than you'd like and more accurate."},
          {"Sable Wren: Countermeasures", "/counter", "Where every trick a runner ever used is catalogued, and answered.",
           {"trace_heuristics.db", "decoy_registry.csv", "burn_list.enc"}, LockType::Keypad, true, true,
           "Countermeasures goes quiet. The decoy registry lists every machine that ever died for a runner. Yours are in it, by name."},
          {"Sable Wren: The Boardroom", "/board", "The fifth chair. It has never once been traced back to.",
           {"board_minutes.enc", "client_list.enc", "landlord_accord.pdf"}, LockType::Rotor, true, true,
           "SABLE WREN, OWNED. The client list is five names long, and you own four of their buildings. The accord's fifth "
           "signature is on a contract for 'tenant pacification'."}},
         "From: chair@sable-wren\nIf a runner ever reaches this room, the countertrace has failed.\nIt has not failed. "
         "Our iron is caged at HELIOS. Tell nobody where."},
        {"HELIOS", "HELIOS PROVING GROUND", "proving.helios", Doctrine::Proving,
         {{"HELIOS: Trial Hall One", "/trial1", "HELIOS tests its doctrines here first. Today, FORTRESS: two doors in one, no ports.",
           {"countermeasure_specs.db", "doctrine_scores.csv", "runner_models.enc"}, LockType::Composite, false, false,
           "Trial Hall One logs a failed test. The runner models are fitted to you - and they have you finishing about here."},
          {"HELIOS: Trial Hall Two", "/trial2", "BIOSENTINEL, at HELIOS's own pace. The sentinels here are the originals.",
           {"sentinel_weights.db", "sweep_tables.enc", "test_subjects.csv"}, LockType::Rotor, true, true,
           "Trial Hall Two goes dark behind you. The test subjects were runners. The ones before you didn't leave."},
          {"HELIOS: The Boardroom", "/board", "The sixth chair. The grid sheds here too, by design - to see what you keep lit.",
           {"board_minutes.enc", "accord_original.pdf", "succession_plan.enc"}, LockType::Cipher, true, true,
           "HELIOS, OWNED. The original accord is a single page: six chairs, one rule - the runners' wins raise everyone's "
           "rent. The succession plan names whoever takes this room as the seventh chair. The seat is empty. The machines stay on."}},
         "From: chair@helios\nEvery doctrine is ours. Every tower runs one.\nThe Crucible holds the last of the chair iron. "
         "Whoever cages it out has passed."},
    };
    return v;
}

const std::vector<std::string> kTowerPhrases = {"COLDROOM", "BOARDSEAT", "RENTROLL", "CLEANROOM", "IRONLEASE", "TIDEMARK",
                                                "QUIETVOTE", "GLASSDOOR", "SIXCHAIRS", "PRICEFLOOR", "SAMPLEHOLD", "BLINDTRUST"};

std::string floorPhrase(uint64_t seed, int tower, int floor, int key) {
    Rng r(seedOf(seed, 0x70E2000 + static_cast<uint64_t>(tower) * 64 + static_cast<uint64_t>(floor) * 8 + static_cast<uint64_t>(key) * 4096));
    return r.pick(kTowerPhrases);
}

}  // namespace

// PROVING GROUND runs a different outer doctrine on each floor; everywhere else a floor runs its tower's.
Doctrine floorDoctrine(int tower, int floor) {
    Doctrine d = towers()[static_cast<size_t>(tower)].doctrine;
    if (d != Doctrine::Proving) return d;
    static const Doctrine exam[kFloors] = {Doctrine::Fortress, Doctrine::Biosentinel, Doctrine::Loadshed};
    return exam[std::clamp(floor, 0, kFloors - 1)];
}

namespace {

std::string floorHost(int tower, int floor) {
    static const char* sub[kFloors] = {"f1", "f2", "board"};
    return std::string(sub[floor]) + "." + towers()[static_cast<size_t>(tower)].host;
}

// CHAIRMAN IRON (0.9): each tower's custom hardware is serviced off the books at a HELIOS site.
// Black-site names, one word each: the site, and the iron it keeps.
// 0.10: the inner chairs' sites are HELIOS CAGES, and their iron is SIGNATURE grade in one fixed job.
struct SiteDef { const char* site; const char* iron; };
const SiteDef kSites[kTowerCount] = {{"CITADEL", "MONOLITH"}, {"OSSUARY", "LEVIATHAN"}, {"FURNACE", "COLOSSUS"},
                                     {"OUBLIETTE", "ABSOLUTION"}, {"SEPULCHRE", "ECLIPSE"}, {"CRUCIBLE", "PANTHEON"}};
}  // namespace

bool innerTower(int tower) { return tower >= kOuterTowers && tower < kTowerCount; }
Job signatureJob(int tower) { return tower == 3 ? Job::Vault : tower == 4 ? Job::Proxy : Job::Gateway; }
Doctrine siteDoctrine(int tower) {
    Doctrine d = towerDoctrine(tower);
    return d == Doctrine::Proving ? Doctrine::Biosentinel : d;   // the Crucible is HELIOS's own: sentinels
}
int outerOwned(const GameState& g) {
    int n = 0;
    for (int k = 0; k < kOuterTowers; ++k) n += towerOwned(g, k) ? 1 : 0;
    return n;
}
bool innerOpen(const GameState& g) { return ledgerOpen(g) && outerOwned(g) >= kInnerUnlock; }
bool towerListed(const GameState& g, int tower) { return ledgerOpen(g) && tower >= 0 && tower < kTowerCount && (!innerTower(tower) || innerOpen(g)); }
int cageDoorId(int tower) { return kCageDoorBase + tower; }
bool isCage(const Target& t) { return t.kind == Kind::Site && innerTower(t.tower); }
bool isCageCapture(const Target& t) { return isCage(t) && t.id == cageDoorId(t.tower); }
std::string cagePassphrase(const GameState& g, int tower) { return floorPhrase(g.worldSeed, tower, kFloors + 1, g.keyOf(tower)); }

const char* siteName(int tower) { return tower >= 0 && tower < kTowerCount ? kSites[tower].site : "?"; }
const char* ironName(int tower) { return tower >= 0 && tower < kTowerCount ? kSites[tower].iron : "?"; }
int siteId(int tower) { return kSiteIdBase + tower; }
int towerOfSite(int id) {
    if (id >= kSiteIdBase && id < kSiteIdBase + kTowerCount) return id - kSiteIdBase;
    if (id >= kCageDoorBase + kOuterTowers && id < kCageDoorBase + kTowerCount) return id - kCageDoorBase;
    return -1;
}
std::string siteHost(int tower) {
    std::string n = siteName(tower);
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return (innerTower(tower) ? "cage." : "svc.") + n + ".helios";
}
std::string sitePassphrase(const GameState& g, int tower) { return floorPhrase(g.worldSeed, tower, kFloors, g.keyOf(tower)); }
bool siteListed(const GameState& g, int tower) {
    return ledgerOpen(g) && towerOwned(g, tower) && (g.siteOf(tower) == 1 || (innerTower(tower) && g.siteOf(tower) == 3));
}

namespace {
// 0.10 - a HELIOS CAGE: the inner chair's iron, in two runs. siteState 1 = the cage run, 3 = the capture run.
Target makeCage(const GameState& g, int tower) {
    Rng r(seedOf(g.worldSeed, 0xCA6E000 + static_cast<uint64_t>(tower)));
    const bool capture = g.siteOf(tower) == 3;
    Target t;
    t.id = capture ? cageDoorId(tower) : siteId(tower);
    t.kind = Kind::Site;
    t.tower = tower;
    t.floor = -1;
    t.doctrine = siteDoctrine(tower);
    t.security = 5;
    t.traceOverride = bal::CAGE_TRACE;
    t.lock = t.doctrine == Doctrine::Ledgerlock ? LockType::Keypad : t.doctrine == Doctrine::Countertrace ? LockType::Cipher : LockType::Rotor;
    t.watchdog = t.doctrine == Doctrine::Biosentinel;
    t.backdoor = true;
    std::string lower = siteName(tower);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!capture) {
        t.name = std::string("Cage ") + siteName(tower);
        t.host = siteHost(tower);
        t.blurb = std::string("HELIOS cages ") + towerCorp(tower) + "'s signature iron behind two doors. This is the outer one: take the "
                  "schematic and the service key, and the inner door will know you.";
        t.passphrase = sitePassphrase(g, tower);
        t.stealable = false;
        File sch;
        sch.name = "cage_schematic.enc";
        sch.dir = "/cage";
        sch.kind = FileKind::Objective;
        sch.size = bal::CAGE_SCHEMATIC_MB + r.range(-4, 4);
        sch.text = std::string("Cage ") + siteName(tower) + ": wiring, hold timings and the capture door's address (core." + lower + ".helios).";
        t.files.push_back(sch);
        File key;
        key.name = "service_key.txt";
        key.dir = "/cage/svc";
        key.kind = FileKind::Intel;
        key.size = 1;
        key.serviceKey = true;
        key.phraseFor = cageDoorId(tower);
        key.text = std::string("HELIOS FACILITIES - CAGE ") + siteName(tower) + " SERVICE KEY\nThe capture door at core." + lower + ".helios answers to '" +
                   cagePassphrase(g, tower) + "'.\nKey burns on any trace at the inner door. Re-issue from this terminal only.";
        t.files.push_back(key);
    } else {
        t.name = std::string("Cage ") + siteName(tower) + ": Inner Door";
        t.host = "core." + lower + ".helios";
        t.blurb = std::string("The inner door. ") + ironName(tower) + " is behind it, SIGNATURE grade, already imaged as a " + jobName(signatureJob(tower)) +
                  ". The hold is long; the service key's phrase shortens it.";
        t.passphrase = cagePassphrase(g, tower);
        t.stealable = true;
        File man;
        man.name = "cage_manifest.txt";
        man.dir = "/cage";
        man.kind = FileKind::Intel;
        man.size = 1;
        man.text = std::string("HELIOS FACILITIES - CAGE ") + siteName(tower) + "\nUnit: " + ironName(tower) + " (" + towerCorp(tower) +
                   " chair)\nGrade: SIGNATURE. Tier 5. Job: " + jobName(signatureJob(tower)) + ", fixed.\nNot to leave the cage. Not to be discussed.";
        t.files.push_back(man);
    }
    File wo;
    wo.name = "work_order.log";
    wo.dir = "/var/log";
    wo.kind = FileKind::Log;
    wo.size = 1;
    wo.text = "[cage] ticket #" + std::to_string(r.range(50000, 59999)) + ": " + ironName(tower) + " tolerances out of spec again.\n[cage] the chair "
              "says ship it anyway. it isn't safe to run. it was never meant to be safe.";
    t.files.push_back(wo);
    return t;
}
}  // namespace

Target makeSite(const GameState& g, int tower) {
    if (innerTower(tower)) return makeCage(g, tower);
    Rng r(seedOf(g.worldSeed, 0x51E0000 + static_cast<uint64_t>(tower)));
    Target t;
    t.id = siteId(tower);
    t.kind = Kind::Site;
    t.tower = tower;
    t.floor = -1;
    t.doctrine = towerDoctrine(tower);
    t.name = std::string("Service Site ") + siteName(tower);
    t.host = siteHost(tower);
    t.blurb = std::string("Where HELIOS services ") + towerCorp(tower) + "'s chairman iron, off every register. One machine, built for the top table.";
    t.security = 5;
    t.traceOverride = bal::SITE_TRACE;
    t.lock = t.doctrine == Doctrine::Fortress ? LockType::Composite : t.doctrine == Doctrine::Biosentinel ? LockType::Rotor : LockType::Cipher;
    t.watchdog = t.doctrine == Doctrine::Biosentinel;
    t.backdoor = true;   // a service site lives on its maintenance door
    t.passphrase = sitePassphrase(g, tower);
    t.stealable = true;
    File man;
    man.name = "service_manifest.txt";
    man.dir = "/svc";
    man.kind = FileKind::Intel;
    man.size = 1;
    man.text = std::string("HELIOS FACILITIES - SERVICE SITE ") + siteName(tower) + "\nUnit: " + ironName(tower) + " (chair iron, " + towerCorp(tower) +
               ")\nGrade: ACCORD. Tier 5. Job: unassigned - images to order on first boot.\nNot on any asset register. Not to be discussed.";
    t.files.push_back(man);
    File wo;
    wo.name = "work_order.log";
    wo.dir = "/var/log";
    wo.kind = FileKind::Log;
    wo.size = 1;
    wo.text = "[svc] ticket #" + std::to_string(r.range(40000, 49999)) + ": " + ironName(tower) + " burn-in complete.\n[svc] the chair wants it back by quarter end. "
              "nobody tells the chair about the door.";
    t.files.push_back(wo);
    return t;
}

int storyCount() { return static_cast<int>(stories().size()); }
// One new contract per board refresh: nobody posts work for you every night.
bool contractAvailable(const GameState& g) { return g.story < storyCount() && g.mapIndex >= g.story; }
// The deep job surfaces once QUIET FLOOR exposes it, or once Act Three begins - whichever comes first.
// Nothing stops you trying it the moment it appears.
bool deepListed(const GameState& g) { return (g.story >= 3 || g.act >= 3) && !g.deepDone; }
const StoryInfo& storyInfo(int idx) { return stories()[static_cast<size_t>(idx)].info; }

bool ledgerOpen(const GameState& g) { return g.deepDone; }
bool enterLedger(GameState& g) {
    if (!g.deepDone || g.act >= 4) return false;
    g.act = 4;
    if (g.towerFloor.size() < static_cast<size_t>(kTowerCount)) g.towerFloor.resize(kTowerCount, 0);
    if (g.towerKey.size() < static_cast<size_t>(kTowerCount)) g.towerKey.resize(kTowerCount, 0);
    g.journal.push_back("THE LEDGER: Kessler-Maru (FORTRESS), Tidewater BioSys (BIOSENTINEL) and Halden Grid (LOADSHED) are on your board.");
    return true;
}
bool towerOwned(const GameState& g, int tower) { return g.floorsCleared(tower) >= kFloors; }
int towerOfId(int id) { return id >= kTowerIdBase && id < kTowerIdBase + kTowerCount * 10 ? (id - kTowerIdBase) / 10 : -1; }
int floorId(int tower, int floor) { return kTowerIdBase + tower * 10 + floor; }
const char* towerCorp(int tower) { return towers()[static_cast<size_t>(tower)].corp.c_str(); }
const char* towerTitle(int tower) { return towers()[static_cast<size_t>(tower)].title.c_str(); }
Doctrine towerDoctrine(int tower) { return towers()[static_cast<size_t>(tower)].doctrine; }
std::string floorOutro(int tower, int floor) { return towers()[static_cast<size_t>(tower)].floors[floor].outro; }
std::string towerPassphrase(const GameState& g, int tower, int floor) { return floorPhrase(g.worldSeed, tower, floor, g.keyOf(tower)); }

Target makeTowerFloor(const GameState& g, int tower, int floor) {
    const TowerDef& d = towers()[static_cast<size_t>(tower)];
    const FloorDef& f = d.floors[floor];
    Rng r(seedOf(g.worldSeed, 0x70E3000 + static_cast<uint64_t>(tower) * 16 + static_cast<uint64_t>(floor)));
    Target t;
    t.id = floorId(tower, floor);
    t.tower = tower;
    t.floor = floor;
    t.doctrine = floorDoctrine(tower, floor);   // PROVING GROUND: a different one on every floor
    t.kind = Kind::Tower;
    t.name = f.name;
    t.host = floorHost(tower, floor);
    t.blurb = f.blurb;
    t.security = 5;
    const int dz = std::clamp(tower, 0, kTowerCount - 1);
    t.traceOverride = bal::TOWER_TRACE[dz][floor];
    t.lock = f.lock;
    t.watchdog = f.watchdog;
    t.backdoor = t.doctrine != Doctrine::Fortress;   // FORTRESS: the maintenance ports are welded shut
    if (t.backdoor) t.passphrase = towerPassphrase(g, tower, floor);
    t.reward = bal::TOWER_REWARD[innerTower(tower) ? 1 : 0][floor];
    for (auto& n : f.objectives) {
        File o;
        o.name = n;
        o.dir = f.dir;
        o.kind = FileKind::Objective;
        o.size = bal::TOWER_OBJ_MB[dz][floor] + r.range(-2, 2);
        t.files.push_back(o);
    }
    addPaydata(t, r, 2, bal::TOWER_PAY_MULT, sizeMin(5), sizeMax(5));
    if (f.honeypot) addHoneypot(t, r);
    if (floor + 1 < kFloors && !t.backdoor) {   // FORTRESS: nothing to leak, only a grudge
        File memo;
        memo.name = "facilities_up.txt";
        memo.dir = "/home/facilities";
        memo.kind = FileKind::Intel;
        memo.size = 1;
        memo.text = "From: facilities@" + t.host + "\nRequest #" + std::to_string(r.range(2000, 9999)) +
                    " denied again: no maintenance ports on " + floorHost(tower, floor + 1) + ". Bring a bigger rig or don't come up. - K.";
        t.files.push_back(memo);
    } else if (floor + 1 < kFloors) {   // the paper trail upward: the next floor's maintenance phrase
        File memo;
        memo.name = "facilities_up.txt";
        memo.dir = "/home/facilities";
        memo.kind = FileKind::Intel;
        memo.size = 1;
        memo.phraseFor = floorId(tower, floor + 1);
        memo.text = "From: facilities@" + t.host + "\nThe maintenance door at " + floorHost(tower, floor + 1) + " still answers to '" +
                    towerPassphrase(g, tower, floor + 1) + "'.\nRotate it if anybody ever gets traced up here. - K.";
        t.files.push_back(memo);
    } else {
        File memo;
        memo.name = "chair_memo.txt";
        memo.dir = f.dir;
        memo.kind = FileKind::Intel;
        memo.size = 1;
        memo.text = d.boardroomMemo;
        t.files.push_back(memo);
        // CHAIRMAN IRON (0.9): the Boardroom's own maintenance log names the HELIOS service site that keeps the chair's iron.
        File log;
        log.name = "maintenance_log.txt";
        log.dir = "/var/log";
        log.kind = FileKind::Log;
        log.size = 1;
        log.revealsSite = tower;
        if (innerTower(tower))
            log.text = std::string("[helios-cage] signature iron for ") + d.corp + " held at HELIOS CAGE " + siteName(tower) + " (" + siteHost(tower) +
                       ").\n[helios-cage] outer door answers to '" + sitePassphrase(g, tower) + "'. the inner door wants the service key.\n[helios-cage] unit " +
                       ironName(tower) + " is imaged and out of spec. do not run it. do not let anyone run it.";
        else
            log.text = std::string("[helios-svc] chair iron for ") + d.corp + " serviced off-site: HELIOS SERVICE SITE " + siteName(tower) + " (" +
                       siteHost(tower) + ").\n[helios-svc] its maintenance door answers to '" + sitePassphrase(g, tower) +
                       "'. rotate on any trace.\n[helios-svc] unit is unimaged. whoever boots it first decides what it is.";
        t.files.push_back(log);
    }
    addLore(t, r);
    return t;
}

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

    // Sometimes, once you own hardware: a HELIOS black-site with prototype (tier 4) iron.
    // Its own random stream, so the rest of the board is unchanged by it.
    Rng pr(seedOf(g.worldSeed, 0x9707A00 + static_cast<uint64_t>(g.mapIndex)));
    if (g.netUnlocked && g.act >= 2 && g.mapIndex >= 2 && pr.chance(0.45)) {
        Target t;
        t.id = 1000 + g.mapIndex * 10 + 6;
        t.kind = Kind::Prototype;
        t.security = 4;   // 5 makes the 30s anchor hold unsurvivable; the watchdog and bait make it hard enough
        t.name = pr.pick(kProtoNames);
        t.host = slug(t.name) + ".helios";
        t.blurb = "A HELIOS black-site testbed. Tier 4 hardware nobody else has. Everything here wants you caught.";
        t.lock = pr.chance(0.5) ? LockType::Rotor : LockType::Cipher;
        t.watchdog = true;
        t.stealable = true;
        addPaydata(t, pr, 2, 1.2, sizeMin(t.security), sizeMax(t.security));
        addHoneypot(t, pr);
        board.push_back(t);
    }
    // Act IV: the current floor of every tower you haven't owned yet, at the top of the board.
    if (ledgerOpen(g)) {
        std::vector<Target> floors;
        for (int k = 0; k < kTowerCount; ++k)   // 0.10: the inner chairs list once you own two of the outer three
            if (towerListed(g, k) && g.floorsCleared(k) < kFloors) floors.push_back(makeTowerFloor(g, k, g.floorsCleared(k)));
        for (int k = 0; k < kTowerCount; ++k)   // CHAIRMAN IRON: a pinned service site stays until you take its iron
            if (siteListed(g, k)) floors.push_back(makeSite(g, k));
        board.insert(board.begin(), floors.begin(), floors.end());
    }
    return board;
}

Target* findTarget(std::vector<Target>& board, int id) {
    for (auto& t : board) if (t.id == id) return &t;
    return nullptr;
}

const std::vector<std::string>& starterPhrases() { return kPhrases; }
