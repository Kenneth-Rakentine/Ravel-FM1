/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Ravel on the FM-1, on M-VAVE's FM1 Open SDK: the platform (plat.h) on the SDK's API, the
 * hooks the SDK's control task calls, and the audio block.
 *
 * The SDK (sdk/lib/libfm1_engine.a, binary) runs the system: its control task, USB (MIDI,
 * audio in and out, SysEx updates), Bluetooth, the user flash partition. It calls this file:
 *   groove_screen_init     once: start the panel, the screen, Ravel
 *   groove_screen_control  repeatedly from the control task: Ravel's interface
 * and the board's audio interrupt (audio.c) calls ravel_audio_block for every 128 frames.
 * The SDK's own instrument (its groove_* engine) is linked but never run: the stubs at the end
 * answer the hooks it would use. */
#include <stdint.h>
#include <string.h>
#include "fm1.h"
#include "fm1_device.h"
#include "fm1_audio_io.h"
#include "fm1_audio.h"
#include "plat.h"

void ravel_start(void);
void rv_render(int32_t *out, int frames);
void ui_frame(void);
void ui_input_only(void);
uint32_t ravel_audio_load(void);

/* ------------------------------------------------------------ controls --- */
static struct fm1_controls ctl;
static int32_t enc_acc[NE];
static uint32_t master_q12 = 3000;

static void controls_poll(void)
{
    int i;
    fm1_controls_read(&ctl);
    for (i = 0; i < NE; i++)
        enc_acc[i] += ctl.encoders[i];
    /* MASTER: a 10-bit pot to 0..4096, with a little dead zone at the bottom */
    {
        uint32_t k = ctl.knob > 1023u ? 1023u : ctl.knob;
        master_q12 = k < 8u ? 0u : (k - 8u) * 4096u / 1015u;
    }
}

uint32_t plat_ms(void) { return fm1_millis(); }
uint32_t plat_buttons(void) { return ctl.buttons & 0x3FFFu; }
uint32_t plat_keys(void) { return ctl.keys & 0x7FFFFFFu; }
int32_t plat_enc(int role)
{
    int32_t v;
    if (role < 0 || role >= NE)
        return 0;
    v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master_q12; }

static uint32_t led_btn = 0xFFFFFFFFu, led_key = 0xFFFFFFFFu;
void plat_leds(uint32_t b, uint32_t k)
{
    unsigned i;
    for (i = 0; i < NB; i++)
        if (((b ^ led_btn) >> i) & 1u)
            fm1_led_set(i, (int)((b >> i) & 1u));
    for (i = 0; i < NKEYS; i++)
        if (((k ^ led_key) >> i) & 1u)
            fm1_led_set(14u + i, (int)((k >> i) & 1u));
    led_btn = b;
    led_key = k;
}

/* ------------------------------------------------------------- screen --- */
/* bands that did not change are not sent: the SPI link is the slow part of a frame */
static uint32_t band_hash[PLAT_H / 8];
void plat_blit(int x, int y, int w, int h, const uint16_t *px)
{
    uint32_t hsh = 2166136261u;
    int n = w * h, i;
    for (i = 0; i < n; i++)
        hsh = (hsh ^ px[i]) * 16777619u;
    if (x == 0 && w == PLAT_W && (y & 7) == 0 && band_hash[y / 8] == hsh)
        return;
    if (x == 0 && w == PLAT_W && (y & 7) == 0)
        band_hash[y / 8] = hsh;
    fm1_screen_blit((unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, px, (size_t)n);
}

/* -------------------------------------------------------------- flash --- */
/* the user partition (512 KB of it) as eight slots of 64 KB: a header, then the data */
#define SLOT_MAGIC 0x4C535652u                  /* "RVSL" */
typedef struct { uint32_t magic, len, crc, rsv; } slot_head;

static uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    int k;
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int plat_load(int slot, void *dst, uint32_t max)
{
    slot_head h;
    uint32_t base = (uint32_t)slot * PLAT_SLOT_MAX, n;
    if (slot < 0 || slot >= PLAT_SLOTS || fm1_flash_size() < (uint32_t)(slot + 1) * PLAT_SLOT_MAX)
        return -1;
    if (fm1_flash_read(base, &h, sizeof h) != FM1_FLASH_OK || h.magic != SLOT_MAGIC || h.len > PLAT_SLOT_MAX - sizeof h)
        return -1;
    n = h.len < max ? h.len : max;
    if (fm1_flash_read(base + sizeof h, dst, n) != FM1_FLASH_OK)
        return -1;
    if (n == h.len && crc32(0, (const uint8_t *)dst, n) != h.crc)
        return -1;
    return (int)h.len;
}

int plat_save(int slot, const void *src, uint32_t len)
{
    slot_head h;
    uint32_t base = (uint32_t)slot * PLAT_SLOT_MAX, off;
    if (slot < 0 || slot >= PLAT_SLOTS || len > PLAT_SLOT_MAX - sizeof h || fm1_flash_size() < (uint32_t)(slot + 1) * PLAT_SLOT_MAX)
        return -1;
    for (off = 0; off < len + sizeof h; off += FM1_FLASH_SECTOR_BYTES)
        if (fm1_flash_erase(base + off) != FM1_FLASH_OK)
            return -1;
    if (fm1_flash_write(base + sizeof h, src, len) != FM1_FLASH_OK)
        return -1;
    h.magic = SLOT_MAGIC;
    h.len = len;
    h.crc = crc32(0, (const uint8_t *)src, len);
    h.rsv = 0;
    /* the header last: a slot interrupted mid-write reads as empty */
    return fm1_flash_write(base, &h, sizeof h) == FM1_FLASH_OK ? 0 : -1;
}

/* --------------------------------------------------------------- MIDI --- */
int plat_midi_in(uint8_t msg[3])
{
    (void)msg;
    return 0;
}
void plat_midi_out(const uint8_t *msg, int n) { fm1_midi_send(msg, (size_t)n); }

uint32_t plat_cpu_pct(void) { return ravel_audio_load(); }

/* -------------------------------------------------------------- audio --- */
static int32_t usb_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));
static int16_t bt_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));
static float in_lr[FM1_BLOCK_WORDS];
static int in_have, in_pos;
static uint32_t gain_q12;

