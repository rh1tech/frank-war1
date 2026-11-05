#!/usr/bin/env bash
# The reference-linked executable exists only outside this repository.
# SPDX-License-Identifier: Zlib
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
REF="${REF:-$ROOT/../war1-reference/recomp}"
REF="$(cd "$REF" && pwd -P)"
case "$REF/" in
    "$ROOT/"*) echo "the original reference must remain outside this repository" >&2; exit 2 ;;
esac
for file in "$REF/runtime/x86.h" "$REF/build120/game.o" "$REF/build120/image.bin"; do
    if [ ! -f "$file" ]; then echo "missing local reference: $file" >&2; exit 2; fi
done
out="$(mktemp -d "$REF/build120/frank-orders.XXXXXX")"
trap 'rm -f "$out/test-orders"; rmdir "$out"' EXIT
clang -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$REF/runtime" "$ROOT/tools/ref/test_order_kernel.c" "$ROOT/port/war_orders_frank.c" \
    "$REF/build120/game.o" -o "$out/test-orders"
"$out/test-orders" "$REF/build120/image.bin"
