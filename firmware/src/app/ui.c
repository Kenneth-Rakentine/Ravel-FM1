/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel's screen and controls, and the project in flash.
 *
 * Part of the unity build (ravel.c) and of the host simulator: gfx.c, plat.h and ravel.h
 * are in scope. The screen is 240 x 240: a title strip, a row of four knob cards that
 * follow KNOB 1 to 4, a picture of what the page controls, and a footer.
 *
 * Buttons open the page nearest their printed word: HOME the tracks, EDIT the voice,
 * ARP the tape, GLO the mixer. SELECT steps through a section's pages, ALGORITHM picks
 * the track. REC catches the tape, PLAY holds the keys, SAVE writes the project. */

/* ----------------------------------------------------------- colours --- */
#define C_BG RGB(15, 19, 17)
#define C_PANEL RGB(23, 29, 26)
#define C_RULE RGB(42, 51, 46)
#define C_MUTED RGB(140, 153, 143)
#define C_INK RGB(221, 229, 222)
#define C_TEAL RGB(98, 184, 203)
#define C_AMBER RGB(214, 154, 88)
#define C_WARN RGB(224, 138, 106)

/* ----------------------------------------------------------- project --- */
project_t proj;

static void project_defaults(void)
{
    int p;
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    engine_defaults();
    for (p = 0; p < NPARAM; p++)
        proj.param[p] = eng_param[p];
    proj.octave = 4;
}

static int project_load(void)
{
    static project_t tmp;
    int n = plat_store_load(OBJ_SOUND, &tmp, sizeof tmp), p;
    if (n != (int)sizeof tmp || tmp.magic != PROJ_MAGIC || tmp.format != PROJ_FORMAT)
        return -1;
    for (p = 0; p < NPARAM; p++) {
        const param_info_t *pi = engine_param_info(p);
        int v = tmp.param[p];
        tmp.param[p] = (int16_t)(v < pi->min ? pi->min : v > pi->max ? pi->max : v);
    }
    proj = tmp;
    if (proj.octave > 7)
        proj.octave = 4;
    return 0;
}

static int project_save(void)
{
    return plat_store_save(OBJ_SOUND, &proj, sizeof proj);
}

static void project_apply(void)
{
    int p;
    for (p = 0; p < NPARAM; p++)
        eng_param[p] = proj.param[p];
}

/* ------------------------------------------------------- app entries --- */
static volatile uint32_t app_cpu;

void app_init(void)
{
    engine_init();
    if (project_load() != 0)
        project_defaults();
    proj.param[P_FREEZE] = 0;                       /* the tape starts listening */
    project_apply();
}
void app_render(int32_t *out_lr, uint32_t n) { engine_render(out_lr, n); }
void app_volume(int32_t *out_lr, uint32_t n) { engine_volume(out_lr, n, plat_master()); }
void app_load(uint32_t pct) { app_cpu = (app_cpu * 7u + pct) / 8u; }
void app_stop(void) { engine_all_off(); }

/* ------------------------------------------------------------- pages --- */
enum { SEC_TRACKS, SEC_VOICE, SEC_TAPE, SEC_MIX, NSEC };
enum { PIC_TRACKS, PIC_SCOPE, PIC_FILTER, PIC_ARTIC, PIC_TAPE, PIC_METERS };

typedef struct {
    const char *name;
    int8_t knob[4];               /* parameters on KNOB 1..4, -1 none */
    uint8_t pic;
} page_t;

static const page_t PAGES_TRACKS[] = {{"TRACKS", {P_LEV1, P_LEV7, P_PAN1, P_PAN7}, PIC_TRACKS}};
static const page_t PAGES_VOICE[] = {
    {"SOURCE", {P_WAVE, P_DETUNE, P_SUB, P_NOISE}, PIC_SCOPE},
    {"FILTER", {P_CUTOFF, P_RESO, P_ENVCUT, P_DRIVE}, PIC_FILTER},
    {"ARTICULATION", {P_ONSET, P_DECAY, P_CONTIN, P_TERM}, PIC_ARTIC},
};
static const page_t PAGES_TAPE[] = {
    {"GRAINS", {P_TIME, P_SIZE, P_SHAPE, P_PITCH}, PIC_TAPE},
    {"CLOUD", {P_DENSITY, P_RANDOM, P_FEEDBACK, P_TMIX}, PIC_TAPE},
    {"INPUT", {P_SOURCE, P_QUALITY, P_TAPEGAIN, P_FREEZE}, PIC_TAPE},
};
static const page_t PAGES_MIX[] = {{"LEVELS", {P_LEV1, P_PAN1, P_LEV7, P_PAN7}, PIC_METERS}};

