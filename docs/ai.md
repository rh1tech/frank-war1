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

## Routine catalogue

### Direct reimplementation boundary

The replacement execution model is `port/war_orders_frank.c`, with its
own stable 200-record unit table contract and no dependency on war1's
state machine, renderer, allocator or entity types. A target reference
is a slot plus one, not a native entity ID: death, corpse retention,
placeholder retention and eventual slot reuse must not silently change
the original pointer semantics.

The kernel owns current/pending/next, the single script PC and byte wait,
target/retained attacker, command suppression, queued sound, script frame
and pixel displacement. Its callbacks are for checkpoint order bodies,
actions, exploration, presentation and the simulation RNG. Existing war1
handlers are not used to infer the kernel's current or following order.
The kernel is now linked into the game through `war_scripts_frank.c`.
The live adapter replaces all four script interpreters (idle, walking,
attack and chopping). Each entity has one persistent kernel PC/wait,
selected by `wor_replace` and executed by `wor_scriptTick`; the separate
idle/walk/attack PCs and waits have been removed from `WarUnitComponent`.
Movement and combat side effects are published through the adapter,
including command action suppression and removal of invisibility.

Worker move/work pairs now use `wor_queue` and `wor_consume`: movement
selects its following work script without running the new handler at the
same checkpoint. Gather/delivery handlers run at work checkpoints, and
the hidden worker's order-13 wait is also interpreted by the kernel.

This does **not** yet replace the entire campaign controller. Native
handlers still construct and complete the other orders; `wor_unitTick`
is not yet the game's central dispatch authority. Presentation and sound
remain native. The adapter's 200 records belong to the entity manager;
they survive dense component compaction and retain death, rubble and
corpse scripts after native component removal. Allocation and the live
unit cursor use these original slots. Resurrection now consumes retained
corpse records through the original cast path; placeholder rebuilding
and complete target/attacker identity still need integration.

`tools/test_order_gameplay.py` runs selected missions (all 24 by default)
headlessly for 10,000 ticks, using the cached reference seeds, RNG logs
and exact per-tick tile traces. It rejects missing coverage, earlier
tile/RNG splits than the saved baseline, kernel errors and duplicate
script execution within a game tick. The `K` records in the host AI log
count live kernel execution by script class; the all-mission run requires
idle, movement, attack, work and spells to have actually executed. Output is
kept separately in `build/ai/kernel`, without replacing the prior baseline.

After worker handovers, record retention, route completion, checkpoint
visibility, target clearing, hostile-path planning, replacement posts,
terrain attacks and invisibility, all 24 seeded 10,000-tick comparisons
and their logging-disabled replays match.
**24/24 exact-tile windows and 24/24 RNG windows now match through 10,000 ticks.**
This is the five-minute campaign milestone, not 100% implementation of
every original AI branch. Longer windows and the remaining implementation
boundaries below are still outstanding.
The gameplay harness now enforces these newer floors, rather than the
older files left in the reference-comparison build directory.
Earlier live-kernel human6 and orc5 runs also completed with AddressSanitizer. Full-game
UndefinedBehaviorSanitizer is not clean: existing unaligned resource
reads and a negative left shift in audio decoding are reported. Those
were not changed or counted as a sanitizer pass; the independent kernel
tests remain clean under both sanitizers.

The complete transition contract from `0x2EE9C` through `0x2EFEA` is:

- All setters/consumption reject mask `0x207`. Queueing additionally
  rejects current order 12; immediate replacement does not.
- Queueing a single order clears next; queueing a pair replaces both.
  Neither restarts the script. Both clear only flag `0x8000`.
- Consumption prefers pending, and considers next only when its caller
  explicitly allows it. Taking pending preserves next.
- Immediate replacement clears both queues. Both forms of selection
  clear a mobile unit's target on entering idle, resolve its animation
  through the user's script resource and start wait one.
- Only consumption can apply the `0x8000` entry-delay draw; immediate
  replacement never draws. Selection clears both `0x8000` and `0x4000`.
- The command prologue (`0x2E1F0`) rejects orders 0, 1 and 12. Otherwise
  it updates target/goal **immediately**, clears the queued sound, sets
  `0x4000` and clears `0x8000`, before constructing/resolving the orders.
  Consequently an old script may continue moving or animating, but its
  action is suppressed until an order is actually selected.

`0x2ECFC` skips free slots and decrements only the placeholder counter on
placeholder slots. Other records decrement their auxiliary timer before
the held-script handshake. A normal script checkpoint updates exploration,
then honours a hold request, then consumes pending before dispatching the
current handler. A nonzero handler result permits another pending-only
consumption; next is never implicitly consumed by the dispatcher. The
held/request combination can enter the handler without interpreting the
script or updating exploration. These paths belong in the kernel, not
in per-state timing exceptions.

The standalone kernel also has direct, local differential checks:
`bash tools/ref/ordercmp.sh` runs identical synthetic records through the
kernel and the already-recompiled original's replacement, queue,
consumption, target-validation, attacker-attribution and script routines.
The comparison covers every modeled record field and the simulation RNG
state, plus consumption/checkpoint return values. Its 12,000 transition
and target cases, 964 death/animation cases, 2,040 exploration/visibility
cases, 200 attack-zone cases, 289 incremental line paths, 200 tree searches,
400 replacement-post cases, 512 terrain-damage cases, 512 invisibility cases,
3,072 mana cases, 512 exit searches, 2,048 summoning cases, 624 decay cases,
512 catapult-safety cases, 3,840 shot-payment cases,
49,152 healing/armour cases, 216 protection-timer cases, 16,129 tile facings
and 2,048 resurrection selection/blocked-creation cases
and 199,680 script ticks include all eight directions,
signed pixel displacement and coordinate wrapping, absolute jumps,
zero/underflow/overflow waits, stop continuation, and action suppression.

This is isolated routine conformance, not campaign verification. Command
constructors, full checkpoint dispatch, action bodies, sound delivery
and complete lifecycle integration are outside this comparison. The
harness uses synthetic scripts; it does not establish the 64-instruction
safety budget for every real resource. Its reference-linked executable
is built temporarily under the external reference's `build120` directory
and removed on exit; neither original code nor resources are copied into
this repository. `tools/test_original_orders.c` separately checks the
kernel's command and checkpoint contracts with synthetic callbacks.

Status is deliberately conservative: **read** means the instruction body
has been examined (including earlier work), not that all its callees or
its port are correct. **Ported** means its implementation has been
reconciled with that reading; **verified** additionally requires a recorded
all-mission regression comparison. The older behavioural measurements
above and below do not by themselves verify a whole routine.

The unit update obtains the 200-record table from the pointer at
`0x596D0`. `0x5A520` is a separate, null-terminated list of pointers sorted
for drawing by `0x2CFC0`, **not** the unit-record table. The missile table
has 100 records and is obtained from `0x596D4`.

### Tick, scripts and order transitions

| Address | Responsibility | Callers | Status |
|---|---|---|---|
| 0x197B0 | Refresh visibility; when a tick is due run the AI clock, units and missiles, then ancillary updates. | 0x19810, 0x19998 | read |
| 0x19810 | Schedule ticks using the selected speed and alternating interval. | 0x19998 | read |
| 0x2ECFC | Run eligible unit scripts and checkpoint handlers in slot order, then missiles and post-unit updates. | 0x197B0 | read |
| 0x10473 | Decrement the byte wait; interpret script operations until one yields. | 0x2ECFC | read |
| 0x26450 | Select a type's animation script and initialize its wait to one. | Order setters, creation and animation changes | read |
| 0x2EB3C | Update each side's simulation exploration from a unit at a script checkpoint. | 0x2ECFC, 0x223D8 | verified |
| 0x2EE9C | Consume pending order first, otherwise optionally next order, and restart its script. | 0x2ECFC, 0x223D8, 0x21EE0, 0x2EFEC | read |
| 0x2EF40 | Immediately replace the current order and clear both queued orders. | Movement, entry, death and other handlers | read |
| 0x2EF9C | Queue one pending order and discard the next order. | Order constructors and handler completions | read |
| 0x2EFC4 | Queue a pending order followed by a next order. | Compound order constructors | read |
| 0x2E1F0 | Store a command's target/goal, construct its order pair, and resolve arrival. | Acquisition, AI, player orders and handlers | read |
| 0x22554 | Drop the old path and blockage count; resolve invalid targets or already-reached goals. | 0x2E1F0 | read |
| 0x2216C | Validate and, if necessary, clear the target and last attacker. | 0x10650, 0x10908, 0x1CFF4, 0x223D8, 0x22554 | read |
| 0x221D8 | Resolve a target building to its nearest edge or side midpoint. | 0x222FC | read |
| 0x222FC | Resolve an order's stopping range and test arrival, clearing the path on arrival. | 0x223D8, 0x223C8, 0x22554 | read |
| 0x223C8 | Test arrival using the current order's range. | Attack, gather, repair and shared checkpoint handlers | read |
| 0x2EFEC | Validate an action target, reacquire when allowed, and reissue movement when required. | 0x113E0, 0x1143C, 0x14140, 0x141E0, 0x1CF1C | read |
| 0x20FA4 | Construct a plain move, with no following action. | Indirect command callback | read |
| 0x21020 | Construct move followed by attack order 6. | Indirect command callback | read |
| 0x21074 | Construct move followed by fixed-target attack 5, or idle without a target. | Indirect command callback | read |
| 0x2109C | Construct the human auto-attack variant, or the computer's move/attack pair. | Indirect command callback | read |
| 0x1143C | Shared attack checkpoint: validate/reacquire, alert, handle range and mana, face target. | 0x114D8, 0x114EC | read |
| 0x110A4 | Check a caster's shot cost; if short, flee an attacker or idle. | Attack handlers | read |
| 0x11140 | Reject targets below minimum range or with too many non-hostile occupied tiles in the surrounding 5x5. | 0x1143C | verified |
| 0x11298 | Try a free neighbouring tile when an auto-attacker cannot reach its target. | 0x1143C | read |
| 0x14334 | Advance or cancel a production job and queue idle on completion. | 0x143CC, 0x14534 | read |
| 0x2D2F4 | Choose building damage graphics and advance occupied construction sites. | 0x2ECFC | read |
| 0x2D3D0 | Draw environmental randomness, then update status timers, mana and summoned-unit decay. | 0x2ECFC | read |
| 0x2CFC0 | Sort the drawing pointer list by vertical tile coordinate, without moving unit records. | 0x2ECFC | read |

### Complete order-handler table

The dispatch table is at `0x5158C`, indexed by current order. Each row is
called indirectly by `0x2ECFC` **only after a script checkpoint**, not
every tick. The script selector is at `0x531CC`; stopping ranges at
`0x52A44`; combat flags at `0x551A4`; spell/entry flags at `0x53434`.
These are distinct tables. All handlers below have been read; shared
callees still marked *not read* above or below remain outstanding.

