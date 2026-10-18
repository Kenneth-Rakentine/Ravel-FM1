/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel's audio engine. See engine.h.
 *
 * Floats are fine on the FM-1 (single-precision FPU) as long as no double creeps in;
 * maths comes from fastmath.h (no libm on the device). The Beads port is integer. */
#include "engine.h"
#include "fastmath.h"
#include "../../../core/beads/bd.h"

#ifdef RAVEL_HOST
#define RAVEL_POOL
#else
#define RAVEL_POOL __attribute__((section(".pool")))
#endif

#define FS ((float)ENGINE_FS)

/* --------------------------------------------------------- parameters --- */

static const param_info_t PARAMS[NPARAM] = {
    [P_WAVE] = {"WAVE", 0, 127, 0, UNIT_RAW},
    [P_DETUNE] = {"DETUNE", 0, 127, 18, UNIT_RAW},
    [P_SUB] = {"SUB", 0, 127, 40, UNIT_PCT},
    [P_NOISE] = {"NOISE", 0, 127, 0, UNIT_PCT},
    [P_CUTOFF] = {"CUTOFF", 0, 127, 72, UNIT_RAW},
    [P_RESO] = {"RESO", 0, 127, 36, UNIT_PCT},
    [P_ENVCUT] = {"ENV>CUT", -64, 63, 28, UNIT_BIP},
    [P_DRIVE] = {"DRIVE", 0, 127, 16, UNIT_PCT},
    [P_ONSET] = {"ONSET", 0, 127, 8, UNIT_MS},
    [P_DECAY] = {"DECAY", 0, 127, 70, UNIT_MS},
    [P_CONTIN] = {"CONTIN", 0, 127, 70, UNIT_PCT},
    [P_TERM] = {"TERM", 0, 127, 76, UNIT_MS},
    [P_TIME] = {"TIME", 0, 127, 40, UNIT_PCT},
    [P_SIZE] = {"SIZE", 0, 127, 84, UNIT_PCT},
    [P_SHAPE] = {"SHAPE", 0, 127, 64, UNIT_PCT},
    [P_PITCH] = {"PITCH", -24, 24, 0, UNIT_ST},
    [P_DENSITY] = {"DENSITY", -64, 63, -40, UNIT_BIP},
    [P_RANDOM] = {"RANDOM", 0, 127, 0, UNIT_PCT},
    [P_FEEDBACK] = {"FEEDBK", 0, 127, 0, UNIT_PCT},
    [P_TMIX] = {"MIX", 0, 127, 127, UNIT_PCT},
    [P_SOURCE] = {"SOURCE", 0, 1, 0, UNIT_LIST},
    [P_QUALITY] = {"QUALITY", 0, 2, 0, UNIT_LIST},
    [P_FREEZE] = {"CATCH", 0, 1, 0, UNIT_LIST},
    [P_TAPEGAIN] = {"INPUT", 0, 127, 100, UNIT_PCT},
    [P_LEV1] = {"T1 LEVEL", 0, 127, 100, UNIT_PCT},
    [P_LEV7] = {"T7 LEVEL", 0, 127, 96, UNIT_PCT},
    [P_PAN1] = {"T1 PAN", -64, 63, 0, UNIT_BIP},
    [P_PAN7] = {"T7 PAN", -64, 63, 0, UNIT_BIP},
};

volatile int16_t eng_param[NPARAM];

const param_info_t *engine_param_info(int p) { return (p >= 0 && p < NPARAM) ? &PARAMS[p] : 0; }

const char *engine_param_text(int p, int v)
{
    static const char *const SOURCE[] = {"T1", "MASTER"};
    static const char *const QUALITY[] = {"CLEAN", "TAPE", "DIRTY"};
    static const char *const FREEZE[] = {"LISTEN", "CAUGHT"};
    switch (p) {
    case P_SOURCE: return SOURCE[v & 1];
    case P_QUALITY: return QUALITY[v < 0 ? 0 : v > 2 ? 2 : v];
    case P_FREEZE: return FREEZE[v & 1];
    default: return 0;
    }
}

void engine_defaults(void)
{
    int p;
    for (p = 0; p < NPARAM; p++)
        eng_param[p] = PARAMS[p].def;
}

