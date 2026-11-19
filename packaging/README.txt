GHOSTNET
stop renting. start owning.
=====================================================================

START
  Double-click GhostNet.exe (or use the Start Menu shortcut "GhostNet").
  It opens in its own window. F11 = borderless fullscreen, Esc = menu,
  M on the title screen = sound on/off. Everything is clickable; at a lock
  you can also just type and press Enter.
  New to the locks? Try the PRACTICE RANGE on the title screen first.

THE CITY
  Server space is real estate and you own none of it. You rent THE RACK, a room
  that used to belong to a courier called SAINT. HELIOS keeps the city's
  machines running; the TRACE meter is how long you've been an open ticket.

THE LOOP  (the rack -> the board -> the connection -> the reckoning)
  1. Open the JOB BOARD, pick a listing and read the briefing: scan it if you
     have a scout running, and hear what the Panel thinks.
  2. CONNECT. The trace fills constantly. Reading costs time. Downloading costs
     time and makes you easier to find.
  3. Open the door: solve the lock in limited guesses, or TRY PHRASE if you've
     read the maintenance passphrase somewhere.
  4. Inside: OPEN reads a file, GET downloads it, CAPTURE HARDWARE takes the
     server's machine (if your scouts flagged it).
  5. JACK OUT before the trace fills. If it fills, your accounts are seized:
     the night's haul is lost and a fine comes out of what you have.

LOCKS
  KEYPAD  4 different digits, judged box by box. Green = right box; amber = in
          the code, another box; dark = not in the code.
  ROTOR   4 digits judged per wheel. Amber "+-1" = ONE NOTCH AWAY (not misplaced!).
  CIPHER  5-letter word. Green = right place, amber = elsewhere in the word.
  COMPOSITE  (Act IV) a KEYPAD, then a ROTOR behind it, on one guess budget.
          Once the keypad opens, every rotor wheel is within 2 notches of the
          keypad digit in the same box. The screen shows each box's range.

THE DECK
  Tools you buy for yourself: more attempts, better tools, a quieter
  connection, faster reading. Permanent, never billed.
  PROGRAMS: two single-use slots - FREEZE, PEEK, SPOOF, BURST. One per lock.

GHOSTNET (after your first contract)
  SAINT's salvaged rack and your home server, N0 (never billed, never worn).
  RELAY speeds downloads, PROXY slows the trace, SCOUT gives reports,
  COMPUTE holds an emergency extra guess (EMERGENCY +1 at a lock),
  JAMMER holds watchdogs back and softens alarms, VAULT keeps your best file
  if you're traced. Captured machines carry an origin trait for life.
  SUBNETS: a GATEWAY opens 3 extra slots with their own power, bill and role
  (STEALTH, SPEED, SIEGE). Once per run each subnet can STRIKE (BLACKOUT,
  MIRROR, CRACK) - it wears every machine in it. Three matching T3+ machines
  in one subnet act a tier higher while together. Rare HELIOS PROTOTYPE labs
  hold the only real tier 4: UNSTABLE, loud and costly.
  Running machines bill you after a run and wear. Can't pay? Nothing is taken:
  everything except N0 goes dark. Arm a PROXY as the DECOY and if the trace
  fills, HELIOS traces it instead of you - it is destroyed, the fine is halved.

THE DEEP JOB
  A HELIOS archive tuned so no deck alone can finish it. Try it whenever it
  appears; if you fall short, the game tells you by how much.

THE LEDGER (Act IV - after the owner ending)
  Two corp towers, three floors each; the Boardroom makes the tower yours.
  You start every climb. Nothing is timed and taken floors stay taken.
  Each tower's DOCTRINE is announced up front:
    KESSLER-MARU (FORTRESS): composite locks, no maintenance ports, one guess
      per door (+Breaker), each compute box answers once, misses ping (+4%).
      Since 0.7 a floor KEEPS ITS CODE between connections: earlier attempts
      return as LOGGED rows that cost nothing, a found keypad shows KEYPAD
      KNOWN (1 guess, never a miss), the rotor shows arrows (up = wheel higher),
      and CODES LEFT counts what still fits. 6 misses fill the ALARM LOG; the
      7th seals the door and re-keys that floor. A trace re-keys them all.
    TIDEWATER BIOSYS (BIOSENTINEL): watchdogs everywhere and a sentinel sweep
      every 12s (+6%; jammers soften it, FREEZE lets one pass). Each floor's
      memo names the next floor's maintenance phrase.
  HEIST PREP, paid once and kept: FLOOR PLAN 350cr, PHRASE 700-1400cr,
  TUMBLER SURVEY 600-1200cr (Kessler: the keypad's digits, unordered).
  PRACTICE RANGE > FORTRESS DRILL: a Kessler door on your real budget.
  Get traced in a tower and it RE-KEYS: your phrases for it go stale.

THE PANEL
  VESPER (risk), LEDGER (money) and MOTH (intel) are advisor AIs in your deck.
  They read your meters and vote before every run. They never solve locks.

SAVES
  %APPDATA%\GhostNet\ghostnet_save.txt  (kept when you uninstall or update;
  saves from 0.2.x carry straight over). If a save can't replace the old
  one (a virus scanner holding the file), the previous save is never
  deleted; the new one waits in ghostnet_save.txt.tmp and loads next time.

TROUBLESHOOTING
  Clicks not registering? Update to 0.3.1 or newer (run GhostNet-Setup.exe
  again); 0.3.0 mis-mapped the mouse on displays scaled above 100%.
  Window won't open? Update your graphics driver (OpenGL 3.3 is needed).
  Every start writes %APPDATA%\GhostNet\ghostnet.log - send it with bug reports.

UPDATING / UNINSTALLING
  Run GhostNet-Setup.exe again to update or repair.
  GhostNet-Setup.exe --uninstall removes the game (your saves stay).

CREDITS
  Built on raylib. Fonts: Orbitron and JetBrains Mono (SIL Open Font License).
  See THIRD-PARTY-NOTICES.txt.

Source and releases: https://github.com/Cilis1/GhostNet
