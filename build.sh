#!/usr/bin/env bash
#
# Builds the FlashDB power-loss lab without requiring make.
#   ./build.sh
#   CC="zig cc" ./build.sh
#   FLASHDB=/path/to/FlashDB ./build.sh

set -euo pipefail

cd "$(dirname "$0")"

FLASHDB="${FLASHDB:-third_party/FlashDB}"
CC="${CC:-cc}"
OUT="build/powerloss-lab"

if [ ! -f "$FLASHDB/src/fdb.c" ]; then
    echo "missing $FLASHDB — run:" >&2
    echo "  git submodule update --init third_party/FlashDB" >&2
    exit 1
fi

mkdir -p build

# shellcheck disable=SC2086
$CC -std=c99 -O1 -g \
    -DFAL_PRINTF=sim_noop_printf \
    -I"$FLASHDB/inc" \
    -I"$FLASHDB/port/fal/inc" \
    -Iport \
    -Isrc \
    "$FLASHDB/src/fdb.c" \
    "$FLASHDB/src/fdb_kvdb.c" \
    "$FLASHDB/src/fdb_tsdb.c" \
    "$FLASHDB/src/fdb_utils.c" \
    "$FLASHDB/src/fdb_file.c" \
    "$FLASHDB/src/fdb_read_cache.c" \
    "$FLASHDB/port/fal/src/fal.c" \
    "$FLASHDB/port/fal/src/fal_flash.c" \
    "$FLASHDB/port/fal/src/fal_partition.c" \
    port/sim_flash.c \
    src/main.c \
    -o "$OUT"

echo "built $OUT"
