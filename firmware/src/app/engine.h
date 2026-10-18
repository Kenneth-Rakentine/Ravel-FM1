/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel's audio engine: eight tracks, the capture tape and the master.
 *
 * engine_render() runs in the audio interrupt. Everything else is called from the main
 * loop. The two meet only through the parameter array (single aligned words, written by
 * the main loop, read by the interrupt) and a small note queue, so neither context ever
 * waits for the other.
 *
 * This first build has two working tracks: T1, a synth voice with Ravel's articulation
 * (onset, continuant, termination), and T7, a tape track that plays the capture tape
 * through the Beads grain engine. The other tracks are present but silent. */
#pragma once
#include <stdint.h>

#define ENGINE_FS 44118u          /* the FM-1's I2S rate (FM-1-RE), effectively 44.1 kHz */
#define NTRACK 8

/* Track kinds (the design: synth, strike, tape). */
enum { KIND_SYNTH, KIND_STRIKE, KIND_TAPE, KIND_OFF };

/* Every parameter of the sound, one flat list. Values are integers within each
 * parameter's own range (see engine_param_info). */
enum {
    /* T1 synth voice: source */
    P_WAVE, P_DETUNE, P_SUB, P_NOISE,
    /* filter */
    P_CUTOFF, P_RESO, P_ENVCUT, P_DRIVE,
    /* articulation */
    P_ONSET, P_DECAY, P_CONTIN, P_TERM,
    /* T7 tape: Beads */
    P_TIME, P_SIZE, P_SHAPE, P_PITCH,
    P_DENSITY, P_RANDOM, P_FEEDBACK, P_TMIX,
    P_SOURCE, P_QUALITY, P_FREEZE, P_TAPEGAIN,
    /* mixer */
    P_LEV1, P_LEV7, P_PAN1, P_PAN7,
    NPARAM
};

typedef struct {
    const char *name;             /* as on the screen, capitals */
    int16_t min, max, def;
    uint8_t unit;                 /* UNIT_* : how the value is shown */
} param_info_t;

enum { UNIT_RAW, UNIT_PCT, UNIT_MS, UNIT_ST, UNIT_BIP, UNIT_LIST };

extern volatile int16_t eng_param[NPARAM];
const param_info_t *engine_param_info(int p);
const char *engine_param_text(int p, int v);    /* a list item's name (UNIT_LIST), else 0 */

void engine_init(void);
void engine_defaults(void);                      /* every parameter to its default */
void engine_render(int32_t *out_lr, uint32_t n); /* interrupt: n stereo frames, 24-bit in int32 */
void engine_volume(int32_t *out_lr, uint32_t n, uint32_t master_q12);

/* main loop -> interrupt */
void engine_note_on(int note, int vel);
void engine_note_off(int note);
void engine_all_off(void);

/* for the screen (read without locking: a stale value only draws one frame late) */
#define SCOPE_N 240u
extern int16_t eng_scope[SCOPE_N];
extern volatile uint32_t eng_scope_w;
extern volatile uint16_t eng_peak[NTRACK];       /* track meters, Q15, decaying */
extern volatile uint8_t eng_voice_stage[4];      /* each voice's articulation state, for the picture */
extern volatile uint16_t eng_voice_level[4];     /* its envelope level, Q15 */
int engine_tape_overview(uint8_t *peaks, int n); /* the tape's level in n columns, oldest first */
int engine_grains(uint16_t *pos, uint8_t *amp, int max);   /* active grains: position 0..65535 along the tape, level */
uint32_t engine_tape_seconds_x10(void);          /* tape length in tenths of a second */
int engine_tape_frozen(void);
