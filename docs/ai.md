# The original computer player (WarCraft 1.20)

How the original game's computer opponent works, worked out from a
decompilation of WarCraft: Orcs & Humans "FLOPPY VERSION 1.20" (WAR.EXE,
sha256 c9254c34…) with warcraft1-rebirth's tools, read instruction by
instruction and checked by tracing a running game. This is a description of
behaviour in our own words, for reimplementing it in war1
(`third_party/war1/src/war_ai.c`). No code or data from the game is copied
here; addresses are given only so the analysis can be re-checked.

Supporting sources: the community "Insider's Guide" (blizzardarchive,
section 1.6), which describes the same behaviour from play, and war1gus's
campaign scripts for the per-mission objectives.

## Where it lives

- One game tick (`0x197B0`) runs, among others, the per-unit update
  (`0x2ECFC`) and the AI clock (`0x29AF8`).
- The per-unit update runs each unit's animation script, then its current
  **order** (a byte in the unit record) through an order-handler table.
  Order 2, "idle/stand", first calls the **think** routine (`0x10EE8`) for
  every idle unit; for computer players that is where all decisions start.
- A player is a computer player when its byte in the level's **alliance
  table** is 1 (level header offset 0x88, one byte per player; war1tool
  called it "mystery"). The human player is player 0.

## Everything tunable is in the level header (DATA.WAR)

The level header is loaded as-is, so its fields are the AI's parameters.
Offsets are into the level-info resource:

| Offset | Size | Meaning |
|---|---|---|
| 0x00 | u32 | allowed units/buildings/spells (bit order in war1tool) — also limits what the AI trains |
| 0x04 | 10 × 5 u8 | upgrade levels per group and player — the AI researches up to these |
| 0x36 | 19 × u16 | **production interval** per building type (0x20…0x32), in AI ticks (see below) |
| 0x5C / 0x70 | 5 × u32 | lumber / gold per player |
| 0x84 | u32 | race |
| 0x88 | 5 × u8 | alliance: 1 = computer player hostile to the human |
| 0x8E | u16 | **AI flags** (see "Attack waves") |
| 0x96 | 5 × u16 | **worker limit** per player: the town hall trains workers up to it |

## The think routine, per idle computer unit

1. Anything hostile in reach: engage it (shared with the human player's
   auto-attack).
2. **Workers** → the economy (below).
3. **Buildings** that can produce: count down their production timer; when
   it runs out, produce (below) and reload the timer from the level's
   production interval for that building type.
4. **Spellcasters** (conjurer, warlock, cleric, necrolyte, Medivh) → the
   spell logic.
5. **Heroes** (Lothar, wounded, Griselda, Garona) do nothing on their own.
6. Other units → the attack/guard logic, by their role.

## Production is free

A computer building's production goes through the same train/research
routine the player's buttons use, but with "pay" switched off: **the
computer never pays for units or upgrades**. Its gold and lumber still go up
from its workers' deliveries (100 per load), and they are spent on one
thing only: rebuilding (below). Per building type:

| Building | Produces, each time its timer runs out |
|---|---|
| Barracks | one soldier chosen at random with weights: footman/grunt 4, archer/spearman 6, catapult 2, knight/raider 8 — only types the level allows; archers need a lumber mill, catapults and knights a blacksmith (owned by a computer player) |
| Church / temple | a cleric / necrolyte, while casters ≤ min(farms, 4) |
| Tower | a conjurer / warlock, while casters ≤ min(farms, 4) |
| Town hall (and the two keeps) | a worker, while workers < the level's worker limit |
| Lumber mill | the next arrows/spears level, up to the level's maximum |
| Stable / kennel | the next horses/wolves level, up to the maximum |
| Blacksmith | swords/axes or shields, whichever is lower, up to the maximum |

## Economy: workers

Each computer worker has a role: gold miner or builder. On every delivery
(and whenever idle) a worker:

- still carrying a load and the town hall exists → goes to deliver it;
- otherwise, while builders < 2/3 × (workers − 1) (or always, if it could
  not deliver), looks for **town-plan work** (below) and takes it;