/* inside the render, the instrument asks for the input a control step at a time */
int plat_audio_in(float *lr, int frames)
{
    int i, n = in_have - in_pos;
    if (n <= 0)
        return 0;
    if (n > frames)
        n = frames;
    for (i = 0; i < 2 * n; i++)
        lr[i] = in_lr[2 * in_pos + i];
    in_pos += n;
    return n;
}

void ravel_audio_reset(void) { gain_q12 = 0; }

FM1_AUDIO_CODE int ravel_audio_block(int32_t *out, unsigned frames)
{
    uint16_t g[2] = {0, 0};
    unsigned i;
    int n;
    if (!out || frames != FM1_BLOCK_FRAMES)
        return -1;
    /* the input: USB audio from a computer (or Bluetooth from a phone) */
    in_have = in_pos = 0;
    n = fm1_usb_audio_read(usb_pcm, frames, g);
    if (n == (int)frames) {
        for (i = 0; i < 2 * frames; i++)
            in_lr[i] = (float)usb_pcm[i] * (1.0f / 8388608.0f) * (float)g[i & 1] * (1.0f / 128.0f);
        in_have = (int)frames;
    } else if (fm1_bluetooth_audio_read(bt_pcm, frames) == (int)frames) {
        for (i = 0; i < 2 * frames; i++)
            in_lr[i] = (float)bt_pcm[i] * (1.0f / 32768.0f);
        in_have = (int)frames;
    }
    rv_render(out, (int)frames);
    /* MASTER, ramped */
    {
        uint32_t target = master_q12;
        for (i = 0; i < 2 * frames; i++) {
            if ((i & 15u) == 0u) {
                if (gain_q12 < target)
                    gain_q12 += target - gain_q12 > 32u ? 32u : target - gain_q12;
                else if (gain_q12 > target)
                    gain_q12 -= gain_q12 - target > 32u ? 32u : gain_q12 - target;
            }
            out[i] = (int32_t)(((int64_t)out[i] * (int32_t)gain_q12) >> 12);
        }
    }
    (void)fm1_usb_audio_write(out, frames);
    return 0;
}

/* ----------------------------------------------- the SDK's control hooks --- */
static uint32_t last_frame;
static uint8_t bt_set;

void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    controls_poll();
    ravel_start();
    last_frame = fm1_millis();
}

void groove_screen_control(void)
{
    uint32_t now = fm1_millis();
    controls_poll();
    if (!bt_set && fm1_bluetooth_configure(0) == 0)  /* Bluetooth off in this version (see BUILDING.md) */
        bt_set = 1;
    if (now - last_frame >= 40u) {
        last_frame = now;
        ui_frame();
    } else {
        ui_input_only();
    }
}

/* the SDK's instrument hooks: answered, never used */
static uint32_t scratch_project[4064 / 4 + 64];
void *groove_project_scratch(void) { return scratch_project; }
void groove_project_init(void) {}
void groove_project_poll(void) {}
void groove_project_metadata(const void *p) { (void)p; }
void groove_project_name(char name[13]) { memcpy(name, "RAVEL       ", 13); name[12] = 0; }
void groove_samples_init(void) {}
void groove_samples_poll(void) {}
void groove_transfer_init(void) {}
void groove_transfer_poll(void) {}
FM1_AUDIO_CODE void groove_fm_program(uint8_t p[156], unsigned tone)
{
    unsigned i;
    (void)tone;
    for (i = 0; i < 156u; i++)
        p[i] = 0;
}
