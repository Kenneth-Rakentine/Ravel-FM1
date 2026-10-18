/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel on the desktop: the app on a simulated FM-1 (host_core.c), driven by a script.
 * Audio goes to a WAV, the screen to PNGs. The PNG writer is X0X's (stored deflate, no zlib).
 *
 *   ravel_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments):
 *   wait MS                       run the device for MS milliseconds
 *   press BTN | release BTN | tap BTN
 *                                 (BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   key K down|up | tapkey K      (K: 0..26 = F3..G5)
 *   turn ENC N                    (ENC: SELECT ALGO PRESET K1 K2 K3 K4; N detents, one a frame)
 *   master N                      MASTER pot 0..4096
 *   wav FILE | wavstop            record the output
 *   shot FILE.png                 save the screen
 *   reboot                        start again from the simulated flash (what SAVE kept)
 *   expect param NAME VALUE       a parameter's value, by its screen name: exit 1 if not
 *   expect peak_db_min DB         the output's peak since the last wav reached DB at least
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "host_core.c"

/* ---------------------------------------------------------- outputs --- */
static FILE *wav;
static uint32_t wav_frames;
static float peak_out;

static void host_audio(const int32_t *blk, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++) {
        float v = fabsf((float)blk[i] / 8388608.0f);
        if (v > peak_out)
            peak_out = v;
    }
    if (!wav)
        return;
    for (i = 0; i < n; i++) {
        int16_t s[2] = {(int16_t)(blk[2 * i] >> 8), (int16_t)(blk[2 * i + 1] >> 8)};
        fwrite(s, 2, 2, wav);
    }
    wav_frames += n;
}

static void wav_open(const char *path)
{
    static const uint8_t h[44] = {0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(h, 1, 44, wav);
    wav_frames = 0;
    peak_out = 0;
}

static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4, v;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + bytes;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t r[2] = {ENGINE_FS, ENGINE_FS * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, wav);
        fwrite(r, 4, 2, wav);
        fwrite(al, 2, 2, wav);
    }
    fwrite("data", 1, 4, wav);
    fwrite(&bytes, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

static void png_write(const char *path)
{
    /* RGB PNG with stored (uncompressed) deflate blocks: no zlib needed */
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

/* ------------------------------------------------------------ script --- */
static const char *const BTN_NAMES[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                          "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_NAMES[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}

static int find_param(const char *s)
{
    int p;
    for (p = 0; p < NPARAM; p++)
        if (!strcmp(engine_param_info(p)->name, s))
            return p;
    return -1;
}

int main(int argc, char **argv)
{
    FILE *f;
    char line[256], outdir[200] = ".", path[512];
    int lineno = 0, fails = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: ravel_host SCRIPT [OUTDIR]\n");
        return 2;
    }
    if (argc > 2)
        snprintf(outdir, sizeof outdir, "%s", argv[2]);
    if (!(f = fopen(argv[1], "r"))) {
        perror(argv[1]);
        return 2;
    }
    boot();
    while (fgets(line, sizeof line, f)) {
        char a[64] = "", b[128] = "", c[64] = "";
        int n;
        lineno++;
        if (line[0] == '#' || (n = sscanf(line, "%63s %127s %63s", a, b, c)) < 1)
            continue;
        if (!strcmp(a, "wait")) {
            run_ms((uint32_t)atoi(b));
        } else if (!strcmp(a, "press") || !strcmp(a, "release") || !strcmp(a, "tap")) {
            int i = find_name(BTN_NAMES, NB, b);
            if (i < 0) {
                fprintf(stderr, "line %d: no button %s\n", lineno, b);
                return 2;
            }
            if (a[0] == 'p' || a[0] == 't')
                held_btn |= 1u << i;
            if (a[0] == 't')
                run_ms(40);
            if (a[0] == 'r' || a[0] == 't')
                held_btn &= ~(1u << i);
            run_ms(20);
        } else if (!strcmp(a, "key") || !strcmp(a, "tapkey")) {
            int k = atoi(b);
            if (k < 0 || k >= NKEYS)
                return 2;
            if (!strcmp(a, "tapkey") || !strcmp(c, "down"))
                held_keys |= 1u << k;
            if (!strcmp(a, "tapkey"))
                run_ms(120);
            if (!strcmp(a, "tapkey") || !strcmp(c, "up"))
                held_keys &= ~(1u << k);
            run_ms(2);
        } else if (!strcmp(a, "turn")) {
            int e = find_name(ENC_NAMES, NE, b), d = atoi(c), s = d < 0 ? -1 : 1;
            if (e < 0)
                return 2;
            while (d) {
                enc_acc[e] += s;
                d -= s;
                run_ms(17);
            }
        } else if (!strcmp(a, "master")) {
            master = (uint32_t)atoi(b);
        } else if (!strcmp(a, "wav")) {
            snprintf(path, sizeof path, "%s/%s", outdir, b);
            wav_open(path);
        } else if (!strcmp(a, "wavstop")) {
            wav_close();
        } else if (!strcmp(a, "shot")) {
            snprintf(path, sizeof path, "%s/%s", outdir, b);
            png_write(path);
        } else if (!strcmp(a, "reboot")) {
            held_btn = held_keys = 0;
            boot();
        } else if (!strcmp(a, "expect")) {
            if (!strcmp(b, "param")) {
                char name[64], val[32];
                int p;
                if (sscanf(line, "%*s %*s %63s %31s", name, val) != 2 || (p = find_param(name)) < 0) {
                    fprintf(stderr, "line %d: bad expect\n", lineno);
                    return 2;
                }
                if (eng_param[p] != atoi(val)) {
                    printf("FAIL line %d: %s is %d, expected %s\n", lineno, name, eng_param[p], val);
                    fails++;
                }
            } else if (!strcmp(b, "peak_db_min")) {
                float db = 20.0f * log10f(peak_out + 1e-9f);
                if (db < atof(c)) {
                    printf("FAIL line %d: peak %.1f dB, expected at least %s dB\n", lineno, db, c);
                    fails++;
                }
            } else {
                fprintf(stderr, "line %d: unknown expect %s\n", lineno, b);
                return 2;
            }
        } else {
            fprintf(stderr, "line %d: unknown command %s\n", lineno, a);
            return 2;
        }
    }
    wav_close();
    fclose(f);
    printf("%s: %s\n", argv[1], fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
