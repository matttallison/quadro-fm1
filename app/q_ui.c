/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO: the panel. The FM-1's controls as one synthesizer's:
 *
 *   ALGORITHM   the section: BASS, STRINGS, POLY, LEAD, PHASER, MIXER (as the original's panel, left to right)
 *   EDIT        the section's sound (VOICES / VCO, VCF, ...)   again: the next page
 *   ENV         its envelope        LFO  its modulation         SEL  the keyboard (split, octaves, trigger, ...)
 *   FX          the PHASE SHIFTER   GLO  the OUTPUT MIXER and the room     ARP  the lead's SEQUENCER
 *   SEQ         the next section (as ALGORITHM)
 *   HOME        the four sections at a glance, KNOB 1-4 their levels (the output mixer); held: SETUP
 *   PRESETS     the 68 programs (load at once, as the original's voice buttons)
 *   SAVE        into a program (PRESETS picks which, SAVE again writes, any other button: no)
 *   SELECT      the phaser's sweep speed, always (a performance knob)
 *   PLAY        HOLD (the keys stay down: the original's HOLD footswitch)    REC  SUSTAIN (the longest release)
 *   OCT- / OCT+ the keyboard's octave (both: back to 0)
 *   KNOB 1-4    the page's four columns; MASTER the volume
 *
 * The screen, top to bottom: the program and the latches; the five sections (a lamp while each sounds, its level);
 * the page (or, while a knob turns, its parameter in full); a picture of what the page sets, live where it moves;
 * the four knobs. Drawn in regions, each when what it shows changed. No hardware here: app.c reads the panel
 * and lights the LEDs (ui_leds); the host simulator (base/host) draws the same screens. */

#ifndef QD_VER
#define QD_VER "0.0"
#endif

/* colours */
#define C_BG RGB(12, 12, 14)
#define C_PANEL RGB(26, 26, 30)
#define C_RULE RGB(52, 52, 58)
#define C_LBL RGB(140, 140, 150)
#define C_TXT RGB(236, 236, 240)
#define C_DIMT RGB(84, 84, 92)
static const uint16_t SEC_COL[SEC_N] = {RGB(236, 84, 60), RGB(244, 180, 52), RGB(92, 204, 124), RGB(72, 154, 244),
                                        RGB(188, 116, 236), RGB(208, 208, 214)};
static const uint16_t SEC_DARK[SEC_N] = {RGB(70, 22, 16), RGB(72, 52, 10), RGB(20, 62, 34), RGB(16, 44, 82),
                                         RGB(56, 30, 74), RGB(60, 60, 64)};

#define NPROG 68u
static q_patch_t progs[NPROG];                  /* the programs (flash: app.c) */

static struct {
    uint8_t sec, page, home;                    /* home: the overview (the knobs are the mixer) */
    uint8_t prog, edited;
    int8_t oct;                                 /* the keyboard: -3 .. +3 octaves */
    uint8_t hold, sus;
    uint8_t setup, setup_sel;                   /* HOME held: SETUP */
    uint8_t saving, save_to;                    /* SAVE: the dialog, its target */
    uint8_t factory_arm;
    int16_t pop_param;                          /* a knob turned: its parameter shows in full ... */
    uint32_t pop_ms;                            /* ... until then */
    uint8_t col_touched;                        /* the column turned last (+1) */
    uint32_t held;                              /* buttons held */
    uint32_t keys;                              /* keys down (0 = F3) */
    uint8_t midi_ch;                            /* 0 = omni, 1..16 */
    uint8_t ble_off;                            /* Bluetooth switched off in SETUP */
    uint8_t tsens;                              /* SETUP TOUCH: 0 off, 1 (least) .. 6; 2 by default */
    uint8_t setup_page;                         /* SETUP: 0 MIDI / BLE / TOUCH / CLOCK, 1 TEMPO / RUN / FACTORY */
    uint8_t clk_out;                            /* CLOCK: QUADRO sends MIDI clock (and follows it itself) */
    uint8_t clk_running;                        /* RUN: START sent (STOP: no) */
    uint16_t tempo10;                           /* TEMPO x 10, 40.0 .. 240.0 BPM */
    char msg[24];
    uint32_t msg_until;
    uint32_t sig_head, sig_rail, sig_title, sig_viz, sig_knobs;
    uint32_t viz_ms, rail_ms;
    uint8_t force, save_req, factory_req, store_req;
} ui;

/* ------------------------------------------------------------------ drawing --- */
static int32_t cv_text_on(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint16_t bg)
{
    (void)bg;                                   /* (fm1-base's cv_text blends over what is drawn) */
    return cv_text(x, y, f, s, c);
}
static void text_bold(int32_t x, int32_t y, const char *s, uint16_t c, uint16_t bg)
{
    cv_text_on(x, y, &FONT_S, s, c, bg);
    cv_text_on(x + 1, y, &FONT_S, s, c, bg);
}
static void text_c(int32_t cx, int32_t y, const char *s, uint16_t c, uint16_t bg, int bold)
{
    int32_t x = cx - text_w(&FONT_S, s) / 2;
    if (bold) text_bold(x, y, s, c, bg);
    else cv_text_on(x, y, &FONT_S, s, c, bg);
}
static void text_r(int32_t rx, int32_t y, const char *s, uint16_t c, uint16_t bg)
{
    cv_text_on(rx - text_w(&FONT_S, s), y, &FONT_S, s, c, bg);
}
static void frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}
static void thick_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    cv_line(x0, y0, x1, y1, c);
    cv_line(x0, y0 + 1, x1, y1 + 1, c);
}
static uint16_t mix565(uint16_t a, uint16_t b, int32_t t)     /* a -> b by t / 256 */
{
    int32_t r = (a >> 11) + (((b >> 11) - (a >> 11)) * t >> 8);
    int32_t g = ((a >> 5) & 63) + ((((b >> 5) & 63) - ((a >> 5) & 63)) * t >> 8);
    int32_t bl = (a & 31) + (((b & 31) - (a & 31)) * t >> 8);
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* ------------------------------------------------------------------ text of values --- */
static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *num(char *p, int32_t v, int sign)
{
    char t[12];
    uint32_t u = (uint32_t)(v < 0 ? -v : v), n = 0;
    if (v < 0) *p++ = '-';
    else if (sign && v > 0) *p++ = '+';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void fmt_value(uint32_t id, int32_t v, char *b)
{
    const q_desc_t *d = &QD[id];
    b[0] = 0;
    switch (d->fmt) {
    case F_ONOFF: cat(b, v ? "ON" : "OFF"); break;
    case F_ENUM: cat(b, d->names[q_clampi(v, d->min, d->max) - d->min]); break;
    case F_SEMI: num(b, v, 1); break;
    case F_CENT: cat(num(b, v, id != QP_L_DET), "c"); break;
    case F_OCT: num(b, v, 1); break;
    case F_NOTE: cat(num(cat(b, NOTE_NAME[v % 12]), v / 12 - 1, 0), ""); break;
    case F_TIME: {
        uint32_t m10 = Q_TIME_MS10[id == QP_P_D ? Q_PDEC(v & 127) : id == QP_B_DEC ? Q_BDEC(v & 127) : v & 127];
        if (id == QP_B_DEC && v >= 127) { cat(b, "HOLD"); break; }
        if (m10 < 10000u) cat(num(b, (int32_t)(m10 + 5u) / 10, 0), "ms");
        else { char *p = num(b, (int32_t)(m10 / 10000u), 0); *p++ = '.'; *p++ = (char)('0' + m10 / 1000u % 10u); cat(p, "s"); }
        break;
    }
    case F_HZ: {
        uint32_t h = Q_HZ[v & 127];
        if (h < 1000u) cat(num(b, (int32_t)h, 0), "Hz");
        else { char *p = num(b, (int32_t)(h / 1000u), 0); *p++ = '.'; *p++ = (char)('0' + h / 100u % 10u); cat(p, "k"); }
        break;
    }
    case F_RATE: {
        uint32_t idx = id == QP_PH_SPD ? (uint32_t)v * 94u / 127u : id == QP_TRSPD ? 72u + (uint32_t)v * 55u / 127u :
                       38u + (uint32_t)v * 82u / 127u, h = Q_LFO_HZ100[idx];
        char *p = num(b, (int32_t)(h / 100u), 0);
        *p++ = '.';
        *p++ = (char)('0' + h / 10u % 10u);
        if (h < 1000u) *p++ = (char)('0' + h % 10u);
        cat(p, "Hz");
        break;
    }
    case F_PCT: num(b, (v * 100 + 63) / 127, 0); break;
    default: num(b, v, 0); break;
    }
    if (id == QP_L_TRILL && !v) { b[0] = 0; cat(b, "OFF"); }
}

/* ------------------------------------------------------------------ pages --- */
static const q_page_t *cur_page(void) { return &PAGES[ui.page % NPAGES]; }
static uint32_t page_find(uint32_t sec, uint32_t kind, uint32_t after)   /* the next page of sec and kind after page
                                                                          * `after` (NPAGES: the first), NPAGES none */
{
    uint32_t i, first = NPAGES;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].sec == sec && PAGES[i].kind == kind) {
            if (first == NPAGES) first = i;
            if (after < NPAGES && i > after) return i;
        }
    return first;
}
static void open_page(uint32_t i)
{
    ui.page = (uint8_t)i;
    ui.sec = PAGES[i].sec;
    ui.home = 0;
    ui.setup = 0;
    ui.pop_ms = 0;
    ui.force = 1;
}
static void open_kind(uint32_t sec, uint32_t kind)       /* again on the same kind: its next page */
{
    uint32_t again = !ui.home && !ui.setup && PAGES[ui.page].sec == sec && PAGES[ui.page].kind == kind;
    uint32_t i = page_find(sec, kind, again ? ui.page : NPAGES);
    if (i == NPAGES)
        i = page_find(sec, PK_EDIT, NPAGES);
    open_page(i);
}
static void set_section(uint32_t sec)
{
    uint32_t kind = ui.home ? PK_EDIT : PAGES[ui.page].kind, i = page_find(sec, kind, NPAGES);
    if (i == NPAGES)
        i = page_find(sec, PK_EDIT, NPAGES);
    open_page(i);
}

static void ui_say(const char *m)
{
    uint32_t i;
    for (i = 0; m[i] && i < sizeof ui.msg - 1u; i++) ui.msg[i] = m[i];
    ui.msg[i] = 0;
    ui.msg_until = QMS + 1400u;
    ui.force = 1;
}

/* the patch was changed by a knob: the sound has it at once (qp is the ISR's) */
static void param_set(uint32_t id, int32_t v)
{
    const q_desc_t *d = &QD[id];
    v = q_clampi(v, d->min, d->max);
    if (qp.v[id] != v) {
        qp.v[id] = (int8_t)v;
        ui.edited = 1;
    }
    ui.pop_param = (int16_t)id;
    ui.pop_ms = QMS + 1500u;
}
static int32_t accel(int32_t d, int32_t range)
{
    int32_t a = d < 0 ? -d : d;
    if (range <= 4) return d > 0 ? 1 : -1;  /* switches and short lists: one step a detent */
    return a >= 3 ? d * 3 : d;
}
static void q_clock_run(int on);              /* app.c: START / STOP out (and to the sequencer) */
static void knob(uint32_t n, int32_t d)
{
    uint32_t id;
    if (ui.setup) {
        if (!ui.setup_page) {
            if (n == 0) ui.midi_ch = (uint8_t)q_clampi((int32_t)ui.midi_ch + (d > 0 ? 1 : -1), 0, 16);
            if (n == 1 && FM1B_BLE) { ui.ble_off = d < 0; plat_ble_enable(!ui.ble_off); }
            if (n == 2) { ui.tsens = (uint8_t)q_clampi((int32_t)ui.tsens + (d > 0 ? 1 : -1), 0, 6); touch_sens((int)ui.tsens - 6); }
            if (n == 3) { ui.clk_out = d > 0; if (!ui.clk_out && ui.clk_running) q_clock_run(0); }
        } else {
            if (n == 0) ui.tempo10 = (uint16_t)q_clampi((int32_t)ui.tempo10 + (d > 2 || d < -2 ? d * 10 : d * 5), 400, 2400);
            if (n == 1 && ui.clk_out) q_clock_run(d > 0);
            if (n == 3) {
                if (!ui.factory_arm) { ui.factory_arm = 1; ui_say("TURN AGAIN: FACTORY"); }
                else { ui.factory_arm = 0; ui.factory_req = 1; }
            }
        }
        ui.store_req = 1;
        ui.force = 1;
        return;
    }
    if (ui.saving)
        return;
    id = ui.home ? (uint32_t)(QP_M_B + n) : cur_page()->p[n];
    if (id >= QP_N)
        return;
    {
        const q_desc_t *dd = &QD[id];
        int32_t range = dd->max - dd->min;
        if (dd->fmt == F_ONOFF)
            param_set(id, d > 0 ? 1 : 0);
        else
            param_set(id, qp.v[id] + accel(d, range));
    }
    ui.col_touched = (uint8_t)(n + 1u);
}

static void prog_load(uint32_t k)
{
    ui.prog = (uint8_t)(k % NPROG);
    qp = progs[ui.prog];
    ui.edited = 0;
    ui.force = 1;
}

/* ------------------------------------------------------------------ regions --- */
static uint32_t sig_of(const void *p, uint32_t n, uint32_t h)
{
    const uint8_t *b = p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}
static int section_on(uint32_t s)               /* a section that can sound */
{
    switch (s) {
    case SEC_BASS: return q_bass_on() && (P(QP_B_EB16) | P(QP_B_EB8) ? P(QP_M_B) > 0 : P(QP_M_S) > 0);
    case SEC_STR: return (P(QP_S_8) | P(QP_S_4)) && P(QP_M_S) > 0;
    case SEC_POLY: return (P(QP_P_8) | P(QP_P_4)) && P(QP_M_P) > 0;
    case SEC_LEAD: return P(QP_M_L) > 0;
    case SEC_PHAS: return P(QP_PH_B) | P(QP_PH_S) | P(QP_PH_P) | P(QP_PH_L);
    default: return 1;
    }
}

/* the header: QUADRO, the program, the latches */
static void draw_head(void)
{
    char b[28], *p;
    int32_t x = 236, nx = 70;
    cv_begin(240, 22, C_BG);
    text_bold(4, 3, "QUADRO", C_TXT, C_BG);
    cv_rect(64, 4, 1, 14, C_RULE);
    {   /* right: SUS, HOLD, the clock's tempo, BT, the octave, as small badges (drawn first: the name fits what is left) */
        if (ui.sus) { int32_t w = text_w(&FONT_S, "SUS") + 6; x -= w; cv_rect(x, 3, w, 16, SEC_COL[SEC_STR]); cv_text_on(x + 3, 3, &FONT_S, "SUS", C_BG, SEC_COL[SEC_STR]); x -= 3; }
        if (ui.hold) { int32_t w = text_w(&FONT_S, "HOLD") + 6; x -= w; cv_rect(x, 3, w, 16, SEC_COL[SEC_POLY]); cv_text_on(x + 3, 3, &FONT_S, "HOLD", C_BG, SEC_COL[SEC_POLY]); x -= 3; }
        if (q_mon.bpm10) { char t[10]; num(t, (q_mon.bpm10 + 5) / 10, 0); { int32_t w = text_w(&FONT_S, t) + 6; x -= w; cv_rect(x, 3, w, 16, SEC_COL[SEC_LEAD]); cv_text_on(x + 3, 3, &FONT_S, t, C_BG, SEC_COL[SEC_LEAD]); x -= 3; } }
        if (FM1B_BLE && !ui.ble_off) { int32_t w = text_w(&FONT_S, "BT") + 6; x -= w; frame(x, 3, w, 16, plat_ble_connected() ? SEC_COL[SEC_LEAD] : C_RULE); cv_text_on(x + 3, 3, &FONT_S, "BT", plat_ble_connected() ? SEC_COL[SEC_LEAD] : C_DIMT, C_BG); x -= 3; }
        if (ui.oct) { char o[6]; num(o, ui.oct, 1); { int32_t w = text_w(&FONT_S, o) + 6; x -= w; frame(x, 3, w, 16, C_LBL); cv_text_on(x + 3, 3, &FONT_S, o, C_TXT, C_BG); x -= 3; } }
    }
    p = cat(b, "P");
    if (ui.prog + 1u < 10u) *p++ = '0';
    p = num(p, ui.prog + 1, 0);
    *p++ = ' ';
    p = cat(p, qp.name);
    if (ui.edited) cat(p, "*");
    while (p > b + 3 && text_w(&FONT_S, b) > x - 2 - nx) {   /* the name shortened to the room (the number stays) */
        *--p = 0;
        if (ui.edited && p > b + 3) { p[-1] = '*'; }
    }
    cv_text_on(nx, 3, &FONT_S, b, C_TXT, C_BG);
    cv_rect(0, 21, 240, 1, C_RULE);
    cv_blit(0, 0);
}

/* the five sections: name, level, a lamp while it sounds */
static void draw_rail(void)
{
    uint32_t s;
    cv_begin(240, 40, C_BG);
    for (s = 0; s < 5u; s++) {
        int32_t x = 2 + (int32_t)s * 47, w = 45;
        int sel = !ui.home && !ui.setup && ui.sec == s, on = section_on(s);
        uint16_t bg = sel ? SEC_COL[s] : C_PANEL, fg = sel ? C_BG : on ? SEC_COL[s] : C_DIMT;
        int32_t lvl = s < 4u ? P(QP_M_B + s) : P(QP_PH_DEP), pk = s < 4u ? q_mon.peak[s] : on ? (int32_t)q_mon.ph_pos : 0;
        cv_rect(x, 3, w, 34, bg);
        if (ui.home || (ui.sec == SEC_MIX && !ui.setup))
            frame(x, 3, w, 34, on ? SEC_COL[s] : C_RULE);
        text_c(x + w / 2, 5, SEC_SHORT[s], fg, bg, sel);
        cv_rect(x + 4, 27, w - 8, 5, sel ? SEC_DARK[s] : C_BG);   /* the level */
        cv_rect(x + 4, 27, (w - 8) * lvl / 127, 5, sel ? C_BG : on ? SEC_COL[s] : C_DIMT);
        if (s < 4u && on) {                                      /* the lamp: brightness from the meter */
            int32_t t = pk > 2000 ? 255 : pk * 255 / 2000;
            cv_rect(x + w - 8, 6, 5, 5, mix565(sel ? SEC_DARK[s] : C_RULE, sel ? C_TXT : SEC_COL[s], t));
        }
    }
    cv_blit(0, 22);
}

/* the page: section > page, the page dots; a knob turned: its parameter and value instead */
static void draw_title(void)
{
    char b[40], v[16];
    uint16_t col = ui.setup ? C_TXT : ui.home ? C_TXT : SEC_COL[ui.sec];
    cv_begin(240, 22, C_BG);
    cv_rect(0, 2, 5, 18, col);
    if (ui.msg_until > QMS) {
        text_bold(10, 3, ui.msg, C_TXT, C_BG);
    } else if (ui.saving) {
        char *p = cat(b, "SAVE TO P");
        if (ui.save_to + 1u < 10u) *p++ = '0';
        num(p, ui.save_to + 1, 0);
        text_bold(10, 3, b, SEC_COL[SEC_STR], C_BG);
        text_r(236, 3, "SAVE: YES", C_LBL, C_BG);
    } else if (ui.pop_ms > QMS && ui.pop_param >= 0) {
        fmt_value((uint32_t)ui.pop_param, qp.v[ui.pop_param], v);
        cv_text_on(10, 3, &FONT_S, QD[ui.pop_param].long_label, C_LBL, C_BG);
        text_bold(234 - text_w(&FONT_S, v), 3, v, col, C_BG);
    } else if (ui.setup) {
        text_bold(10, 3, ui.setup_page ? "SETUP: CLOCK" : "SETUP", C_TXT, C_BG);
        text_r(236, 3, ui.setup_page ? "2/2" : "1/2", C_LBL, C_BG);
    } else if (ui.home) {
        text_bold(10, 3, "OUTPUT MIXER", C_TXT, C_BG);
        text_r(236, 3, "HOME", C_LBL, C_BG);
    } else {
        const q_page_t *pg = cur_page();
        uint32_t i, n = 0, k = 0;
        char *p = cat(b, SEC_NAME[pg->sec]);
        int32_t x;
        x = cv_text_on(10, 3, &FONT_S, b, col, C_BG);
        (void)p;
        cv_text_on(x + 6, 3, &FONT_S, pg->name, C_TXT, C_BG);
        cv_text_on(x + 7, 3, &FONT_S, pg->name, C_TXT, C_BG);
        for (i = 0; i < NPAGES; i++)            /* the pages of this button: dots */
            if (PAGES[i].sec == pg->sec && PAGES[i].kind == pg->kind) {
                if (i == ui.page) k = n;
                n++;
            }
        if (n > 1u)
            for (i = 0; i < n; i++)
                cv_rect(236 - (int32_t)(n - i) * 9, 9, 6, 6, i == k ? col : C_RULE);
    }
    cv_rect(0, 21, 240, 1, C_RULE);
    cv_blit(0, 62);
}

/* ---- the pictures (240 x 90) */
#define VZ_H 90
static void vz_grid(void)
{
    int32_t x;
    for (x = 0; x < 240; x += 30)
        cv_rect(x, 0, 1, VZ_H, C_PANEL);
    cv_rect(0, VZ_H - 1, 240, 1, C_RULE);
}
/* a wave's sample at phase ph (0..65535) for the pictures, -32767..32767 */
static int32_t vz_wave(uint32_t kind, uint32_t ph, uint32_t pw)
{
    ph &= 0xFFFFu;
    switch (kind) {
    case 1: return ph < 32768u ? 26000 : -26000;                        /* square */
    case 2: return ph < pw ? 26000 : -26000;                            /* pulse of width pw / 65536 */
    default: return 30000 - (int32_t)(ph * 60000u / 65536u);                /* saw */
    }
}
static void vz_trace(int32_t x0, int32_t w, int32_t y0, int32_t h, uint16_t c, int32_t (*f)(int32_t x, void *a), void *a)
{
    int32_t x, py = 0;
    for (x = 0; x < w; x++) {
        int32_t y = y0 + h / 2 - f(x, a) * (h / 2) / 32768;
        if (x) thick_line(x0 + x - 1, py, x0 + x, y, c);
        py = y;
    }
}
typedef struct { int32_t w8, w4, hol, cyc, pw, kind2; } vzw_t;
static int32_t vz_tone_at(int32_t x, void *a)    /* the 8' / 4' tone generator: the sum */
{
    vzw_t *w = a;
    uint32_t ph = (uint32_t)(x * 65536 * w->cyc / 200);
    int32_t s = 0, n = 0;
    if (w->w8) { s += vz_wave(w->hol, ph, 32768), n++; }
    if (w->w4) { s += vz_wave(w->hol, ph * 2u, 32768), n++; }
    return n ? s / n : 0;
}
static void vz_voices(void)
{
    vzw_t w = {0, 0, 0, 2, 32768, 0};
    uint32_t s = ui.sec;
    uint16_t c = SEC_COL[s];
    if (s == SEC_BASS) {                        /* the four bass voices: two pictures, ELEC and STRING */
        int32_t i;
        for (i = 0; i < 2; i++) {
            int32_t on16 = P(i ? QP_B_SB16 : QP_B_EB16), on8 = P(i ? QP_B_SB8 : QP_B_EB8), x0 = 6 + i * 120;
            uint16_t cc = on16 | on8 ? c : C_DIMT;
            int32_t x, py = 0;
            for (x = 0; x < 108; x++) {         /* 16' square + 8' pulse narrowing with the envelope (ELEC) */
                uint32_t ph = (uint32_t)(x * 65536 * 2 / 108);
                int32_t v = 0, n = 0, y;
                if (on16) v += vz_wave(1, ph >> 1, 0), n++;
                if (on8) v += vz_wave(i ? 1 : 2, ph, i ? 32768u : 22000u), n++;
                v = n ? v / n : 0;
                y = 40 - v * 26 / 32768;
                if (x) thick_line(x0 + x - 1, py, x0 + x, y, cc);
                py = y;
            }
            cv_text_on(x0, 70, &FONT_S, i ? "STRING BASS" : "ELEC BASS", cc, C_BG);
        }
        return;
    }
    if (s == SEC_STR) w.w8 = P(QP_S_8), w.w4 = P(QP_S_4);
    else w.w8 = P(QP_P_8), w.w4 = P(QP_P_4);
    w.hol = P(QP_HOLLOW);
    vz_trace(8, 200, 6, 64, w.w8 | w.w4 ? c : C_DIMT, vz_tone_at, &w);
    cv_text_on(8, 72, &FONT_S, w.hol ? "HOLLOW (SQUARE)" : "SAWTOOTH", C_LBL, C_BG);
    text_r(234, 72, w.w8 && w.w4 ? "8'+4'" : w.w8 ? "8'" : w.w4 ? "4'" : "--", c, C_BG);
}
/* the VCF: a 24 dB low-pass drawn in dB over a log frequency axis (integer: -24 dB an octave above the cutoff,
 * a resonance bump around it), the envelope's reach shaded */
static int32_t vcf_db(int32_t x, int32_t xc, int32_t res)   /* x, xc: 0..239 (20 Hz .. 18 kHz, 9.8 octaves); dB x 8 */
{
    int32_t d8 = (x - xc) * 327 / 1000, db8 = d8 > 0 ? -d8 * 24 : 0, b = 8 - (d8 < 0 ? -d8 : d8);
    if (b > 0)                                  /* the resonance: up to ~18 dB, an octave either side */
        db8 += res * 18 * 8 / 127 * b * b / 64;
    return db8;
}
static void vz_vcf(void)
{
    uint32_t s = ui.sec;
    int32_t cut = s == SEC_BASS ? P(QP_B_CUT) : s == SEC_POLY ? P(QP_P_CUT) : P(QP_L_CUT);
    int32_t res = s == SEC_BASS ? P(QP_B_RES) : s == SEC_POLY ? P(QP_P_RES) : P(QP_L_RES);
    int32_t env = s == SEC_BASS ? P(QP_B_ENV) : s == SEC_POLY ? P(QP_P_ENV) : P(QP_L_ENV);
    int32_t xc = cut * 239 / 127, xe = q_clampi((cut + env / 2) * 239 / 127, 0, 239), x, py = 0;
    int32_t live = s == SEC_BASS ? q_mon.bass_env : s == SEC_POLY ? q_mon.poly_env : q_mon.lead_env;
    uint16_t c = SEC_COL[s];
    vz_grid();
    cv_rect(xc, 4, xe - xc, VZ_H - 8, SEC_DARK[s]);                 /* how far the envelope opens it */
    {
        int32_t xl = q_clampi((cut + (env * live >> 15) / 2) * 239 / 127, 0, 239);
        cv_rect(xl, 4, 2, VZ_H - 8, mix565(C_BG, c, 160));          /* where it is now */
    }
    for (x = 0; x < 240; x++) {
        int32_t db = vcf_db(x, xc, res), y = 30 - db / 12;
        y = q_clampi(y, 2, VZ_H - 3);
        if (x) thick_line(x - 1, py, x, y, c);
        py = y;
    }
    cv_rect(xc, 2, 1, VZ_H - 4, C_TXT);
    cv_text_on(4, VZ_H - 18, &FONT_S, "24dB", C_LBL, C_BG);
}
/* envelopes: segment widths from the times (log), the level drawn; the voice's live level on the right */
static int32_t seg_w(int32_t v) { return 6 + v * 46 / 127; }
static void vz_env(uint32_t vz)
{
    uint32_t s = ui.sec;
    uint16_t c = SEC_COL[s];
    int32_t top = 10, bot = VZ_H - 12, x = 6, y, live = 0;
#define LV(l) (bot - ((l) * (bot - top) >> 7))
    vz_grid();
    switch (vz) {
    case VZ_ENV_AD: {                           /* BASS: preset attack, DECAY while held, preset release */
        int32_t d = P(QP_B_DEC);
        thick_line(x, bot, x + 4, top, c); x += 4;
        y = d >= 127 ? top : LV(d > 100 ? 64 : 10);
        thick_line(x, top, x + seg_w(d) * 2, y, c); x += seg_w(d) * 2;
        thick_line(x, y, x + 60, d >= 127 ? top : LV(4), c); x += 60;
        thick_line(x, d >= 127 ? top : LV(4), x + 10, bot, c);
        live = q_mon.bass_env;
        break;
    }
    case VZ_ENV_AR: {                           /* STRINGS: attack, hold, release */
        int32_t a = seg_w(q_clampi(P(QP_S_ATK), 24, 127)), r = seg_w(q_clampi(P(QP_S_REL), 30, 127)) * 2;
        thick_line(x, bot, x + a, top, c); x += a;
        thick_line(x, top, x + 70, top, c); x += 70;
        thick_line(x, top, x + r, bot, c);
        live = q_mon.str_env;
        break;
    }
    case VZ_ENV_ADSR: {
        int32_t a = seg_w(P(QP_P_A)), d = seg_w(P(QP_P_D)), sl = LV(P(QP_P_S)), r = seg_w(P(QP_P_R));
        thick_line(x, bot, x + a, top, c); x += a;
        thick_line(x, top, x + d, sl, c); x += d;
        thick_line(x, sl, x + 60, sl, c); x += 60;
        thick_line(x, sl, x + r, bot, c);
        live = q_mon.poly_env;
        break;
    }
    default: {                                  /* LEAD ADS/ASR: the sforzando, or SUS LOW */
        int32_t a = seg_w(P(QP_L_A)), r = seg_w(P(QP_L_R)), slow = P(QP_L_SLOW);
        int32_t s1 = slow ? LV(26) : LV(P(QP_L_DEC) * 3 / 4);
        thick_line(x, bot, x + a, top, c); x += a;
        if (slow) {
            int32_t d = seg_w(40 + P(QP_L_DEC) * 70 / 127);
            thick_line(x, top, x + d, s1, c); x += d;
            thick_line(x, s1, x + 60, s1, c); x += 60;
            thick_line(x, s1, x + r, bot, c);
        } else {
            thick_line(x, top, x + 14, s1, c); x += 14;
            thick_line(x, s1, x + 10, s1, c); x += 10;
            thick_line(x, s1, x + 34, top, c); x += 34;
            thick_line(x, top, x + 40, top, c); x += 40;
            thick_line(x, top, x + r, bot, c);
        }
        live = q_mon.lead_env;
        break;
    }
    }
#undef LV
    cv_rect(228, top, 6, bot - top, C_PANEL);   /* the live level */
    cv_rect(228, bot - (live * (bot - top) >> 15), 6, live * (bot - top) >> 15, c);
}
static void vz_vco(void)
{
    int32_t wave = P(QP_L_WAVE), i;
    uint16_t c = SEC_COL[SEC_LEAD];
    for (i = 0; i < 2; i++) {
        int32_t x0 = 6 + i * 120, x, py = 0, on = i == 0 || P(QP_L_VCO2);
        uint32_t pw = wave == 2 ? 6554u : wave == 3 ? 16384u + (uint32_t)(q_mon.lead_env * P(QP_L_PWM) >> 7) * 2u / 3u :
                      wave == 4 ? 39322u + (uint32_t)(q_mon.lead_env * P(QP_L_PWM) >> 7) / 3u : 32768u;
        char b[16];
        for (x = 0; x < 108; x++) {
            uint32_t ph = (uint32_t)(x * 65536 * 2 / 108) * (i && P(QP_L_INT) >= 12 ? 2u : 1u);
            int32_t y = 36 - (wave ? vz_wave(2, ph, pw) : vz_wave(0, ph, 0)) * 26 / 32768;
            if (x) thick_line(x0 + x - 1, py, x0 + x, y, on ? c : C_DIMT);
            py = y;
        }
        if (i) { cat(num(cat(b, "VCO2 "), P(QP_L_INT), 1), " st"); }
        else cat(b, "VCO1");
        cv_text_on(x0, 70, &FONT_S, b, on ? C_LBL : C_DIMT, C_BG);
    }
}
static int32_t vz_lfo_at(int32_t x, void *a)
{
    int32_t cyc = *(int32_t *)a;
    return q_sin((uint32_t)(x * cyc) * (0xFFFFFFFFu / 220u)) * 3 / 4;
}
static void vz_mod(void)
{
    uint32_t s = ui.sec;
    int32_t cyc = 1 + P(QP_LFO) * 7 / 127, dot;
    uint16_t c = SEC_COL[s];
    vz_grid();
    if (s == SEC_LEAD) {                        /* the vibrato coming in after its delay */
        int32_t d = seg_w(P(QP_L_VDEL)) * 2, x, py = 0, dep = P(QP_L_VIB);
        cv_rect(0, 4, d, VZ_H - 8, C_PANEL);
        for (x = 0; x < 220; x++) {
            int32_t ramp = x < d ? x * 32767 / (d ? d : 1) : 32767;
            int32_t y = 40 - (qm15(q_sin((uint32_t)(x * cyc) * (0xFFFFFFFFu / 220u)), ramp) * (4 + dep * 30 / 127) >> 15);
            if (x) thick_line(10 + x - 1, py, 10 + x, y, c);
            py = y;
        }
        if (P(QP_L_TRILL)) { char b[16]; cat(num(cat(b, "TRILL "), P(QP_L_TRILL), 1), " st"); cv_text_on(140, 70, &FONT_S, b, c, C_BG); }
        cv_text_on(6, 70, &FONT_S, "DELAY", C_LBL, C_BG);
    } else {
        vz_trace(10, 220, 6, 60, c, vz_lfo_at, &cyc);
        cv_text_on(6, 70, &FONT_S, s == SEC_POLY ? "TREMOLO / VCF" : "LFO", C_LBL, C_BG);
    }
    dot = 10 + (int32_t)q_mon.lfo * 220 / 256 / cyc;           /* the LFO now */
    cv_rect(dot, VZ_H - 6, 4, 4, C_TXT);
}
/* the FM-1's 27 keys (F3 .. G5, shifted by the octave): the bass zone, the split, the keys down */
static uint32_t is_black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }
static void vz_keys(void)
{
    int32_t base = 53 + ui.oct * 12, k, wx = 0, split = q_split() ? q_split() : -1;
    int32_t xs[27];
    for (k = 0; k < 27; k++) {                 /* the whites first */
        int32_t n = base + k;
        if (is_black((uint32_t)n)) { xs[k] = wx * 15 - 5; continue; }
        xs[k] = wx * 15;
        {
            int down = (ui.keys >> k) & 1u, bass = n < split;
            uint16_t col = down ? (bass ? SEC_COL[SEC_BASS] : SEC_COL[SEC_LEAD]) : bass ? SEC_DARK[SEC_BASS] : RGB(196, 196, 200);
            cv_rect(xs[k] + 1, 8, 13, 56, col);
        }
        wx++;
    }
    for (k = 0; k < 27; k++) {
        int32_t n = base + k;
        if (!is_black((uint32_t)n)) continue;
        {
            int down = (ui.keys >> k) & 1u, bass = n < split;
            cv_rect(xs[k] + 2, 8, 9, 34, down ? (bass ? SEC_COL[SEC_BASS] : SEC_COL[SEC_LEAD]) : bass ? RGB(40, 10, 8) : RGB(20, 20, 24));
        }
    }
    if (split >= base && split <= base + 26) { /* the split line */
        int32_t sx = xs[split - base] + (is_black((uint32_t)split) ? 6 : 0);
        cv_rect(sx, 2, 2, 66, C_TXT);
    }
    {
        char b[24], *p = cat(b, "F");
        p = num(p, base / 12 - 1, 0);
        p = cat(p, "-G");
        num(p, (base + 26) / 12 - 1, 0);
        cv_text_on(4, 70, &FONT_S, b, C_LBL, C_BG);
        if (split >= 0) { char t[16]; fmt_value(QP_SPLIT, split, t); { char u[24]; cat(cat(u, "SPLIT "), t); text_r(236, 70, u, SEC_COL[SEC_BASS], C_BG); } }
        else text_r(236, 70, "NO SPLIT", C_LBL, C_BG);
    }
}
static void vz_arp(void)
{
    uint16_t c = SEC_COL[SEC_LEAD];
    int32_t mode = P(QP_ARP), i, n = q.nheld, x = 6;
    static const char *const M[5] = {"OFF: THE TOP KEY", "UP", "UP / DOWN", "DOWN", "RANDOM"};
    cv_text_on(6, 4, &FONT_S, M[mode % 5], mode ? c : C_LBL, C_BG);
    for (i = 0; i < n && i < 12; i++) {
        char b[8];
        int32_t nt = q.held[i];
        cat(num(cat(b, NOTE_NAME[nt % 12]), nt / 12 - 1, 0), "");
        cv_rect(x, 30, 34, 30, nt == q_mon.lead_note ? c : C_PANEL);
        text_c(x + 17, 37, b, nt == q_mon.lead_note ? C_BG : C_TXT, nt == q_mon.lead_note ? c : C_PANEL, 0);
        x += 38;
        if (x > 200) break;
    }
    if (!n) cv_text_on(6, 38, &FONT_S, "HOLD KEYS (PLAY: HOLD)", C_DIMT, C_BG);
    {
        char b[32], *p;
        if (q_mon.bpm10 && P(QP_SYNC)) {
            p = cat(cat(b, "CLOCK "), N_SYNC[P(QP_SYNC) % 7]);
            p = cat(p, " @ ");
            cat(num(p, (q_mon.bpm10 + 5) / 10, 0), " BPM");
        } else {
            cat(b, P(QP_SYNC) ? "FREE (NO CLOCK)" : "FREE");
        }
        cv_text_on(6, 70, &FONT_S, b, q_mon.bpm10 && P(QP_SYNC) ? c : C_LBL, C_BG);
    }
}
static void vz_ph_in(void)
{
    uint32_t s;
    uint16_t c = SEC_COL[SEC_PHAS];
    cv_rect(150, 24, 50, 40, SEC_DARK[SEC_PHAS]);
    frame(150, 24, 50, 40, c);
    text_c(175, 36, "PHASE", c, SEC_DARK[SEC_PHAS], 1);
    cv_text_on(214, 26, &FONT_S, "L", C_LBL, C_BG);
    cv_text_on(214, 48, &FONT_S, "R", C_LBL, C_BG);
    thick_line(200, 34, 212, 30, c);
    thick_line(200, 54, 212, 56, c);
    for (s = 0; s < 4u; s++) {
        int32_t y = 4 + (int32_t)s * 21, in = P(QP_PH_B + s);
        cv_rect(6, y, 56, 18, in ? SEC_COL[s] : C_PANEL);
        text_c(34, y + 1, SEC_SHORT[s], in ? C_BG : C_DIMT, in ? SEC_COL[s] : C_PANEL, 0);
        if (in) thick_line(62, y + 9, 150, 44, SEC_COL[s]);
    }
}
static void vz_ph_sweep(void)
{
    int32_t x, py = 0, dep = P(QP_PH_DEP);
    int32_t pos = q_mon.ph_pos, c32 = 32 * 256 + (((pos - 16384) * dep) >> 13) * 2;   /* the corner, as q_phaser */
    uint16_t c = SEC_COL[SEC_PHAS];
    vz_grid();
    {   /* the sweep's range */
        int32_t lo = 32 * 256 - dep * 256 / 4, hi = 32 * 256 + dep * 256 / 4;
        int32_t xl = q_clampi(lo * 240 / (64 * 256), 0, 239), xh = q_clampi(hi * 240 / (64 * 256), 0, 239);
        cv_rect(xl, VZ_H - 8, xh - xl, 4, SEC_DARK[SEC_PHAS]);
    }
    for (x = 0; x < 240; x++) {                 /* |cos(14 atan(f / fc))|: x and the corner on one log axis */
        int32_t k = 128 + (x * 256 / 240 - c32 / 64) * 62 / 80;    /* (256 = 64 steps = 6.2 octaves; the table: 32 an octave) */
        uint32_t ph = Q_PH14[q_clampi(k, 0, 256)] / 2u;            /* half the phase */
        int32_t m = q_sin(((uint32_t)ph << 16) + 0x40000000u);     /* cos */
        int32_t y;
        m = m < 0 ? -m : m;
        y = VZ_H - 12 - m * 64 / 32768;
        if (x) thick_line(x - 1, py, x, y, c);
        py = y;
    }
    {
        static const char *const M[4] = {"SWEEP", "S&H", "ENV", "LFO"};
        cv_text_on(4, 2, &FONT_S, M[P(QP_PH_MOD) & 3], C_LBL, C_BG);
        text_r(236, 2, "14 STAGES", C_DIMT, C_BG);
    }
}
static void vz_mixer(void)
{
    uint32_t s;
    for (s = 0; s < 4u; s++) {
        int32_t x = 12 + (int32_t)s * 58, lvl = P(QP_M_B + s), pk = q_mon.peak[s], on = section_on(s);
        int32_t h = 62, cap = 8 + h - lvl * h / 127;
        cv_rect(x + 20, 8, 4, h, C_PANEL);                         /* the slot */
        cv_rect(x + 30, 8 + h - (pk > 16000 ? h : pk * h / 16000), 5, pk > 16000 ? h : pk * h / 16000, on ? SEC_COL[s] : C_DIMT);
        cv_rect(x + 8, cap - 4, 28, 8, on ? SEC_COL[s] : C_DIMT);   /* the cap */
        cv_rect(x + 8, cap, 28, 1, C_BG);
        text_c(x + 22, 72, SEC_SHORT[s], on ? SEC_COL[s] : C_DIMT, C_BG, 0);
    }
}
static void vz_fx(void)
{
    char b[24];
    int32_t rv = P(QP_REV), dl = P(QP_DLY), i;
    uint16_t c = SEC_COL[SEC_MIX];
    cv_text_on(6, 4, &FONT_S, "REVERB", C_LBL, C_BG);
    for (i = 0; i < 24; i++) {                  /* a reverb's falling tail */
        int32_t h = rv * (24 - i) / 24 * 40 / 127;
        cv_rect(6 + i * 4, 64 - h, 3, h, c);
    }
    cv_text_on(124, 4, &FONT_S, "DELAY", C_LBL, C_BG);
    for (i = 0; i < 5; i++) {                   /* its echoes */
        int32_t h = dl * 40 / 127;
        h = h * (6 - i) / 6;
        cv_rect(124 + i * 22, 64 - h, 6, h, c);
    }
    fmt_value(QP_DTIME, P(QP_DTIME), b);
    text_r(234, 70, b, C_LBL, C_BG);
}
static void vz_tune(void)
{
    int32_t t = P(QP_B_TUNE), x = 120 + t * 2;
    uint16_t c = SEC_COL[SEC_BASS];
    char b[16];
    cv_rect(20, 40, 200, 2, C_RULE);
    for (int32_t k = -50; k <= 50; k += 10) cv_rect(120 + k * 2, 34, 1, 14, k ? C_RULE : C_LBL);
    cv_rect(x - 2, 20, 5, 40, c);
    cat(num(b, t, 1), " CENTS");
    text_c(120, 64, b, C_TXT, C_BG, 0);
    if (P(QP_B_OCT)) cv_text_on(6, 4, &FONT_S, "BASS -1 OCT", c, C_BG);
}
static void vz_ens(void)
{
    int32_t k, x, dep = P(QP_S_ENS);
    uint16_t c = SEC_COL[SEC_STR];
    vz_grid();
    for (k = 0; k < 3; k++) {                   /* the three taps' delay over time */
        int32_t py = 0;
        uint32_t o = (uint32_t)k * 0x55555555u;
        for (x = 0; x < 240; x += 2) {
            uint32_t t1 = q.eph1 + o + (uint32_t)x * 0x00800000u, t2 = q.eph2 + o + (uint32_t)x * 0x08000000u;
            int32_t y = 44 - ((q_sin(t1) * dep >> 7) * 9 / 16 + (q_sin(t2) * dep >> 7) / 14) * 30 / 32768;
            if (x) cv_line(x - 2, py, x, y, mix565(c, C_BG, k * 70));
            py = y;
        }
    }
    cv_text_on(4, 2, &FONT_S, "ENSEMBLE", C_LBL, C_BG);
}
static void vz_home(void)
{
    uint32_t s;
    for (s = 0; s < 4u; s++) {                  /* the four sections: what each plays, its meter */
        int32_t x = 4 + (int32_t)s * 59, on = section_on(s), pk = q_mon.peak[s];
        char b[16];
        cv_rect(x, 2, 55, 84, on ? SEC_DARK[s] : C_PANEL);
        text_c(x + 27, 4, SEC_SHORT[s], on ? SEC_COL[s] : C_DIMT, on ? SEC_DARK[s] : C_PANEL, 1);
        b[0] = 0;
        switch (s) {
        case SEC_BASS: cat(b, P(QP_B_EB16) | P(QP_B_EB8) ? "ELEC" : P(QP_B_SB16) | P(QP_B_SB8) ? "STRING" : "OFF"); break;
        case SEC_STR: cat(b, P(QP_S_8) && P(QP_S_4) ? "8'+4'" : P(QP_S_8) ? "8'" : P(QP_S_4) ? "4'" : "OFF"); break;
        case SEC_POLY: cat(b, P(QP_P_8) && P(QP_P_4) ? "8'+4'" : P(QP_P_8) ? "8'" : P(QP_P_4) ? "4'" : "OFF"); break;
        default: cat(b, N_LWAVE[P(QP_L_WAVE) % 5]); break;
        }
        text_c(x + 27, 24, b, on ? C_TXT : C_DIMT, on ? SEC_DARK[s] : C_PANEL, 0);
        {
            int32_t h = pk > 16000 ? 34 : pk * 34 / 16000;
            cv_rect(x + 8, 46, 39, 34, C_BG);
            cv_rect(x + 8, 80 - h, 39, h, on ? SEC_COL[s] : C_DIMT);
        }
    }
}
static void vz_setup(void)
{
    char b[32], *p;
    if (!ui.setup_page) {
        cv_text_on(6, 6, &FONT_S, "MIDI CHANNEL", C_LBL, C_BG);
        if (ui.midi_ch) num(b, ui.midi_ch, 0); else cat(b, "OMNI");
        text_r(234, 6, b, C_TXT, C_BG);
        cv_text_on(6, 28, &FONT_S, "BLUETOOTH MIDI", C_LBL, C_BG);
        text_r(234, 28, !FM1B_BLE ? "NOT BUILT" : ui.ble_off ? "OFF" : plat_ble_connected() ? "CONNECTED" : "WAITING", C_TXT, C_BG);
        p = cat(b, "CPU ");
        p = cat(num(p, (int32_t)plat_cpu_pct(), 0), "%  ");
        cat(p, FM1B_CPU2 && plat_cpu2_on() ? "2 CORES" : "1 CORE");
        cv_text_on(6, 50, &FONT_S, b, C_LBL, C_BG);
        text_r(234, 50, "v" QD_VER, C_DIMT, C_BG);
        cv_text_on(6, 68, &FONT_S, "EDIT: TEMPO, CLOCK, FACTORY", C_DIMT, C_BG);
    } else {
        cv_text_on(6, 6, &FONT_S, "MIDI CLOCK OUT", C_LBL, C_BG);
        text_r(234, 6, !ui.clk_out ? "OFF (KNOB 4, PAGE 1)" : q_mon.bpm10 && !ui.clk_running && 0 ? "" : "USB + BLUETOOTH", C_TXT, C_BG);
        cv_text_on(6, 28, &FONT_S, "TEMPO", C_LBL, C_BG);
        p = num(b, ui.tempo10 / 10, 0); *p++ = '.'; *p++ = (char)('0' + ui.tempo10 % 10); cat(p, " BPM");
        text_r(234, 28, b, C_TXT, C_BG);
        cv_text_on(6, 50, &FONT_S, "KNOB 2: START / STOP", C_LBL, C_BG);
        cv_text_on(6, 68, &FONT_S, "KNOB 4 TWICE: FACTORY", C_DIMT, C_BG);
    }
}