static inline float pnorm(int p) { return (float)eng_param[p] / 127.0f; }

/* 0..127 -> 1 ms .. ~4 s, exponential */
static float time_ms(int v) { return fm_exp2f((float)v * (12.0f / 127.0f)); }

/* ------------------------------------------------------------- notes --- */

#define NQ 64u
static volatile uint8_t nq_note[NQ], nq_vel[NQ];   /* vel 0 = off, 255 = all off */
static volatile uint32_t nq_w, nq_r;

static void nq_push(int note, int vel)
{
    uint32_t w = nq_w;
    if (w - nq_r >= NQ)
        return;                                       /* full: dropped */
    nq_note[w % NQ] = (uint8_t)note;
    nq_vel[w % NQ] = (uint8_t)vel;
    __asm__ volatile("" ::: "memory");
    nq_w = w + 1;
}
void engine_note_on(int note, int vel) { nq_push(note, vel < 1 ? 1 : vel > 127 ? 127 : vel); }
void engine_note_off(int note) { nq_push(note, 0); }
void engine_all_off(void) { nq_push(0, 255); }

/* ------------------------------------------------------- synth voice --- */

#define NVOICE 4
enum { ST_IDLE, ST_ONSET, ST_CONTIN, ST_TERM };

typedef struct {
    int note, gate;
    uint32_t age;
    float ph1, ph2, phs;          /* oscillator phases, 0..1 */
    float env, vel;
    int stage;
    float ic1, ic2;               /* filter state */
} voice_t;

static voice_t vox[NVOICE];
static uint32_t vox_age;
static uint32_t noise_state = 22222u;
volatile uint8_t eng_voice_stage[4];
volatile uint16_t eng_voice_level[4];

static void voice_on(int note, int vel)
{
    int i, pick = 0;
    uint32_t oldest = 0xFFFFFFFFu;
    for (i = 0; i < NVOICE; i++)                      /* the same note again, else a free voice, else the oldest */
        if (vox[i].stage != ST_IDLE && vox[i].note == note) {
            pick = i;
            goto found;
        }
    for (i = 0; i < NVOICE; i++)
        if (vox[i].stage == ST_IDLE) {
            pick = i;
            goto found;
        }
    for (i = 0; i < NVOICE; i++)
        if (vox[i].age < oldest) {
            oldest = vox[i].age;
            pick = i;
        }
found:
    vox[pick].note = note;
    vox[pick].gate = 1;
    vox[pick].age = ++vox_age;
    vox[pick].vel = 0.35f + 0.65f * (float)vel / 127.0f;
    vox[pick].stage = ST_ONSET;
}

static void voice_off(int note)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (vox[i].gate && vox[i].note == note) {
            vox[i].gate = 0;
            vox[i].stage = ST_TERM;
        }
}

static inline float polyblep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline float osc(float ph, float dt, float wave)
{
    float saw = 2.0f * ph - 1.0f - polyblep(ph, dt);
    float ph2 = ph + 0.5f;
    float sq;
    if (ph2 >= 1.0f)
        ph2 -= 1.0f;
    sq = (ph < 0.5f ? 1.0f : -1.0f) + polyblep(ph, dt) - polyblep(ph2, dt);
    return saw + (sq - saw) * wave;
}

/* per-block values the voices share */
typedef struct {
    float wave, det, sub, noise, cut, k, envcut, drive, drive_norm;
    float a_coef, d_coef, level, r_coef;
} vparams_t;