| Order | Handler | Responsibility | Status |
|---|---|---|---|
| 0 | 0x13F60 | Release an expired unit record. | read |
| 1 | 0x13F68 | Finish death: leave a corpse where supported, or clear the building/unit. | read |
| 2 | 0x14030 | Think, then process a route waypoint or perform an idle facing draw. | read |
| 3 | 0x223D8 | Acquire while walking, validate, arrive/hand over, plan and take a tile step. | read |
| 4 | 0x1132C | Diagnostic-only invalid attack handler; returns without a handover. | read |
| 5 | 0x113E0 | Fixed-target attack, without automatic target replacement. | read |
| 6 | 0x114D8 | Attack with reacquisition and pursuit through move/attack orders. | read |
| 7 | 0x11344 | Attack a terrain target; pursue its tile and stop when it is gone. | read |
| 8 | 0x114EC | Auto-attack; only non-caster melee units pursue through the shared range branch. | read |
| 9 | 0x141D0 | Clear the last attacker and wait 255 script ticks. | read |
| 10 | 0x1CD50 | Gather: enter a mine, chop a tree, find another tree, or deliver the completed load. | read |
| 11 | 0x1CF1C | Deliver: answer an attacker, validate/approach the hall, then enter. | read |
| 12 | 0x1CFF4 | Step off the occupancy grid into a building, hide, wait, and mine/deliver/start construction. | read |
| 13 | 0x1D1A0 | Exit a building, deliver a load or choose/resume work. | read |
| 14 | 0x141E0 | Repair by eight HP per checkpoint, charging resources; enter an unstarted site. | read |
| 15 | 0x143CC | Finish/cancel training or wall construction and account for output/refund. | read |
| 16 | 0x14534 | Finish/cancel an upgrade and update its research byte/refund. | read |
| 17 | 0x14140 | Spell 17: common flag-driven facing/range and spell dispatch. | read |
| 18 | 0x14140 | Spell 18: common dispatch, without approach/range validation. | read |
| 19 | 0x14140 | Spell 19: common flag-driven facing/range and spell dispatch. | read |
| 20 | 0x14140 | Spell 20: face a point or validate a unit, then dispatch. | read |
| 21 | 0x14140 | Spell 21: common flag-driven facing/range and spell dispatch. | read |
| 22 | 0x14140 | Spell 22: dispatch without the common target-facing branch. | read |
| 23 | 0x14140 | Spell 23: dispatch without the common target-facing branch. | read |
| 24 | 0x14140 | Spell 24: dispatch without the common target-facing branch. | read |
| 25 | 0x14140 | Spell 25: dispatch without the common target-facing branch. | read |
| 26 | 0x14140 | Spell 26: face a point or validate a unit, then dispatch. | read |
| 27 | 0x14140 | Spell 27: face a point or validate a unit, then dispatch. | read |
| 28 | 0x13FE8 | Passive checkpoint that allows a pending-order handover. | read |
| 29 | 0x14084 | Rescue on proximity or healing, otherwise turn eligible neutral units. | read |
| 30 | 0x145D0 | Diagnostic-only invalid order handler. | read |
| 31 | 0x145D0 | No-order sentinel; invalid if dispatched as a current order. | read |

### Think, roles, movement and combat callees

| Address | Responsibility | Callers | Status |
|---|---|---|---|
| 0x10EE8 | Dispatch idle decisions by alliance and unit type after answering an attacker. | 0x14030 | read |
| 0x10D34 | Answer/forget the last attacker, or run a worker's economy/flee decision. | Think, movement, attack, gather, repair and production handlers | read |
| 0x107A0 | Choose a randomized flee destination using the attacker's direction. | 0x10D34, 0x110A4 | read |
| 0x10834 | Score one visible hostile target for shared acquisition. | 0x10908 | read |
| 0x10908 | Score current target, attacker and a row-major tile scan; issue a changed target. | 0x10EE8, 0x11004, 0x2EFEC | read |
| 0x10B1C | Alert eligible nearby friendly units to the attacker. | 0x10D34, 0x113E0, 0x1143C | read |
| 0x10CE0 | Retain the first active hostile attacker; replace it only after it becomes inactive. | 0x10650 | verified |
| 0x11004 | Clear attackers on plain walks; acquire/answer only for the appropriate next order. | 0x223D8 | read |
| 0x115F4 | Start a training job, optionally charging its price. | Production callbacks, 0x216E0, 0x22B88 | read |
| 0x11990 | Dispatch a computer building's free production. | 0x10EE8 | read |
| 0x13270 | Chebyshev distance to a target's footprint. | Acquisition, routes, home defence and area targeting | read |
| 0x13990 | Check whether a unit/building fits on the map. | 0x21518, 0x2AF14, 0x2E7E4 | read |
| 0x13FF0 | Draw and apply an occasional idle facing change. | 0x14030, 0x14084 | read |
| 0x1DF50 | Choose a facing from a tile displacement. | 0x11344, 0x14140, 0x1CD50, 0x1DFE0 | read |
| 0x1DFE0 | Face the target's tile, or the stored goal when there is no target. | Attack checkpoints and other action handlers | read |
| 0x1BB90 | Queue the type's attack sound, randomizing one sound family with the platform RNG. | Attack checkpoints | read |
| 0x1E734 | Parse and create the selected placement alternative. | 0x1E888 | read |
| 0x1E8A0 | Select alternatives and place the level's unit groups. | 0x195AC | read |
| 0x21EE0 | Recover failed movement, including a worker's nearby-tree fallback. | 0x22080, 0x223D8 | read |
| 0x21F48 | Decide whether an occupied tile blocks route planning. | 0x21FB0, 0x2C66C, 0x2CA48 | read |
| 0x21FB0 | Test whether a friendly blocking unit is walking away. | 0x22080 | read |
| 0x22080 | Wait/replan/give up when the next tile is occupied. | 0x223D8 | read |
| 0x224AC | Clip an untargeted destination at the first obstacle along the line. | 0x22554 | read |
| 0x27760 | Restore at most 40 HP for three mana each, or subtract that amount from a daemon. | Indirect spell dispatch | read |
| 0x27890 | Spend 125 mana and refresh invisibility to 3000 ticks. | Indirect spell dispatch | read |
| 0x278C4 | Spend 125 mana, refresh armour to 1000 ticks, and reduce recipient HP. | Indirect spell dispatch | read |
| 0x2799C | Raise nearby retained corpses, paying seventy mana per successful skeleton. | Indirect spell dispatch | ported |
| 0x27AB0 | Summon units while mana permits. | Indirect spell dispatch | read |
| 0x27B94 | Check spell affordability without acting; immediately idle if unaffordable. | 0x14140 | read |
| 0x27BCC | Execute the spell body and queue idle, except continuing order 26. | 0x1077C via the order-action table | read |
| 0x29734 | Score human records by their top-left tile and choose the first best map zone. | 0x29AF8 | verified |
| 0x29868 | Advance a route or finish it, with the caster stopping rule. | 0x29908, 0x29990 | read |
| 0x29908 | Release a gathered route group and update group growth. | 0x29990 | read |
| 0x29990 | Check waypoint arrival only with neither pending nor next order. | 0x14030 | read |
| 0x29A44 | Apply each player's wave quota in unit-slot order. | 0x29AF8 | read |
| 0x29AF8 | Count down re-aim and wave clocks; advance the 16-bit quota. | 0x197B0 | read |
| 0x29B8C | Select a retained mobile placeholder by owner and saved type; reverse the draw gate using the entry alert cache. | 0x29DE8 | verified |
| 0x29C9C | Choose a random direction away from the lost hall's site. | 0x2A030 | read |
| 0x29D48 | Choose a weighted route and submit its first move/attack order. | 0x29DE8 | read |
| 0x29DE8 | Assign creation-time roles, posts and home positions. | 0x2E7E4 | read |
| 0x29F98 | Clear a dying unit's AI role bookkeeping. | 0x2E23C | read |
| 0x2A030 | Move off a lost hall's site as a last resort. | 0x2A9E4, 0x2AC44, 0x2B154 | read |
| 0x2A0DC | Select a computer soldier's highest-priority target. | 0x2A1D0, 0x2A258 | read |
| 0x2A1D0 | Choose the soldier's search radius by type and role. | 0x2AC44 | read |
| 0x2A258 | Search near home unless the hall's HP equals the calling unit type's base HP. | 0x2A9E4, 0x2AC44 | read |
| 0x2A2E4 | Clip a centred square to the map and return its origin and dimensions. | 0x2A360, 0x2A57C, 0x2A714, 0x2A864 | read |
| 0x2A4BC | Select the first eligible corpse within a strict ten-tile circle, in original slot order. | 0x2A9E4 | ported |
| 0x2A57C | Choose an invisibility recipient and send it towards the attack destination. | 0x2A9E4 | read |
| 0x2A714 | Choose an unholy-armour recipient under attack. | 0x2A9E4 | read |
| 0x2A864 | Aim an area spell near the first hostile occupied tile using exactly two draws. | 0x2A9E4 | ported |
| 0x2A9E4 | Dispatch caster decisions, including Medivh's visibility branch at 0x2ABA7. | 0x10EE8 | read |
| 0x2AC44 | Dispatch soldier engagement, home defence, attack, post or fallback decisions. | 0x10EE8, 0x2A9E4 | read |
| 0x2AFEC | Expand and scan the player's persistent wood-search rectangle. | 0x2B154 | read |
| 0x2B154 | Release old worker role bookkeeping and select delivery, construction, gold or wood. | 0x10D34, 0x10EE8, 0x1D1A0 | read |
| 0x2BCD4 | Find a nearby tree in clockwise expanding rings. | 0x1CD50, 0x21EE0 | read |
| 0x2C66C | Trace one side of an obstacle while planning. | 0x2CD44 | read |
| 0x2C880 | Straighten the selected detour. | 0x2CD44 | read |
| 0x2C9B0 | Copy the next bounded section of a planned path. | 0x2CD44 | read |
| 0x2CA48 | Trace a bounded straight line to the destination. | 0x2CD44 | read |
| 0x2CC1C | Prepare the destination's surrounding footprint for planning. | 0x2CD44 | read |
| 0x2CD44 | Plan the next path segment, choosing between straight travel and detours. | 0x223D8 | read |
| 0x2D740 | Refresh per-unit human and computer visibility bits. | 0x197B0 | verified |
| 0x2D6B8 | Test a footprint for any tile not fully hidden from a side. | 0x2D740 | read |
| 0x1EA90 | Combine one sight-stencil tile with the existing fog artwork. | Indirect stencil callback | read |
| 0x1EB08–0x1EC48 | Reveal clipped, corner-trimmed sight stencils of widths 5, 7, 8, 9 or 11. | 0x2EB3C and sight callers | read |
| 0x2B2A0 | Traverse a centered stencil, clipped to the map. | Sight and other stencil callers | read |
| 0x2E554 | Find the first free exit tile on rings around a building. | 0x2E6B8, 0x2E740 | read |
| 0x2E6B8 | Search a worker exit, register occupancy and select idle, or kill the worker when no exit fits. | 0x1D1A0 and release callers | read |
| 0x2E740 | Find an exit before creating a unit, then attempt its appearance effect. | Production and summoning | read |
| 0x2E7E4 | Create a unit, with facing, role and start-delay draws in their original order. | Placement, training, construction and summoning | read |
| 0x2E23C | Enter death immediately, remove bookkeeping, trigger mission/wave consequences and kill occupants. | 0x10650, 0x1E8A0, 0x2D3D0, 0x2E474, 0x2E6B8; recursively itself | read |
| 0x25F48 | Remove a unit from selection and refresh affected command/selection UI. | Death and entry, among other callers | read |
| 0x13C84 | Decrement owner totals, completed-type counts and applicable type callbacks. | Death and rescue, among other callers | read |
| 0x13954 | Clear mobile occupancy at the unit's tile, preserving another occupant's pointer. | Death and movement | read |
| 0x25288 | Update unit-kill outcome bookkeeping, with same-owner and population-ratio branches. | 0x10650 | read |
| 0x2538C | Update building-kill outcome bookkeeping, with same-owner and population-ratio branches. | 0x10650 | read |
| 0x104B0 | Roll physical damage and the miss threshold. | 0x106EC, 0x11A10 | read |
| 0x10650 | Attribute a strike before immunity/zero-damage checks; kill or subtract HP and validate the killer's target. | 0x106EC, 0x120C8, 0x12240 | read |
| 0x106EC | Apply the attack script's blow or launch its missile. | Indirect script action | read |
| 0x105D0 | Reduce terrain durability by half damage plus one, capped at 20, and remove exhausted terrain. | 0x106EC, 0x120C8 | read |
| 0x11EC0 | Construct the fire elemental's additional strike effect before its damage roll. | 0x106EC and effect callers | not read |
| 0x11A10 | Initialize a missile's damage, randomized aim and flight. | 0x106EC | read |
| 0x27914 | Spend 50 mana and attempt a poison-cloud allocation. | Indirect spell dispatch | ported |
| 0x2793C | Spend 50 mana and attempt four rain impacts; queue idle when unaffordable. | Indirect spell dispatch | read |
| 0x11C50 | Initialize an area effect at the command goal's pixel centre, retaining its source. | 0x11CB0, 0x11CD8 | read |
| 0x11CB0 | Allocate a poison-cloud effect in the shared missile table. | 0x27914 | ported |
| 0x11CD8 | Allocate one rain effect before drawing its aim, retrying positions shared by preceding impacts. | 0x2793C | read |
| 0x11F10 | Attempt 30 keep-death effects: two position draws each, and one delay draw only on allocation success. | 0x2E23C | read |
| 0x11E70 | Allocate and initialize a stationary missile effect; type 8 additionally plays positional sound. | 0x11F10 and spell/effect callers | read |
| 0x119C0 | Clear and return the first free missile slot, or fail when all 100 are occupied. | Missile/effect constructors | read |
| 0x11DDC | Allocate the ordinary building-death effect at the footprint centre. | 0x2E23C | read |
| 0x1B954 | Select a unit-type death sound; catapults skip this branch. | 0x2E23C | read |
| 0x1B92C | Select one of three collapse sounds using the separate platform RNG. | 0x2E23C | read |
| 0x2F82C | Schedule the defeat screen. | Death and defeat checks | read |
| 0x33B12 | Advance the separate platform RNG, not the simulation LCG. | Collapse/attack sound selection and platform callers | read |
| 0x24F40 | Sum mobile-unit ownership counts for players 0 through 3. | Outcome bookkeeping | read |
| 0x24F60 | Sum the corresponding building counts for players 0 through 3. | Outcome bookkeeping | read |
| 0x11FA8 | Initialize a flying missile's next phase. | Indirect missile dispatch | read |
| 0x11FF0 | Advance a missile and draw its flight randomness. | Missile dispatch, 0x12074 | read |
| 0x1DFFC | Initialize the shared signed tile/pixel line accumulator. | Route and missile construction | verified |
| 0x1E084 | Advance that accumulator and return the signed step and facing. | Routes and missile flight | verified |
| 0x12074 | Advance a catapult rock's flight. | Indirect missile dispatch | read |
| 0x120C8 | Scan impact tiles row-major, quartering off-center and building damage when enabled. | 0x12240, 0x122D0, 0x12338 | read |
| 0x12240 | Resolve a missile impact against its target or splash area. | Indirect missile dispatch | read |
| 0x122D0 | Move a poison cloud, apply area damage and count down its remaining pulses. | Effect state 3 | ported |
| 0x12338 | Fall and strike three times using one rain record, allocating each impact visual. | Effect state 4 | read |
| 0x123CC | Report an invalid effect state. | Effect state 7 | read |
| 0x123E0 | Release a completed effect record. | Effect state 5 | read |
| 0x26480 | Select an effect animation from DATA.WAR's shared script resource. | Missile/effect constructors and phase changes | read |