static uint32_t viz_live(void)                  /* the page moves by itself (redraw while it plays) */
{
    uint32_t vz = ui.home ? 0xFFu : cur_page()->vz;
    if (ui.setup) return 1;                     /* (the CPU reading moves) */
    switch (vz) {
    case VZ_VCF: case VZ_ENV_AD: case VZ_ENV_AR: case VZ_ENV_ADSR: case VZ_ENV_LEAD: case VZ_MOD: case VZ_PH_SWEEP:
    case VZ_MIXER: case VZ_ENS: case VZ_ARP: case 0xFFu: case VZ_VCO:
        return 1;
    default:
        return 0;
    }
}
static void draw_viz(void)
{
    uint32_t vz = cur_page()->vz;
    cv_begin(240, VZ_H, C_BG);
    if (ui.setup) vz_setup();
    else if (ui.home) vz_home();
    else switch (vz) {
    case VZ_VOICES: vz_voices(); break;
    case VZ_VCF: vz_vcf(); break;
    case VZ_ENV_AD: case VZ_ENV_AR: case VZ_ENV_ADSR: case VZ_ENV_LEAD: vz_env(vz); break;
    case VZ_VCO: vz_vco(); break;
    case VZ_MOD: vz_mod(); break;
    case VZ_KEYS: vz_keys(); break;
    case VZ_ARP: vz_arp(); break;
    case VZ_PH_IN: vz_ph_in(); break;
    case VZ_PH_SWEEP: vz_ph_sweep(); break;
    case VZ_MIXER: vz_mixer(); break;
    case VZ_FX: vz_fx(); break;
    case VZ_TUNE: vz_tune(); break;
    case VZ_ENS: vz_ens(); break;
    default: break;
    }
    cv_blit(0, 84);
}