static void vparams(vparams_t *v)
{
    float res = pnorm(P_RESO);
    v->wave = pnorm(P_WAVE);
    v->det = fm_exp2f(pnorm(P_DETUNE) * 0.5f / 12.0f) - 1.0f;     /* up to half a semitone apart */
    v->sub = pnorm(P_SUB);
    v->noise = pnorm(P_NOISE) * 0.6f;
    v->cut = pnorm(P_CUTOFF) * 10.0f;                              /* octaves above 20 Hz */
    v->k = 2.0f - 1.94f * res;                                     /* damping: resonance near self-oscillation at the top */
    v->envcut = (float)eng_param[P_ENVCUT] / 64.0f * 6.0f;         /* +-6 octaves at full envelope */
    v->drive = 1.0f + pnorm(P_DRIVE) * 6.0f;
    v->drive_norm = 1.0f / fm_tanhf(v->drive * 0.6f);
    v->a_coef = 1.0f - fm_expf(-1000.0f / (time_ms(eng_param[P_ONSET]) * FS));
    v->d_coef = 1.0f - fm_expf(-1000.0f / (time_ms(eng_param[P_DECAY]) * FS));
    v->level = pnorm(P_CONTIN);
    v->r_coef = 1.0f - fm_expf(-1000.0f / (time_ms(eng_param[P_TERM]) * FS));
}

static float voice_sample(voice_t *vc, const vparams_t *p, float f0)
{
    float dt1 = f0 * (1.0f + p->det) / FS, dt2 = f0 * (1.0f - p->det * 0.5f) / FS;
    float s, fc, g, hp, bp, lp, v1, v2;
    /* articulation */
    switch (vc->stage) {
    case ST_ONSET:
        vc->env += (1.08f - vc->env) * p->a_coef;
        if (vc->env >= 1.0f) {
            vc->env = 1.0f;
            vc->stage = ST_CONTIN;
        }
        break;
    case ST_CONTIN:
        vc->env += (p->level - vc->env) * p->d_coef;
        break;
    case ST_TERM:
        vc->env -= vc->env * p->r_coef;
        if (vc->env < 1.0e-4f) {
            vc->env = 0.0f;
            vc->stage = ST_IDLE;
            vc->ic1 = vc->ic2 = 0.0f;
        }
        break;
    default:
        return 0.0f;
    }
    /* source */
    s = osc(vc->ph1, dt1, p->wave) + osc(vc->ph2, dt2, p->wave);
    s *= 0.5f;
    s += (vc->phs < 0.5f ? 1.0f : -1.0f) * p->sub * 0.8f;
    noise_state = noise_state * 1664525u + 1013904223u;
    s += ((float)(int32_t)noise_state * (1.0f / 2147483648.0f)) * p->noise;
    vc->ph1 += dt1;
    if (vc->ph1 >= 1.0f)
        vc->ph1 -= 1.0f;
    vc->ph2 += dt2;
    if (vc->ph2 >= 1.0f)
        vc->ph2 -= 1.0f;
    vc->phs += dt1 * 0.5f;
    if (vc->phs >= 1.0f)
        vc->phs -= 1.0f;
    /* skin: drive into the filter */
    s = fm_tanhf(s * p->drive * 0.6f) * p->drive_norm * 0.7f;
    /* filter: a state-variable low pass (TPT), the cutoff opened by the envelope */
    fc = 20.0f * fm_exp2f(p->cut + p->envcut * vc->env);
    fc = fm_clampf(fc, 20.0f, 0.42f * FS);
    g = fm_tanf(FM_PI * fc / FS);
    hp = (s - (p->k + g) * vc->ic1 - vc->ic2) / (1.0f + g * (p->k + g));
    v1 = g * hp;
    bp = v1 + vc->ic1;
    vc->ic1 = bp + v1;
    v2 = g * bp;
    lp = v2 + vc->ic2;
    vc->ic2 = lp + v2;
    return lp * vc->env * vc->vel;
}

/* -------------------------------------------------------------- tape --- */

#define TAPE_BYTES (192u * 1024u)
static uint8_t tape_buf[TAPE_BYTES + 2048u + 64u] RAVEL_POOL __attribute__((aligned(8)));
static bd_engine beads;
static int beads_quality = -1;

