#!/bin/bash
#
# frank-war1 — Warcraft: Orcs & Humans for RP2350
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-war1
# SPDX-License-Identifier: GPL-2.0-or-later
#
# release.sh — build the release firmware (after frank-blood's).
#
# Usage: ./release.sh [VERSION] [--publish]
#   VERSION    e.g. "1.00"; asked for when omitted (default: the next minor)
#   --publish  also tag vVERSION, push the tag and create a GitHub release
#              with the UF2s (gh CLI); without it the files stay in release/.
#              The notes' "What's new" comes from release/whatsnew_A_BB.md
#              (Markdown), which --publish requires.
#
# Output: release/core2_frank-war1_A_BB.uf2 (FRANK Core 2, the master chip)
#         release/m2_frank-war1_A_BB.uf2    (Murmulator / FRANK M2)
# Both: 504 MHz CPU, flash 100.8 MHz; PSRAM 133 MHz (Core 2) or 168 MHz (M2);
# the native USB is the keyboard/mouse/gamepad host, the console UART0; the
# sound output (HDMI or I2S) is chosen in the settings window (F12).
# No game data is included: the player copies DATA.WAR to /war1 on the SD
# card.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Build matrix: "FRANK_BOARD:prefix"
BUILD_MATRIX=(
    "CORE2:core2_"
    "M2:m2_"
)

PUBLISH=0
ARGS=()
for a in "$@"; do
    case "$a" in
        --publish) PUBLISH=1 ;;
        *) ARGS+=("$a") ;;
    esac
done