### All direct random callers

`0x19478` has 20 direct callers in the decoded executable. The argument is
not an input to the generator. The complete inventory is: `0x104B0`,
`0x107A0`, `0x116C4`, `0x11A10`, `0x11CD8`, `0x11F10`, `0x11FF0`,
`0x12074`, `0x13FF0`, `0x194A8`, `0x195AC`, `0x1E8A0`,
`0x29C9C`, `0x29D48`, `0x2A864`, `0x2D3D0`, `0x2E7E4`, `0x2EE9C`,
`0x2FB20`. This includes initialization and platform-related draws, not
only combat/AI.

| Address | Responsibility | Callers | Status |
|---|---|---|---|
| 0x19478 | Advance the shared 32-bit LCG and return bits 16 through 30. | The 20 callers listed above | read |
| 0x116C4 | Weighted barracks production selection. | Indirect production dispatch | not read |
| 0x194A8 | Initialization-time random consumer; body not yet classified. | 0x195AC | not read |
| 0x195AC | Level initialization, including random seeding and placement. | 0x1E248 | not read |
| 0x2FB20 | Random consumer outside the main game-code range; body not yet classified. | 0x2FBB4 | not read |

### Order handover established from instructions

The three order bytes are not a stack: current `+0x1F`, pending `+0x20`,
next `+0x21`; 31 means no queued order. A command updates target/goal
immediately but normally leaves the current script running until its
checkpoint. Queueing one order discards the follow-up; queueing a pair
replaces both. Ordinary queue setters reject units in the protected
inactive/death states and current order 12. The immediate setter is
separate and can end order 12.

At a checkpoint `0x2ECFC` first tries pending-only consumption. If that
succeeds it skips the old order handler. Otherwise it dispatches the
current handler and, only if that handler returns nonzero, tries
pending-only consumption again. It never automatically consumes *next*
here. Movement explicitly requests pending-or-next consumption on arrival
or invalidation. If neither exists it immediately becomes idle. Every
actual order change selects its animation and initializes wait to one;
the replacement handler does not run at that same checkpoint.

The randomized wait in `0x2EE9C` occurs only when the unit's `0x8000`
flag is set: one draw, low three bits plus one, after script selection.
The queue setters clear that flag. Idle entry clears a mobile unit's
target. Environmental randomness is drawn after missiles and building
updates, before status/mana/decay processing, not inside each unit's turn.

Consequences for the port: an attack-move arriving with no target really
becomes attack order 6, whose **next script checkpoint** queues idle.
It is not an idle unit with an extra delay. The distinction matters if a
new command arrives in between. The human's normal acquisition and answer
both use `0x2109C`, whereas computer acquisition uses `0x21020`.
`0x2109C` queues move/8 only for a non-caster melee unit with a last
attacker; otherwise it queues 8 alone. Range resolution may then replace
that pending 8 with plain move/none. Walking acquisition keys off
**next == 6**, not an attack state somewhere on war1's parent stack.

The port now represents the attack variant (5, 6 or 8) and the move's
following order explicitly. Move completion selects the retained attack
state; an empty attack ends at its own checkpoint. Script entry/resume
marks a script change instead of comparing pool addresses or adding
`frankAtkNew`/`frankIdleHold` delays. Commands to another idle, walking,
attacking or chopping unit stay in its pending transition until that
unit's old script reaches a checkpoint; pending consumption precedes the
old handler. Other order families still need this reconciliation.

### Worker following-order checkpoints

The gather constructor (`0x21184`) queues move 3 followed by gather 10,
clears the stroke count, and selects wood or mine work. Delivery
(`0x2120C`) queues move 3 followed by delivery 11. Command resolution
(`0x22554`) and movement (`0x223D8`) both test arrival using the
**following order's range**, one for these worker actions. In command
resolution the pending byte gates the test, but the range argument still
contains the next byte; reading pending does not replace that argument.
An already-arrived command selects work directly. Otherwise movement
consumes next on arrival, starts its script with wait one, and returns
without executing the work handler.

Gather (`0x1CD50`) checks the mine or tree at its own checkpoint. A missing
tree causes a new nearby-tree command or idle, not immediate execution of
the replacement work. A standing tree counts one stroke per checkpoint,
not at the no-op work action opcode. Starting entry (`0x1CD10`) selects
order 12 with the normal wait one. Entry (`0x1CFF4`) releases occupancy
and steps onto the building at the first checkpoint; at the next it hides
the worker and selects exit order 13 with wait 80. Release (`0x2E6B8`)
registers the exit tile and selects idle; exit (`0x1D1A0`) may then queue
delivery or work, without executing the new move handler that tick.

The original/port watch for orc10 slot 73/native entity 91 exposed the
old skipped gather checkpoint: the original selected gather at tick 823,
idle at 824 and move at 825; the port reached idle one tick earlier.
Native worker payloads are now gated to work checkpoints, movement
explicitly consumes the kernel's next order, and the old entry wait-two
compensation is removed. Inside waits use the shared kernel clock.
This is not yet complete worker conformance: mining resources still move
at native exit rather than original entry, and attacker handling,
occupancy registration, failed exits and creation need reconciliation.

The complete entry body also distinguishes mine depletion from worker
delivery (`0x1CFF4`, `0x1CF54`). At the second entry checkpoint, after
snapping to the building and selecting the eighty-tick inside wait,
mining immediately grants at most the mine's remaining amount or one
hundred. The special five-gold deduction applies only to controller one
**and a mine currently hidden from the human**; the load was already set
to its full amount. Depletion invokes the mine's removal path and clears
the worker's saved-return flag. It is not an unconditional five-gold
charge for every computer worker, nor a deduction eighty ticks later.
Those accounting/depletion paths remain native and incomplete.

Release (`0x2E6B8`) chooses the search corner from home when carrying,
the saved work location when returning, or the worker's current tile.
On success it clears inside, reconstructs pixels from the exit tile,
registers mobile occupancy, and immediately selects idle. Exhausting the
exit rings enters death, not a clamped fallback or a retry. Only successful
release proceeds to delivery, computer work selection or the saved gather
command (`0x1D1A0`). The existing native failed-exit and missing-building
fallbacks do not yet implement that contract.

Failed-path recovery (`0x21EE0`) considers **any worker whose following
order is gather 10**, including a worker heading toward a mine. It scans
the clockwise tree rings of widths 3, 5, 7 and 9, commands gathering at
the first eligible tree, and consumes that command immediately. The
native implementation previously required a wood state already on the
stack. In human9 that sent the blocked miner to idle instead, causing
alternating idle/move retries while the original reissued gather every
checkpoint. The replacement now constructs a fresh wood command for
either kind of gather movement.