- otherwise mines gold (keeping about the rest on gold), else chops wood,
  else returns home.

Mines give a computer worker only 5 gold per trip from the mine itself,
though the player is credited 100 per delivery.

## The town plan: rebuilding and repairing

The computer never founds new buildings. Its starting buildings are its
plan: a destroyed one stays as a placeholder at its spot. A worker looking
for work picks, among its placeholders and its damaged buildings (under
3/4 hit points), the one with the highest priority:

| Building | Rebuild | Repair |
|---|---|---|
| Town hall | 120 | 45 |
| Farm | 50 | 45 |
| Barracks, church/temple, tower | 40 | 35 |
| Lumber mill, stable/kennel, blacksmith | 20 | 15 |
| Stormwind Keep / Blackrock Spire | never | 50 |

With less than 200 gold and 100 lumber only the town hall is considered.
Rebuilding is **paid** at the normal price (the town hall is free); a site
that is not clear, or not enough gold, is retried later. The building is
recreated in place under construction and the worker sent to build it.

## Roles of the other units

Every new computer unit gets a role when created:

- monsters (ogres, spiders, …) placed with the level: **guard** their spot;
  created later: **attack**;
- soldiers: guard if AI flag bit 0, otherwise assigned to a defence post
  (or kept at home);
- catapults: kept home except on two specific levels;
- a town hall or keep marks the player's home position.

## Attack waves (the AI clock)

Every AI tick:

- a wave or an all-out switches every mobile unit of the player that is
  not a hero (types 0x0E–0x12) to attack — guards and posts included
  (`0x29a44`);
- every 800 ticks the computer **re-aims**: the map is split into 4×4
  zones of 16×16 tiles; each zone is scored by the human's presence
  (building 4, unit 1); the centre of the best zone is the attack target.
  It always knows where you are — no scouting.
- a wave timer runs down; when it runs out, each computer player's
  **wave quota** of mobile units (not heroes) is switched to "attack" and
  the quota resets. The timer is reloaded from the AI flags: bits 0x84 →
  350 ticks, bit 0x40 → 6000 ticks, neither → the computer never attacks.

- besides the timer, four events send a player **all-out** (quota "all",
  wave on the next tick):
  - one of its buildings is destroyed (a keep: all three computer players);
  - a neutral unit is rescued by the human — unless it is a worker
    (`0x14084`: a player-4 unit with a human unit in the 3×3 around it
    joins the human; a rescued wounded soldier becomes an archer);
  - Griselda dies (her player).

### Attack routes and gathering

The level carries the computer's attack routes, in the level-info data right
after the header's message records (header 0xDC, then 3-byte records up to
a 0, then the route table):

- a choice list of (weight in %, route) pairs, ending 0xFFFF;
- the routes: waypoints of (count, x, y) in tiles, each route ending
  0xFFFF, the table ending with a second 0xFFFF. The count is live state:
  how many units wait at that waypoint.

A soldier that is not a guard picks a route at random by weight, walks to
its first waypoint and waits there (`0x29990`; a unit word at +0x14 holds
the waypoint index, bit 0x8000 "on the way to gather", bit 0x4000
"waiting"). When the units waiting there reach the **group size**, they
all move on (`0x29908`), and from then on walk the rest of the route
**without stopping**: only the first waypoint gathers. Within 4 tiles of a
waypoint counts as arrived. At the end of the route they become attackers;
spellcasters (types 0x0A–0x0E) stop one waypoint short and stay there
(`0x29868`). The group size is one global value: it starts at header 0xA2
and grows by one every N groups (header 0xAC), up to header 0xC0 — the
first word of each of those per-player arrays; the count of groups sent is
kept in header 0xB6. Example, human mission 5: routes 70 % / 30 % with 6 and
7 waypoints, groups of 1 growing every 3 groups to 6; human mission 12:
groups of 4 growing every 2 to 12.

A new soldier of a type that held a post at the start of the level takes
over the post of a dead one of its type (3 times in 4).

