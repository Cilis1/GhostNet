// GhostNet - every tunable number in one place.
// Values marked [doc] are taken directly from the concept's "THE NUMBERS" section.
#pragma once

namespace bal {

// ---- Rack & archive -------------------------------------------------------
constexpr int MAX_EXTERNAL      = 14;                 // [doc] said ten; 0.5 adds two 3-slot subnet bays
constexpr int START_SLOTS       = 3;                  // [doc]
constexpr int MAX_SLOTS         = 6;                  // [doc]
constexpr int SLOT_COST[3]      = {350, 650, 1000};   // [doc] 4th, 5th, 6th slot
constexpr int HOME_UPGRADE[2]   = {600, 1000};        // [doc] tier 1->2, 2->3

// ---- Buying & servicing -----------------------------------------------------
// Indexed by Job: Relay, Proxy, Scout, Compute, Jammer, Vault, Gateway
constexpr int BUY_PRICE[7]      = {400, 500, 450, 550, 425, 475, 700};   // [doc] for the first four
constexpr int BUY_TIER          = 2;                  // [doc] clean machines stop at the middle tier
constexpr int REIMAGINE_FEE     = 125;                // [doc] changing a job
constexpr int REPAIR_PER_POINT  = 2;                  // [doc]
constexpr int SCRAP_BOUGHT_PCT  = 40;                 // a fraction of what you paid
constexpr int SCRAP_CAPTURED[6] = {0, 40, 80, 140, 260, 260};   // small flat amount by tier

// ---- Job effects (index by tier 1..5; T4 = prototype, T5 = a cluster of T4s) ---
constexpr int RELAY_BONUS[6]    = {0, 10, 15, 20, 26, 32};    // [doc] small, ~1.5x, ~2x for 1..3
constexpr int RELAY_CAP         = 42;                 // [doc] just above double the top-tier value
constexpr int PROXY_BONUS[6]    = {0, 6, 9, 12, 16, 20};      // [doc] smaller, same scaling for 1..3
constexpr int PROXY_CAP         = 30;                 // [doc] below the relay cap
constexpr int SCOUT_MAX_CHARGES = 3;                  // [doc]
constexpr int COMPUTE_MAX       = 2;                  // [doc]
constexpr int COMPUTE_WEAR[6]   = {0, 14, 9, 5, 3, 2};      // [doc] less at higher tiers, none for home
constexpr int SCOUT_WEAR        = 3;                  // [doc] "a small amount"
// JAMMER: holds back a watchdog's acceleration and softens alarm spikes.
constexpr int JAM_DELAY[6]      = {0, 20, 30, 45, 60, 75};    // seconds before a watchdog starts to ramp
constexpr int JAM_DELAY_CAP     = 75;
constexpr int JAM_SPIKE[6]      = {0, 20, 30, 40, 50, 60};    // % off lockout / wrong-phrase / honeypot spikes
constexpr int JAM_SPIKE_CAP     = 60;
// VAULT: on a trace-out, the best working vault keeps part of your single most valuable file.
constexpr int VAULT_KEEP[6]     = {0, 60, 80, 100, 100, 100};   // % of each kept file's value
constexpr int VAULT_FILES[6]    = {0, 1, 1, 1, 2, 3};           // how many of your best files it keeps
constexpr int SCOUT_CHARGES[6]  = {0, 1, 1, 1, 2, 2};           // charges each scout adds

// ---- Origin traits (fixed for life) -------------------------------------------
constexpr int SALVAGED_EXTRA_WEAR  = 2;               // wears a little faster...
constexpr int SALVAGED_REPAIR      = 1;               // ...but repairs cost 1cr a point, not 2
constexpr int OLDNET_FLOOR         = 30;              // normal wear never takes it below this
constexpr int OLDNET_REJOB_MULT    = 2;               // old firmware fights a re-image
constexpr int HOTIRON_UPKEEP_PCT   = 10;              // HELIOS wants it back: louder on the bill...
constexpr int CORP_UPKEEP_PCT      = 20;              // corporate build: bills more...
constexpr int CORP_WEAR_PCT        = 60;              // ...and wears at 60% of the normal rate
constexpr int UNSTABLE_WEAR_MULT   = 2;               // prototype iron: wears double...
constexpr int UNSTABLE_REPAIR      = 3;               // ...repairs cost 3cr a point...
constexpr int UNSTABLE_UPKEEP_PCT  = 40;              // ...and bills 40% more

// ---- Subnets ----------------------------------------------------------------
constexpr int MAX_BAYS          = 2;                  // working gateways that open a bay
constexpr int BAY_SLOTS         = 3;
constexpr int POWER_DRAW[6]     = {0, 1, 2, 3, 5, 5};          // by a member's real tier
constexpr int GATEWAY_POWER[6]  = {0, 5, 7, 9, 12, 12};        // a bay's budget, by gateway tier
constexpr int BAY_RELAY_CAP     = 30;                 // each bay has its own caps...
constexpr int BAY_PROXY_CAP     = 18;
constexpr int RELAY_HARD_CAP    = 90;                 // ...under a hard ceiling for everything
constexpr int PROXY_HARD_CAP    = 50;
constexpr int SPEED_BONUS[6]    = {0, 8, 12, 16, 22, 22};      // SPEED role: + download %, by gateway tier
constexpr int STEALTH_BONUS[6]  = {0, 4, 6, 8, 11, 11};        // STEALTH role: - trace %
constexpr int SIEGE_SPIKE[6]    = {0, 10, 15, 20, 25, 25};     // SIEGE role: % off alarm spikes (+1 emergency attempt)
constexpr int STRIKE_WEAR       = 8;                  // every machine in the bay, after the run
constexpr int STRIKE_BLACKOUT   = 8;                  // STEALTH strike: seconds the trace stands still
constexpr int STRIKE_MIRROR     = 12;                 // SPEED strike: download seconds done instantly
constexpr int STRIKE_CRACK      = 2;                  // SIEGE strike: lock boxes revealed

// ---- Upkeep -----------------------------------------------------------------
// [doc] tier cost + surcharge per signal step above the minimum, up to three steps.
// Signal 1 = minimum (all bought machines). Captured hardware is always louder.
//   clean T2 = 20cr   open T1 capture = 25cr   watched T2 = 50cr   corporate T3 = 90cr
constexpr int TIER_UPKEEP[6]    = {0, 10, 20, 45, 80, 80};
constexpr int NOISE_UPKEEP      = 15;
constexpr int MAX_NOISE         = 4;

// ---- Running lean (0.8) -----------------------------------------------------
// SIGNAL SCRUB: a paid, permanent step down in noise. It costs what it saves in this many billed runs,
// and captured hardware never gets quieter than SCRUB_FLOOR_CAPTURED (capturing stays the loud way in).
constexpr int SCRUB_PAYBACK_RUNS   = 25;
constexpr int SCRUB_FLOOR_CAPTURED = 2;
// HOUSE POWER: each tower you own runs one subnet on its own power - that subnet bills at half (rounded up).
constexpr int HOUSE_POWER_PCT      = 50;
// STANDBY SUGGESTED: LEDGER only proposes a machine that does nothing tonight, or one this loud on a job that won't miss it.
constexpr int STANDBY_SUGGEST_MIN  = 35;

// ---- Wear -------------------------------------------------------------------
constexpr int WEAR_NORMAL_BOUGHT    = 4;
constexpr int WEAR_NORMAL_CAPTURED  = 5;
constexpr int WEAR_TRACED_EXTRA_BOUGHT   = 12;        // [doc] ~3x the normal amount on top
constexpr int WEAR_TRACED_EXTRA_CAPTURED = 18;        // [doc] more for captured hardware

// ---- The connection ---------------------------------------------------------
constexpr double BASE_SPEED_MBPS = 4.0;
constexpr double TRACE_RATE[6]   = {0, 1.0, 1.5, 2.0, 2.8, 3.6};   // %/s by security
constexpr double DEEP_TRACE_RATE = 4.4;               // the deep job: tuned past any deck
constexpr double DOWNLOAD_TRACE_MULT = 1.25;          // [doc] downloading makes you easier to find
constexpr int    COST_LS         = 1;                 // seconds
constexpr int    COST_CAT        = 3;                 // reduced by LENS
constexpr int    COST_GUESS      = 2;
constexpr int    COST_PHRASE     = 2;
constexpr int    COST_EXTRA      = 1;
constexpr double LOCKOUT_SPIKE   = 10.0;
constexpr double WRONG_PHRASE_SPIKE = 5.0;
constexpr double HONEYPOT_SPIKE  = 25.0;
constexpr double ANCHOR_TRACE_MULT = 1.5;             // [doc] noticeably easier to find
constexpr int    ANCHOR_FLOOR    = 5;                 // [doc] never less than ~5 seconds
constexpr double WATCHDOG_RAMP   = 0.5;               // +50% trace rate per minute connected
constexpr int    RUNS_PER_MAP    = 3;                 // board refresh cadence

// ---- Capture ----------------------------------------------------------------
constexpr int CAPTURE_PER_FILE   = 10;                // [doc] ten points per file already taken
constexpr int CAPTURE_FLOOR      = 20;
constexpr int CAPTURE_BAIT_HIT   = 15;                // triggered bait file: worse...
                                                      // ...and +1 signal step

// ---- Money ------------------------------------------------------------------
constexpr int    FINE_PER_SECURITY = 110;
constexpr int    START_CREDITS   = 150;
constexpr int    PAY_BY_SECURITY[6] = {0, 60, 100, 160, 230, 320};   // per payout file (avg)

// ---- Your own deck (tools you buy for yourself; separate from GhostNet) -----
constexpr int    DECK_MAX        = 3;
constexpr int    BREAKER_COST[3] = {450, 900, 1600};  // more attempts: +1 guess per level
constexpr int    UPLINK_COST[3]  = {400, 850, 1500};  // better tools: +15% base download per level
constexpr int    SHROUD_COST[3]  = {500, 1000, 1800}; // quieter connection: -7% trace, -15% fines
constexpr int    LENS_MAX        = 2;
constexpr int    LENS_COST[2]    = {300, 700};        // faster reading: -1s per file read
constexpr double UPLINK_STEP     = 0.15;
constexpr double SHROUD_TRACE    = 0.07;
constexpr double SHROUD_FINE     = 0.15;

// ---- Deck programs (single use, bought between runs) ---------------------------
// Indexed by Program: None, Freeze, Peek, Spoof, Burst
constexpr int    PROGRAM_COST[5] = {0, 120, 90, 80, 70};
constexpr int    FREEZE_SECS     = 4;                 // the trace stands still
constexpr int    BURST_SECS      = 5;                 // download seconds at double speed
constexpr double BURST_TRACE_MULT = 1.5;              // ...while the trace climbs faster

// ---- Act IV - THE LEDGER: corp towers (0.6) ---------------------------------------
// Indexed by [doctrine - 1][floor]: FORTRESS is a wall at the door, BIOSENTINEL a clock inside.
constexpr double TOWER_TRACE[2][3]  = {{3.0, 3.3, 3.6}, {3.6, 3.9, 4.2}};   // %/s; the deep job is 4.4
constexpr int    TOWER_OBJ_MB[2][3] = {{30, 35, 40}, {60, 70, 80}};         // each of a floor's three objective files
constexpr int    TOWER_REWARD[3]    = {1500, 2500, 9000};  // paid for the floor's objectives; the Boardroom is the jackpot
constexpr double TOWER_PAY_MULT  = 2.5;               // paydata vs. an ordinary security-5 file
constexpr int    PREP_PLAN_COST  = 350;               // FLOOR PLAN: a full-quality report, no scout needed
constexpr int    PREP_PHRASE_COST[3] = {700, 1000, 1400};   // a bought maintenance phrase (BIOSENTINEL: FORTRESS has no ports)
// FORTRESS (Kessler-Maru): a composite lock on a tiny shared budget, no maintenance ports; misses are logged.
constexpr int    FORTRESS_GUESSES   = 1;              // keypad + rotor share this, + BREAKER, + one per compute box / SIEGE subnet
constexpr double FORTRESS_MISS_SPIKE = 4.0;           // every wrong guess pings the trace (jammers/SIEGE cut it)
// 0.7: the code persists per floor until a re-key. Misses fill an ALARM LOG; one more miss on a full log
// seals the door and re-keys that floor. Never timed: only your own misses fill it.
constexpr int    FORTRESS_ALARM_LOG = 6;
constexpr int    FORTRESS_ROWS_MAX  = FORTRESS_ALARM_LOG + 2;   // + the keypad's code and the rotor's, once found
constexpr int    PREP_SURVEY_COST[3] = {600, 900, 1200};  // TUMBLER SURVEY: names the keypad's four digits, unordered
// BIOSENTINEL (Tidewater BioSys): a hungrier watchdog and regular sentinel sweeps.
constexpr double BIO_RAMP        = 1.0;               // +100% trace rate per minute (a normal watchdog is +50%)
constexpr int    BIO_SWEEP_EVERY = 12;                // seconds between sweeps (jammers soften them; they don't delay them)
constexpr double BIO_SWEEP_SPIKE = 6.0;               // a sweep's ping; it passes over a frozen trace

}  // namespace bal
