# Agent campaign kit (host only)

Helpers that play the campaign through the host build's agent mode
(`WAR_AGENT=dir`, `ai ag ...` commands, see `port/war_ai_frank.c`), and the
winning replays. Nothing here touches the board build.

- `start.sh DIR RACE MISSION [BIN]` starts a game in `build/agent/DIR`;
  `turn.sh`, `until.sh` step it; `feed.sh SRC DST LAST` replays turns into a
  fresh game (same build) to resume from a point.
- `macro.py` economy and defence, `fight2.py` group tactics (HOLD, SPREAD,
  SPREADWALK, LEASH, SIEGE, KEEP), `bait.py`, `explore.py`, `doors.py`,
  `route.py`; `toscript.py DIR OUT` turns a game into a `WAR_SCRIPT` replay.
- `BUGS.md` the bugs found while playing (fixed ones are in git history).

Replays (`replays/*-win.txt`) only reproduce on the build they were made with;
`replays/README.txt` names the binary for each. Local binary names map to:
`war1-host-2f04753`, `war1-host-93bad3a`, `war1-host-245b6e9` (those commits),
`war1-host-agentwall` (bb40373), `war1-host-agent38` (dc76c05),
`war1-host-agent39` (0b2c613); human1-5 replay on a build of 3ae53ff. Build with `host/build.sh` at that commit, copy
`build/host/war1-host` to the name, then from `build/host/run`:

    WAR_AILOG=/dev/null WAR_FIXED=1 WAR_SCRIPT=../../../tools/agent/replays/orc12-win.txt \
        ../war1-host-agent39 --race orc --mission 12

Expect `campaign result: win at tick T` (the tick is in replays/README.txt).