static const struct {
    const char *name;
    const page_t *pages;
    uint8_t n;
} SECTIONS[NSEC] = {
    {"HOME", PAGES_TRACKS, 1},
    {"VOICE", PAGES_VOICE, 3},
    {"TAPE", PAGES_TAPE, 3},
    {"MIXER", PAGES_MIX, 1},
};

static const char *const TRACK_NAME[NTRACK] = {"SYNTH", "SYNTH", "SYNTH", "SYNTH", "STRIKE", "STRIKE", "TAPE", "TAPE"};
static const uint8_t TRACK_LIVE[NTRACK] = {1, 0, 0, 0, 0, 0, 1, 0};

static struct {
    uint8_t sec, page[NSEC], track, hold, dirty_top, dirty_pic;
    uint32_t prev_btn, prev_keys, held_notes, sounding;   /* held_notes: keys whose notes are on */
    uint8_t key_note[NKEYS];
    char msg[40];
    uint32_t msg_until, last_pic, last_foot;
} ui;

void ui_say(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a && *a && i < sizeof ui.msg - 1)
        ui.msg[i++] = *a++;
    while (b && *b && i < sizeof ui.msg - 1)
        ui.msg[i++] = *b++;
    ui.msg[i] = 0;
    ui.msg_until = plat_ms() + 1500u;
    ui.last_foot = 0;
}

static const page_t *cur_page(void) { return &SECTIONS[ui.sec].pages[ui.page[ui.sec]]; }

/* the track a page belongs to: the voice pages are T1's and the tape pages T7's in this build,
 * the tracks and mixer pages show the one ALGORITHM picked */
static int page_track(void) { return ui.sec == SEC_VOICE ? 0 : ui.sec == SEC_TAPE ? 6 : ui.track; }