static void beads_params(bd_params *bp)
{
    int32_t r = eng_param[P_RANDOM] * 516;                         /* 0..1 Q16 */
    int32_t d = eng_param[P_DENSITY];
    bp->time.base = eng_param[P_TIME] * 516;
    bp->time.amount = r;
    bp->time.cv = 0;
    bp->time.has_cv = 0;
    bp->size.base = eng_param[P_SIZE] * 516;
    bp->size.amount = r / 2;
    bp->size.cv = 0;
    bp->size.has_cv = 0;
    bp->shape.base = eng_param[P_SHAPE] * 516;
    bp->shape.amount = 0;
    bp->shape.cv = 0;
    bp->shape.has_cv = 0;
    bp->pitch.base = eng_param[P_PITCH] * 65536;
    bp->pitch.amount = 0;
    bp->pitch.cv = 0;
    bp->pitch.has_cv = 0;
    /* density: left of centre regular grains, faster towards the end; right of centre random */
    bp->density = d < 0 ? 32768 + d * 512 : 32768 + d * 512;
    if (bp->density < 0)
        bp->density = 0;
    if (bp->density > 65535)
        bp->density = 65535;
    bp->density_cv = 0;
    bp->feedback = eng_param[P_FEEDBACK] * 516;
    bp->dry_wet = eng_param[P_TMIX] * 516;
    bp->monophonic = 0;
}

static void tape_quality(void)
{
    int q = eng_param[P_QUALITY];
    if (q == beads_quality)
        return;
    beads_quality = q;
    bd_configure(&beads, q == 2 ? BD_RES_8 : q == 1 ? BD_RES_12 : BD_RES_16, 1);
}

uint32_t engine_tape_seconds_x10(void) { return (uint32_t)beads.tape.max_delay * 10u / ENGINE_FS; }
int engine_tape_frozen(void) { return beads.tape.frozen; }

int engine_tape_overview(uint8_t *peaks, int n)
{
    int32_t len = beads.tape.max_delay, c;
    if (len <= 0 || n <= 0)
        return 0;
    for (c = 0; c < n; c++) {                         /* oldest on the left */
        int32_t d0 = len - 1 - (int32_t)((int64_t)c * len / n), d1 = len - 1 - (int32_t)((int64_t)(c + 1) * len / n);
        int32_t d, step = (d0 - d1) / 24 + 1, pk = 0;
        for (d = d0; d > d1; d -= step) {
            int32_t v = bd_tape_peek(&beads, d);
            if (v < 0)
                v = -v;
            if (v > pk)
                pk = v;
        }
        peaks[c] = (uint8_t)(pk > 32767 ? 255 : pk >> 7);
    }
    return n;
}

int engine_grains(uint16_t *pos, uint8_t *amp, int max)
{
    int32_t d[32], l[32], len = beads.tape.max_delay;
    int n, i;
    if (max > 32)
        max = 32;
    n = bd_active_grains(&beads, d, l, max);
    for (i = 0; i < n; i++) {
        int32_t x = len > 0 ? (int32_t)((int64_t)(len - 1 - d[i]) * 65535 / len) : 0;
        pos[i] = (uint16_t)(x < 0 ? 0 : x > 65535 ? 65535 : x);
        amp[i] = (uint8_t)(l[i] >> 7);
    }
    return n;
}

/* ----------------------------------------------------------- render --- */

int16_t eng_scope[SCOPE_N];
volatile uint32_t eng_scope_w;
volatile uint16_t eng_peak[NTRACK];

void engine_init(void)
{
    int i;
    engine_defaults();
    for (i = 0; i < NVOICE; i++)
        vox[i].stage = ST_IDLE;
    bd_init(&beads, tape_buf, sizeof tape_buf, (int32_t)ENGINE_FS);
    bd_seed_rng(&beads, 0x21);
    beads_quality = -1;
    tape_quality();
}

static void take_notes(void)
{
    while (nq_r != nq_w) {
        uint32_t r = nq_r;
        int note = nq_note[r % NQ], vel = nq_vel[r % NQ];
        nq_r = r + 1;
        if (vel == 255) {
            int i;
            for (i = 0; i < NVOICE; i++)
                if (vox[i].gate) {
                    vox[i].gate = 0;
                    vox[i].stage = ST_TERM;
                }
        } else if (vel) {
            voice_on(note, vel);
        } else {
            voice_off(note);
        }
    }
}

static inline float pan_l(int p) { return fm_cosf((float)(eng_param[p] + 64) / 128.0f * FM_PI * 0.5f); }
static inline float pan_r(int p) { return fm_sinf((float)(eng_param[p] + 64) / 128.0f * FM_PI * 0.5f); }