Building entry is an immediate order replacement, not a fresh movement
command. It preserves the old blockage count. In human3, worker slot 23
enters order 12 with blockage one at tick 9649. Clearing that count in the
native move-state entry makes it appear passable to worker slot 38's
planner, which then chooses an occupied tile instead of the original
detour. The entry adapter now retains this count; ordinary movement
commands still reset it (`0x22554`, `0x21F48`).

Friendly-blocker handling uses the original hostility matrix: owner zero
against owners one through three, which are mutually non-hostile; owner
four is neutral. The generic native predicate treated different computer
owners as enemies. In human9 that discarded the raider's saved route
behind a friendly spearman from another computer owner. The shared
predicate now drives both simulation targeting and native movement
checks while original ticks are active.

Planning through hostile occupants (`0x2CD44`) depends on **next order 6
and no retained attacker**, not the native move's `checkForAttacks` flag
or a scan for units currently attacking the mover. The explicit-order
move adapter sets the native flag false, so using it truncated routes
before occupied enemy goals. Even an unused final step matters: another
unit's friendly-blocker check peeks at it to decide whether to retain its
own path (`0x21FB0`, `0x2C5D0`).

Replacement-post selection (`0x29B8C`) scans the stable 200 records, not
a permanent list of starting positions whose native occupants have died.
Only records with placeholder bit `0x200`, the same owner, a non-building
current type and the requested saved type qualify. Taking one copies its
post coordinates and sets its flags to exactly free. An expired corpse
that became neutral is not such a placeholder.

The per-player alert cache changes the draw gate. If it was set on entry,
refresh it from any same-owner mobile placeholder, then try the selection
scan on three of four draws. Otherwise try on one of four. The refreshed
cache does not determine this call's gate. Computer building death sets
the cache for its owner; keep death sets it for owners one through three
(`0x2E23C`). There is always exactly one draw, even with no available post.
The kernel is compared directly against 400 original calls, including
cache changes, scan order, saved type, released flags and RNG.

Creating the script record now precedes role assignment, with the random
creation delay added afterward (`0x2E7E4`). This lets the role selector
use the original record without freezing its wait before the final draw.
Together these changes remove orc5's remaining split at tick 9583:
its tile and RNG windows now match through tick 10000.

The script clock must advance even without diagnostics. `wrng_tick` is
now assigned from the simulation tick outside host/logging conditionals.
Previously the duplicate-script guard froze ordinary, unlogged gameplay
after its first tick. `--check-unlogged` reproduces each run without
`WAR_RNGLOG` and requires identical unit-state traces and outcomes;
human1, human6 and orc5 cover this regression.

### Creation and the live slot cursor

The original unit loop (`0x2ECFC`) scans the live 200-record table, not a
snapshot of units present at tick start. Creation (`0x2E7E4`) takes its
first free slot. A newborn after the cursor therefore executes its first
script tick during its birth tick; a reused slot before the cursor waits
until the next tick. Native entity slots cannot substitute for this
ordering: audio and other non-unit entities allocate and release those
slots too.

The human6 newborn at original slot 68 illustrates the distinction. Both
engines create it during tick 1828 at tile 52,5 with start-delay draw one.
Before tick 1829 the original's wait is already one, while the port leaves
two. The port reused slot 8, whose destroyed human lumber mill the original
still retains in order 0. Changing the loop to persistent bindings alone
does not fix it. Both slot retention and the live cursor must follow the
original.

Production placement (`0x2E740`) finds an exit before calling creation;
it returns failure without allocation or creation draws when no tile is
available. Creation validates placement before the facing draw,
initializes stats/owner, registers occupancy and assigns the role, then
adds the low three bits of its final draw to the selected script wait.
Full-table rejection likewise precedes every creation draw. The native
train-at-0,0-and-relocate path and corpse/placeholder slot release remain
separate reconciliation work.

### Death, rubble and corpse record lifetime

Death (`0x2E23C`) immediately selects order 1, sets the dying bit and
clears the held/request bits. The record continues executing its death
script even though it is no longer an active target. Wounded units and
neutral Lothar instead select removal order 0 and set removed/inside.

At the death checkpoint (`0x13F68`), corpse-producing mobile types become
type 51 (`0x33`), owner 4, frame zero in the **same record**, keeping order 1.
Human bodies use corpse animation 1, orc bodies animation 2; Medivh uses
2 and the brigand 1. Catapults and monsters other than the brigand have
no corpse. Finishing a corpse, building or no-corpse death clears dying/removed,
selects removal order 0 with wait one, then sets removed again.

Only order 0's checkpoint (`0x13F60` → `0x2EA5C`) reclaims the record.
Computer records with the original retention bit `0x400` instead become
placeholders; buildings receive countdown 255. The others become free
and owner 4. Native sprite/entity removal must not release a record that
is still executing these scripts. Record retention does not by itself
implement building cleanup, keep/fire-elemental effects, or the
complete role/hidden-occupant death graph.

### Route completion is an idle-handler tail

The idle handler (`0x14030`) runs the unit's think **before** its route
checkpoint (`0x29990`). Route processing requires both order queues to
be empty. A routed idle checkpoint never performs the idle-facing draw,
even when processing the route clears the route word.

At the last waypoint, `0x29868` changes the role to attacker and clears
the route word; it does not issue an attack-move. The existing idle
script continues, and the next think makes the new attack decision.
Likewise a caster stopping short of the end clears its route without
another command. Valid subsequent waypoints update the saved goal before
queuing their move/attack command.

The orc5 knight at slot 14 reaches attack order 6 before tick 5746 and
idle before 5747. That idle checkpoint finishes its route, but its attack
move is not selected until tick 5756. The native route-advance helper
instead submitted that move during the completion checkpoint. Correcting
the idle tail and role-only transition preserves the existing script
cadence; it needs neither an added delay nor an extra facing draw.

Group release (`0x29908`) scans original slots, clears the waiting bit,
decrements the waypoint's word count and advances each matching route.
The group-growth counter is a wrapping word and grows the group on
equality with the configured threshold, not a clamped minimum interval.

### Checkpoint exploration and visibility snapshots

Exploration (`0x2EB3C`) is not a per-frame square around every friendly
unit. Stationary human-controlled units reveal their type's sight stencil
once, recording completion in flag `0x20` (owner zero) or `0x100` (the
other side). Walking clears both flags and reveals only the smaller
five-tile-wide stencil, at the old tile's checkpoint and again after
successfully taking a tile step. The next stationary checkpoint reveals
the full sight area. Computer-controlled units reveal a small area to
the human when an eligible combat order targets a hostile unit; spell
orders 26/27 reveal it without that target condition (`0x2EAE4`).

The sight routines use centered, map-clipped stencils. Width five trims
one tile from each corner; the wider stencils trim two tiles on the
outer rows and one on the next rows. Buildings use width eight, centered
on top-left plus half their footprint. These shapes, rather than war1's
larger rectangular sight ranges, determine acquisition visibility.
The fog-combination operation (`0x1EA90`) never makes an already revealed
tile fully hidden again. Since visibility only tests for fully hidden
tiles (`0x2D6B8`), the independent simulation represents each side with
one bit per tile, without copying the original's fog-artwork tables.

`0x2D740` snapshots both visibility bits before the tick's AI/unit work.
Free, removed, inside and placeholder records remain hidden; dying
records are still examined. Any revealed footprint tile makes a unit
visible. Acquisition and Medivh now use this snapshot rather than the
renderer's periodically refreshed fog map.

An earlier assumption about computer sight was incorrect. Initialization
at `0x196D5` sets owner zero's acquisition mask to `0x100`; owner one's
mask is **zero in campaign play**, and `0x200` only in network play.
Campaign computers deliberately acquire without a fog restriction.
The independent exploration/visibility routines cover both sides and
the network-sharing branches; the live adapter drives campaign mode.
Native fog presentation and vision-spell effects remain separate.

The direct original comparison covers 2,040 type/controller/order/flag
and map-edge combinations, including overlapping reveals and visibility
refresh. In human7, the old rectangle exposed the approaching spearman
to the catapult at tick 9699 while the original still marked it hidden.
The source-derived visibility correction removes the route migration's
early shot without changing attack delays.

### Attack zones and cleared movement targets

The zone scorer (`0x29734`) uses each active human record's top-left
tile, not its footprint center. Buildings contribute four, other records
one; inside workers still count. Ties keep the first row-major zone,
including an entirely empty map, whose destination is `(8,8)`. The
independent implementation has 200 direct comparisons against the original.
In human6 this changes the knight's destination from `(8,56)` to `(40,56)`.

Acquisition (`0x10908`) validates the old target before scoring. If it
clears a dead target and finds no replacement, movement's subsequent
validation sees a null target and succeeds. The unit keeps walking toward
its last resolved goal rather than immediately entering attack or idle.
The live bridge now preserves that goal and clears the native move/attack
references together. It also validates the killer's target after a lethal
strike. These changes make the human6 window exact; the validator's other
command/worker callers are not all integrated.

Attack-checkpoint reacquisition (`0x2EFEC`) passes zero to `0x10908`,
which **allows out-of-range candidates**. The earlier opposite reading
was incorrect. In human3 the spearman keeps its moving peasant target:
priority 60 minus distance squared 25 beats the nearby footman's
40 minus 9. Restricting acquisition to range wrongly switches targets.
Correcting the parameter removes that RNG split; a later worker tile
difference remains.

### Rock flight and per-tile unit splash

Missiles store integer pixel coordinates. The same line accumulator
used by route planning (`0x1DFFC`, `0x1E084`) produces one signed step
per flight update; multiplying that step by missile speed advances the
pixels. It is not repeated once per pixel. The shared independent helper
matches 289 original line paths, each checked for 128 steps, including
zero-length lines, signs, shallow slopes and ties.

A rock's impact (`0x12240`) ignores its rolled damage for the blast.
The roll affects aim spread, but the 3x3 row-major unit scan (`0x120C8`)
uses 255 at the impact tile and 63 elsewhere. A building quarters those
amounts again, to 63 and 15. Each occupied tile is hit separately, so
a building may take several hits; the shooter is skipped, but friendly
units are not. Map-edge clipping applies to individual tiles.

The port now advances missile pixels along that line and applies this
unit/building damage at the original impact checkpoint. Native visual
projectiles no longer apply a second unit blast. Human7 now matches
tiles and RNG through 10,000 ticks; the knight's HP also matches at
every tick. Previously native splash killed it at 9815, removing its
later idle-facing draw.

This is not full missile conformance. Raw terrain now takes original
damage on unoccupied splash tiles; native constructed-wall damage and
connectivity are not yet one authority with that terrain model.
The original type-7 impact effect shares the
100-record missile pool; its allocation/lifetime and other effect
allocations are not yet represented there. Splash occupancy still uses
live logical footprints rather than a fully reconciled original grid.

### Terrain attacks and starting combat research