/* --------------------------------------------------------- formatting --- */
static void ui_fmt_int(char *b, int v, int sign)
{
    char t[12];
    int i = 0, n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0)
        b[n++] = '-';
    else if (sign && v > 0)
        b[n++] = '+';
    do {
        t[i++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (i)
        b[n++] = t[--i];
    b[n] = 0;
}

static void param_text(int p, char *b)
{
    const param_info_t *pi = engine_param_info(p);
    int v = eng_param[p];
    const char *t = engine_param_text(p, v);
    if (t) {
        int i = 0;
        while (t[i] && i < 11) {
            b[i] = t[i];
            i++;
        }
        b[i] = 0;
        return;
    }
    switch (pi->unit) {
    case UNIT_PCT:
        ui_fmt_int(b, (v * 100 + 63) / 127, 0);
        break;
    case UNIT_MS: {
        float ms = fm_exp2f((float)v * (12.0f / 127.0f));
        if (ms < 1000.0f) {
            ui_fmt_int(b, (int)(ms + 0.5f), 0);
            {
                int n = 0;
                while (b[n])
                    n++;
                b[n] = 'M', b[n + 1] = 'S', b[n + 2] = 0;
            }
        } else {
            int t10 = (int)(ms / 100.0f + 0.5f), n;
            ui_fmt_int(b, t10 / 10, 0);
            n = 0;
            while (b[n])
                n++;
            b[n++] = '.';
            b[n++] = (char)('0' + t10 % 10);
            b[n++] = 'S';
            b[n] = 0;
        }
        break;
    }
    case UNIT_ST:
    case UNIT_BIP:
        ui_fmt_int(b, v, 1);
        break;
    default:
        ui_fmt_int(b, v, 0);
    }
}

/* ------------------------------------------------------------ drawing --- */
static void draw_top(void)
{
    const page_t *pg = cur_page();
    char b[24];
    int i, k;
    cv_begin(240, 72, C_BG);
    /* title strip: the track, the section and the page */
    b[0] = 'T';
    b[1] = (char)('1' + page_track());
    b[2] = ' ';
    for (i = 0; TRACK_NAME[page_track()][i]; i++)
        b[3 + i] = TRACK_NAME[page_track()][i];
    b[3 + i] = 0;
    cv_text(8, 3, &FONT_B, b, TRACK_LIVE[page_track()] ? C_INK : C_MUTED);
    {
        char t[32];
        const char *s = SECTIONS[ui.sec].name;
        int n = 0;
        while (*s)
            t[n++] = *s++;
        t[n++] = ' ';
        t[n++] = ' ';
        for (s = pg->name; *s;)
            t[n++] = *s++;
        if (SECTIONS[ui.sec].n > 1) {
            t[n++] = ' ';
            t[n++] = ' ';
            t[n++] = (char)('1' + ui.page[ui.sec]);
            t[n++] = '/';
            t[n++] = (char)('0' + SECTIONS[ui.sec].n);
        }
        t[n] = 0;
        cv_text(232 - text_w(&FONT_XS, t), 4, &FONT_XS, t, C_MUTED);
    }
    cv_rect(0, 22, 240, 1, C_RULE);
    /* knob cards */
    for (k = 0; k < 4; k++) {
        int p = pg->knob[k], x = k * 60;
        const param_info_t *pi;
        int32_t w;
        if (k)
            cv_rect(x, 26, 1, 42, C_RULE);
        if (p < 0)
            continue;
        pi = engine_param_info(p);
        cv_text(x + 6, 26, &FONT_XS, pi->name, C_MUTED);
        param_text(p, b);
        cv_text(x + 6, 41, text_w(&FONT_M, b) > 52 ? &FONT_B : &FONT_M, b, p == P_FREEZE && eng_param[p] ? C_AMBER : C_INK);
        cv_rect(x + 6, 66, 48, 2, C_RULE);
        if (pi->min < 0 && pi->unit != UNIT_LIST) {   /* bipolar: from the centre */
            int32_t c = x + 30, e = x + 6 + (int32_t)((eng_param[p] - pi->min) * 48 / (pi->max - pi->min));
            cv_rect(e < c ? e : c, 66, e < c ? c - e : e - c + 1, 2, C_TEAL);
        } else {
            w = (int32_t)((eng_param[p] - pi->min) * 48 / (pi->max - pi->min ? pi->max - pi->min : 1));
            cv_rect(x + 6, 66, w, 2, C_TEAL);
        }
    }
    cv_blit(0, 0);
}

static void pic_tracks(void)
{
    int t;
    for (t = 0; t < NTRACK; t++) {
        int y = t * 18, sel = t == ui.track;
        char b[4] = {'T', (char)('1' + t), 0, 0};
        uint32_t pk = eng_peak[t];
        int32_t w = (int32_t)(pk * 120u / 32767u);
        if (sel)
            cv_rect(0, y, 240, 17, C_PANEL);
        cv_text(8, y + 1, &FONT_B, b, sel ? C_INK : C_MUTED);
        cv_text(38, y + 1, &FONT_S, TRACK_NAME[t], TRACK_LIVE[t] ? C_INK : C_RULE);
        if (!TRACK_LIVE[t]) {
            cv_text(112, y + 1, &FONT_XS, "NOT YET", C_RULE);
            continue;
        }
        cv_rect(112, y + 7, 120, 3, C_RULE);
        cv_rect(112, y + 7, w, 3, t == 6 ? C_AMBER : C_TEAL);
    }
}

static void pic_scope(void)
{
    uint32_t w = eng_scope_w, i;
    int32_t prev = 72;
    cv_rect(0, 72, 240, 1, C_RULE);
    for (i = 0; i < 240u; i++) {
        int32_t v = eng_scope[(w + i) % SCOPE_N], y = 72 - v * 64 / 32768;
        if (i)
            cv_line((int32_t)i - 1, prev, (int32_t)i, y, C_TEAL);
        prev = y;
    }
}

static void pic_filter(void)
{
    /* the low pass's response, 20 Hz to 20 kHz, with the envelope's reach shaded */
    float cut = (float)eng_param[P_CUTOFF] / 127.0f * 10.0f, k = 2.0f - 1.94f * (float)eng_param[P_RESO] / 127.0f;
    float reach = (float)eng_param[P_ENVCUT] / 64.0f * 6.0f;
    int x, prev = 0;
    int32_t x0 = (int32_t)(cut / 10.0f * 239.0f), x1 = (int32_t)((cut + reach) / 10.0f * 239.0f);
    if (x1 < x0) {
        int32_t t = x0;
        x0 = x1;
        x1 = t;
    }
    cv_rect(x0 < 0 ? 0 : x0, 8, (x1 > 239 ? 239 : x1) - (x0 < 0 ? 0 : x0) + 1, 128, C_PANEL);
    for (x = 0; x < 240; x += 24)
        cv_rect(x, 0, 1, 144, C_RULE);
    for (x = 0; x < 240; x++) {
        float oct = (float)x / 239.0f * 10.0f, r = fm_exp2f(oct - cut), r2 = r * r;
        float mag = 1.0f / fm_sqrtf((1.0f - r2) * (1.0f - r2) + k * k * r2);
        float db = 20.0f * fm_log2f(mag + 1e-6f) * 0.30103f;
        int y = 40 - (int)(db * 2.2f);
        if (y < 2)
            y = 2;
        if (y > 141)
            y = 141;
        if (x)
            cv_line(x - 1, prev, x, y, C_TEAL);
        prev = y;
    }
    cv_text(4, 126, &FONT_XS, "20 HZ", C_MUTED);
    cv_text(236 - text_w(&FONT_XS, "20 KHZ"), 126, &FONT_XS, "20 KHZ", C_MUTED);
}

static void pic_artic(void)
{
    /* the three states as a path: rise to the onset, settle to the continuant, fall away */
    float on = fm_log2f(fm_exp2f((float)eng_param[P_ONSET] * 12.0f / 127.0f) + 1.0f);
    float de = fm_log2f(fm_exp2f((float)eng_param[P_DECAY] * 12.0f / 127.0f) + 1.0f);
    float te = fm_log2f(fm_exp2f((float)eng_param[P_TERM] * 12.0f / 127.0f) + 1.0f);
    float total = on + de + te + 6.0f, sc = 224.0f / total, lvl = (float)eng_param[P_CONTIN] / 127.0f;
    int32_t xa = 8 + (int32_t)(on * sc), xd = xa + (int32_t)(de * sc), xh = xd + (int32_t)(6.0f * sc), xe = 232;
    int32_t top = 16, base = 120, yl = base - (int32_t)(lvl * (float)(base - top)), v;
    cv_line(8, base, xa, top, C_INK);
    cv_line(xa, top, xd, yl, C_INK);
    cv_line(xd, yl, xh, yl, C_INK);
    cv_line(xh, yl, xe, base, C_INK);
    cv_rect(8, base + 1, 224, 1, C_RULE);
    cv_text(8, base + 6, &FONT_XS, "ONSET", C_MUTED);
    cv_text(xd - 10 < 60 ? 60 : xd - 10, base + 6, &FONT_XS, "CONTIN", C_MUTED);
    cv_text(232 - text_w(&FONT_XS, "TERM"), base + 6, &FONT_XS, "TERM", C_MUTED);
    for (v = 0; v < 4; v++) {                       /* each sounding voice where it is on the path */
        int st = eng_voice_stage[v];
        float l = (float)eng_voice_level[v] / 32767.0f;
        int32_t x, y = base - (int32_t)(l * (float)(base - top));
        if (st == 0)
            continue;
        x = st == 1 ? 8 + (int32_t)(l * (float)(xa - 8)) : st == 2 ? (xa + xh) / 2 : xh + (int32_t)((1.0f - l / (lvl > 0.05f ? lvl : 0.05f)) * (float)(xe - xh));
        if (x > xe)
            x = xe;
        cv_rect(x - 3, y - 3, 7, 7, C_TEAL);
    }
}

static void pic_tape(void)
{
    static uint8_t pk[240];
    uint16_t gp[32];
    uint8_t ga[32];
    int i, n;
    uint16_t col = engine_tape_frozen() ? C_AMBER : RGB(120, 92, 60);
    char b[24];
    engine_tape_overview(pk, 240);
    for (i = 0; i < 240; i++) {
        int h = pk[i] * 44 / 255;
        cv_rect(i, 50 - h, 1, 2 * h + 1, col);
    }
    /* grains: where each one reads, drawn on the tape, and a lane under it where they leave a
     * trail that fades over about half a second (flecks of light) */
    {
        static uint8_t trail[240];
        n = engine_grains(gp, ga, 32);
        for (i = 0; i < 240; i++)
            trail[i] = (uint8_t)(trail[i] * 7 / 8);
        for (i = 0; i < n; i++) {
            int x = gp[i] * 239 / 65535, a = 128 + ga[i] * 127 / 255;
            if (a > trail[x])
                trail[x] = (uint8_t)a;
            cv_rect(x, 6, 1, 89, C_INK);
        }
        cv_rect(0, 118, 240, 1, C_RULE);
        for (i = 0; i < 240; i++) {
            int a = trail[i], h;
            if (a < 16)
                continue;
            h = 2 + a * 16 / 255;
            cv_rect(i, 118 - h, 1, h, RGB(98 * a / 255, 184 * a / 255, 203 * a / 255));
        }
    }
    {
        uint32_t s = engine_tape_seconds_x10();
        int k = 0;
        const char *q = engine_param_text(P_QUALITY, eng_param[P_QUALITY]);
        ui_fmt_int(b, (int)(s / 10u), 0);
        while (b[k])
            k++;
        b[k++] = '.';
        b[k++] = (char)('0' + s % 10u);
        b[k++] = ' ';
        b[k++] = 'S';
        b[k++] = ' ';
        b[k++] = ' ';
        while (*q)
            b[k++] = *q++;
        b[k] = 0;
    }
    cv_text(8, 126, &FONT_XS, b, C_MUTED);
    cv_text(232 - text_w(&FONT_XS, engine_tape_frozen() ? "CAUGHT" : "LISTENING"), 126, &FONT_XS,
            engine_tape_frozen() ? "CAUGHT" : "LISTENING", engine_tape_frozen() ? C_AMBER : C_MUTED);
}

static void pic_meters(void)
{
    int t;
    for (t = 0; t < NTRACK; t++) {
        int x = 10 + t * 28, h = (int)(eng_peak[t] * 110u / 32767u);
        char b[3] = {'T', (char)('1' + t), 0};
        cv_rect(x, 8, 16, 110, C_PANEL);
        cv_rect(x, 118 - h, 16, h, t == 6 ? C_AMBER : C_TEAL);
        cv_text(x + 1, 124, &FONT_XS, b, TRACK_LIVE[t] ? C_INK : C_RULE);
    }
}

static void draw_pic(void)
{
    cv_begin(240, 144, C_BG);
    switch (cur_page()->pic) {
    case PIC_TRACKS: pic_tracks(); break;
    case PIC_SCOPE: pic_scope(); break;
    case PIC_FILTER: pic_filter(); break;
    case PIC_ARTIC: pic_artic(); break;
    case PIC_TAPE: pic_tape(); break;
    default: pic_meters();
    }
    cv_blit(0, 72);
}

static void draw_foot(void)
{
    char b[48];
    int n = 0;
    cv_begin(240, 24, C_BG);
    cv_rect(0, 0, 240, 1, C_RULE);
    if (ui.msg[0] && (int32_t)(ui.msg_until - plat_ms()) > 0) {
        cv_text(8, 5, &FONT_S, ui.msg, C_AMBER);
    } else {
        const char *s;
        ui.msg[0] = 0;
        for (s = "OCT "; *s;)
            b[n++] = *s++;
        {
            char t[6];
            int i = 0;
            ui_fmt_int(t, (int)proj.octave - 4, 1);
            while (t[i])
                b[n++] = t[i++];
        }
        for (s = ui.hold ? "   HOLD" : ""; *s;)
            b[n++] = *s++;
        if (app_cpu) {                              /* measured on the device only */
            for (s = "   CPU "; *s;)
                b[n++] = *s++;
            {
                char t[6];
                int i = 0;
                ui_fmt_int(t, (int)app_cpu, 0);
                while (t[i])
                    b[n++] = t[i++];
            }
            b[n++] = '%';
        }
        b[n] = 0;
        cv_text(8, 6, &FONT_XS, b, C_MUTED);
        cv_text(232 - text_w(&FONT_XS, "RAVEL"), 6, &FONT_XS, "RAVEL", C_RULE);
    }
    cv_blit(0, 216);
}

/* ------------------------------------------------------------- input --- */
static void set_param(int p, int v)
{
    const param_info_t *pi = engine_param_info(p);
    if (v < pi->min)
        v = pi->min;
    if (v > pi->max)
        v = pi->max;
    if (v == eng_param[p])
        return;
    eng_param[p] = (int16_t)v;
    proj.param[p] = (int16_t)v;
    ui.dirty_top = 1;
}

static void open_section(int s)
{
    if (ui.sec == s && SECTIONS[s].n > 1)
        ui.page[s] = (uint8_t)((ui.page[s] + 1) % SECTIONS[s].n);   /* again: the next page */
    ui.sec = (uint8_t)s;
    ui.dirty_top = ui.dirty_pic = 1;
}

static void keys_update(void)
{
    uint32_t keys = plat_keys(), down = keys & ~ui.prev_keys, up = ui.prev_keys & ~keys, k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if (down & bit) {
            if (ui.hold && (ui.sounding & bit)) {        /* HOLD: a held key pressed again lets go */
                engine_note_off(ui.key_note[k]);
                ui.sounding &= ~bit;
                continue;
            }
            ui.key_note[k] = (uint8_t)(53 + (int)k + ((int)proj.octave - 4) * 12);
            engine_note_on(ui.key_note[k], 100);
            ui.sounding |= bit;
        }
        if ((up & bit) && !ui.hold && (ui.sounding & bit)) {
            engine_note_off(ui.key_note[k]);
            ui.sounding &= ~bit;
        }
    }
    ui.prev_keys = keys;
}