static void meter(int t, float x)
{
    uint32_t a = (uint32_t)(fm_fabsf(x) * 32767.0f);
    if (a > 32767u)
        a = 32767u;
    if (a > eng_peak[t])
        eng_peak[t] = (uint16_t)a;
}

void engine_render(int32_t *out_lr, uint32_t n)
{
    vparams_t vp;
    bd_params bp;
    float f0[NVOICE], lev1, lev7, p1l, p1r, p7l, p7r, gain_in;
    uint32_t i, k;
    int v, src;
    take_notes();
    vparams(&vp);
    tape_quality();
    beads_params(&bp);
    for (v = 0; v < NVOICE; v++)
        f0[v] = 440.0f * fm_exp2f((float)(vox[v].note - 69) / 12.0f);
    lev1 = pnorm(P_LEV1);
    lev7 = pnorm(P_LEV7);
    p1l = pan_l(P_PAN1) * lev1;
    p1r = pan_r(P_PAN1) * lev1;
    p7l = pan_l(P_PAN7) * lev7;
    p7r = pan_r(P_PAN7) * lev7;
    gain_in = pnorm(P_TAPEGAIN) * 2.0f;
    src = eng_param[P_SOURCE];
    for (k = 0; k < n; k += BD_MAX_BLOCK) {
        uint32_t m = n - k > BD_MAX_BLOCK ? BD_MAX_BLOCK : n - k;
        float t1[BD_MAX_BLOCK];
        bd_frame bin[BD_MAX_BLOCK], bout[BD_MAX_BLOCK];
        uint8_t frz[BD_MAX_BLOCK];
        uint8_t frozen = (uint8_t)(eng_param[P_FREEZE] != 0);
        for (i = 0; i < m; i++) {
            float s = 0.0f;
            for (v = 0; v < NVOICE; v++)
                if (vox[v].stage != ST_IDLE)
                    s += voice_sample(&vox[v], &vp, f0[v]);
            t1[i] = s * 0.9f;
        }
        for (i = 0; i < m; i++) {
            float x = t1[i] * (src ? lev1 : 1.0f) * gain_in;  /* MASTER source: T1 as it goes to the mix */
            int32_t q = (int32_t)(fm_clampf(x, -1.0f, 1.0f) * 32767.0f);
            bin[i].l = bin[i].r = q;
            frz[i] = frozen;
        }
        bd_process(&beads, &bp, BD_SEED_ON, 0, frz, bin, bout, (int)m);
        for (i = 0; i < m; i++) {
            float a = t1[i], b = (float)(bout[i].l + bout[i].r) * (0.5f / 32768.0f);
            float l = a * p1l + b * p7l, r = a * p1r + b * p7r;
            int32_t ol, or_;
            meter(0, a * lev1);
            meter(6, b * lev7);
            l = fm_tanhf(l * 0.9f) * 1.05f;           /* master: a soft ceiling */
            r = fm_tanhf(r * 0.9f) * 1.05f;
            ol = (int32_t)(fm_clampf(l, -1.0f, 1.0f) * 8388607.0f);
            or_ = (int32_t)(fm_clampf(r, -1.0f, 1.0f) * 8388607.0f);
            out_lr[2 * (k + i)] = ol;
            out_lr[2 * (k + i) + 1] = or_;
            if (((k + i) & 3u) == 0u) {
                uint32_t w = eng_scope_w;
                eng_scope[w % SCOPE_N] = (int16_t)(((ol >> 8) + (or_ >> 8)) >> 1);
                eng_scope_w = w + 1;
            }
        }
    }
    for (v = 0; v < NVOICE; v++) {
        eng_voice_stage[v] = (uint8_t)vox[v].stage;
        eng_voice_level[v] = (uint16_t)(fm_clampf(vox[v].env, 0.0f, 1.0f) * 32767.0f);
    }
    for (v = 0; v < NTRACK; v++)                      /* meters fall about 20 dB a second */
        eng_peak[v] = (uint16_t)(eng_peak[v] * 0.97f);
}

void engine_volume(int32_t *out_lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++)
        out_lr[i] = (int32_t)(((int64_t)out_lr[i] * master_q12) >> 12);
}