/* the four knobs: label, value, and its position (a bar; ON / OFF a pill) */
static void draw_knobs(void)
{
    uint32_t n;
    uint16_t col = ui.home || ui.setup ? C_TXT : SEC_COL[ui.sec];
    cv_begin(240, 66, C_BG);
    cv_rect(0, 0, 240, 1, C_RULE);
    for (n = 0; n < 4u; n++) {
        int32_t x = (int32_t)n * 60, cx = x + 30;
        char v[16];
        if (n) cv_rect(x, 6, 1, 54, C_RULE);
        if (ui.setup) {
            static const char *const L[2][4] = {{"MIDI", "BLE", "TOUCH", "CLOCK"}, {"TEMPO", "RUN", "", "FACT"}};
            text_c(cx, 6, L[ui.setup_page & 1u][n], C_LBL, C_BG, 0);
            if (!ui.setup_page) {
                if (n == 0) { if (ui.midi_ch) num(v, ui.midi_ch, 0); else cat(v, "OMNI"); text_c(cx, 26, v, C_TXT, C_BG, 1); }
                if (n == 1) text_c(cx, 26, !FM1B_BLE ? "--" : ui.ble_off ? "OFF" : "ON", C_TXT, C_BG, 1);
                if (n == 2) { if (ui.tsens) num(v, ui.tsens, 0); else cat(v, "OFF"); text_c(cx, 26, v, C_TXT, C_BG, 1); }
                if (n == 3) text_c(cx, 26, ui.clk_out ? "OUT" : "OFF", C_TXT, C_BG, 1);
            } else {
                if (n == 0) { char *p = num(v, ui.tempo10 / 10, 0); *p++ = '.'; *p++ = (char)('0' + ui.tempo10 % 10); *p = 0; text_c(cx, 26, v, C_TXT, C_BG, 1); }
                if (n == 1) text_c(cx, 26, !ui.clk_out ? "--" : ui.clk_running ? "PLAY" : "STOP", C_TXT, C_BG, 1);
                if (n == 3) text_c(cx, 26, ui.factory_arm ? "AGAIN" : "-->", C_TXT, C_BG, 1);
            }
            continue;
        }
        {
            uint32_t id = ui.home ? (uint32_t)(QP_M_B + n) : cur_page()->p[n];
            uint16_t cc = ui.home ? SEC_COL[n] : col;
            const q_desc_t *d;
            int32_t val, t;
            if (id >= QP_N) continue;
            d = &QD[id];
            val = qp.v[id];
            text_c(cx, 6, d->label, ui.col_touched == n + 1u && ui.pop_ms > QMS ? C_TXT : C_LBL, C_BG, 0);
            fmt_value(id, val, v);
            text_c(cx, 26, v, C_TXT, C_BG, 1);
            if (d->fmt == F_ONOFF) {
                cv_rect(cx - 14, 48, 28, 10, val ? cc : C_PANEL);
                frame(cx - 14, 48, 28, 10, val ? cc : C_RULE);
            } else {
                t = (val - d->min) * 44 / (d->max - d->min ? d->max - d->min : 1);
                cv_rect(cx - 22, 51, 44, 4, C_PANEL);
                if (d->min < 0) {               /* signed: from the middle */
                    int32_t z = (0 - d->min) * 44 / (d->max - d->min);
                    if (t >= z) cv_rect(cx - 22 + z, 51, t - z + 1, 4, cc);
                    else cv_rect(cx - 22 + t, 51, z - t, 4, cc);
                    cv_rect(cx - 22 + z, 49, 1, 8, C_LBL);
                } else {
                    cv_rect(cx - 22, 51, t, 4, cc);
                }
                cv_rect(cx - 22 + t - 1, 48, 3, 10, C_TXT);
            }
        }
    }
    cv_blit(0, 174);
}

