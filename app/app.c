/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO on fm1-base: the glue between the platform and QUADRO's sound (q_synth.c), programs (q_presets.c) and
 * panel (q_ui.c). The keys and buttons are read in the main loop (posted to the sound as events); MIDI comes in in
 * the audio interrupt, where the sound lives, and the keys go out over USB and Bluetooth MIDI. */
#define QMS plat_ms()
#define QD_VER APP_VERSION
#define Q_PAR_RUN(fn) (FM1B_CPU2 && plat_cpu2_on() && plat_cpu2_run(fn, 0))   /* the voices on the second core */
#define Q_PAR_WAIT() plat_cpu2_wait()
#ifdef PLAT_HOST
#include <stdlib.h>
#endif
#include "q_params.c"
#include "q_synth.c"
#include "q_presets.c"
#include "q_ui.c"

int quadro_ble_off(void) { return ui.ble_off; }

/* ------------------------------------------------------------------ MIDI --- */
/* the clock out (SETUP > CLOCK OUT): 24 a quarter at TEMPO from the 1 kHz tick, out over USB and Bluetooth; QUADRO's
 * own sequencer counts the same ticks (the render takes them). A clock coming in wins: then none goes out */
static volatile uint32_t clk_made, clk_taken, clk_ext_ms;
static volatile uint8_t clk_start_req;          /* 1 START, 2 STOP: for the render (the sequencer) */
#define CLK_EXT_ALIVE() (clk_ext_ms && plat_ms() - clk_ext_ms < 500u)
void app_tick(void)
{
    static uint32_t acc;
    if (!ui.clk_out || CLK_EXT_ALIVE()) {
        acc = 0;
        return;
    }
    acc += (uint32_t)ui.tempo10 * 171799u >> 16;  /* ticks a ms, Q16: tempo x 24 / 60000 (x 10 in tempo10) */
    if (acc >= 65536u) {
        acc -= 65536u;
        plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB | TO_BLE);
        clk_made++;
    }
}
static void q_clock_run(int on)
{
    ui.clk_running = (uint8_t)(on != 0);
    plat_midi_send(0x0Fu | (on ? 0xFAu : 0xFCu) << 8, TO_USB | TO_BLE);
    clk_start_req = on ? 1u : 2u;
    ui_say(on ? "CLOCK START" : "CLOCK STOP");
}

static volatile int16_t midi_prog = -1;         /* a Program Change, for the main loop */
static volatile uint8_t midi_sus = 0xFF;        /* CC64, for the screen */
static uint32_t midi_chan(void) { return ui.midi_ch ? ui.midi_ch - 1u : 0u; }

static void q_midi_key(uint32_t note, uint32_t vel, int on)
{
    uint32_t st = (on ? 0x90u : 0x80u) | midi_chan();
    if (on) vel = 72u + (vel & 127u) * 55u / 127u;      /* the keys' velocity, flattened: 72..127 (the sensor is very sensitive) */
    plat_midi_send((st >> 4) | st << 8 | (note & 127u) << 16 | (on ? (vel ? vel : 1u) & 127u : 0u) << 24, TO_USB | TO_BLE);
}

/* the sections a channel's messages go to: one with a channel of its own takes only that, the others the channel of SETUP
 * page 1 (or any, with OMNI) */
static uint8_t midi_map[128];                    /* the note each MIDI key sounds as (the scale lock, TRANSPOSE) */
static uint32_t midi_mask(uint32_t ch)
{
    uint32_t s, m = 0;
    for (s = 0; s < 4u; s++)
        if (ui.sch[s] ? ch + 1u == ui.sch[s] : (!ui.midi_ch || ch + 1u == ui.midi_ch))
            m |= 1u << s;
    return m;
}
/* the controllers: the mod wheel, the pedal, then the sounds of the section on the screen (CC 71 RESONANCE, 72 RELEASE,
 * 73 ATTACK, 74 CUTOFF), the sends (91 REVERB, 93 CHORUS, 94 DELAY) and the four levels (20-23) */