The planner (`0x2CD44`) normally blocks terrain bits `0xC`. Non-human
controllers without a retained attacker, except workers, may plan through
hostile walls; in dungeon maps they may plan through both wall bits.
The actual blocked step (`0x22080`) still stops at that terrain. A visible,
unattacked non-worker submits move/terrain-attack order 7; an invisible
unit or one retaining an attacker discards its route instead.

Terrain durability (`0x105D0`) occupies the visual word's high five bits.
Damage removes half the incoming damage plus one, capped at twenty.
Exhaustion advances artwork through the user's transition resource,
then adds twenty durability. The transition can remove obstacle bits or
break the immediate vertical neighbours, with linked recursion disabled
for those neighbours (`0x2C3B4`). The port keeps this simulation word
separate from rendered artwork. Synthetic transitions cover map edges,
durability bounds, unchanged artwork and linked breaks against the original.

A targetless strike is not zero damage: valid terrain takes full
unarmoured basic plus piercing damage, without the unit-target miss draw
(`0x104B0`). The owner's ranged research adds to piercing for archers and
spearmen; melee research adds to basic damage for every other type,
including brigands. Target armour research is subtracted from basic
damage. The header loader (`0x195AC`) preserves starting research for
the human as well as computer players. Native initialization discarded
the human values, while cached NPC damage omitted some bonuses; correcting
only one side produced an early combat regression. Damage now uses base
stats and actual research, and the shared script uses owner speed research.

Command resolution (`0x22554`, `0x224AC`) also clears the old path and
blockage count and normalizes targetless destinations. From a blocked
requested goal it walks backward along the source line until a free
tile, then advances one step again. It is not a nearest-free-tile search.
Applying this to attack commands fixes the brigand's later path around
the dungeon doors. Orc8 now matches tiles and RNG through 10,000 ticks.
Order 7 now shares caster affordability and flee/idle handling with unit
attacks, after terrain validation and before facing (`0x11344`).
Constructed-wall integration remains outstanding; this does not mark the
entire order-7 handler complete.

### Original invisibility execution

Orc10's former first RNG split was spell eligibility: at tick 7769,
cleric slot 67 selects invisibility for knight slot 77. Native research
had been filtered through the human allowed-feature mask. The original
caster dispatch (`0x2A9E4`) reads unfiltered player-one research instead.
The level AI metadata now retains every starting research byte independently
of native UI restrictions.

Selection (`0x2A57C`) scans a clipped 21-by-21 occupancy box in row-major
order. It rejects inactive, hostile or already invisible recipients,
requires current move order 3 and health above half the type's base HP,
and accepts knights or archers. It submits the recipient's **plain move
before** commanding the caster, rather than an attack-move afterward.

The caster's move/order-19 pair has range eight (`0x21258`, `0x222FC`).
Its original script now runs through the same kernel as movement and
combat. Checkpoints validate the target, reapproach when necessary, face
it and check affordability without spending mana (`0x14140`, `0x27B94`).
The action spends 125 mana and refreshes a 3000-tick invisibility timer
(`0x27890`), then queues idle for the next checkpoint (`0x27BCC`).
The timer decrements after the unit/missile pass, not on rendering frames,
and survives script synchronization without being collapsed to a boolean.

The spell body matches the original for all 256 mana values, both check
and action calls. Its differential cases use a full original effect pool
to isolate state changes. Gameplay still uses a native presentation
effect: shared 100-slot effect allocation and other spell bodies remain
incomplete. The complete 24-mission sweep matches both 10,000-tick windows
in every mission. Orc10 executes 56 kernel spell ticks and Orc12 executes
112; matching those traces is not complete spell parity.

### Mana, summoning and placement

Creation (`0x2E7E4`) initializes caster mana to 85 and its byte countdown
to 40, even for a computer-controlled owner. Status processing (`0x2D3D0`)
decrements that byte only below 255 mana. Reaching zero adds one mana and
reloads 60 for controller one, 40 otherwise; a full caster preserves its
countdown until spending mana. Zero wraps to 255 rather than running
immediately. The live adapter now owns this counter instead of accumulating
floating-point frame time. The independent comparison covers 3,072
type/controller/countdown/mana combinations.

The first extended Orc10 split exposed this distinction: original conjurer
slot 69 queues scorpion order 25 at tick 10,184, while the floating-timer
port still has 252 mana. Correct regeneration alone is insufficient:
the original creates the summons at the later script action, not during
the AI decision.

The AI queues a summon order alone (`0x2A9E4`); a player spell command
queues it followed by idle (`0x21258`). Orders 22 through 25 share the
spell checkpoint, without target-facing or approach. Affordability does
not spend mana. The action (`0x27AB0`) attempts up to four creations,
checking available mana before each and charging only successful ones.
A failed attempt neither spends mana nor ends the loop. Order 22 creates
water elemental type 31 for 255; 23 creates daemon type 28 for 255;
24 creates spider type 21 for 60; 25 creates scorpion type 24 for 60.
The action then queues idle for its next checkpoint (`0x27BCC`).
These orders now execute through the live script kernel.

The factory (`0x2E740`) searches before allocation or creation draws.
The exit search (`0x2E554`) walks clockwise rectangular rings from the
corner chosen relative to the requested destination, rejecting map edges
and terrain/occupancy mask `0x3CC`. Failure leaves the input coordinates
unchanged. Its vertical expansion is asymmetric: only an upper-right
start shifts downward; every other start shifts upward. A conventional
geometric expansion is not equivalent. The independent search matches
512 direct original cases. Summoning rebuilds live occupancy between
attempts, so each new unit blocks the next search. The legacy worker
exit wrapper still retains its old failure fallback; failed worker exits
are not yet reconciled.

The spell-body comparison covers all 256 mana values, check/action calls
and all four orders with a fully blocked original map. Those 2,048 cases
verify eligibility and failed-placement state changes, not successful
original allocation. Separate synthetic callbacks cover failed-attempt
retries and successful-only charging. Successful creation is additionally
exercised by the extended gameplay comparison. Shared missile/effect-pool
allocation remains outside the native appearance effect.

Summoned types 21, 24, 26, 28 and 31 use a separate 45-tick decay countdown.
The level-placement flag `0x400` suppresses decay entirely. A due update
removes one mana; once mana is already zero it subtracts five HP, or enters
death when HP is below five. Exactly five becomes zero without immediate
death: only the next due update requests it. The portable routine and live
status counter now preserve this distinction. There are 624 direct original
decay cases; the subsequent death request is checked synthetically, not
through the original's full death call graph. Native HP-zero occupancy and
target eligibility still require lifecycle reconciliation.

The isolated summoning build advances Orc10's first RNG split from
10,185 to 10,579 and first tile/existence split from 10,207 to 10,659.
The four scorpions appear at the original action time. With exact decay,
human8 matches both extended windows through 20,000 ticks: the old native
timer incorrectly killed level-placed monsters. These are selected extended
results, not full spell conformance.

The full summoning/decay/invisible-idle regression again matches all 24
10,000-tick tile and RNG windows and their logging-disabled replays.
Invisible idle soldiers skip their ordinary think acquisition, but the
route tail still runs (`0x10EE8`). On Orc10 this changes the knight's
post-move choice from a distant catapult to the original move/attack
reacquisition of the adjacent necrolyte. The next RNG split is 10,585;
the ensuing combat still needs reconciliation.

### Healing, unholy armour and shared targeted spells

Healing (`0x27760`, order 17) rejects buildings. Its affordability check
accepts any nonzero mana, although the AI waits for at least three.
The action restores at most forty HP towards the target type's base HP,
spending three mana per restored HP. For a daemon, it instead subtracts
up to forty HP, limited by its remaining HP and the caster's mana.
Neither path invokes damage or immediate death; a daemon can reach zero
HP here without becoming an inactive record. The action still requests
its effect when no HP changes. Word arithmetic is preserved, including
targets already above their type's base HP.

Unholy armour (`0x278C4`, order 21) costs 125 mana and sets a 1000-tick
timer. Targets at thirty HP or below become one HP; healthier targets
take floor-half their current HP. This is not native war1's thirteen-second
timer or subtraction of floor-half. Both protection timers decrement once
after the unit/missile pass, skipping inactive records but not merely
inside records (`0x2D3D0`).

Orders 17, 19 and 21 now share the original eight-tile movement, script
checkpoint, target validation, facing and action path. The native floating
armour timer is disabled under original tick ownership. The healing AI
considers non-hostile owners, not just its own player, and scores loss
against base HP. Armour selection uses base HP too. The spell payloads
match 49,152 direct original calls, and timer handling matches 216 original
status updates. As with invisibility, those payload comparisons use a full
original effect pool: shared effect allocation is still not implemented.

`bash tools/test_live_spells.sh`, after `bash host/build.sh`, builds a
test-only game translation unit and uses the real user's resources and
headless script loop. Twelve fixtures check the forty-HP cap, near-full
healing, a daemon reaching zero, both sides of armour's thirty-HP boundary,
all four player summon commands, resurrection, poison and replacement of a
deferred AI redirect. They check deferred
action, actual HP/mana, protection, completion and created-unit counts.
Resurrection additionally checks a blocked corpse, success-only payment,
retained facing and multi-corpse creation. The redirect fixture checks that
the replaced state is released and player priority remains intact. Poison
checks deferred payment, script-driven pulses and caster immunity without
a legacy damage component. All twelve also
pass with `SAN=-fsanitize=address`. The fixture hook is not compiled into
the normal game. Player summons now submit the original cast order with
following idle rather than immediately spawning in the command handler.

All 24 original 10,000-tick tile/RNG windows and their unlogged replays
remain exact after these changes; all 24 AI-versus-AI replays remain
deterministic. The campaign measurements are still finite windows, not
proof of complete spell or lifecycle parity.

### Script control: complete opcode audit

`0x10473` decrements the byte wait **before** testing it, so zero means
256 ticks, not "run now" or "uninitialized". It executes operations until
a wait/checkpoint/stop yields, saving the PC after that instruction.
`0x26450` selects an animation and initializes the wait to one.

| Opcode | Handler | Effect | Status |
|---|---|---|---|
| 0 | 0x1041F | Wait one tick, yield with a checkpoint result. | verified |
| 1 | 0x103F5 | Store the following byte as the wait; yield without a checkpoint. | verified |
| 2 | 0x103D2 | Store wait zero and yield; after 256 ticks continue **after** the stop. | verified |
| 3 | 0x10415 | Continue at the encoded little-endian absolute offset into the script resource. | verified |
| 4 | 0x10426 | Set the frame from the next byte and continue. | read |
| 5 | 0x1042D | Move by the next signed byte along the current direction and continue. | read |
| 6 | 0x1044C | Perform move, then frame; continue. | read |
| 7 | 0x10402 | Wait operand minus the owner's speed upgrade, with byte wraparound. | verified |
| 8 | 0x10457 | Clear and play the queued sound, then continue. | read |
| 9 | 0x10468 | Invoke the current order's action through `0x1077C`, then continue. | read |
| 10 | 0x103DD | Wait operand plus one when the slowdown flag is set, with byte wraparound. | verified |