static void ui_draw(void)
{
    uint32_t h, live = viz_live();
    h = sig_of(&qp, sizeof qp, ui.prog * 7u + ui.edited * 3u + ui.hold * 11u + ui.sus * 13u + (uint32_t)(ui.oct + 4) * 17u +
               (uint32_t)(FM1B_BLE && !ui.ble_off) * 19u + (uint32_t)(FM1B_BLE && plat_ble_connected()) * 23u + (uint32_t)((q_mon.bpm10 + 5) / 10) * 29u);
    if (ui.force || h != ui.sig_head) { ui.sig_head = h; draw_head(); }
    h = sig_of(&qp, sizeof qp, ui.sec + ui.home * 31u + ui.setup * 37u);
    if (ui.force || h != ui.sig_rail || QMS - ui.rail_ms >= 90u) {   /* (its lamps: ~11 frames a second) */
        ui.sig_rail = h;
        ui.rail_ms = QMS;
        draw_rail();
    }
    h = sig_of(&qp, sizeof qp, ui.page + ui.home * 31u + ui.setup * 37u + ui.saving * 41u + ui.save_to * 43u +
                                   (ui.msg_until > QMS) * 47u + (ui.pop_ms > QMS) * 53u + (uint32_t)ui.pop_param * 59u);
    if (ui.force || h != ui.sig_title) { ui.sig_title = h; draw_title(); }
    h = sig_of(&qp, sizeof qp, ui.page + ui.home * 31u + ui.setup * 37u + ui.keys * 3u + ui.oct * 5u + ui.midi_ch * 7u +
                                   ui.factory_arm * 11u + ui.ble_off * 13u + ui.tsens * 29u + ui.setup_page * 37u + ui.clk_out * 41u + ui.clk_running * 43u + ui.tempo10 * 47u + (uint32_t)(FM1B_BLE && plat_ble_connected()) * 17u);
    if (ui.force || h != ui.sig_viz || (live && QMS - ui.viz_ms >= 66u)) {
        ui.sig_viz = h;
        ui.viz_ms = QMS;
        draw_viz();
    }
    h = sig_of(&qp, sizeof qp, ui.page + ui.home * 31u + ui.setup * 37u + ui.midi_ch * 7u + ui.factory_arm * 11u + ui.ble_off * 19u + ui.tsens * 31u + ui.setup_page * 41u + ui.clk_out * 43u + ui.clk_running * 47u + ui.tempo10 * 53u +
                                   (ui.pop_ms > QMS) * ui.col_touched * 13u);
    if (ui.force || h != ui.sig_knobs) { ui.sig_knobs = h; draw_knobs(); }
    ui.force = 0;
}

