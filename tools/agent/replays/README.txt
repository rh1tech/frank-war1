Agent game replays (host script, frame-stamped). Replay:
  cd build/host/run && WAR_FIXED=1 WAR_SCRIPT=../../agent/replays/humanN-win.txt ../BIN --race human --mission N
(add WAR_WINDOW=1 to watch). Expect "campaign result: win at tick T".

Binaries: the game changes, so a replay plays out only on the build it was made with.
  war1-host (here): human1..5 wins, as recorded (2026-11-05 builds before the attack-button fix).
  build/host/war1-fix2 and later: human1, human4, human5 still win (same ticks for 5);
    human2 differs (its one ground attack now attack-moves), human3 differs (the agent's
    "sel" no longer takes a peasant inside a mine or hall, as a real player can't).
  war1-host-5db5207 (commit 5db5207): human6-win.txt, win at tick 78915 (try 6; tries 1-5 lost,
    human6-try*.txt). Plan: hold the base with the army at 42,47, 10 peasants on the 43,44 mine,
    footmen/archers/knights/clerics with smithy, stables and church upgrades; attack north
    (37,42 .. 52,10) with 28 at tick 67000 when the mine ran dry.
  war1-host-55c2304 (commit 55c2304): human8-win.txt, win at tick 58082 (Medivh at 19,14 behind
    doors 31,21-22 and 25,16; explore.py + attackxy on the door tiles). human7-try10..15 too.
  war1-host-55c2304: human9-win.txt, win at tick 193481 (tries 1-3 lost; resumed from hm9c turn 717; final strike on the SW base 12-15,53 with 31).
  war1-host-55c2304: human10-win.txt, win at tick 15938 (army spread 2 apart at 25,20 holding, catapults SIEGE=16,19 on the walled town).
  war1-host-55c2304: human7-win.txt, win at tick 195674 (h8a to bait end, then the army spread at 37,37 holding south of the corridor, 6 catapults SIEGE=44,54).
  human11-try1..11, human12-try1..7 (lost; see BUGS.md and the memory note).
  war1-host-2d608d3 (commit 2d608d3): human11-win.txt, win at tick 188478 (try 29: catapults+footmen from hm11o turn 341, NE siege at 50,32, home rebuild on mine 20, NW siege from the south at 18,19, last necrolytes at 14,58).
  war1-host-agentwall (2d608d3 + agent wallxy): human12-win.txt, win at tick 429877 (try 15 hm12p: walls round the base at 30k from hm12n, conjurers' water elementals in threes at each town, catapult sieges of NE (55,20), ML (24,28), TL (26,18 then 16,18) with the rest at home).
  war1-host-agent38 (agent train from the panel only): orc1-win.txt, win at tick 116638 (base mine 3000 only; peons to 39,1 then 41,53; 6 farms).
  war1-host-agent38: orc2-win.txt, win at tick 91044 (peons first; base mine 32,13 dry at ~67k; grunts explored; the last archer came to the base).
  war1-host-agent38: orc3-win.txt, win at tick 191857 (2nd hall 4,37 at mine 27; 27 grunts+spearmen with axes/shields/spears at 147k when the mines ran dry; town at 50-60,34-42 (east, by the ## marks) fell to the march at 165-172k).
  war1-host-agent38: orc4-win.txt, win at tick 42433 (explore.py with the whole party; Griselda's band in the SE corner).
  war1-host-agent39 (+ agent foes debug list): orc5-win.txt, win at tick 265372 (try 2, from o5 turn 421: 2nd hall 46,61 at mine 20; 16 catapults + 28 at 165k, spread hold 34,22 SIEGE 37,13 flattened the town 30-43,8-15; the last cleric at 55,62 found with the 'foes' cheat). Try 1 lost: army marched into the town (o5-loss1).
  war1-host-agent39: orc6-win.txt, win at tick 317891 (tries a-h; final o6h: 2nd hall 7,29 at mine 20, 3rd hall 55,37 at mine 23 at 128k, 10 catapults + 13 spearmen; bait at the bridge 34,42; siege hold 33,21 SIEGE 33,12 razed the town; the mill 28,3 by the tower 26,6 killed from 34,4 (beyond the 0x10908 scan, so the spearmen did not turn on the tower); last conjurer 42,44 by catapults).
  war1-host-agent39: orc7-win.txt, win at tick 101272 (tries 1-2 lost wood-starved; from o7 turn 238 (tick 49928, 17750 gold banked from the adjacent hall 33,47 at mine 19): 3rd hall 6,59 at mine 20, 3 barracks + smithy, 42 with 7 catapults at 81k, spread hold 36,13 then 26,9 with SIEGE 18,7 razed the compound).
  war1-host-agent39: orc8-win.txt, win at tick 157022 (maze with doors: explore.py + doors.py breaking the W tile next to explored ground; prisoner peons and Garona joined from the cells; then all to the exit 33,60).
  war1-host-agent39: orc9-win.txt, win at tick 172692 (hall 56,19 by mine 21 at the start; from o9 turn 109: spearmen + catapults (mill first, spear upgrades), no WORKERSFLEE (it broke the wood trips); 32 at 113k; west town by the north strip, hold 8,25 SIEGE 8,34; then south town hold 31-35,58 SIEGE 41,57/44,57; last cleric/footman hunted).
  war1-host-agent39: orc10-win.txt, win at tick 13059 (the starting army spread-holds at 59,25 below the wall gap 57-60,16; 4 catapults SIEGE 58,12; warlocks summon spiders sent at 58,10).
  war1-host-agent39: orc10-win.txt verified (toscript now skips stray lines). orc11-win.txt, win at tick 133841 (hall 20,60 by mine 19 at the start; 35 spearmen + 6 catapults by 85k; NW town hold 14,19 SIEGE 12,10; E town hold 42,32 SIEGE 50,32; last 2 knights at home).
  orc12-try*: lost (o12, o12b-e on war1-agent39); see the memory note.
  war1-host-agent39: orc12-win.txt, win at tick 390599 (o12j: from o12 turn 622 (NE razed, 42k gold): 19 catapults + 31 spearmen; S town by a SPREADWALK march + LEASH hold 23,44 SIEGE 23,51; E town by a spread march along the north, then leashed holds stepped 54,12 -> 52,21 -> 51,25 -> 51,27 -> 50,31 -> 51,35 -> 52,40 with no SIEGE (an out-of-range SIEGE walks the catapults into the gate); conjurers' rain of fire punishes a clumped march).
