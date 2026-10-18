/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel in a browser: the same app and simulated FM-1 as the desktop simulator (host/host_core.c),
 * built freestanding for WebAssembly (web/emu/build.sh). The page (index.html) drives it:
 *
 *   rv_boot()                 start from the simulated flash
 *   rv_run(ms)                run the device; returns the audio frames it made (rv_audio())
 *   rv_audio()                float stereo, interleaved, from the last rv_run
 *   rv_fb()                   the 240x240 RGB565 screen (byte-swapped, as the panel takes it)
 *   rv_set_buttons(mask) rv_set_keys(mask) rv_enc(role, detents) rv_set_master(0..4096)
 *   rv_leds_buttons() rv_leds_keys()
 *   rv_store(obj) rv_store_len(obj) rv_store_set_len(obj, n) rv_store_writes()
 *                             the flash objects, so the page can keep them between visits
 *   rv_param_count() rv_param(p) rv_param_name(p)
 */
#include <stdint.h>

#define EXPORT(name) __attribute__((export_name(#name)))

/* the C library the app needs: the compiler's built-ins call these */
void *memset(void *d, int c, unsigned long n)
{
    uint8_t *p = d;
    while (n--)
        *p++ = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
void *memmove(void *d, const void *s, unsigned long n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    if (p < q) {
        while (n--)
            *p++ = *q++;
    } else {
        p += n;
        q += n;
        while (n--)
            *--p = *--q;
    }
    return d;
}
int memcmp(const void *a, const void *b, unsigned long n)
{
    const uint8_t *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

#include "../../host/host_core.c"

#define AUDIO_MAX 16384u                 /* frames: 370 ms, more than one rv_run makes */
static float audio[AUDIO_MAX * 2u];
static uint32_t audio_n;

static void host_audio(const int32_t *blk, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && audio_n < AUDIO_MAX; i++, audio_n++) {
        audio[2u * audio_n] = (float)blk[2u * i] * (1.0f / 8388608.0f);
        audio[2u * audio_n + 1u] = (float)blk[2u * i + 1u] * (1.0f / 8388608.0f);
    }
}

EXPORT(rv_boot) void rv_boot(void)
{
    held_btn = held_keys = 0;
    boot();
}
EXPORT(rv_run) uint32_t rv_run(uint32_t ms)
{
    audio_n = 0;
    if (ms > 250u)
        ms = 250u;
    run_ms(ms);
    return audio_n;
}
EXPORT(rv_audio) float *rv_audio(void) { return audio; }
EXPORT(rv_fb) uint16_t *rv_fb(void) { return fb; }
EXPORT(rv_set_buttons) void rv_set_buttons(uint32_t m) { held_btn = m; }
EXPORT(rv_set_keys) void rv_set_keys(uint32_t m) { held_keys = m; }
EXPORT(rv_enc) void rv_enc(int role, int32_t d)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += d;
}
EXPORT(rv_set_master) void rv_set_master(uint32_t v) { master = v > 4096u ? 4096u : v; }
EXPORT(rv_leds_buttons) uint32_t rv_leds_buttons(void) { return lit_btn; }
EXPORT(rv_leds_keys) uint32_t rv_leds_keys(void) { return lit_keys; }
EXPORT(rv_store) uint8_t *rv_store(uint32_t obj) { return obj < OBJ_NOBJ ? store[obj] : 0; }
EXPORT(rv_store_len) int rv_store_len(uint32_t obj) { return obj < OBJ_NOBJ ? store_len[obj] : -1; }
EXPORT(rv_store_set_len) void rv_store_set_len(uint32_t obj, int n)
{
    if (obj < OBJ_NOBJ && n <= (int)PLAT_STORE_MAX)
        store_len[obj] = n;
}
EXPORT(rv_store_writes) uint32_t rv_store_writes(void) { return store_writes; }
EXPORT(rv_store_objects) uint32_t rv_store_objects(void) { return OBJ_NOBJ; }
EXPORT(rv_store_max) uint32_t rv_store_max(void) { return PLAT_STORE_MAX; }
EXPORT(rv_sample_rate) uint32_t rv_sample_rate(void) { return ENGINE_FS; }
EXPORT(rv_param_count) int rv_param_count(void) { return NPARAM; }
EXPORT(rv_param) int rv_param(int p) { return p >= 0 && p < NPARAM ? eng_param[p] : 0; }
EXPORT(rv_param_name) const char *rv_param_name(int p) { return p >= 0 && p < NPARAM ? engine_param_info(p)->name : ""; }
