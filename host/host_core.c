/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel on a simulated FM-1: the whole app (screen, controls, engine) against a platform
 * made of plain memory. No C library is used here, so the same file builds the desktop
 * simulator (host/ravel_host.c adds the script runner, PNG and WAV output) and the browser
 * emulator (web/emu/ravel_web.c, WebAssembly).
 *
 * The device's timing is reproduced: the clock advances one millisecond at a time, the
 * engine renders 256 frames whenever that much audio is due (44,118 Hz), and the UI draws
 * a frame every 16 ms with input polled in between, as the FM-1's main loop does.
 *
 * After X0X's host simulator (host/x0x_host.c), GPL-3.0-only. */
#ifndef RAVEL_HOST
#define RAVEL_HOST 1
#endif
#include <stdint.h>

/* the front end (ravel_host.c or ravel_web.c) receives the audio */
static void host_audio(const int32_t *blk, uint32_t n);

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static uint32_t blits;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#include "../firmware/src/gfx.c"

/* ---------------------------------------------------------- platform --- */
#include "../firmware/src/platform/plat.h"

static uint32_t now_ms, held_btn, held_keys, master = 3000;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys, dim_keys;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v;
    if (role < 0 || role >= NE)
        return 0;
    v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master; }
int plat_battery(void) { return 3; }
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
}
void plat_glow(int on) { (void)on; }
void plat_play_red(int red) { (void)red; }
void plat_keys_dim(uint32_t k) { dim_keys = k; }
int plat_midi_in(uint32_t *pkt)
{
    (void)pkt;
    return 0;
}
void plat_midi_out(uint32_t pkt) { (void)pkt; }

/* flash: one buffer per object, kept across a simulated reboot */
static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ] = {-1, -1, -1, -1, -1, -1, -1};
static uint32_t store_writes;
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int n, i;
    if (obj >= OBJ_NOBJ || (n = store_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    for (i = 0; i < n; i++)
        ((uint8_t *)dst)[i] = store[obj][i];
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t i;
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX)
        return -1;
    for (i = 0; i < len; i++)
        store[obj][i] = ((const uint8_t *)src)[i];
    store_len[obj] = (int)len;
    store_writes++;
    return 0;
}
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max)
{
    (void)obj, (void)dst, (void)max;
    return -1;
}

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }
int plat_cpu2_set(int on) { (void)on; return 0; }
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn, (void)arg; return 0; }
int plat_cpu2_wait(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
int plat_vdc14_mv(void) { return 1600; }
int plat_sysvdd_mv(void) { return 1380; }
int plat_last_crash(uint32_t v[5]) { (void)v; return 0; }
uint32_t plat_xruns(void) { return 0; }
void plat_perf_init(void) {}
uint32_t plat_cycles(void) { return 0; }
uint32_t plat_cycles_hz(void) { return 24000000u; }
int plat_cycles_cpu(void) { return 0; }
uint32_t plat_ticks24(void) { return now_ms * 24000u; }
uint32_t plat_irq_stack_used(void) { return 0; }
uint32_t plat_irq_stack_size(void) { return 0; }
uint32_t plat_cpu_peak_pct(void) { return cpu_pct; }
int plat_stalls(uint32_t s[3]) { (void)s; return -1; }
void plat_stalls_enable(int on) { (void)on; }

/* ----------------------------------------------------------- the app --- */
#include "../firmware/src/app/ravel.h"
#include "../firmware/src/app/ui.c"

/* ----------------------------------------------------------- running --- */
#define BLOCK 256u
static int32_t abuf[BLOCK * 2u];
static uint32_t frames_due_x1000, last_frame;

static void boot(void)
{
    now_ms = 0;
    last_frame = 0;
    frames_due_x1000 = 0;
    palette_set(0);
    lcd_fill(0, 0, 240, 240, 0);
    app_init();
    ui_init();
}

/* the device for ms milliseconds: audio as it falls due, the UI every 16 ms */
static void run_ms(uint32_t ms)
{
    while (ms--) {
        now_ms++;
        frames_due_x1000 += ENGINE_FS;
        while (frames_due_x1000 >= BLOCK * 1000u) {
            frames_due_x1000 -= BLOCK * 1000u;
            app_render(abuf, BLOCK);
            app_volume(abuf, BLOCK);
            host_audio(abuf, BLOCK);
        }
        if (now_ms - last_frame >= 16u) {
            last_frame = now_ms;
            ui_frame();
        } else {
            ui_input_only();
        }
    }
}
