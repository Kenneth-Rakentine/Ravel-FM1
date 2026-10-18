#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the browser emulator: web/emu/ravel.wasm, from the same sources as the firmware.
# Needs clang with the wasm32 target and wasm-ld (no emscripten, no C library).
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-clang}"
mkdir -p build/gen
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
$CC --target=wasm32 -nostdlib -ffreestanding -O2 -ffp-contract=off -std=gnu99 \
    -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    -DRAVEL_HOST -DBD_NUM_GRAINS=16 -Ifirmware/src -Ifirmware/src/app -Ibuild/gen \
    -Wl,--no-entry -Wl,--export-memory -Wl,-z,stack-size=262144 -Wl,--initial-memory=8388608 \
    -o web/emu/ravel.wasm web/emu/ravel_web.c firmware/src/app/engine.c core/beads/bd.c core/beads/bd_tables.c
echo "web: web/emu/ravel.wasm ($(wc -c < web/emu/ravel.wasm) B)"