static void midi_cc(uint32_t cc, uint32_t v)
{
    static const uint8_t CUT[4] = {QP_B_CUT, QP_S_TONE, QP_P_CUT, QP_L_CUT}, RES[4] = {QP_B_RES, QP_S_ENS, QP_P_RES, QP_L_RES},
                         ATK[4] = {QP_B_ENV, QP_S_ATK, QP_P_A, QP_L_A}, REL[4] = {QP_B_DEC, QP_S_REL, QP_P_R, QP_L_R};
    uint32_t s = ui.sec < 4u ? ui.sec : SEC_POLY, id = QP_N;
    if (cc == 74u) id = CUT[s];
    else if (cc == 71u) id = RES[s];
    else if (cc == 73u) id = ATK[s];
    else if (cc == 72u) id = REL[s];
    else if (cc == 91u) id = QP_REV;
    else if (cc == 93u) id = QP_X_CHR;
    else if (cc == 94u) id = QP_DLY;
    else if (cc >= 20u && cc <= 23u) id = QP_M_B + (cc - 20u);
    if (id < QP_N)
        qp.v[id] = (int8_t)q_clampi((int32_t)v, QD[id].min, QD[id].max);
}

/* the audio interrupt: what came in plays at once (q_key and the rest belong to this context) */
static void midi_in_isr(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, ty = st & 0xF0u, ch = st & 15u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, mask;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {   /* the clock: any channel, any port */
            if (st == 0xF8u) clk_ext_ms = plat_ms() ? plat_ms() : 1u;
            q_clock(st);
            continue;
        }
        if (st >= 0xF0u)
            continue;
        mask = midi_mask(ch);
        if (!mask)
            continue;
        switch (ty) {
        case 0x90u:
            if (d2) {
                uint32_t m = qp.v[QP_MIDI_LOCK] ? scale_note((int32_t)d1) : d1;    /* the scale lock and TRANSPOSE, if set to reach MIDI in */
                midi_map[d1] = (uint8_t)m;
                q.nvel[m] = (uint8_t)d2;
                q.cmask = (uint8_t)mask;
                q_key((uint8_t)m, 1);
            } else {
                q_key(midi_map[d1], 0);
            }
            break;
        case 0x80u: q_key(midi_map[d1], 0); break;
        case 0xE0u: q.bend = ((int32_t)(d1 | d2 << 7) - 8192) * 32 / 8192; break;   /* +- 2 semitones */
        case 0xD0u: q.touch_tgt = (int32_t)d1; break;                                  /* channel pressure: the TOUCH SENSOR */
        case 0xC0u: midi_prog = (int16_t)(d1 % NPROG); break;
        case 0xB0u:
            if (d1 == 1u) q.modw = (int32_t)d2;                                    /* the mod wheel: the lead's vibrato */
            else if (d1 == 68u) q.flat = d2 >= 64u;                                /* the FLAT footswitch: the lead a semitone down */
            else if (d1 == 64u) { q.sustain = d2 >= 64u; midi_sus = q.sustain; }
            else if (d1 == 74u || d1 == 71u || d1 == 72u || d1 == 73u || d1 == 91u || d1 == 93u || d1 == 94u || (d1 >= 20u && d1 <= 23u)) midi_cc(d1, d2);
            else if (d1 == 120u || d1 == 123u) q_all_off();
            break;
        default: break;
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, b;
    midi_in_isr();
    if (clk_start_req == 1u) q_clock(0xFAu);    /* our own START: the sequence from its first step */
    clk_start_req = 0;
    while (clk_taken != clk_made) {             /* our own clock's ticks: QUADRO follows them too */
        clk_taken++;
        if (!CLK_EXT_ALIVE()) q_clock(0xF8u);
    }
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    for (b = 0; b + Q_BLK <= n; b += Q_BLK)
        q_block(lr + 2u * b);
    for (i = 0; i < 2u * n; i++)
        lr[i] = q_out((lr[i] * (int32_t)master_q12) >> 12) << 8;
}

void app_panic(void) { q_all_off(); }

void app_overload(void)
{
    if (!q.vcap || q.vcap > 5u)                 /* 8 -> 6 voices: the render had no time left */
        q.vcap = 6;
}

/* ------------------------------------------------------------------ the store --- */
#define QS_MAGIC 0x35524451u                     /* "QDR5": only the programs that differ from the factory's (1.1) */
#define QS_MAGIC_V4 0x34524451u                  /* "QDR4" (1.0) and "QDR3" (0.4 - 0.9): every program, two objects: ... */
#define QS_MAGIC_V3 0x33524451u                  /* ... their settings are kept, their programs are not (the factory ones are new) */
#define QS_PARTS 4u
#define QS_PART ((NPROG + QS_PARTS - 1u) / QS_PARTS)
static uint8_t qs_buf[PLAT_STORE_MAX] __attribute__((aligned(4)));
static uint32_t qs_last[QS_PARTS];               /* what each part held when last written (an unchanged part is not written) */
static const uint8_t QS_OBJ[QS_PARTS] = {OBJ_PROGS, OBJ_PROGS2, OBJ_PROGS3, OBJ_PROGS4};
/* part k: the programs [k * QS_PART, +QS_PART) that are not the factory's (index, name, parameters), then (part 0) the
 * settings. A program you have not changed follows the factory's, so an update's better programs reach it. */
static uint32_t qs_pack(uint8_t *b, uint32_t k)
{
    uint8_t *p = b, *count;
    uint32_t i, m = QS_MAGIC;
    memcpy(p, &m, 4); p += 4;
    *p++ = (uint8_t)QP_N;
    count = p++;
    *count = 0;
    for (i = k * QS_PART; i < (k + 1u) * QS_PART && i < NPROG; i++) {
        q_patch_t f;
        q_factory(&f, i);
        if ((memcmp(f.v, progs[i].v, QP_N) || memcmp(f.name, progs[i].name, QD_NAME)) &&
            (uint32_t)(p - b) + 1u + QD_NAME + QP_N + 16u <= PLAT_STORE_MAX) {    /* (a part that is full keeps what fits: room is left for the settings) */
            *p++ = (uint8_t)i;
            memcpy(p, progs[i].name, QD_NAME); p += QD_NAME;
            memcpy(p, progs[i].v, QP_N); p += QP_N;
            (*count)++;
        }
    }
    if (k == 0) {
        *p++ = ui.prog;
        *p++ = ui.midi_ch;
        *p++ = ui.ble_off;
        *p++ = ui.tsens;
        *p++ = ui.clk_out;
        *p++ = (uint8_t)(ui.tempo10 & 255u);
        *p++ = (uint8_t)(ui.tempo10 >> 8);
        *p++ = ui.sch[0]; *p++ = ui.sch[1]; *p++ = ui.sch[2]; *p++ = ui.sch[3];
    }
    return (uint32_t)(p - b);
}
static void qs_settings(const uint8_t *p, uint32_t left)     /* the settings bytes, as many as there are */
{
    if (left < 3u) return;
    ui.prog = (uint8_t)(p[0] % NPROG);
    ui.midi_ch = (uint8_t)(p[1] <= 16u ? p[1] : 0u);
    ui.ble_off = p[2] ? 1u : 0u;
    if (left >= 4u)
        ui.tsens = (uint8_t)(p[3] <= 6u ? p[3] : 2u);
    if (left >= 7u) {
        ui.clk_out = p[4] ? 1u : 0u;
        ui.tempo10 = (uint16_t)q_clampi(p[5] | p[6] << 8, 400, 2400);
    }
    if (left >= 11u) {
        uint32_t s;
        for (s = 0; s < 4u; s++) ui.sch[s] = (uint8_t)(p[7 + s] <= 16u ? p[7 + s] : 0u);
    }
}
static int qs_unpack(const uint8_t *b, uint32_t len, int settings)
{
    uint32_t m, np, i, k;
    const uint8_t *p, *end = b + len;
    if (len < 7u) return -1;
    memcpy(&m, b, 4);
    np = b[4];
    if (m == QS_MAGIC_V4 || m == QS_MAGIC_V3) {   /* 0.4 - 1.0: header, ns whole programs, the settings */
        uint32_t ns = b[6];
        if (np < 8u || np > 200u || len < 7u + ns * (np + QD_NAME) + 3u) return -1;
        if (settings) qs_settings(b + 7 + ns * (np + QD_NAME), len - (7u + ns * (np + QD_NAME)));
        return 0;
    }
    if (m != QS_MAGIC || np < 8u || np > 200u) return -1;
    p = b + 6;
    for (i = 0; i < b[5]; i++) {
        uint32_t idx;
        if (p + 1 + QD_NAME + np > end) return -1;
        idx = p[0];
        if (idx < NPROG) {
            q_patch_init(&progs[idx], "");
            memcpy(progs[idx].name, p + 1, QD_NAME);
            progs[idx].name[QD_NAME - 1] = 0;
            for (k = 0; k < np && k < QP_N; k++)
                progs[idx].v[k] = (int8_t)q_clampi((int8_t)p[1 + QD_NAME + k], QD[k].min, QD[k].max);
        }
        p += 1 + QD_NAME + np;
    }
    if (settings) qs_settings(p, (uint32_t)(end - p));
    return 0;
}
static uint32_t qs_hash(const uint8_t *b, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}
static int qs_save_all(void)
{
    uint32_t k;
    for (k = 0; k < QS_PARTS; k++) {
        uint32_t n = qs_pack(qs_buf, k), h = qs_hash(qs_buf, n);
        if (h != qs_last[k]) {
            int rc = plat_store_save(QS_OBJ[k], qs_buf, n);
            if (rc) return rc;
            qs_last[k] = h;
        }
    }
    return 0;
}

void app_init(void)
{
    int n;
    if (FM1B_CPU2)
        plat_cpu2_set(1);                       /* (here only: at power-on, before the audio; one core if it fails) */
    q_init();
    ui_init();
    for (n = 0; n < (int)QS_PARTS; n++) {
        int len = plat_store_load(QS_OBJ[n], qs_buf, sizeof qs_buf);
        if (len > 0)
            qs_unpack(qs_buf, (uint32_t)len, n == 0);
    }
    for (n = 0; n < (int)QS_PARTS; n++)                 /* what is stored now: a part that stays so is not written again */
        qs_last[n] = qs_hash(qs_buf, qs_pack(qs_buf, (uint32_t)n));
    prog_load(ui.prog);
    ui.home = 1;
    ui.setup = ui.saving = ui.hold = ui.sus = 0;
    ui.setup_page = 0;
    ui.clk_running = 0;
    ui.oct = 0;
    ui.keys = 0;
}

void app_say(const char *a, const char *b)
{
    char t[24];
    uint32_t i = 0;
    while (a && *a && i < 22u) t[i++] = *a++;
    if (b && *b && i < 21u) t[i++] = ' ';
    while (b && *b && i < 22u) t[i++] = *b++;
    t[i] = 0;
    ui_say(t);
}

/* ------------------------------------------------------------------ the panel --- */
static uint32_t in_keys, in_btn;
static void input(void)
{
    uint32_t keys, btn = plat_buttons(), pressed, released, k, lead_p = 0;
    int32_t d, split = q_split();
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(in_keys & bit))
            key_down_v(k, touch_vel(k));
        else if (!(keys & bit) && (in_keys & bit))
            key_up(k);
        if ((keys & bit) && (int32_t)key_note(k) >= split && touch_pressure(k) > lead_p)
            lead_p = touch_pressure(k);         /* the TOUCH SENSOR: the lead's keys pressed harder */
    }
    {   /* SETUP's TOUCH: 0 off; 1 (least) .. 6 (most). A dead zone first (a key played normally does nothing), then
         * the rest of the travel spread to 0..127 */
        static const uint8_t DEAD[7] = {127, 104, 96, 88, 80, 72, 62};
        uint32_t dz = DEAD[ui.tsens % 7u];
        lead_p = lead_p > dz ? (lead_p - dz) * 127u / (127u - dz) : 0u;
    }
    {
        static uint32_t sent;
        if (lead_p != sent) { sent = lead_p; q_post(QE_TOUCH, lead_p); }
    }
    in_keys = keys;
    pressed = btn & ~in_btn;
    released = in_btn & ~btn;
    in_btn = btn;
    pressed = ui_holds(btn, pressed, released);
    for (k = 0; pressed; k++, pressed >>= 1)
        if (pressed & 1u)
            button(k);
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0)
            knob(k, d);
    if ((d = plat_enc(EN_SELECT)) != 0) ui_select(d);
    if ((d = plat_enc(EN_ALGO)) != 0) ui_algorithm(d);
    if ((d = plat_enc(EN_PRESET)) != 0) ui_preset(d);
    {
        uint32_t lb, lk, lg;
        ui_leds(&lb, &lk, &lg);
        if (lb & (1u << B_PLAY))                /* HOLD: PLAY green */
            lb = (lb & ~(1u << B_PLAY)) | LED_PLAY_GREEN;
        plat_lights(lb, lk, 0, lg, 1);
    }
}