### What attackers and guards do

- Attackers walk (attack-move) to the target zone; on the way and at the
  target they fight whatever comes within 8 tiles, choosing by priority:

  | Target | Priority |
  |---|---|
  | catapult | 105 |
  | worker | 60 |
  | conjurer/warlock | 55 |
  | cleric/necrolyte | 50 |
  | knight/raider | 45 |
  | monsters | 41 (slime, brigand 35; skeleton 30) |
  | footman, grunt, archer, spearman | 40 |
  | keep 25, town hall 22, farm 20, barracks/church/tower 18, stable 12, blacksmith 10, lumber mill 8 | buildings |
  | heroes | 1 |

- Guards react within 4 tiles; catapults within their range + 2.
- When the town hall is damaged, units within 30 tiles of home turn on
  any enemy within 10 tiles of it.
- Units with nothing to do return to their post, or wander near home.

### Spellcasters

Mana thresholds per spell; research is taken from the computer's (player 1)
column of the level's upgrade table:

| Caster | Casts |
|---|---|
| cleric | invisibility on an attacker (mana ≥ 125), else heals a wounded unit (≥ 3) |
| conjurer | at full mana: water elemental (else scorpions); else rain of fire on enemies (≥ 150) |
| necrolyte | unholy armour (≥ 125), else raise dead (≥ 70) |
| warlock | at full mana: daemon (else spiders); else poison cloud (≥ 50) |
| Medivh | at full mana: daemon; at ≥ 60: spiders |

## Timing

One AI tick is one game tick. The game's speed setting (default "normal",
setting 3 of 0–5) gives 14.3, 18.2, 25, **33.3**, 50 and 66.7 ticks a
second. At normal speed: re-aim every 24 s; waves every 10.5 s (flags 0x84)
or 3 min (0x40); a barracks with interval 400 produces every 12 s; a town
hall replaces a worker within 1.5 s.

## Mission objectives (also from the AI flags)

The same flags word selects the win check; defeat is common to all:

- **Defeat**: nothing of the human's left in play or in production, and
  either no units at all (unless a producing building and ≥ 400 gold
  remain), or only workers, no town hall, and less than 400 gold or lumber.
- flag 0x01 — **build**: ≥ 6 farms, ≥ 2 of barracks/church/tower/town hall,
  and a town hall (human and orc mission 1).
- flag 0x02 — **destroy**: computer players 1–3 have no units and no
  buildings left (the neutral player 4 does not count).
- flag 0x10 — **raze but spare one**: no enemy units and exactly one
  enemy building left (orc mission 6, "save the tower").
- flag 0x04 (or 0x80) — additionally **reach the exit**: the hero(es)
  (Lothar or Garona) on map tiles marked as exit, at least the number in
  header 0x92 (human mission 4).
- Heroes: if Lothar or Garona dies, the mission is lost.

## The 24 campaign missions' values

| Mission | AI flags | Barracks | Church/temple | Tower | Group start/grow/max |
|---|---|---|---|---|---|
| H1 / O1 | 0x01 | 200 / 1 | 500 | 500 | — |
| H2 / O2 | 0x42 | 400 / 1 | 500 | 500 | — |
| H3 | 0x02 | 400 | 800 | 500 | |
| O3 | 0x02 | 400 | 1600 | 500 | |
| H4 | 0x04 | 400 | 500 | 500 | |
| O4 | 0x02 | 1 | 500 | 500 | |
| H5 / O5 | 0x02 | 370 / 400 | 740 / 900 | 500 | 1/3/6 |
| H6 / O6 | 0x02 / 0x12 | 340 | 680 | 800 | |
| H7 / O7 | 0x02 | 310 | 620 | 760 | |
| H8 / O8 | 0x02 / 0x06 | 400 / 1 | 500 | 500 | |
| H9 / O9 | 0x02 | 600 | 1200 | 1600 | |
| H10 / O10 | 0x02 | 400 / 700 | 600 / 650 | 600 / 850 | 2/3/9 |
| H11 / O11 | 0x02 | 600 / 900 | 1400 | 1800 | |
| H12 / O12 | 0x02 | 1000 | 2400 / 1800 | 2400 / 3200 | 4/2/12 |

