# Bugs and suspicions found while playing (agent games)

1. FIXED: map tileset sprite freed per forest/road/wall/ruin entity at map exit (crash after a mission).
2. FIXED: a walk handing over to a player's attack ran two scripts in one tick ("two scripts in tick").
3. FIXED: a player's attack on the ground (no target) did nothing: the units stayed idle.
   The original's Attack button is command 4 (0x20FC4): unit target -> order 5, wall tile
   (bits 0xC) -> order 7, open ground -> attack-move order 6, each after the walk.
   war1's attack state with no target was never prepared under the kernel. (human2, tick ~23000;
   human2-win.txt has one attackxy and must be re-recorded or re-checked.)
4. FIXED: a walk handing over to a player's spell (healing) ran two scripts in one tick (human4 t64895, cleric 22); same pattern as 2, in wst_frankCastTick.
5. NOT A GAME BUG (agent tool): war1's updateCommandButtons asserted on a selected non-unit.
   war1 drops dying, collapsing and hidden units from the selection; only the agent's "sel"
   added any id (a stale id reused by a forest/road entity in a diverged replay). "sel" now
   takes only living units on the map.
6. FIXED: human 6 and orc 7 level headers give player 0 the enemy's race; war1 then counted no farms for food (the hall could never train). Player 0 now takes its campaign's race.
7. FIXED (log only): a move giving up while the player's newer order was pending reported 'failed movement completion' (human6, knight 39); the pending order takes over.
8. FIXED: an order given mid-step (heal, work, stop) ran its script while the walk step was
   unfinished: "two scripts in tick" for clerics and peasants (human6 try 5, clerics 771/994,
   peasant 1160). Only the step's script runs until its checkpoint now.
9. FIXED: a spell started with the range checked at order time; a recipient that walked off
   left the caster in a walk script without a move ("invalid targeted spell order").
10. TOOL: the agent's "cmd N" ran any command on the selected building, even one with no
   button there (orc axes/shields researched at a human blacksmith in human6 try 5). It now
   takes only the commands the panel shows.
11. FIXED: war1 accepted a building that overhung the map edge (the rect check skipped
   off-map tiles); the agent's buildnear put a barracks and stables at -3,5 on top of each other
   (human7): "invalid attack-zone tile" from the enemy AI, then a pathfinder assertion crash
   when the construction finished. A building must now lie wholly on the map.
12. SUSPECT: "rejected flee movement" (human7 t~90000, entity 2483): a unit's auto-flee
   (0x107A0) was refused while a player order was pending. Check which should win.
13. FIXED: a walk begun mid-step (a player's command) never took its walk order: the step's
   checkpoint went on with the old script. A peasant sent to the trees while stepping into the
   mine (order 12) walked on with the entry script and stepped off the map's east edge (tile 64,9;
   the enemy AI then met "invalid attack-zone tile"). The walk's order is taken at the step's
   checkpoint now, and its script starts at the next tick.
14. FIXED: a player's wood order given to a worker standing by its last goal (a tree it had
   felled) was dropped at once: the wood state read the record's old goal, found itself there and
   no tree round it, and idled. It is "there" only once the work order (10) has been taken.
15. TOOL: attack/heal/harvest/repair by id now take only units a player could click
   (a stale id reused by a forest asserted in wu_getUnitCenterPosition).
16. FIXED: war1 dropped a tree from its forest whenever its neighbours left it no tile shape,
   without taking it down: it stayed standing in the walk grid, the pathfinder and the art, but
   could never be chopped. Every wood order to it failed, so 9 peasants idled with "harvestxy" on
   such a tree and lumber stayed 0 for 60,000 ticks (human7). Such trees stay trees now.
17. FIXED: war1 never freed an animation entity whose animations were spent (spell sparkles,
   impacts): about 400 of the 512 entities after 220,000 ticks of human7, then "Entity pool
   exhausted" and a crash training a unit (wu_setUnitCenterPosition on a unit never made).
18. FIXED (log): a cleric's heal reaching its action after the recipient died logged
   "missing targeted spell recipient"; it now does nothing, as with the original's cleared target.
19. FIXED: the command panel asserted (wu_getUnitCommands: no player) when a selection outlived
   its unit (human7, a 48-unit selection); the selection is pruned of anything not a living,
   owned unit each frame. (Same family as #5.)
20. TOOL: the macro's mine fallback sent a peasant to the enemy's mine, and "defend the
   workers" leashed the whole army into the enemy base (64 -> 22). Only listed mines now.
21. Version note: the walkthrough (warcraftone.blogspot.com, human map 7) says pre-1.21 sends all
   defenders at the player's town once the peasants are rescued ("unbeatable without patching");
   the port is 1.20, which matches the steady pressure seen here.
22. FIXED: with the original's 200-record unit table full (human12, ~76 of ours plus buildings
   and the enemy), training still made a war1 unit with no record: "all 200 records occupied;
   entity N has no script record" every tick (296,997 times), and those units had no script.
   The original makes nothing then (0x143CC: 0x2E740 fails), refunds the price (0x20E64) and
   tells the human "Cannot create more units"; the port does the same.
23. TOOL (human11 rework): research never started at the tower (a conjurer order the same turn
   overrode it; buildings with research queued now train nothing); spending now reserves the next
   wanted building's real price; idle conjurers summon water elementals, sent in pairs (summons
   decay); elementals and conjurers join the defence; worker roles fixed (GOLDERS, adaptive with
   spare lumber); the SPREAD grid stays on the map, and agent map clicks refuse off-map tiles (an
   off-map move asserted in the flow field: tool-only, a click cannot land there).
24. FIXED: a destroyed building did not kill its hidden occupants. The original's death routine
   (0x2E23C) kills, in turn, the hidden units whose target is that building: a builder inside its
   construction, a worker inside a hall or a mine. In the port the orc peon inside a town hall
   under construction survived each destruction and the AI rebuilt the (free) hall at once, for
   ever (human11 north-east town): a siege could not finish the town.
25. FIXED: war1's return-goods command asserted (crash) when no town hall was left
   (wcmd_executeDeliverCommand); a player whose halls are all destroyed could hit it. The order
   now does nothing then.