/* ------------------------------------------------------------------ the controls --- */
static void button(uint32_t b)
{
    ui.factory_arm = 0;
    if (ui.saving) {                            /* the SAVE dialog: SAVE writes, anything else cancels */
        ui.saving = 0;
        if (b == B_SAVE) {
            progs[ui.save_to] = qp;
            ui.prog = ui.save_to;
            ui.edited = 0;
            ui.store_req = 1;
            ui_say("SAVED");
        } else {
            ui_say("NOT SAVED");
        }
        ui.force = 1;
        return;
    }
    if (ui.setup && b == B_EDIT) { ui.setup_page ^= 1u; ui.force = 1; return; }
    switch (b) {
    case B_EDIT: open_kind(ui.sec, PK_EDIT); break;
    case B_ENV: open_kind(ui.sec, PK_ENV); break;
    case B_LFO: open_kind(ui.sec, PK_LFO); break;
    case B_SEL: open_kind(ui.sec <= SEC_LEAD ? ui.sec : SEC_LEAD, PK_SCL); break;
    case B_FX: open_kind(SEC_PHAS, PK_EDIT); break;
    case B_GLO: open_kind(SEC_MIX, PK_EDIT); break;
    case B_ARP: open_page(page_find(SEC_LEAD, PK_SCL, page_find(SEC_LEAD, PK_SCL, NPAGES))); break;
    case B_SEQ: set_section(ui.home ? 0u : (ui.sec + 1u) % 4u); break;
    case B_HOME: ui.home = 1; ui.setup = 0; ui.force = 1; break;
    case B_SAVE: ui.saving = 1; ui.save_to = ui.prog; ui.force = 1; break;
    case B_PLAY:
        ui.hold ^= 1u;
        q_post(QE_HOLD, ui.hold);
        ui_say(ui.hold ? "HOLD ON" : "HOLD OFF");
        break;
    case B_REC:
        ui.sus ^= 1u;
        q_post(QE_SUS, ui.sus);
        ui_say(ui.sus ? "SUSTAIN ON" : "SUSTAIN OFF");
        break;
    case B_OCTDN: case B_OCTUP: {
        uint32_t both = (ui.held >> B_OCTDN & 1u) && (ui.held >> B_OCTUP & 1u);
        char m[16];
        ui.oct = both ? 0 : (int8_t)q_clampi(ui.oct + (b == B_OCTUP ? 1 : -1), -3, 3);
        q_post(QE_PANIC, 0);                    /* (the keys down would end on other notes) */
        cat(num(cat(m, "OCTAVE "), ui.oct, 1), "");
        ui_say(m);
        break;
    }
    default: break;
    }
}

