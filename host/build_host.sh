#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the Ravel simulator (the whole app on this machine) into build/host/ravel_host.
# Same sources as the firmware, -ffp-contract=off like the device (no fused multiply-add).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen build/host
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
$CC -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    -DRAVEL_HOST -DBD_NUM_GRAINS=16 -Ifirmware/src -Ifirmware/src/app -Ibuild/gen \
    -o build/host/ravel_host host/ravel_host.c firmware/src/app/engine.c core/beads/bd.c core/beads/bd_tables.c -lm
echo "host: build/host/ravel_host"
