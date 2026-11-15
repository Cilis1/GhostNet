// GhostNet - every tunable number in one place.
// Values marked [doc] are taken directly from the concept's "THE NUMBERS" section.
#pragma once

namespace bal {

// ---- Rack & archive -------------------------------------------------------
constexpr int MAX_EXTERNAL      = 10;                 // [doc] never more than ten external machines
constexpr int START_SLOTS       = 3;                  // [doc]
constexpr int MAX_SLOTS         = 6;                  // [doc]
constexpr int SLOT_COST[3]      = {350, 650, 1000};   // [doc] 4th, 5th, 6th slot
constexpr int HOME_UPGRADE[2]   = {600, 1000};        // [doc] tier 1->2, 2->3

// ---- Buying & servicing -----------------------------------------------------
// Indexed by Job: Relay, Proxy, Scout, Compute, Jammer, Vault
constexpr int BUY_PRICE[6]      = {400, 500, 450, 550, 425, 475};   // [doc] for the first four
constexpr int BUY_TIER          = 2;                  // [doc] clean machines stop at the middle tier
constexpr int REIMAGINE_FEE     = 125;                // [doc] changing a job
constexpr int REPAIR_PER_POINT  = 2;                  // [doc]
constexpr int SCRAP_BOUGHT_PCT  = 40;                 // a fraction of what you paid
constexpr int SCRAP_CAPTURED[4] = {0, 40, 80, 140};   // small flat amount by tier

// ---- Job effects (index by tier 1..3) ---------------------------------------
constexpr int RELAY_BONUS[4]    = {0, 10, 15, 20};    // [doc] small, ~1.5x, ~2x
constexpr int RELAY_CAP         = 42;                 // [doc] just above double the top-tier value
constexpr int PROXY_BONUS[4]    = {0, 6, 9, 12};      // [doc] smaller, same scaling
constexpr int PROXY_CAP         = 30;                 // [doc] below the relay cap
constexpr int SCOUT_MAX_CHARGES = 3;                  // [doc]
constexpr int COMPUTE_MAX       = 2;                  // [doc]
constexpr int COMPUTE_WEAR[4]   = {0, 14, 9, 5};      // [doc] less at higher tiers, none for home
constexpr int SCOUT_WEAR        = 3;                  // [doc] "a small amount"
// JAMMER: holds back a watchdog's acceleration and softens alarm spikes.
constexpr int JAM_DELAY[4]      = {0, 20, 30, 45};    // seconds before a watchdog starts to ramp
constexpr int JAM_DELAY_CAP     = 60;
constexpr int JAM_SPIKE[4]      = {0, 20, 30, 40};    // % off lockout / wrong-phrase / honeypot spikes
constexpr int JAM_SPIKE_CAP     = 50;
// VAULT: on a trace-out, the best working vault keeps part of your single most valuable file.
constexpr int VAULT_KEEP[4]     = {0, 60, 80, 100};   // % of that one file's value

// ---- Origin traits (fixed for life) -------------------------------------------
constexpr int SALVAGED_EXTRA_WEAR  = 2;               // wears a little faster...
constexpr int SALVAGED_REPAIR      = 1;               // ...but repairs cost 1cr a point, not 2
constexpr int OLDNET_FLOOR         = 30;              // normal wear never takes it below this
constexpr int OLDNET_REJOB_MULT    = 2;               // old firmware fights a re-image
constexpr int HOTIRON_UPKEEP_PCT   = 10;              // HELIOS wants it back: louder on the bill...
constexpr int CORP_UPKEEP_PCT      = 20;              // corporate build: bills more...
constexpr int CORP_WEAR_PCT        = 60;              // ...and wears at 60% of the normal rate

// ---- Upkeep -----------------------------------------------------------------
// [doc] tier cost + surcharge per signal step above the minimum, up to three steps.
// Signal 1 = minimum (all bought machines). Captured hardware is always louder.
//   clean T2 = 20cr   open T1 capture = 25cr   watched T2 = 50cr   corporate T3 = 90cr
constexpr int TIER_UPKEEP[4]    = {0, 10, 20, 45};
constexpr int NOISE_UPKEEP      = 15;
constexpr int MAX_NOISE         = 4;

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

}  // namespace bal