/* HOME held: SETUP */
static uint32_t home_down_ms;
static uint32_t ui_holds(uint32_t held, uint32_t pressed, uint32_t released)
{
    ui.held = held;
    if (pressed & (1u << B_HOME))
        home_down_ms = QMS ? QMS : 1u;
    if (home_down_ms && (held >> B_HOME & 1u) && QMS - home_down_ms > 700u) {
        home_down_ms = 0;
        ui.setup = 1;
        ui.home = 0;
        ui.force = 1;
        pressed &= ~(1u << B_HOME);
    }
    if (released & (1u << B_HOME)) {
        if (home_down_ms) pressed |= 1u << B_HOME;   /* a tap: HOME */
        home_down_ms = 0;
    }
    if (pressed & (1u << B_HOME) && (held >> B_HOME & 1u))
        pressed &= ~(1u << B_HOME);             /* (acted on at release) */
    return pressed;
}

static void ui_preset(int32_t d)                /* PRESETS: the programs, or SAVE's target */
{
    if (ui.saving) {
        ui.save_to = (uint8_t)((ui.save_to + NPROG + (d > 0 ? 1u : NPROG - 1u)) % NPROG);
        ui.force = 1;
        return;
    }
    prog_load((ui.prog + NPROG + (d > 0 ? 1u : NPROG - 1u)) % NPROG);
}
static void ui_algorithm(int32_t d)             /* ALGORITHM: the section */
{
    uint32_t s = ui.home ? (d > 0 ? 0u : SEC_N - 1u) : (ui.sec + SEC_N + (d > 0 ? 1u : SEC_N - 1u)) % SEC_N;
    set_section(s);
}
static void ui_select(int32_t d)                /* SELECT: the phaser's sweep speed */
{
    param_set(QP_PH_SPD, qp.v[QP_PH_SPD] + d * 2);
}