All eleven handlers are called indirectly by `0x10473`; `0x1044C` also
calls the move and frame handlers. All have been read. Before interpreting,
`0x2ECFC` obtains the speed modifier from the owner's upgrade low nibble
and slowdown from unit `+0x49` bit `0x20`. `0x1077C` skips the action
when unit flag `0x4000` is set; otherwise it clears invisibility and
dispatches through the order-action table at `0x510A0`. That action
dispatch is separate from the checkpoint-order table: orders 4 through 8
call `0x106EC`; 10 and 14 call the no-op `0x106E8`; spells 17 through 27
call `0x27BCC`; the other entries are null. In particular the work
script's action is not where gathering counts its strokes.

The four live script paths now run the kernel interpreter through the
adapter, sharing a single persistent PC and byte countdown. It preserves
encoded jumps into earlier/shared blocks, rather than
restarting an animation or rejecting a PC below its entry point. Stop
keeps its continuation PC; wait zero is no longer an idle-entry sentinel.
Malformed/truncated instructions and exhausted instruction budgets are
reported rather than silently restarting a script. The synthetic
`tools/test_unit_scripts.c` cases exercise shared blocks, stop continuation,
zero/underflow/overflow waits and invalid instructions without game data.
Run them with:

```sh
clang -std=c11 -Wall -Wextra -Werror tools/test_unit_scripts.c -o build/host/test-unit-scripts
build/host/test-unit-scripts
```

The synthetic checks pass with address/undefined-behaviour sanitizers.
Before live kernel integration, all 24 seeded five-minute RNG comparisons
were unchanged from the attacker-retention result below. That verified
the shared-decoder control-flow slice, not the entire `0x10473` call graph.

Visual action reconstruction still approximates loops, and sound/frame
presentation remains native. The kernel implements the action suppression
and invisibility branches, but complete attack/spell action bodies and
canonical command flag ownership remain separate migration work.

### Attack checkpoint guards and remaining reconciliation

The bodies of `0x113E0`, `0x1143C`, and the two wrappers `0x114D8`/
`0x114EC` establish an important ordering distinction. Fixed-target
order 5 and pursuit order 6 run the shared validation/approach stage
**before** alerting friends. Order 8 requests that approach stage only
for non-casters whose range is at most one. If it changes the order,
the rest of the old checkpoint does not run.

After that stage, order 5 alerts eligible friends, checks shot mana,
then selects facing and the queued sound. Orders 6/8 alert friends,
run catapult safety, test range for the single-tile fallback or idle,
check shot mana, then select facing and sound. The shared native attack
checkpoint now performs the required movement reissue before alerting.
The complete target/command and facing/sound call graph is not yet reconciled.

`0x11140` first rejects a catapult target below minimum range. Otherwise
it scans the clipped 5x5 occupancy square centred on the target's
**top-left tile**, not its footprint centre. It counts non-hostile
occupied **tiles**, not distinct units: a human-controlled catapult
tolerates zero, other controllers two. Neutral occupants count too.
The independent guard queues idle when either safety check rejects the
shot. It matches 512 direct original cases, including clipped corners,
interior tiles, building footprints, repeated occupant pointers and
different controller/owner combinations. Its live occupancy bridge uses
canonical records and current native footprints; complete entry/exit-grid
ownership remains separate worker work.

On Orc10, the missing guard allowed catapult native entity 56 to
fire into friendly necrolytes near the newly visible knight at tick 10,584.
The original cancels at the preceding checkpoint. With this guard,
Orc10's first extended RNG split advances to 15,354 and its tile/existence
split to 15,355; human7 retains its later 10,499/10,529 splits.

`0x110A4` lets non-casters pass. A caster needs 5 mana for controller
type zero, otherwise 1. When short, a retained attacker triggers the
existing randomized flee command and is then cleared; without one, idle
is queued. This is a checkpoint decision, not a failed shot.
At the script action, `0x106EC` deducts that byte cost **before**
attempting missile allocation; allocation failure does not refund it.
The port now checks affordability at the checkpoint and performs byte
subtraction before attempting allocation, using the raw controller byte
rather than an owner-number or autoplay classification. The payment
matches 3,840 original actions with a full missile pool, including byte
underflow; affordability thresholds are checked separately. Worker/hero
and low-mana-caster fleeing share the two-draw destination calculation,
normal command clipping and attacker clearing.

The full combat regression retains all 24 exact 10,000-tick tile/RNG
windows and their unlogged replays. In the selected 20,000-tick combat
checks, human12 now matches both windows; human10 matches RNG throughout
and first differs in tiles at 19,249. These replace the summoning-only
milestone above, without asserting full combat conformance.

A fire elemental's melee branch additionally creates an effect before
rolling damage; this remains outstanding. A targetless strike now uses
the terrain-damage routine. Shared effect allocation and complete target
validation still prevent a claim of full combat parity.

### Logical attack facing at checkpoints

Facing (`0x1DFE0`) uses the target's logical tile, or its stored goal,
not its interpolated sprite rectangle or the nearest building edge.
Direction quantization (`0x1DF50`) uses half the larger absolute
displacement, rounded down with minimum one, as the diagonal threshold.
Coincident tiles face east. The independent implementation matches all
16,129 map-valid displacement pairs against the original.

The native attack updater previously recomputed facing from sprite
geometry outside the original checkpoint. Even with matching hits and
tiles, this changes a worker's next flee destination: `0x107A0` uses
the retained attacker's facing. Human9's worker slot 18/native 34
originally reissued its flee towards `(40,54)` before tick 10,054;
the native attacker instead sent it towards `(44,54)`. Their first few
path tiles agree, hiding the error until tick 10,178.

Attack facing and animation selection now belong to the original
checkpoint; the ordinary native updater only prepares new attack
commands. Terrain attacks and targeted spells share the independent
quantizer. The selected Human9 comparison advances to RNG 17,384 and
tile 17,385; Orc7 now matches both 20,000-tick windows. Human7 and Orc10
retain their preceding extended splits. The complete all-mission regression below also eliminates Orc6's late
tile split, without bringing any mission's tile or RNG split forward.

### Resurrection selection and payload

The corpse selector (`0x2A4BC`) scans all two hundred original records,
not the live occupancy grid or the native visual corpse entities. It
accepts type 51 with the dying bit set and no free, removed, inside or
placeholder bits. The first record at squared distance less than one
hundred supplies the spell's point goal. Command order 20 approaches
within five tiles and faces the stored point at its checkpoint.

The action (`0x2799C`) needs seventy mana but pays only for successful
skeleton creation. It scans the same stable records for eligible corpses
within squared distance thirty-six of the **caster**, not the stored
goal. Failed creation continues without spending. Successful creation
spends seventy and sets the corpse's inside bit; it does not immediately
free that record. The scan ends when mana is below seventy. The action
returns success even when no skeleton can be made, then the normal spell
epilogue queues idle. Skeleton creation preserves the corpse's facing,
starts its summoned-unit lifetime and draws only its normal start delay,
not a new facing.

The independent selector and payload now replace the native visual-corpse
scan and action. Order 20 uses point movement with range five, the original
script checkpoint, deferred action and queued idle. Native corpse visuals
retain their source record identity so resurrection and original record
expiry remove the right visual without releasing a still-running script.
The selector and blocked-creation body match 2,048 direct original cases
covering all byte mana values, both action/check modes, flags, strict
search radius and inclusive action radius. Portable and full-game fixtures
also cover failed placement followed by successful creation.

The full 20,000-tick comparison advances Human5 and Human11 to both full
windows, Human7 to tile 17,667/RNG 18,688 and Human9 to tile 17,681/RNG
17,680, without regressing any preceding split. All 24 extended unlogged
replays and all 24 self-play replays match. Self-play reaches twenty full
windows, a Human10 win at 9,557, and the same three earlier defeats.

### Replacing an already queued AI command

The original command constructor and queued-order setter overwrite an
earlier pending command (`0x2E1F0`, `0x2EFC4`). The native state machine
normally keeps the first equal-priority request instead. Under original
tick ownership, a later AI reset now replaces an earlier AI reset;
the existing player, status and lifecycle priorities are unchanged.

Orc10 exposes the mismatch when two clerics select the same moving
archer. The first redirects original slot 76/native 250 at tick 15,350.
Its move remains pending while the walking script finishes its tile.
The second cleric, original 75/native 95, selects it at tick 15,353;
the original overwrites the pending move and queues that cleric's spell.
The native equal-priority rejection previously abandoned the whole
second cast, leaving an extra idle-turn draw. The recipient's original
invisibility timer is still zero throughout these checkpoints.

With source-equivalent pending replacement, the Orc10 comparison
advances from tile 15,355/RNG 15,354 to tile 17,427 and a complete
20,000-tick RNG window. The full all-mission comparison preserves every
first split from the resurrection build: nineteen tile windows and
twenty-two RNG windows are exact. All 24 unlogged and self-play replays
also match.

### Poison and area targeting

Area targeting (`0x2A864`) scans the eleven-tile box near the attack
destination, provided the caster is within twenty-five tiles. The first
eligible hostile occupied tile supplies a unit whose top-left coordinates
receive two draws, X then Y, each modulo five minus two. There is no
explicit clamp in that routine; the command constructor subsequently
normalizes its stored goal. The independent selector now uses original
record identities and occupied footprints, rather than sorting native
entity origins. It removes the random expressions from the
multiply-evaluating `CLAMP` macro and does not clamp the returned aim.
The movement adapter's out-of-map goal normalization still needs
reconciliation; it must not be mistaken for completed edge-case parity.

Poison cloud (`0x27914`, order 27) spends fifty mana before attempting
its shared-pool allocation. Its live command now approaches within nine
tiles, casts at the original script action and queues idle. It allocates
from the same one-hundred-record array as missiles. Each cloud runs its
animations from the player's DATA.WAR, drifts before each pulse, and expires
after twenty-five pulses or leaving the map. Each occupied tile in its
three-by-three area receives five damage, or one for a building tile;
the source record is exempt. It neither damages empty terrain nor
quarters peripheral damage. A separate visual follows the effect's
position and script frame without a legacy poison-damage component.

The formerly discarded environmental draws update the signed wind bytes
(`0x2D3D0`): one draw tests the low four bits; on zero, another chooses
the axis and a third adds modulo three minus one, bounded to -2 through 2.
This happens after the effect pass, before status timers.

Direct original comparisons cover 1,024 area selections, 20,000 wind
ticks and 1,536 poison affordability/allocation cases. Successful
allocations also compare script PC, byte wait, phase, frame, position,
countdown and release against the original interpreter using synthetic
programs on empty terrain. Portable fixtures additionally cover full-pool
payment, the first free slot, repeated building-tile damage, caster
immunity and boundary departure. The live resource fixture covers deferred
payment and actual script pulses. Human9 now matches both selected
20,000-tick windows and its unlogged replay; Human7's split is unchanged.
The full poison-build regression is still pending.

### Remaining area effects