static void input_update(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.prev_btn;
    int k, d;
    ui.prev_btn = btn;
    if (down & (1u << B_HOME))
        open_section(SEC_TRACKS);
    if (down & (1u << B_EDIT))
        open_section(SEC_VOICE);
    if (down & (1u << B_ARP))
        open_section(SEC_TAPE);
    if (down & (1u << B_GLO))
        open_section(SEC_MIX);
    if (down & (1u << B_REC)) {
        set_param(P_FREEZE, !eng_param[P_FREEZE]);
        ui_say(eng_param[P_FREEZE] ? "TAPE CAUGHT" : "TAPE LISTENING", 0);
        ui.dirty_pic = 1;
    }
    if (down & (1u << B_PLAY)) {
        ui.hold = !ui.hold;
        if (!ui.hold) {
            engine_all_off();
            ui.sounding = 0;
        }
        ui_say(ui.hold ? "HOLD ON" : "HOLD OFF", 0);
    }
    if (down & (1u << B_OCTDN) && proj.octave > 1) {
        proj.octave--;
        ui.last_foot = 0;
    }
    if (down & (1u << B_OCTUP) && proj.octave < 7) {
        proj.octave++;
        ui.last_foot = 0;
    }
    if (down & (1u << B_SAVE)) {
        int rc = project_save();
        ui_say(rc == 0 ? "SAVED" : "NOT SAVED", 0);
    }
    d = plat_enc(EN_SELECT);
    if (d) {
        int n = SECTIONS[ui.sec].n, p = (int)ui.page[ui.sec] + d;
        p = p < 0 ? 0 : p >= n ? n - 1 : p;
        if (p != ui.page[ui.sec]) {
            ui.page[ui.sec] = (uint8_t)p;
            ui.dirty_top = ui.dirty_pic = 1;
        }
    }
    d = plat_enc(EN_ALGO);
    if (d) {
        int t = (int)ui.track + d;
        ui.track = (uint8_t)(t < 0 ? 0 : t > NTRACK - 1 ? NTRACK - 1 : t);
        ui.dirty_top = ui.dirty_pic = 1;
    }
    (void)plat_enc(EN_PRESET);
    for (k = 0; k < 4; k++) {
        int p = cur_page()->knob[k];
        d = plat_enc(EN_K1 + k);
        if (!d || p < 0)
            continue;
        if (d > 1 || d < -1)
            d *= 3;                                     /* fast turns cover the range */
        if (engine_param_info(p)->unit == UNIT_LIST)
            d = d > 0 ? 1 : -1;
        set_param(p, eng_param[p] + d);
    }
    keys_update();
    {
        uint32_t lit = 0;
        static const uint8_t SEC_BTN[NSEC] = {B_HOME, B_EDIT, B_ARP, B_GLO};
        lit |= 1u << SEC_BTN[ui.sec];
        if (eng_param[P_FREEZE])
            lit |= 1u << B_REC;
        if (ui.hold)
            lit |= 1u << B_PLAY;
        plat_leds(lit, ui.sounding | plat_keys());
    }
}

/* ---------------------------------------------------------- the loop --- */
void ui_init(void)
{
    ui.sec = SEC_TRACKS;
    ui.dirty_top = ui.dirty_pic = 1;
    ui.prev_btn = plat_buttons();
    ui.prev_keys = plat_keys();
    ui.last_pic = ui.last_foot = 0;
}

void ui_input_only(void) { input_update(); }

void ui_frame(void)
{
    uint32_t now = plat_ms();
    input_update();
    if (ui.dirty_top) {
        ui.dirty_top = 0;
        draw_top();
    }
    if (ui.dirty_pic || now - ui.last_pic >= 66u) {   /* the picture moves at ~15 frames a second */
        ui.dirty_pic = 0;
        ui.last_pic = now;
        draw_pic();
    }
    if (!ui.last_foot || now - ui.last_foot >= 250u) {
        ui.last_foot = now ? now : 1;
        draw_foot();
    }
}
