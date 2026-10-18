/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel app: what the platform calls (boot, the audio interrupt, the main loop), and the
 * project as it is saved. Included by the unity build (ravel.c) after gfx.c, and by the
 * host simulator, so drawing helpers and plat.h are in scope. */
#pragma once
#include <stdint.h>
#include "engine.h"
#include "fastmath.h"

#ifndef RAVEL_VERSION                   /* a release build passes its own (tools/build.py --release) */
#define RAVEL_VERSION "DEV"
#endif

#define PROJ_MAGIC 0x4A505652u          /* "RVPJ" */
#define PROJ_FORMAT 1u

typedef struct {
    uint32_t magic, format;
    int16_t param[NPARAM];
    uint8_t octave;                     /* keyboard octave offset + 4 */
    uint8_t rsv[31];
} project_t;

extern project_t proj;

/* the platform calls these */
void app_init(void);                    /* boot: the saved project (or defaults) into the engine */
void app_render(int32_t *out_lr, uint32_t n);   /* audio interrupt: n stereo frames, 24-bit in int32 */
void app_volume(int32_t *out_lr, uint32_t n);   /* audio interrupt, after the USB tap: the MASTER knob */
void app_load(uint32_t pct);            /* audio interrupt: the last block's cost, % of its time */
void app_stop(void);                    /* main loop: an update is starting, notes off */
void ui_init(void);
void ui_frame(void);                    /* main loop, ~60 frames a second */
void ui_input_only(void);               /* main loop, between frames */
void ui_say(const char *a, const char *b);   /* a message in the footer for a moment */