Town halls 50, lumber mill/stable/blacksmith 8000, farms never; each
computer player keeps 4 workers. (Read from the user's DATA.WAR; the port
reads them at run time.)

## Testing it (host build)

- `WAR_SCRIPT` lines `N ai kill K`, `N ai exit` and `N ai reveal` (host
  only) destroy the computer's side but K buildings, put Lothar/Garona on
  the exit, or lift the fog, to check the objective code and take
  screenshots.
- `WAR_AI_HUMAN=1` lets the AI play the human side too (it pays for what
  it trains, founds farms, barracks, mill and blacksmith, and attacks in
  waves of 8, +2 per wave up to 16); `WAR_AI_HUMAN=free` trains for free
  like the computer. This plays every mission AI vs AI headless, as a
  soak test. The computer beats this simple player in most missions.

## Verified against the running original (2026-11-04)

Measured by running the decompiled 1.20 on the Mac with a state log on
the AI clock (`war1-reference` runtime: `WAR_AILOG`, `WAR_AIWATCH`,
`WAR_SPEED_SETTING=3`, script `type:`/`speed:`; missions reached with the
`corwin of amber` / `orc N` cheats), next to the port's log in the same
format (host `WAR_AILOG`), compared minute by minute.

- **Tick rate.** The tick scheduler (`0x19810`) adds `0x517f1[speed*2 +
  alternating]` hundredths of a second per tick; at the default speed 3
  (normal) that is 30 ms, 33.3 ticks a second. The AI clock (`0x29AF8`)
  and the unit update (`0x2ECFC`) run once per tick (`0x197B0` with a tick
  due). The runtime's own config ran at speed 6 (100 ticks/s) until forced.
- **Think cadence.** An idle unit's think (`0x10EE8`, from the idle order
  `0x14030`) runs once every **9 ticks**, and on the first tick it is idle
  again. All per-think counters go by thinks, not ticks: a barracks'
  production countdown (+0x4A) of 400 is about 3600 ticks (108 s).
- **Countdown.** At a think: 1 → produce (`0x11990`: pay flag `0x51120`
  off, the per-type handler at `0x510D8`), 0 → load the level's interval,
  else count down. It starts at 0.
- **Training.** `0x115F4` starts it (cost checked only when paying; no food
  check): build steps from the byte table `0x51A14` (footman 60, peasant
  75, catapult 100, knight 80, archer 70, conjurer 90, cleric 80); a step
  every 18 ticks (the training handler runs every 9 ticks, its step
  counter +0x3F reloads at 2). From the production think to done: steps ×
  18 + 27 ticks (archer: 1289 measured). war1 trained 1.5× slower.
- **Computer soldier think** (`0x2AC44`): target search `0x2A1D0` (radius:
  catapults their range + 2, guards 4, others 8); else home defence
  `0x2A258` (town hall damaged, unit within 30 of the hall's tile + 1,1:
  search 10 around it); else attackers attack-move to the re-aim target;
  else units off their post walk back; else `0x2A030`.
- **Target choice** (`0x2A0DC`): the unit table in slot order; hostile
  (matrix `0x51883`: 0 vs 1–3, 1–3 vs 0), active, within the radius by the
  Chebyshev distance to a building's footprint (`0x13270`); highest
  priority (`0x51D28`) wins, ties keep the first; a unit beats a building
  held so far; one within the attacker's own range (`0x51C6C`) beats one
  held outside it.
- **Human's idle soldier** (`0x10908`, scoring `0x10834`): the current
  target and the last attacker first, then the square of side 2·range+1
  (+2 for the human, +4 otherwise when nothing in range) in row order;
  score priority − distance² (at least 1); out of range only if nearer
  than the best; catapults' minimum range 3 (`0x511E1`); a building gives
  way to the attacker.
