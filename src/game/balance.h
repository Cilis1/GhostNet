// GhostNet - every tunable number in one place.
// Part Thirteen of the design doc: "Build the numbers and the bill before you
// build anything beautiful." If the game feels wrong, start here.
#pragma once

namespace bal {

// ---- Rack & archive -------------------------------------------------------
constexpr int MAX_EXTERNAL      = 10;   // external machines you may own
constexpr int START_SLOTS       = 3;    // active slots at the start (home counts)
constexpr int MAX_SLOTS         = 6;
constexpr int SLOT_COST[3]      = {150, 450, 1000};   // cheap, expensive, serious
constexpr int HOME_UPGRADE[2]   = {300, 750};         // tier 1->2, 2->3

// ---- Buying & servicing -----------------------------------------------------
// Indexed by Job: Relay, Proxy, Scout, Compute
constexpr int BUY_PRICE[4]      = {120, 150, 100, 220};
constexpr int BUY_TIER          = 2;    // clean machines top out at the middle tier
constexpr int REIMAGINE_FEE     = 40;
constexpr int REPAIR_PER_POINT  = 1;    // credits per condition point
constexpr int SCRAP_BOUGHT_PCT  = 35;   // % of purchase price
constexpr int SCRAP_CAPTURED[4] = {0, 10, 20, 35};    // flat, by tier

// ---- Job effects (index by tier 1..3) ---------------------------------------
constexpr int RELAY_BONUS[4]    = {0, 10, 15, 20};    // % download speed
constexpr int RELAY_CAP         = 40;
constexpr int PROXY_BONUS[4]    = {0, 6, 9, 12};      // % trace-rate reduction
constexpr int PROXY_CAP         = 30;                 // lower than relay cap: time > speed
constexpr int SCOUT_MAX_CHARGES = 3;
constexpr int COMPUTE_MAX       = 2;                  // emergency attempts per run
constexpr int COMPUTE_WEAR[4]   = {0, 14, 9, 5};      // better machines wear less
constexpr int SCOUT_WEAR        = 4;

// ---- Upkeep ---------------------------------------------------------------
// upkeep = TIER_UPKEEP[tier] + (noise-1) * NOISE_UPKEEP
// clean T2 (noise 1) = 5cr ; trapped corporate T3 (noise 6) = 25cr  (5x)
constexpr int TIER_UPKEEP[4]    = {0, 2, 5, 10};
constexpr int NOISE_UPKEEP      = 3;
constexpr int MAX_NOISE         = 6;

// ---- Wear -----------------------------------------------------------------
constexpr int WEAR_NORMAL_BOUGHT    = 5;
constexpr int WEAR_NORMAL_CAPTURED  = 7;
constexpr int WEAR_TRACED_BOUGHT    = 18;
constexpr int WEAR_TRACED_CAPTURED  = 28;

// ---- Runs -----------------------------------------------------------------
constexpr double BASE_SPEED_MBPS = 4.0;               // download speed before bonuses
constexpr double TRACE_RATE[6]   = {0, 1.0, 1.5, 2.0, 2.8, 3.6}; // %/s by security
constexpr int    COST_LS         = 1;                 // seconds of connection time
constexpr int    COST_CAT        = 2;
constexpr int    COST_GUESS      = 2;
constexpr int    COST_PHRASE     = 2;
constexpr int    COST_EXTRA      = 1;
constexpr double LOCKOUT_SPIKE   = 10.0;              // trace % when a lock seals
constexpr double WRONG_PHRASE_SPIKE = 5.0;
constexpr double HONEYPOT_SPIKE  = 25.0;
constexpr double ANCHOR_TRACE_MULT = 1.5;             // easier to find while anchoring
constexpr double WATCHDOG_RAMP   = 0.5;               // +50% trace rate per minute connected
constexpr int    RUNS_PER_MAP    = 3;                 // map refresh cadence

constexpr int    FINE_PER_SECURITY = 45;
constexpr int    START_CREDITS   = 50;

// ---- Your own deck (tools you buy for yourself; separate from GhostNet) ----
constexpr int    DECK_MAX        = 3;
constexpr int    BREAKER_COST[3] = {200, 450, 900};   // +1 lock guess per level
constexpr int    UPLINK_COST[3]  = {180, 400, 850};   // +15% base download per level
constexpr int    SHROUD_COST[3]  = {220, 500, 1000};  // -7% trace, -15% fines per level
constexpr double UPLINK_STEP     = 0.15;
constexpr double SHROUD_TRACE    = 0.07;
constexpr double SHROUD_FINE     = 0.15;

}  // namespace bal