if [ -z "$PICO_SDK_PATH" ] || [ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
    export PICO_SDK_PATH="$HOME/pico/pico-sdk"
fi

VERSION_FILE="version.txt"
if [[ -f "$VERSION_FILE" ]]; then
    read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
else
    LAST_MAJOR=1
    LAST_MINOR=0
fi
NEXT_MINOR=$((LAST_MINOR + 1))
NEXT_MAJOR=$LAST_MAJOR
if [[ $NEXT_MINOR -ge 100 ]]; then
    NEXT_MAJOR=$((NEXT_MAJOR + 1))
    NEXT_MINOR=0
fi

echo ""
echo -e "${CYAN}frank-war1 release builder${NC}"
echo -e "Last version: ${YELLOW}${LAST_MAJOR}.$(printf '%02d' $LAST_MINOR)${NC}"
echo -e "Variants:     ${CYAN}${#BUILD_MATRIX[@]}${NC} (Core 2, M2)"
echo ""

DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"
if [[ ${#ARGS[@]} -gt 0 ]]; then
    INPUT_VERSION="${ARGS[0]}"
    echo -e "Version (from command line): ${CYAN}${INPUT_VERSION}${NC}"
else
    read -p "Enter version [default: $DEFAULT_VERSION]: " INPUT_VERSION
    INPUT_VERSION=${INPUT_VERSION:-$DEFAULT_VERSION}
fi

if [[ "$INPUT_VERSION" == *"."* ]]; then
    MAJOR="${INPUT_VERSION%%.*}"
    MINOR="${INPUT_VERSION##*.}"
else
    read -r MAJOR MINOR <<< "$INPUT_VERSION"
fi
MINOR=$((10#$MINOR))
MAJOR=$((10#$MAJOR))
if [[ $MAJOR -lt 0 || $MINOR -lt 0 || $MINOR -ge 100 ]]; then
    echo -e "${RED}Error: bad version ${INPUT_VERSION}${NC}"
    exit 1
fi

VERSION="${MAJOR}_$(printf '%02d' $MINOR)"
VERSION_DOT="${MAJOR}.$(printf '%02d' $MINOR)"
echo -e "${GREEN}Building release version: ${VERSION_DOT}${NC}"
echo "$MAJOR $MINOR" > "$VERSION_FILE"

RELEASE_DIR="$SCRIPT_DIR/release"
mkdir -p "$RELEASE_DIR"
BUILD_DIR="$SCRIPT_DIR/build/release"

SUCCEEDED=()
FAILED=()
for ENTRY in "${BUILD_MATRIX[@]}"; do
    IFS=':' read -r BOARD PREFIX <<< "$ENTRY"
    OUTPUT_NAME="${PREFIX}frank-war1_${VERSION}.uf2"
    echo ""
    echo -e "${CYAN}Building: $OUTPUT_NAME${NC}"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    if cmake -S master -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DFRANK_BOARD="$BOARD" -DUSB_HID=ON > "$BUILD_DIR/../release-cmake.log" 2>&1 &&
       cmake --build "$BUILD_DIR" > "$BUILD_DIR/../release-build.log" 2>&1 &&
       [[ -f "$BUILD_DIR/frank-war1.uf2" ]]; then
        cp "$BUILD_DIR/frank-war1.uf2" "$RELEASE_DIR/$OUTPUT_NAME"
        echo -e "  ${GREEN}ok${NC} -> release/$OUTPUT_NAME"
        SUCCEEDED+=("$OUTPUT_NAME")
    else
        echo -e "  ${RED}failed${NC} (see build/release-*.log)"
        FAILED+=("$BOARD")
    fi
done
[[ ${#FAILED[@]} -eq 0 ]] && rm -rf "$BUILD_DIR"

echo ""
for FNAME in "${SUCCEEDED[@]}"; do
    ls -la "$RELEASE_DIR/$FNAME" | awk '{printf "  %-40s (%s bytes)\n", $9, $5}'
done
echo -e "Version: ${CYAN}${VERSION_DOT}${NC}"
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo -e "${RED}${#FAILED[@]} variant(s) failed${NC}"
    exit 1
fi

if [[ $PUBLISH -eq 1 ]]; then
    TAG="v${VERSION_DOT}"
    NOTES="$RELEASE_DIR/notes_${VERSION}.md"
    WHATSNEW="$RELEASE_DIR/whatsnew_${VERSION}.md"
    if [[ ! -s "$WHATSNEW" ]]; then
        echo -e "${RED}Error: write ${WHATSNEW} (What's new) first${NC}"
        exit 1
    fi
    cat > "$NOTES" <<NOTES_EOF
Warcraft: Orcs & Humans (Blizzard, 1994) for the FRANK Core 2 and the Murmulator / FRANK M2 (RP2350, 8 MB PSRAM, HDMI, USB keyboard and mouse). The engine is war1, an open-source reimplementation in C, with a computer opponent that plays like the original's.

**What's new in ${VERSION_DOT}**

$(cat "$WHATSNEW")

**Install**

1. Pick the firmware for your board: \`${SUCCEEDED[0]}\` (FRANK Core 2, flash the master chip)
   or \`${SUCCEEDED[1]}\` (M2). Hold BOOTSEL, plug the board in and copy it to the RP2350
   drive (or \`picotool load -f FILE.uf2\`).
2. Copy \`DATA.WAR\` from Warcraft: Orcs & Humans for DOS (retail or the shareware demo) to
   \`/war1/DATA.WAR\` on the SD card (FAT32 or exFAT).
3. Start the board. F12 opens the settings: sound over HDMI or from the I2S DAC, volume,
   sound effects, music, mouse speed (kept in \`/war1/frank.cfg\`).

No game data is included.
NOTES_EOF
    git add "$VERSION_FILE"
    git commit -m "Release ${VERSION_DOT}" -- "$VERSION_FILE" || true
    git tag -a "$TAG" -m "frank-war1 ${VERSION_DOT}"
    git push origin HEAD "$TAG"
    gh release create "$TAG" "${SUCCEEDED[@]/#/$RELEASE_DIR/}" --title "frank-war1 ${VERSION_DOT}" --notes-file "$NOTES"
    echo -e "${GREEN}Published ${TAG}${NC}"
fi