- **Answer to an attacker** (`0x10D34`, the first step of every think):
  the last attacker (+0x3A); soldiers attack it (the human's also alert
  friends, `0x10B1C`); workers and Griselda/Garona flee (`0x107A0`);
  buildings forget it.
- **Idle facing** (`0x13FF0`): idle units (not catapults) turn a step at
  random; nobody wanders.

### More from the code, and how the port follows it

- **Attacking a place** (`0x21020`): orders 3 (move) then 6 (attack). The
  move order looks for nobody on the way (order flags `0x551A4`); at the
  place, idle, the think finds targets. The port's computer "attack-moves"
  are plain walks.
- **While attacking** (`0x1143C` → `0x2EFEC`): orders with flag 4 (4, 6, 8)
  re-run `0x10908` without out-of-range targets, the current one scored
  first: once a blow cycle (the attack script's checkpoint), both sides.
  Hence the first think's pick can change on the next tick (an archer that
  picked a grunt shoots the barracks in range).
- **Damage** (`0x104B0`): basic + weapon upgrade − (armor + shield upgrade),
  not below 0, + piercing (archers/spearmen: arrow upgrade on piercing),
  capped at 255; 20 % miss (`rand() < 0x6666`). war1's formula is the same.
- **Random numbers** (`0x19478`): seed × 0x15A4E35 + 1, result bits 16–30;
  the argument is ignored.
- **Unit scripts**: DATA.WAR entry 212 (war1's numbering; the original's
  resource numbers are 2 higher). Per type six animations (death, -, idle,
  walk, attack/work, train) of byte opcodes (0 checkpoint, 1 wait, 2 stop,
  3 jump, 4 frame, 5/6 move, 7 wait − speed upgrade, 8 sound, 9 the action,
  10 wait). Attack every 40 ticks (footman, knight), 61 (archer), 320
  (catapult)…; walking 11 px/s (footman), 13 (knight). The port rebuilds
  war1's attack and walk actions and walking speeds from it at start-up.
- **Mana** (`0x2D3D0`): casters +1 every 60 ticks (computer) or 40 (human),
  to 255, starting at 85; summoned units (spider, scorpion, skeleton,
  daemon, water elemental) −1 every 45, then −5 hit points every 45, dying
  under 5.
- **Summons** (`0x27AB0`): up to four while the mana lasts; spiders and
  scorpions 60 each, a daemon or water elemental 255.
- **Unit numbers**: war1 had no type for the original's 20 ("dragon",
  unused): its monsters from 20 on were one off. The original's 21 spider,
  22 slime, 23 fire elemental, 24 scorpion, 25 brigand (armor 4), 26
  skeleton (raise dead makes them), 27 the dead, 28 daemon, 31 water
  elemental. The port maps a level's numbers to war1's types.
- **Workers** (`0x2B154`): a load goes home; with no hall, only town-plan
  work; builders up to 2/3 of the other workers; gold while the miners are
  fewer than the other workers, or when attacked; else wood; else off a
  lost hall's spot (`0x2A030`).
- **Casters** (`0x2A9E4`): spells by mana as tabled above, research from
  the level for player 1; heal the most wounded friend in a 21-tile square;
  rain of fire / poison cloud at the first enemy in the 11-tile square round
  the re-aim target (caster within 25 of it), 2 tiles off at random; then
  home defence; casters on attack or route roles stay where they are;
  others go back to their post.
- **Human units** (`0x10908`, `0x10B1C`): every idle fighter (casters and
  Lothar included) acquires; a unit under attack calls the idle fighters in
  its 3x3.

### What still differs

Movement: war1's own pathfinding and unit avoidance put units on slightly
different tiles than the original's walking does, and a tile matters to
the target rules ("within range beats out of range"); fights then drift
apart (orc mission 5 matches minute by minute to 2:00, then buildings fall
in a different order). Flee positions (`0x107A0`), the tile a unit leaves a
lost hall's spot to (`0x29C9C`) and the health tests of invisibility and
unholy armor are approximate.