Rain (`0x2793C`, order 26) also spends
fifty per action, attempts four effects, and continues casting until
an affordability check queues idle. Its AI's 150-mana threshold is
not the per-action price. Each rain effect (`0x11CD8`) allocates
before drawing its two modulo-seven pixel offsets. A duplicate of an
earlier impact retries both draws; allocation failure draws neither.
The native five-projectile, twenty-five-mana action is not equivalent.
Its constructor must be integrated with its per-tick effect handler.

The shared pool (`0x119C0`) scans one hundred records and clears the
first with the free bit. It is updated after all units by the same
resource-script interpreter, operating on each effect's animation fields,
then an eight-entry checkpoint table at `0x511F4`. Its animation lookup
(`0x26480`) uses the same DATA.WAR script resource, with the effect-type
table starting at byte 104. The current native missile implementation
still substitutes fixed waits for ordinary missiles and does not
allocate ordinary spell visuals from this shared pool.

All eight effect checkpoint handlers have now been read. States zero,
one and two initialize, fly and arc ordinary missiles; six applies an
impact and releases it. State three (`0x122D0`) moves a poison cloud by
the signed wind bytes, damages its area, decrements its twenty-five-pulse
counter and releases it on expiry or departure from the map. State four
(`0x12338`) drops each rain effect four pixels per checkpoint. Every
impact allocates a type-seven visual and applies area damage; that same
rain record repeats its fall three times before release. State five
releases a finished effect; seven is an error handler. Apart from poison,
these resource-script-driven effect phases remain unintegrated.

### Strike attribution and target validation: branch audit

The field often called "last attacker" in these notes is **not the most
recent attacker**. `0x10CE0` has exactly these decisions:

1. Ignore a non-hostile source. The matrix is human owner 0 against
   owners 1 through 3, and vice versa; owner 4 is neutral to everyone.
2. Ignore an inactive victim (flags masked by `0x207`).
3. If the retained attacker exists and is still active under that same
   mask, leave the pointer unchanged, regardless of who struck now.
4. Otherwise remember this source.

There is no distance, damage, visibility, invisibility or immunity check
in that routine. `0x10650` calls it **before** checking unholy armour and
before rejecting zero damage, so a miss can establish the retained
attacker but cannot displace a living one. The port previously overwrote
the pointer on every strike. `wai_noteAttacker` now follows the retention
rule, treating death/collapse already queued in war1 as inactive and
excluding corpse entities. This attribution routine is verified; the whole
damage/death call graph is not yet verified.

`0x2216C` is a different predicate, not interchangeable with `0x10CE0`.
It uses mask `0x20F` (also excluding units inside buildings) and the
invisibility timer at `+0x36`. With no target it returns success without
examining the attacker. With an invalid target it clears that target and
clears the attacker **only if it was the same pointer**, then returns
failure. With a valid target it separately clears an invalid attacker.
After a lethal strike, `0x10650` invokes this on the **killer**, after
death processing and accounting. The port still needs the shared
validation semantics reconciled across all five callers; delaying all
cleanup to an attack checkpoint is not equivalent.

`0x2E23C` first deselects, releases AI job/gather counts, immediately
selects death order 1, marks the record inactive and decrements ownership
counts. Mobile units leave the occupancy grid immediately. Wounded units
and neutral Lothar take the direct-removal branch. Lothar/Garona trigger
defeat; Griselda schedules an all-out wave. Buildings additionally create
their death effects, schedule the computer's all-out response (a keep
affects owners 1 through 3), release production/research/wall reservations,
adjust unfinished-building counts and recursively kill hidden occupants
whose target is that building. These branches must not be replaced with
just an HP-zero test. The keep's effect constructor attempts 30 effects:
two position draws even if allocation fails, then one delay draw only on
success. Effects share the same 100-slot pool as combat missiles, so
discarding them as purely cosmetic would also change later shot allocation.
The ordinary collapse sound uses a separate platform RNG, not `0x19478`.
There is also an explicit defeat branch when flags equal `0x82` and the
destroyed building is original type `0x26` (human tower).
Selection/audio presentation's downstream callees remain outside this
completed damage-branch reading.

### Wall terrain bits and same-pass placement

The original's terrain word (`[0x5860C]`, 64x64 words) carries more than
the level's passability resource: bit `0x2`/`0x1` road, `0x100` unit
occupancy, `0x200`/`0x800` buildings, and the wall bits `0xC`. A wall
piece is laid by `0x2C054`, which clears the word's low nibble and ORs in
the piece's bit, then refits its four neighbours. The bit comes from a
table by **owner index** (`0x51DC0`): 4 for owner 0, 8 for owners 1 to 3,
none for owner 4. It is not chosen by race. Roads use `0x51DCA` (1, 2, 2,
2, 0). Starting constructs (`0x2C2D8` roads, `0x2C2F8` walls) and a
finished wall (`0x143CC`, through `0x2C23C`, which also sets durability
20 in the visual word) take the same path.

war1 keeps walls as `WarWallPiece` records, not terrain, so every port
reader of bits `0xC` saw no wall: the stop range (`0x222FC`), the
targetless strike, order-7 validation, the blocked-step terrain attack and
placement. Human 10's grunt (original slot 78) is ordered to move to a
computer wall tile, 16,18 (terrain word `0x0008`), with attack order 6
next. The original stops one tile short at unit range, takes order 6 for
one checkpoint and then idles. The port used range 0, failed to plan
("line free, 0 steps") and idled a tick early.

The port now lays each finished war1 wall piece into its terrain words,
as `0x2C23C` does, and from then on the terrain words are the wall:
- the owner's bit replaces the word's low nibble;
- the tile and its four neighbours get their artwork from the wall shape
  (`0x55104`), chosen by which neighbours have a wall bit (8 below, 4
  right, 2 left, 1 above). The shape is a row of the tileset's
  wall/transition resource, the one `0x2BA0C` loads (the port's
  199/203/207: rows after a 4-byte header of width and height). Column 0
  is a finished wall, column 3 one being built; the artwork is the entry's
  low 12 bits plus 9;
- the laid tile's durability is set to 20 in the visual word's top five
  bits.

Strikes then wear the wall through `0x105D0` and its transitions. When a
transition removes the wall bits, the war1 piece's HP becomes 0. A laid
piece is drawn by its map tile instead of war1's own wall table, and
war1's native wall damage is routed into the same wear. Wall pieces are
no longer extra obstacles in `wmv_built`, so the planner's hostile-wall
rule (`0x2CD44`) applies to them, and a computer unit blocked by a player
wall attacks it as terrain (order 7). Against the original, all 16 wall
tiles near Human 10's grunt split have the same terrain and visual words
(bit, artwork and durability) before the first tick. Still not modelled:
the under-construction column and placeholder bit `0x10`, roads (`0x1`/
`0x2`), and the building bits `0x200`/`0x800` in these words.

A unit put on the map marks its tile's occupancy at once (`0x13928`,
called from creation `0x2E7E4`, from `0x2E6B8` and from the walk
checkpoint `0x223D8`). Before this, the port rebuilt `wmv_unitAt` only at the start
of each tick and on walking steps. In Orc 10, worker slot 71 finishes
delivering (order 13) in hall 41 during tick 17426 and comes out onto
54,5. Peasant slot 74 then finds 54,5 occupied and waits 40 ticks (blockage
1). In the port the peasant walked onto the same tile. `wmv_occupy` now
marks the tile when a worker leaves a hall or mine, a building finishes
training, or a summoning is placed.

### Verification corrections and current measurement

The old harness hid several distinctions:

- The reference runtime deliberately omitted mission-1 RNG records;
  "tick 1" was an empty log, not a seed mismatch. Local `WAR_AIFIRST`
  logging and a strict final-mission RNG reader now cover both mission 1s.
- A fresh reference trace could use a different placement seed from the
  cached comparison. The local runtime now accepts `WAR_RNGSEED` before
  placement; draw/watch runs reuse the cached seed and draw caches are
  checked against it. In this session the old cached seed was
  `6aeb64b4`, but an unseeded fresh trace used `6aecb634`; those traces
  cannot explain each other's decisions.
- The RNG comparison now requires the requested tick count, rejects
  empty/short/discontinuous logs, selects the last restarted mission and
  ignores a longer run's extra tail.
- `WAR_AIEVERY=1` logs **every tick** in both host runtimes.
  `tools/ai_ref_compare.sh` then requires all 10,000 ticks for five
  minutes; `--tolerance 0` requires exact tiles. The comparator checks
  sample alignment and notices port survivors whose paired original died.
  Corpse entities are excluded from the host's active-unit log, as they
  are by the original's inactive-record mask.

After the order/checkpoint and pending-command changes, before the
attacker-retention change, 11 missions matched every active unit's tile
for all 10,000 ticks: human 1, 2, 8, 10, 11; orc 1, 2, 4, 9, 11, 12.
13 missions matched RNG (also human 5 and 12). Every comparable mission's
first RNG mismatch stayed put or moved later: human 3, 8022 to 8049;
human 11, 9491 to beyond 10000; orc 2, 6796 to beyond 10000; orc 5,
5790 to 5815. No whole-handler status is inferred from those numbers.

With retained-attacker attribution, orc 5's RNG split moves again,
**5815 to 6494**. Every other mission retains the same first RNG split,
and orc 5's earlier newborn-worker tile divergence remains at 1825.

After the worker, record-retention, route, visibility, target, post, terrain and invisibility changes, the
complete per-tick comparisons retain or improve every first divergence
from the previous complete sweep. Both checks require ticks 1 through
10000; the placement seeds were checked against the cached reference
manifests. `=` below means no divergence
through tick 10000, not that the mission or every branch is fully ported.

| Mission | Human tile | Human RNG | Orc tile | Orc RNG |
|---|---:|---:|---:|---:|
| 1 | = | = | = | = |
| 2 | = | = | = | = |
| 3 | = | = | = | = |
| 4 | = | = | = | = |
| 5 | = | = | = | = |
| 6 | = | = | = | = |
| 7 | = | = | = | = |
| 8 | = | = | = | = |
| 9 | = | = | = | = |
| 10 | = | = | = | = |
| 11 | = | = | = | = |
| 12 | = | = | = | = |

The subsequent full 20,000-tick pending-command sweep matches nineteen
tile windows and twenty-two RNG windows, preserving every first split from
the preceding resurrection build. It includes mana, summoning,
decay, catapult safety, shot payment, the caller-base-HP home-defence
condition, original-slot target ties, logical checkpoint facing and
resurrection and replacement of deferred AI commands.
All 24 extended unlogged replays passed. All 24 self-play replays also
matched: twenty full windows, a Human10 win at 9,557, and the same three
defeats.
`=` here means no split through tick 20,000.

| Mission | Human tile | Human RNG | Orc tile | Orc RNG |
|---|---:|---:|---:|---:|
| 1 | = | = | = | = |
| 2 | = | = | = | = |
| 3 | = | = | = | = |
| 4 | = | = | = | = |
| 5 | = | = | = | = |
| 6 | = | = | = | = |
| 7 | 17667 | 18688 | = | = |
| 8 | = | = | = | = |
| 9 | 17681 | 17680 | = | = |
| 10 | 19249 | = | 17427 | = |
| 11 | = | = | = | = |
| 12 | = | = | 16834 | = |