void app_input_only(void) { input(); }

void app_ui_init(void)
{
    touch_on(1);                                /* velocity and pressure: the keys' readings */
    touch_sens((int)ui.tsens - 6);              /* (the platform's own pressure sensitivity: -5 least .. 0) */
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.force = 1;
}

void app_frame(void)
{
    input();
    if (midi_prog >= 0) { prog_load((uint32_t)midi_prog); midi_prog = -1; }
    if (midi_sus != 0xFFu) { ui.sus = midi_sus; midi_sus = 0xFF; ui.force = 1; }
    if (ui.factory_req) {
        uint32_t i;
        ui.factory_req = 0;
        for (i = 0; i < NPROG; i++)
            q_factory(&progs[i], i);
        prog_load(0);
        ui.store_req = 1;
        ui_say("FACTORY PROGRAMS");
    }
    if (ui.store_req && !in_keys && !q.nheld) { /* (a flash write stops the sound a moment: not while playing) */
        ui.store_req = 0;
        if (qs_save_all())
            ui_say("SAVE ERROR");
    }
    ui_draw();
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "prog")) *got = ui.prog + 1;
    else if (!strcmp(what, "nprog")) *got = NPROG;
    else if (!strncmp(what, "sch", 3)) *got = ui.sch[atoi(what + 3) & 3];
    else if (!strcmp(what, "cmp")) *got = cmp_on;
    else if (!strcmp(what, "tempo10")) *got = ui.tempo10;
    else if (!strcmp(what, "section")) *got = ui.sec;
    else if (!strcmp(what, "page")) *got = (int)ui.page;
    else if (!strcmp(what, "pwave")) *got = qp.v[QP_P_WAVE];
    else if (!strcmp(what, "bwave")) *got = qp.v[QP_B_WAVE];
    else if (!strcmp(what, "lwave")) *got = qp.v[QP_L_WAVE];
    else if (!strcmp(what, "pdet")) *got = qp.v[QP_P_DET];
    else if (!strcmp(what, "pfmode")) *got = qp.v[QP_P_FMODE];
    else if (!strcmp(what, "edited")) *got = ui.edited;
    else if (!strncmp(what, "knote", 5)) *got = (int)key_note((uint32_t)atoi(what + 5));      /* the note pad N plays */
    else if (!strcmp(what, "pm")) *got = qp.v[QP_M_P];
    else if (!strcmp(what, "sm")) *got = qp.v[QP_M_S];
    else if (!strcmp(what, "s8")) *got = qp.v[QP_S_8];
    else return 0;
    return 1;
}
#endif
