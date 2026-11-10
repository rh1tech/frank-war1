# frank-war1

**Warcraft: Orcs & Humans on the RP2350.** The whole game runs on
one RP2350 microcontroller with PSRAM: HDMI video, sound with OPL2 music over HDMI or I2S,
USB keyboard, mouse and gamepads, and the game data on a microSD card.

The engine is [war1](https://github.com/acoto87/war1), an open-source
reimplementation of Warcraft: Orcs & Humans in C. The port adds a platform
layer for the board, and a computer opponent that behaves like the original
game's (WAR.EXE "FLOPPY VERSION 1.20"): same decisions, same order of work,
same random draws. `docs/ai.md` describes that behaviour in detail.

No Blizzard code or data is in this repository. You need your own copy of the
game's `DATA.WAR`.

## What's here

| Path | What it is |
|---|---|
| `master/` | Firmware: boot, console shell, HDMI video and audio, I2S audio, USB host, microSD, the game's platform layer (`src/game/`) |
| `port/` | Glue between war1 and the platform: a small SDL3 stand-in (`fsdl.c`), the original computer player, unit scripts, orders and movement |
| `common/` | Board definitions (FRANK Core 2, M2), clocks, PSRAM setup |
| `host/` | The same game on macOS/Linux with SDL3, for development: headless deterministic runs, input scripts, replays, audio capture |
| `third_party/` | war1, emu8950, Apogee AL_MIDI (see Credits) |
| `tools/` | Bench and debug tools (flashing, UART console, SD copy, frame capture, AI comparison) |
| `tools/psram_unstick/` | RAM-only loader that frees a board whose flash reads back as noise in BOOTSEL |
| `tools/agent/` | The kit that played both campaigns on the host build, and the 24 winning replays |
| `docs/ai.md` | How the original computer player works, routine by routine |

## Building

Firmware (Raspberry Pi Pico SDK, ARM GCC, CMake, Ninja):

    ./build.sh            # FRANK Core 2         -> build/master/frank-war1.elf
    ./build.sh M2         # Murmulator / FRANK M2 -> build/m2/frank-war1.elf
    ./build.sh M2 cdc     # development build: the USB port is a serial console
                          # (no keyboard; "~BOOT~" restarts into BOOTSEL)

Release firmware (both boards, into `release/`; `--publish` tags and makes
the GitHub release):

    ./release.sh 1.00     # -> release/core2_frank-war1_1_00.uf2, m2_frank-war1_1_00.uf2

Host build (clang, SDL3 via pkg-config):

    host/build.sh         # -> build/host/war1-host

## Game data

Copy your `DATA.WAR` to `/war1/DATA.WAR` on the microSD card. An optional
`/war1/frank.cfg` holds the settings: F12 opens the settings window (sound
output, volume, sound effects, music, mouse speed; F12 on the welcome screen
too). Sound goes out over HDMI (44.1 kHz stereo audio packets) or the I2S DAC.

For the host build, link or copy `DATA.WAR` into `build/host/run/` and run
the binary from there:

    cd build/host/run && ../war1-host                        # windowed play: WAR_WINDOW=1
    ../war1-host --race human --mission 3                    # straight into a campaign mission

The host build is headless and deterministic by default: virtual time only
advances while the game waits for its next frame. `host/plat_host.c` lists
its options (input scripts, recording, replay checks, WAV capture).

## Campaign replays

`tools/agent/replays/` holds a winning playthrough of every campaign mission,
human 1-12 and orc 1-12, as frame-stamped host scripts. Each replay
reproduces only on the build it was made with; `replays/README.txt` names it,
and `tools/agent/README.md` explains how to rebuild it and play a replay:

    cd build/host/run
    WAR_AILOG=/dev/null WAR_FIXED=1 WAR_SCRIPT=../../../tools/agent/replays/orc12-win.txt \
        ../war1-host-agent39 --race orc --mission 12

A replay ends with `campaign result: win at tick T`.

On this build the orc replays win with the fog edge fix off (`WAR_FOGFIX=0`
on the host, `fogfix=0` in `/war1/launch.cfg` on the board; it is on by
default); the human ones were made on earlier builds and drift.

A whole campaign can be played from the menus, each mission by its replay:
`WAR_CAMPAIGN=DIR` on the host, the console's `campaign` on the board (the
`cdc` build), with `DIR/L<level>.txt` (`/war1/L<level>.txt` on the card) per
level: the replay with its `quit` line dropped, level 117 + 2 x (mission - 1)
for humans, 118 + 2 x (mission - 1) for orcs. The board's `speed <n>` plays
up to n times faster (as fast as it can: about 5x).

## Credits

- **Warcraft: Orcs & Humans** © 1994 Blizzard Entertainment. Not included;
  bring your own data.
- **[war1](https://github.com/acoto87/war1)** by Alejandro Coto Gutiérrez:
  the engine (zlib license), with its bundled libraries in
  `third_party/war1/deps` (shl, TinySoundFont, stb).
- **[emu8950](https://github.com/digital-sound-antiques/emu8950)** by Mitsutaka
  Okazaki, with Graham Sanderson's RP2040 optimisations: the OPL2 emulator
  for the music (MIT).
- **Apogee Sound System AL_MIDI** © 1994-1995 Apogee Software, Ltd., as
  released under the GPL and used in Chocolate Blood: the Adlib MIDI player and
  General MIDI timbre bank (GPL-2.0-or-later).
- **[FatFs](http://elm-chan.org/fsw/ff/)** by ChaN: the FAT file system
  (FatFs license).
- **[pico_fatfs / SD card SPI driver](https://github.com/elehobica/pico_fatfs)**
  by Elehobica (BSD 2-Clause), with Raspberry Pi's PIO SPI (BSD-3-Clause).
- **XInput host driver** from [usb64](https://github.com/Ryzee119/usb64) by
  Ryan Wendland (MIT), and the **TinyUSB** HID host example (MIT).
- **[Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)** and
  **[TinyUSB](https://github.com/hathach/tinyusb)** (BSD-3-Clause, MIT).
- **[SDL3](https://www.libsdl.org/)** for the host build (zlib).
- PSRAM setup from **frank-blood** / **frank-quake2**, earlier ports to the
  FRANK boards.

The original computer player was worked out from a decompilation of WAR.EXE
1.20 made with **warcraft1-rebirth**'s tools, and checked against the community's
**[Insider's Guide](https://www.blizzardarchive.com/pub/Misc/INSIDE.PDF)** and
**[war1gus](https://github.com/Wargus/war1gus)**'s campaign scripts.
`docs/ai.md` describes the behaviour in our own words; no code or data from
the game is copied.

The campaign strategies behind the replays drew on the mission walkthroughs at
[warcraftone.blogspot.com](https://warcraftone.blogspot.com/),
[StrategyWiki](https://strategywiki.org/wiki/Warcraft:_Orcs_%26_Humans) and
[GameFAQs](https://gamefaqs.gamespot.com/pc/565021-warcraft-orcs-and-humans/faqs).

## License

Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>, https://rh1.tech

GPL-2.0-or-later (see `LICENSE`). Third-party code keeps its own license, as
noted in each file and above.