The poison/area/wind, later-AI-command replacement and same-turn
failed-route idle changes (commit 7e13d2f) bring the 20,000-tick sweep to
**22 tile and 24 RNG windows**. Human 7, Human 9 and Orc 12 are now exact;
Human 10's tile split stays at 19249 and Orc 10's moves from 15355 to
17427. All 24 unlogged replays and all 24 self-play replays match;
thirteen live spell/transition fixtures pass under ASan.

| Mission | Human tile | Human RNG | Orc tile | Orc RNG |
|---|---:|---:|---:|---:|
| 1–9, 11, 12 | = | = | = | = |
| 10 | 19249 | = | 17427 | = |

Reading the owner's wall bits into the terrain queries and marking a
placed unit's tile at once ("Wall terrain bits and same-pass placement")
removes both remaining splits: **all 24 missions match every unit's tile
and the RNG through 20,000 ticks**, with all 24 unlogged replays matching.
Fifteen live fixtures pass under ASan. With walls laid in and worn through
the terrain words, the same 24/24 holds. All 24 self-play replays match.
Human 5, Human 6 and Orc 5 lose at the same ticks as before. Orc 10, which
won at 10777, now reaches the end of its 11,000-tick window, because the
self-play game itself changed. This is a finite window, not proof of every
branch; the unfinished boundaries listed below remain.

The active scope remains original-code reconciliation, not trace-driven
timing adjustments. The shared target validator's remaining callers,
complete death/occupant/role/effect lifecycle, worker branches, remaining
spells and full canonical order dispatch are still incomplete.

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

When a computer's mine is hidden from the human, mining deducts at most
5 from the mine but grants a load up to 100. Visible mines use the ordinary
deduction; a nearly exhausted mine limits the load.

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
- Home defence searches within 10 tiles of home when the caller is
  within 30 tiles, except when the hall's current HP equals the caller
  type's base HP. Soldiers and casters now share this source-derived
  condition instead of the old damaged-hall test. Target ties scan
  original unit slots, not native entity-pool slots.
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
- `PYTHONDONTWRITEBYTECODE=1 python3 tools/test_order_gameplay.py --self-play`
  runs all 24 missions with the paid human-side autoplay and repeats each
  with the same seed. It checks RNG replay, per-tick unit type/owner/tile,
  state and HP, kernel diagnostics, and real script execution. A short
  run is accepted only with an explicitly logged win/loss, and is reported
  with its actual tick count. AI-versus-AI uses the port's custom human-side
  strategy, not a corresponding reference mode: it is additional stress
  and determinism coverage, **not** proof of original-AI parity.

The healing/armour version's AI-versus-AI sweep and its 24 same-seed replays pass.
Twenty-one missions reach 11,000 ticks; human5 loses at tick 9,726
(9,727 logged ticks), human6 at 6,337 (6,338 logged ticks), and orc5 at
9,929 (9,930 logged ticks).
Replays reproduce the outcomes, RNG streams and checked unit
state traces, without kernel diagnostics. Reports and replay logs are in
`build/ai/kernel-self-play-war1-protection`; early defeats are not counted as full
five-minute coverage. The separate original-reference sweep still
requires all 24 complete 10,000-tick windows.

`PYTHONDONTWRITEBYTECODE=1 python3 tools/test_order_gameplay.py --ticks 20000`
extends the original-reference comparison to ten minutes, caching the
longer source traces and native results separately. It still enforces the
complete five-minute floor; later splits are reported for source analysis,
not silently accepted as ten-minute parity.
`--host PATH` selects an isolated host executable and keeps its output
under a separate executable-named directory, allowing further work without
changing the binary used by an in-progress baseline sweep.

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
  `0x2A258` (hall HP differs from the caller type's base HP, unit within 30 of the hall's tile + 1,1:
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

- **Attacking** (`0x21020`, set up by `0x2E1F0`/`0x22554`): orders 3
  (move) then 6 (attack). If the target is already within range the
  attack starts at once; otherwise the unit walks, and at every checkpoint
  of the walk a computer unit (not a worker; casters only answer an
  attacker) looks for the best target again (`0x11004` → `0x10908`) and
  goes for it; a human unit answers its attacker; on a plain walk the last
  attacker is forgotten.
- **While attacking** (`0x1143C` → `0x2EFEC`): orders with flag 4 (4, 6, 8)
  re-run `0x10908` allowing out-of-range targets, the current one scored
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

- **Walking** (`0x223D8`, port/war_move_frank.inc): one tile at a time,
  one unit to a tile. At the walk script's checkpoint the move order checks
  for arrival (within the order's range of the goal; a building as goal is
  its nearest edge tile, or the middle of a side, `0x221D8`), plans a route
  when it has none (`0x2CD44`: a straight line to the goal; past an
  obstacle a walk along its edge both ways round, `0x2C66C`, straightened,
  `0x2C880`, the shorter kept; at most 20 steps), and takes the next tile
  at once: the unit stands on it from the start of the step while the
  script slides the picture there (16 pixels). A unit on the next tile:
  wait 40 ticks, keep the route while it is a friend walking away (the
  first 7 times), else plan again, give up the 15th time (`0x22080`). A
  walking unit is no obstacle when routes are planned (`0x21F48`); an
  attack-move plans through enemies. Op 10 waits a tick longer while
  carrying gold or wood. A walk to a place ends at the first tile in the way
  on the line there (`0x224AC`).
- **Gathering**: at the mine or the hall the worker takes one more step,
  onto the building (order 12, `0x1CFF4`), is inside 80 ticks, and comes
  out on the first free tile of rings round the building, from the corner
  nearest where it goes next, clockwise (`0x2E554`). Wood: the player's
  search rectangle starts at the hall's corner and grows a ring at a time,
  scanned clockwise, and stays grown (`0x2AFEC`); the walk stops at the
  forest's edge on the line to the tree, which it chops (50 strokes of 2,
  30 ticks each for a peasant), or the nearest tree round it (`0x2BCD4`).
  Each trip brings 100.
- **Levels** (`0x1E8A0`, `0x1E734`): after the header the units come in
  groups, each a list of alternatives; the game takes one of each group at
  random. A unit record is 4 bytes, an amount follows only after a 0xFE
  (gold in 256s), a 0xFF ends the list. A mine with no amount gets
  26000–34191, rounded down to 100s. A unit that does not fit where the
  level puts it (`0x13990`) is not made. The orc kennel is 3 by 2.
- **Making a unit** (`0x2E7E4`): a random facing (not for a summoned one),
  a mine's random amount, and a random start delay of 0–7 ticks for every
  unit: its first idle checkpoint, so who thinks first. A neutral side's
  unit waits to be rescued with 5 hit points; healed above 5 it is rescued
  too (`0x14084`).
- **The tick** (`0x197B0`): the AI clock (a timed wave adds one to player
  1's quota, 16 bits, so an all-out's 0xFFFF goes round to 0, `0x29AF8`;
  the wave sends that many units of any kind but heroes, in slot order,
  `0x29A44`), then the unit table in slot order (each unit's script; at the
  idle checkpoint the think, then, with no new order, the idle turn
  `0x13FF0`), the missile table, and the screen's water and clouds
  (`0x2D3D0`), which draw random numbers every tick too.
- **Training** (`0x115F4`, `0x143CC`): taken on the think's tick; the
  building's checkpoint 11 ticks on and every 9 after; two checkpoints a
  step; the unit comes out at the second after the last: steps × 18 + 20
  ticks, on the first free tile round the building from its top left.
- **Blows and missiles** (`0x106EC`, port/war_fight_frank.inc): a melee
  blow rolls its miss at the attack script's action; a shot launches a
  missile that has rolled it and aims off by up to 7 pixels (15 on a miss)
  each way, flies 5 (arrow), 4 (fireball) or 2 (rock) pixels a tick with a
  random draw a step. Non-rock missiles strike the target, a miss for
  nothing; rocks instead apply the fixed per-tile impact damage described
  above. Casters pay mana a shot (5 human, 1 computer).
- **Visibility** (`0x2D740`): every tick each unit is marked seen or unseen
  by the human (and by the computer); Medivh acts only while the human sees
  him.
- **Invisibility and unholy armour** (`0x2A57C`, `0x2A714`): invisibility
  for a friendly knight or archer on the move above half health, which then
  walks to the re-aim target; unholy armour for a friend under attack at
  half health or less, not a building, a daemon or a catapult.
- **Catapults** (`0x29DE8`) take a route, except on the last level of each
  side, where they stay home. The flee point (`0x107A0`): 4 tiles the way
  the attacker faces, −2..+5 at random each way. A unit off a lost hall's
  spot (`0x29C9C`): 4 tiles a random way from the spot's corner + 1.

### Orders and their first ticks

Found by watching single units tick by tick in the reference:

- **A new unit's role** is given inside unit creation (`0x2E7E4` calls
  `0x29DE8` between the facing draw and the start-delay draw), so its route
  roll comes before the other units' turns in that tick. The route pick
  (`0x29D48`) takes `rand % 100` and walks the weight list, taking the
  first route with `r <= weight`; it orders the attack-move to the first
  waypoint at once (`0x2E1F0` with `0x21020`). That order is taken at the
  unit's first idle checkpoint, after its start delay.
- **Peasants deliver to their own player's** nearest town hall or keep.
  war1 took the nearest hall of the race, anyone's. war1's `building` flag
  is also set while a hall trains, so it doesn't mean "under construction".
- **The human's answer to an attacker** out of reach:
  - a ranged unit gets attack order 8. At its script's first checkpoint
    it can try the single free neighbouring tile towards the target
    (`0x11298`), a plain move with nothing next, then idle. It does not
    start a follow-to-range order; the next think chooses again;
  - a melee unit gets order 3 with the attack next, at once. On the way it
    answers no one, and on arrival it attacks.
- **An attack-move ends** in its attack order (6) with no target. The
  unit spends one tick there, then goes idle (melee and ranged alike).
- **A new target** restarts the attack script. The look-round runs at the
  script's checkpoint (op 0), once per blow cycle.

### Checking it tile by tile

`tools/ai_ref_compare.sh RACE N MINUTES` plays the mission in the
reference and in the port's host build, starting the port from the seed
the reference had when it loaded the level (`WAR_RNGSEED`). For strict
measurement use `WAR_AIEVERY=1 tools/ai_ref_compare.sh RACE N 5 --tolerance 0`:
every active unit's tile and existence are compared at all 10,000 ticks.
The default 33-tick sampling with one-tile tolerance is only a coarse
diagnostic. The current exact measurements are recorded above.

### What still differs

- native fog artwork and vision-spell effects are not yet driven by the
  independent exploration model; campaign computers intentionally ignore fog;
- a catapult's terrain damage and shared impact-effect allocation remain
  native/incomplete; its unit blast now runs at the original missile checkpoint;
- the workers' job counts are counted afresh each tick, where the original
  keeps them as role bits, set when a job is given and cleared at the
  worker's next think.