/* the keys: F3 .. G5 (key 0 = F3) at the octave */
static uint32_t key_note(uint32_t k) { return (uint32_t)q_clampi(53 + ui.oct * 12 + (int32_t)k, 0, 127); }
static void q_midi_key(uint32_t note, uint32_t vel, int on);   /* app.c: the key out over MIDI too */
static uint8_t key_sent[NKEYS];                 /* the note each key sent (an octave change between down and up) */
static void key_down_v(uint32_t k, uint32_t vel) { ui.keys |= 1u << k; key_sent[k] = (uint8_t)key_note(k); q_post(QE_ON, key_sent[k]); q_midi_key(key_sent[k], vel, 1); }
static void key_down(uint32_t k) { key_down_v(k, 100); }
static void key_up(uint32_t k) { ui.keys &= ~(1u << k); q_post(QE_OFF, key_sent[k]); q_midi_key(key_sent[k], 0, 0); }

/* the LEDs: the page's button lit, HOLD on PLAY, SUSTAIN on REC, the octave, the keys: the bass zone glows,
 * the keys sounding light */
static void ui_leds(uint32_t *btn, uint32_t *keys, uint32_t *glow)
{
    uint32_t b = 0, k = 0, g = 0, i;
    int32_t split = q_split() ? q_split() : -1;
    if (ui.hold) b |= 1u << B_PLAY;
    if (ui.sus) b |= 1u << B_REC;
    if (ui.saving) b |= ((QMS / 200u) & 1u) << B_SAVE;
    if (ui.home) b |= 1u << B_HOME;
    else if (!ui.setup) {
        const q_page_t *pg = cur_page();
        if (pg->sec == SEC_PHAS) b |= 1u << B_FX;
        else if (pg->sec == SEC_MIX) b |= 1u << B_GLO;
        else if (pg->vz == VZ_ARP) b |= 1u << B_ARP;
        else b |= 1u << (pg->kind == PK_EDIT ? B_EDIT : pg->kind == PK_ENV ? B_ENV : pg->kind == PK_LFO ? B_LFO : B_SEL);
    }
    if (ui.oct < 0) b |= 1u << B_OCTDN;
    if (ui.oct > 0) b |= 1u << B_OCTUP;
    if (P(QP_ARP) && q_mon.lead_note) b |= ((q.l.arpph >> 31) & 1u) << B_ARP;
    for (i = 0; i < 27u; i++) {
        int32_t n = (int32_t)key_note(i);
        if (n < split) g |= 1u << i;            /* the bass zone glows */
        if (n >= 48 && n < 80 && (q_mon.keys_lit >> (n - 48)) & 1u) k |= 1u << i;
        if (n == q_mon.lead_note && q_mon.lead_note) k |= 1u << i;
        if (n == q_mon.bass_note && q_mon.bass_note) k |= 1u << i;
    }
    *btn = b, *keys = k, *glow = g;
}

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < NPROG; i++)
        q_factory(&progs[i], i);
    qp = progs[0];
    ui.page = (uint8_t)page_find(SEC_STR, PK_EDIT, NPAGES);
    ui.sec = SEC_STR;
    ui.home = 1;
    ui.pop_param = -1;
    ui.tsens = 2;
    ui.tempo10 = 1200;
    ui.force = 1;
}

/* the start: QUADRO, its four colours */
static void ui_splash(uint32_t t)
{
    uint32_t s;
    cv_begin(240, 90, C_BG);
    text_c(120, 10, "Q U A D R O", C_TXT, C_BG, 1);
    for (s = 0; s < 4u; s++) {
        int32_t w = (int32_t)(t > s * 150u ? (t - s * 150u) / 4u : 0u);
        cv_rect(20, 40 + (int32_t)s * 10, w > 200 ? 200 : w, 6, SEC_COL[s]);
    }
    cv_blit(0, 60);
    cv_begin(240, 40, C_BG);
    text_c(120, 4, "BASS STRINGS POLY LEAD", C_LBL, C_BG, 0);
    text_c(120, 22, "v" QD_VER, C_DIMT, C_BG, 0);
    cv_blit(0, 160);
}
