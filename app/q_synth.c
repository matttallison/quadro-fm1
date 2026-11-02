/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRA: the sound. Four instruments on one keyboard, as the 1978 ARP Quadra built them (ARP and Quadra are
 * trademarks of their owners; no affiliation), after its owner's and service manuals:
 *
 *   BASS     a monophonic synth on the keys below the split (low-note priority): one VCO divided down into an 8'
 *            pulse whose width follows the AD envelope (the comparator of the service manual's 3.5.4) and a 16'
 *            square (ELEC BASS 16' / 8'), a 24 dB VCF that the envelope opens, decay while held, a short release.
 *            STRING BASS 16' / 8': the same VCO as squares into the strings' chorus and AR, at its own volume.
 *            Any bass voice on makes the split; the poly and the lead then play above it.
 *   STRINGS  divide-down: every key 8' (viola) and / or 4' (violin), sawtooth (square with HOLLOW), one AR a key,
 *            into one bus: a tone filter, then the ensemble (three delay taps swept 120 degrees apart, a slow and a
 *            fast sweep), in stereo. All keys.
 *   POLY     the same tone generator through a 24 dB low-pass a key with an ADSR on VCF and VCA (ADSR DEPTH), key
 *            tracking, the LFO on the VCF and as tremolo.
 *   LEAD     two VCOs (saw, square, 10 % pulse, PWM from 10 % or 50 %), VCO 2 at an interval and a detune; one voice
 *            (both on the top key) or 2 VOICE (VCO 1 the lowest, VCO 2 the highest key); portamento per VCO;
 *            a 24 dB VCF tracking the key 1:1; the ADS/ASR envelope (the sforzando: a quick decay to a level DECAY
 *            sets, a second attack back to full; SUS LOW: decay to 20 %); delayed vibrato; trill; the sequencer
 *            (arpeggio up, up / down over the lead's keys); single or multiple trigger.
 *   PHASER   the voltage-controlled phase shifter: a 14-stage all-pass ladder on the sections routed to it, swept
 *            by its own triangle, sample-and-hold, the strings / poly envelope or the LFO, resonance feedback,
 *            and stereo: left the dry plus the shifted, right the dry minus it (the original's two outputs).
 *   MIXER    the four levels; a reverb and a delay send (QUADRO's own: q_fx.c).
 *
 * The audio ISR calls q_block for each 32 samples; the main loop posts the keys as events (q_post). Integer DSP,
 * no 64-bit division. Everything shown on the screen that moves is published in q_mon. */
#include "q_tables.h"

#define Q_FS 44100
#define Q_BLK 32
#define Q_NV 8                                  /* keys sounding at once (strings and poly) */
#define Q_NHELD 16                              /* keys held at once */

static q_patch_t qp;                            /* the sound now (the UI writes it; the ISR reads it) */
#define P(id) ((int32_t)qp.v[id])

static inline int32_t qm15(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline int32_t qclamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t qmul16(int32_t a, uint32_t k)    /* a * k / 65536 without 64 bits: a up to 2^31, k Q16 */
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}

#ifndef PLAT_POOL_ATTR
#ifdef PLAT_POOL
#define PLAT_POOL_ATTR PLAT_POOL
#else
#define PLAT_POOL_ATTR
#endif
#endif
static inline int32_t q_clamp_fx(int32_t v) { return v < -32767 ? -32767 : v > 32767 ? 32767 : v; }

/* ------------------------------------------------------------------ building blocks --- */
/* phase increment of a pitch in 1/16 semitone (note * 16) */
static uint32_t q_inc(int32_t p16)
{
    uint32_t b, f;
    p16 = qclamp(p16, 0, 127 * 16 + 15);
    b = Q_NOTE_INC[p16 >> 4];
    f = Q_FRAC16[p16 & 15];
    return b + (b >> 16) * f + (((b & 0xFFFFu) * f) >> 16);
}
/* + cents (0..100) */
static inline uint32_t q_cents(uint32_t inc, int32_t ct) { return inc + (uint32_t)((int32_t)(inc >> 12) * (ct * 2367 / 100)); }

static inline int32_t q_sin(uint32_t ph)        /* Q15 */
{
    uint32_t i = ph >> 24;
    int32_t a = Q_SINE[i], b = Q_SINE[i + 1];
    return a + (((b - a) * (int32_t)((ph >> 8) & 0xFFFF)) >> 16);
}

/* polyBLEP residual around a wrap (Q15) */
static inline int32_t q_blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline int32_t q_saw(uint32_t ph, uint32_t inc) { return ((int32_t)(ph >> 16) - 32768) - q_blep(ph, inc); }
static inline int32_t q_pulse(uint32_t ph, uint32_t inc, uint32_t pw) { return (q_saw(ph, inc) - q_saw(ph + pw, inc)) >> 1; }

static inline int32_t q_tanh(int32_t x)         /* x Q15 (up to 2.0), tanh(x) Q15 */
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    y = i >= 256 ? Q_TANH[256] : Q_TANH[i] + (((Q_TANH[i + 1] - Q_TANH[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* the trapezoidal state-variable low-pass (A. Simper): coefficients a block, Q13; state within +-150000 */
typedef struct { int32_t a1, a2, a3; } q_svf_t;
static void q_svf_coef(q_svf_t *c, int32_t cut, int32_t res)    /* cut 0..127 << 8, res 0..127 */
{
    int32_t i, g, den, k = 8192 - res * 7700 / 127;
    cut = qclamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = Q_SVF_G[i];
    if (i < 127)
        g += ((Q_SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
static inline int32_t q_svf(const q_svf_t *c, int32_t in, int32_t *z)
{
    int32_t v3 = in - z[1];
    int32_t v1 = (c->a1 * z[0] + c->a2 * v3) >> 13;
    int32_t v2 = z[1] + ((c->a2 * z[0] + c->a3 * v3) >> 13);
    z[0] = qclamp(2 * v1 - z[0], -150000, 150000);
    z[1] = qclamp(2 * v2 - z[1], -150000, 150000);
    return v2;
}
/* 24 dB: two of them, the resonance on the first, a soft limit after (only the peaks) */
static inline int32_t q_lp24(const q_svf_t *c1, const q_svf_t *c2, int32_t x, int32_t *z)
{
    int32_t y = q_svf(c2, q_svf(c1, x, z), z + 2), a = y < 0 ? -y : y;
    if (a > 20000) {
        a = 20000 + (q_tanh((a - 20000) >> 1) >> 1);
        y = y < 0 ? -a : a;
    }
    return y;
}
static inline uint32_t q_rand(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* a level knob as a Q15 gain (an audio taper: half way is -12 dB) */
static inline int32_t q_gain(int32_t v) { v = qclamp(v, 0, 127); return (v * v * 2032) >> 10; }

/* ------------------------------------------------------------------ state --- */
typedef struct {
    uint8_t note, gate, on, held;               /* held: the key is down (gate may stay up with HOLD) */
    uint8_t root;                               /* the key that started it (CHORD: a key's chord notes share it) */
    uint32_t age;
    uint32_t ph;                                /* the 4' phase; the 8' is half of it (divide-down: locked) */
    int32_t se, pe;                             /* strings AR, poly ADSR, Q24 */
    uint8_t ss, ps;                             /* their stages (1 attack, 2 held / decay, 3 release, 0 off) */
    int32_t z[4];                               /* the poly VCF */
    int32_t sg, pg;                             /* last block's gains (ramped) */
} q_voice_t;

static struct {
    q_voice_t v[Q_NV];
    uint8_t held[Q_NHELD], nheld;               /* the keys down (and latched by HOLD), in the order pressed */
    uint8_t phys[Q_NHELD], nphys;               /* the keys physically down */
    uint32_t age;
    uint8_t hold, sustain;                      /* PLAY: HOLD (the gates stay up), REC: SUSTAIN (the longest release) */
    int32_t bend;                               /* pitch bend, 1/16 semitone (+-2 semitones: +-32) */
    int32_t modw;                               /* the mod wheel 0..127: more vibrato on the lead, at once */
    int32_t touch, touch_tgt;                   /* the TOUCH SENSOR: the lead's keys' pressure 0..127 (smoothed) */
    uint8_t vcap;                               /* voices allowed (8; fewer after an overload) */
    /* MIDI clock (24 a quarter): the ticks counted in (the render's MIDI), the ones the sequencer has used, the
     * samples since the last one and between the last two (the tempo) */
    uint32_t clk_count, clk_used, clk_trill, samples, clk_last, clk_span;
    uint8_t clk_run;                            /* a tick in the last 0.5 s */
    /* PARA: the original's paraphonic strings and poly, one AR, one ADSR and one VCF for all the keys */
    int32_t pse, ppe, pz[4], psg, ppg;           /* the shared envelopes (Q24), the VCF, last block's gains */
    uint8_t pss, pps, para_gate;
    uint32_t para_age;
    /* BASS */
    struct { uint8_t gate, note, stage, sbst; int32_t pitch, env, sbe, g, sg; uint32_t ph; int32_t z[4]; } b;
    /* LEAD */
    struct {
        uint8_t gate, stage, note1, note2;
        int32_t t1, t2, p1, p2, env, g, vib;    /* targets / pitches (1/16 st), Q24 env, gain, vibrato ramp Q15 */
        uint32_t ph1, ph2, trph, arpph;
        uint8_t arpi, arpup, trill_hi;
        int32_t z[4];
    } l;
    uint32_t lfo_ph, lfo_tri;                   /* the LFO (sine), the phase shifter's sweep */
    int32_t sh, sh_tgt;                         /* sample and hold */
    uint32_t sh_ph, seed;
    /* the strings' bus and the ensemble */
    int32_t sbus[Q_BLK], pbus[Q_BLK], bbus[Q_BLK], lbus[Q_BLK], sbbus[Q_BLK];   /* (sbbus: the string bass, CPU0's) */
    int16_t dl[1024];
    uint32_t dw, eph1, eph2;
    int32_t tone_lp, tone_hp;
    /* the phase shifter */
    int32_t ap[14], apx, pfb, pa;
    int32_t ph_env;                             /* its ENV mode: the strings / poly envelope (Q15) */
    uint16_t ph_quiet;                          /* blocks with no input: then its ladder is cleared (no limit cycle) */
} q;

/* what the screen shows (the ISR writes, the UI reads) */
static volatile struct {
    uint16_t peak[4];                           /* the sections' level now (bass strings poly lead), 0..32767 */
    uint16_t ph_pos;                            /* where the phaser's notches are, 0..32767 */
    uint16_t lead_env, poly_env, str_env, bass_env;   /* the newest envelope of each, Q15 */
    uint8_t lead_stage, bass_note, lead_note, voices;
    uint8_t lfo;                                /* 0..255 */
    uint8_t arp_step;                           /* the sequencer's step now */
    uint16_t bpm10;                             /* the MIDI clock's tempo x 10, 0 = none */
    uint32_t keys_lit;                          /* bit k: MIDI note 48 + k sounds */
} q_mon;

/* ------------------------------------------------------------------ keys --- */
static int q_bass_on(void) { return P(QP_B_EB16) | P(QP_B_EB8) | P(QP_B_SB16) | P(QP_B_SB8); }
/* the split the strings / poly / lead see: none without a bass voice, none with CH BASS (the bass then plays the lowest
 * key held anywhere, an octave or two down: a chord's root) */
static int32_t q_split(void) { return q_bass_on() && !P(QP_CHBASS) ? P(QP_SPLIT) : 0; }

static void held_add(uint8_t *list, uint8_t *n, uint8_t note)
{
    uint32_t i;
    for (i = 0; i < *n; i++)
        if (list[i] == note)
            return;
    if (*n < Q_NHELD)
        list[(*n)++] = note;
}
static void held_del(uint8_t *list, uint8_t *n, uint8_t note)
{
    uint32_t i, j;
    for (i = 0; i < *n; i++)
        if (list[i] == note) {
            for (j = i + 1u; j < *n; j++)
                list[j - 1u] = list[j];
            (*n)--;
            return;
        }
}

static void q_voice_on(uint8_t note, uint8_t root)
{
    q_voice_t *v = 0;
    uint32_t i, best = 0xFFFFFFFFu;
    uint32_t nv = q.vcap ? q.vcap : Q_NV;
    for (i = 0; i < nv; i++)                    /* the key's own voice again (retrigger) */
        if (q.v[i].on && q.v[i].note == note) { v = &q.v[i]; break; }
    if (!v)
        for (i = 0; i < nv; i++)                /* a free one */
            if (!q.v[i].on) { v = &q.v[i]; break; }
    if (!v)
        for (i = 0; i < nv; i++)                /* the oldest released, else the oldest */
            if (!q.v[i].gate && q.v[i].age < best) { best = q.v[i].age; v = &q.v[i]; }
    if (!v)
        for (i = 0, best = 0xFFFFFFFFu; i < nv; i++)
            if (q.v[i].age < best) { best = q.v[i].age; v = &q.v[i]; }
    if (!v->on) {
        v->ph = 0;
        v->se = v->pe = 0;
        v->z[0] = v->z[1] = v->z[2] = v->z[3] = 0;
        v->sg = v->pg = 0;
    }
    v->note = note;
    v->root = root;
    v->gate = v->held = v->on = 1;
    v->ss = v->ps = 1;
    v->age = ++q.age;
}

static void q_key(uint8_t note, int on)
{
    if (on) {
        if (q.hold && !q.nphys) {               /* HOLD: a new hand after letting go replaces what was latched */
            uint32_t i;
            for (i = 0; i < Q_NV; i++)
                q.v[i].gate = 0;
            q.nheld = 0;
        }
        held_add(q.phys, &q.nphys, note);
        held_add(q.held, &q.nheld, note);
        {   /* CHORD: the key plays its shape on the strings and the poly (the bass and the lead take the key) */
            static const int8_t SHAPE[10][4] = {{0, -1, -1, -1}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 3, 7, 10},
                                                {0, 4, 7, 11}, {0, 5, 7, -1}, {0, 7, 12, -1}, {0, 7, 14, 19}, {0, 3, 7, 14}};
            uint32_t c = (uint32_t)qclamp(P(QP_CHORD), 0, 9), k;
            for (k = 0; k < 4u && SHAPE[c][k] >= 0; k++)
                if (note + SHAPE[c][k] < 128)
                    q_voice_on((uint8_t)(note + SHAPE[c][k]), note);
        }
    } else {
        uint32_t i;
        held_del(q.phys, &q.nphys, note);
        if (q.hold)
            return;                             /* latched */
        held_del(q.held, &q.nheld, note);
        for (i = 0; i < Q_NV; i++)
            if (q.v[i].on && q.v[i].root == note)
                q.v[i].gate = q.v[i].held = 0;
    }
}

static void q_set_hold(int on)
{
    uint32_t i, k, down;
    q.hold = (uint8_t)on;
    if (on)
        return;
    for (i = 0; i < Q_NV; i++) {                /* let go: what is not physically down ends */
        for (k = 0, down = 0; k < q.nphys; k++)
            down |= q.phys[k] == q.v[i].root;
        if (!down)
            q.v[i].gate = q.v[i].held = 0;
    }
    q.nheld = 0;
    for (k = 0; k < q.nphys; k++)
        q.held[q.nheld++] = q.phys[k];
}

static void q_all_off(void)
{
    uint32_t i;
    q.nheld = q.nphys = 0;
    q.hold = 0;
    for (i = 0; i < Q_NV; i++)
        q.v[i].gate = q.v[i].held = 0;
}

/* MIDI clock in (the audio interrupt: app.c's MIDI): F8 a tick, FA start, FB continue, FC stop */
static void q_clock(uint32_t st)
{
    if (st == 0xF8u) {
        uint32_t span = q.samples - q.clk_last;
        if (q.clk_last && span < Q_FS / 2u)     /* the tempo: the ticks' spacing, smoothed */
            q.clk_span = q.clk_span ? q.clk_span + ((int32_t)(span << 8) - (int32_t)q.clk_span) / 8 : span << 8;
        q.clk_last = q.samples ? q.samples : 1u;
        q.clk_count++;
    } else if (st == 0xFAu) {                   /* start: the sequence from its first step, on the next tick */
        q.clk_count = q.clk_used = q.clk_trill = 0;
        q.l.arpi = 0;
        q.l.arpup = 1;
    }
}
/* the steps the clock owes at a division (ticks a step), 0 when no clock runs */
static int q_clock_step(uint32_t *used, uint32_t div)
{
    if (!q.clk_run || !div)
        return 0;
    if (q.clk_count - *used >= div) {
        *used += div;
        if (q.clk_count - *used >= div) *used = q.clk_count;   /* (fallen behind: catch up, one step) */
        return 1;
    }
    return 0;
}
static const uint8_t Q_SYNC_DIV[7] = {0, 24, 12, 8, 6, 4, 3};   /* OFF 1/4 1/8 1/8T 1/16 1/16T 1/32 */

/* events from the main loop: a ring the ISR drains each block */
enum { QE_ON = 1, QE_OFF, QE_HOLD, QE_SUS, QE_PANIC, QE_TOUCH };
#define QEV_N 64u
static volatile uint32_t qev_ring[QEV_N];
static volatile uint32_t qev_w, qev_r;
static void q_post(uint32_t type, uint32_t a)
{
    uint32_t w = qev_w;
    if (w - qev_r >= QEV_N)
        return;
    qev_ring[w % QEV_N] = type | a << 8;
    __asm__ volatile("" ::: "memory");
    qev_w = w + 1u;
}
static void q_events(void)
{
    while (qev_r != qev_w) {
        uint32_t e = qev_ring[qev_r % QEV_N], a = e >> 8;
        qev_r++;
        switch (e & 255u) {
        case QE_ON: q_key((uint8_t)(a & 127u), 1); break;
        case QE_OFF: q_key((uint8_t)(a & 127u), 0); break;
        case QE_HOLD: q_set_hold((int)a); break;
        case QE_SUS: q.sustain = (uint8_t)a; break;
        case QE_PANIC: q_all_off(); break;
        case QE_TOUCH: q.touch_tgt = (int32_t)(a & 127u); break;
        default: break;
        }
    }
}

/* the poly's DECAY knob: the long half of the table (0: ~50 ms, 64: ~0.5 s, 127: ~5 s to -40 dB) */
#define Q_PDEC(d) (40 + (d) * 87 / 127)
/* the bass's DECAY knob: ~0.1 s .. ~8 s (127: it holds) */
#define Q_BDEC(d) (56 + (d) * 70 / 127)

/* ------------------------------------------------------------------ BASS --- */
static __attribute__((noinline)) void q_bass(void)
{
    int32_t i, low = -1, split = P(QP_CHBASS) ? 128 : P(QP_SPLIT), e0 = q.b.env, e1, se0 = q.b.sbe, se1, g1, sg1;
    int32_t eb = P(QP_B_EB16) | P(QP_B_EB8), sb = P(QP_B_SB16) | P(QP_B_SB8);
    for (i = 0; i < (int32_t)q.nheld; i++)       /* the lowest key below the split */
        if (q.held[i] < split && (low < 0 || q.held[i] < low))
            low = q.held[i];
    if (!q_bass_on())
        low = -1;
    if (low >= 0) {
        if (!q.b.gate || low != q.b.note) {     /* a new bass key: the envelopes again */
            q.b.stage = 1;
            q.b.sbst = 1;
        }
        q.b.note = (uint8_t)low;
        q.b.gate = 1;
    } else {
        q.b.gate = 0;
    }
    /* the electric bass's AD: a preset attack (~2 ms), DECAY while held (to nothing: at the top it holds), a
     * preset release (~40 ms) */
    switch (q.b.stage) {
    case 1:
        e1 = e0 + (int32_t)Q_LIN[12];
        if (e1 >= 1 << 24) { e1 = 1 << 24; q.b.stage = 2; }
        if (!q.b.gate) q.b.stage = 3;
        break;
    case 2:
        e1 = P(QP_B_DEC) >= 127 ? e0 : e0 - qmul16(e0, Q_EXP[Q_BDEC(P(QP_B_DEC))]);
        if (!q.b.gate) q.b.stage = 3;
        break;
    case 3:
        e1 = e0 - qmul16(e0, Q_EXP[62]);           /* ~90 ms */
        if (e1 < 1 << 10) { e1 = 0; q.b.stage = 0; }
        break;
    default:
        e1 = 0;
        break;
    }
    q.b.env = e1;
    /* the string bass: the strings' AR */
    switch (q.b.sbst) {
    case 1:
        se1 = se0 + (int32_t)Q_LIN[qclamp(P(QP_S_ATK), 24, 127)];
        if (se1 >= 1 << 24) { se1 = 1 << 24; q.b.sbst = 2; }
        if (!q.b.gate) q.b.sbst = 3;
        break;
    case 2:
        se1 = se0;
        if (!q.b.gate) q.b.sbst = 3;
        break;
    case 3:
        se1 = se0 - qmul16(se0, Q_EXP[q.sustain ? 118 : qclamp(P(QP_S_REL), 30, 127)]);
        if (se1 < 1 << 10) { se1 = 0; q.b.sbst = 0; }
        break;
    default:
        se1 = 0;
        break;
    }
    q.b.sbe = se1;
    q_mon.bass_env = (uint16_t)(e1 >> 9);
    q_mon.bass_note = q.b.gate ? q.b.note : 0;
    g1 = eb ? qm15(e1 >> 9, 26000) : 0;
    sg1 = sb ? qm15(qm15(se1 >> 9, q_gain(P(QP_B_SBVOL))), 9000) : 0;
    if (!g1 && !q.b.g && !sg1 && !q.b.sg)
        return;
    {
        /* an octave above the Quadra's: the FM-1's keys start at F3, not two octaves below, and its speaker gives
         * nothing at 40-90 Hz; 16' sounds at the key's pitch, 8' an octave up (BASS OCTAVE -1: the Quadra's) */
        int32_t p16 = (int32_t)q.b.note * 16 + (1 + P(QP_B_OCT) - P(QP_CHBASS)) * 192 + q.bend;
        uint32_t inc8 = q_cents(q_inc(p16), P(QP_B_TUNE) > 0 ? P(QP_B_TUNE) : 0), inc16 = inc8 >> 1, ph = q.b.ph;
        int32_t eq = e1 >> 9;
        uint32_t pw = 0x80000000u - (uint32_t)eq * 0x8000u;            /* 50 % -> 25 % at the envelope's top */
        int32_t cut = (P(QP_B_CUT) << 8) + ((eq * P(QP_B_ENV)) >> 8);
        int32_t g0 = q.b.g, dg = (g1 - g0) / Q_BLK, s0 = q.b.sg, ds = (sg1 - s0) / Q_BLK;
        int32_t w16 = P(QP_B_EB16), w8 = P(QP_B_EB8), s16 = P(QP_B_SB16), s8 = P(QP_B_SB8);
        q_svf_t f1, f2;
        if (P(QP_B_TUNE) < 0)                   /* (flat: from the semitone below, sharp) */
            inc8 = q_cents(q_inc(p16 - 16), 100 + P(QP_B_TUNE)), inc16 = inc8 >> 1;
        q_svf_coef(&f1, cut, P(QP_B_RES));
        q_svf_coef(&f2, cut, 0);
        for (i = 0; i < Q_BLK; i++) {
            uint32_t ph8 = ph << 1;             /* 8' = the 16' phase doubled: one VCO, divided down */
            int32_t sq16 = q_pulse(ph, inc16, 0x80000000u), x = 0;
            if (g0 | g1) {
                if (w16) x += sq16;
                if (w8) x += q_pulse(ph8, inc8, pw);
                q.bbus[i] += qm15(q_lp24(&f1, &f2, x >> 1, q.b.z), g0);
            }
            if (s0 | sg1) {
                int32_t y = 0;
                if (s16) y += sq16;
                if (s8) y += q_pulse(ph8, inc8, 0x80000000u);
                q.sbbus[i] += qm15(y, s0);
            }
            ph += inc16;
            g0 += dg;
            s0 += ds;
        }
        q.b.ph = ph;
        q.b.g = g1;
        q.b.sg = sg1;
    }
}

/* ------------------------------------------------------------------ STRINGS and POLY --- */
static __attribute__((noinline)) void q_voices(void)
{
    int32_t split = q_split(), hol = P(QP_HOLLOW);
    int32_t s8 = P(QP_S_8), s4 = P(QP_S_4), p8 = P(QP_P_8), p4 = P(QP_P_4);
    int32_t sgain = (s8 | s4) ? q_gain(P(QP_M_S) ? 127 : 0) : 0;   /* (the mixer's level comes later) */
    int32_t pgain = p8 | p4;
    int32_t lfo = q_sin(q.lfo_ph);
    int32_t trem = 32767 - qm15((lfo + 32768) >> 1, P(QP_P_TREM) * 200);
    uint32_t n, i, nv = 0, newest = 0;
    int32_t ens_env = 0;
    for (n = 0; n < Q_NV; n++) {
        q_voice_t *v = &q.v[n];
        int32_t se0, se1, pe0, pe1, sg1, pg1;
        uint32_t ss, ps, poly;
        if (!v->on)
            continue;
        se0 = v->se, pe0 = v->pe, ss = v->ss, ps = v->ps;
        if (!v->gate) {
            if (ss == 1u || ss == 2u) ss = 3;
            if (ps == 1u || ps == 2u) ps = 3;
        }
        se1 = se0, pe1 = pe0;
        switch (ss) {                           /* STRINGS AR (never a click: the preset's fast AR at least) */
        case 1:
            se1 = se0 + (int32_t)Q_LIN[qclamp(P(QP_S_ATK), 24, 127)];
            if (se1 >= 1 << 24) { se1 = 1 << 24; ss = 2; }
            break;
        case 3:
            se1 = se0 - qmul16(se0, Q_EXP[q.sustain ? 118 : qclamp(P(QP_S_REL), 30, 127)]);
            if (se1 < 1 << 10) { se1 = 0; ss = 0; }
            break;
        default:
            break;
        }
        switch (ps) {                           /* POLY ADSR */
        case 1:
            pe1 = pe0 + (int32_t)Q_LIN[P(QP_P_A)];
            if (pe1 >= 1 << 24) { pe1 = 1 << 24; ps = 2; }
            break;
        case 2:
            pe1 = pe0 + qmul16((P(QP_P_S) << 17) - pe0, Q_EXP[Q_PDEC(P(QP_P_D))]);
            break;
        case 3:
            pe1 = pe0 - qmul16(pe0, Q_EXP[q.sustain ? 118 : P(QP_P_R)]);
            if (pe1 < 1 << 10) { pe1 = 0; ps = 0; }
            break;
        default:
            break;
        }
        v->se = se1, v->pe = pe1, v->ss = (uint8_t)ss, v->ps = (uint8_t)ps;
        if (!ss && !ps) {
            v->on = 0;
            continue;
        }
        nv++;
        if (v->age > newest) {
            newest = v->age;
            q_mon.str_env = (uint16_t)(se1 >> 9);
            q_mon.poly_env = (uint16_t)(pe1 >> 9);
        }
        if ((se1 >> 9) > ens_env) ens_env = se1 >> 9;
        if ((pe1 >> 9) > ens_env) ens_env = pe1 >> 9;
        poly = pgain && v->note >= split;
        sg1 = sgain ? qm15(se1 >> 9, 7000) : 0;
        pg1 = poly ? qm15(qm15(pe1 >> 9, 9000), trem) : 0;
        if (!sg1 && !v->sg && !pg1 && !v->pg)
            continue;
        {
            uint32_t inc8 = q_inc((int32_t)v->note * 16 + q.bend), inc4 = inc8 << 1, ph = v->ph;
            int32_t s0 = v->sg, ds = (sg1 - s0) / Q_BLK, g0 = v->pg, dg = (pg1 - g0) / Q_BLK;
            int32_t *sb = q.sbus, *pb = q.pbus;
            q_svf_t f1, f2;
            int32_t usep = pg1 | g0;
            if (usep) {                         /* VCF: cutoff, the ADSR, key tracking (half: 2 V/oct), the LFO */
                int32_t cut = (P(QP_P_CUT) << 8) + (((pe1 >> 9) * P(QP_P_ENV)) >> 7) +
                              ((((int32_t)v->note - 60) * P(QP_P_KBD) * 13) >> 2) + ((lfo * P(QP_P_LFO)) >> 10);
                q_svf_coef(&f1, cut, P(QP_P_RES));
                q_svf_coef(&f2, cut, 0);
            }
            for (i = 0; i < Q_BLK; i++) {
                uint32_t ph8 = ph >> 1;         /* (the 4' phase halved: the 8', locked) */
                int32_t a = 0, b = 0, x;
                if (hol) {
                    if (s8 | p8) a = q_pulse(ph8, inc8, 0x80000000u) * 2;
                    if (s4 | p4) b = q_pulse(ph, inc4, 0x80000000u) * 2;
                } else {
                    if (s8 | p8) a = q_saw(ph8 + 0x80000000u, inc8);
                    if (s4 | p4) b = q_saw(ph, inc4);
                }
                ph += inc4;
                if (s0 | sg1) {
                    x = (s8 ? a : 0) + (s4 ? b : 0);
                    sb[i] += qm15(x, s0);
                }
                if (usep) {
                    x = (p8 ? a : 0) + (p4 ? b : 0);
                    pb[i] += qm15(q_lp24(&f1, &f2, x >> 1, v->z), g0);
                }
                s0 += ds;
                g0 += dg;
            }
            v->ph = ph;
            v->sg = sg1;
            v->pg = pg1;
        }
    }
    q_mon.voices = (uint8_t)nv;
    q.ph_env += (ens_env - q.ph_env) >> 3;
}

/* PARA: every key still has its own oscillators (divide-down), but the strings share one AR and the poly one ADSR
 * and one 24 dB VCF, as on the original. A new key fires them again (KYBD TRIG MULTI) or only when all were up
 * (SINGLE); a key let go while others are held drops out at once (~5 ms); when the last one goes, the keys held
 * last ring on through the release. One filter for all: cheaper than the voices' own. */
static __attribute__((noinline)) void q_voices_para(void)
{
    static int32_t sraw[Q_BLK], praw[Q_BLK];
    int32_t split = q_split(), hol = P(QP_HOLLOW);
    int32_t s8 = P(QP_S_8), s4 = P(QP_S_4), p8 = P(QP_P_8), p4 = P(QP_P_4), top = -1;
    int32_t lfo = q_sin(q.lfo_ph), trem = 32767 - qm15((lfo + 32768) >> 1, P(QP_P_TREM) * 200);
    int32_t se0 = q.pse, pe0 = q.ppe, se1 = se0, pe1 = pe0, anyg = 0, newkey = 0;
    uint32_t n, i, nv = 0, ss = q.pss, ps = q.pps, maxage = q.para_age;
    for (n = 0; n < Q_NV; n++) {
        q_voice_t *v = &q.v[n];
        if (!v->on) continue;
        if (v->gate) { anyg = 1; if (v->note > top) top = v->note; }
        if (v->age > q.para_age) { newkey = 1; if (v->age > maxage) maxage = v->age; }
    }
    q.para_age = maxage;
    if (newkey && (P(QP_L_TRIG) || !q.para_gate)) { ss = 1; ps = 1; }   /* (KYBD TRIG: the original's switch for both) */
    q.para_gate = (uint8_t)anyg;
    if (!anyg) { if (ss == 1u || ss == 2u) ss = 3; if (ps == 1u || ps == 2u) ps = 3; }
    switch (ss) {
    case 1: se1 = se0 + (int32_t)Q_LIN[qclamp(P(QP_S_ATK), 24, 127)]; if (se1 >= 1 << 24) { se1 = 1 << 24; ss = 2; } break;
    case 3: se1 = se0 - qmul16(se0, Q_EXP[q.sustain ? 118 : qclamp(P(QP_S_REL), 30, 127)]); if (se1 < 1 << 10) { se1 = 0; ss = 0; } break;
    default: break;
    }
    switch (ps) {
    case 1: pe1 = pe0 + (int32_t)Q_LIN[P(QP_P_A)]; if (pe1 >= 1 << 24) { pe1 = 1 << 24; ps = 2; } break;
    case 2: pe1 = pe0 + qmul16((P(QP_P_S) << 17) - pe0, Q_EXP[Q_PDEC(P(QP_P_D))]); break;
    case 3: pe1 = pe0 - qmul16(pe0, Q_EXP[q.sustain ? 118 : P(QP_P_R)]); if (pe1 < 1 << 10) { pe1 = 0; ps = 0; } break;
    default: break;
    }
    q.pse = se1, q.ppe = pe1, q.pss = (uint8_t)ss, q.pps = (uint8_t)ps;
    q_mon.str_env = (uint16_t)(se1 >> 9);
    q_mon.poly_env = (uint16_t)(pe1 >> 9);
    q.ph_env += (((se1 > pe1 ? se1 : pe1) >> 9) - q.ph_env) >> 3;
    for (i = 0; i < Q_BLK; i++)
        sraw[i] = praw[i] = 0;
    for (n = 0; n < Q_NV; n++) {
        q_voice_t *v = &q.v[n];
        int32_t k1, k0, dk;
        if (!v->on) continue;
        if (!ss && !ps) { v->on = 0; v->sg = 0; continue; }      /* the section has ended */
        k1 = v->gate || !anyg ? 32767 : 0;                         /* a key let go among held ones: out */
        k0 = v->sg;
        if (!k1 && !k0) { v->on = 0; continue; }
        nv++;
        k1 = k1 >= k0 ? k1 : qclamp(k0 - 4096, 0, 32767);         /* in at once, out over ~8 blocks (6 ms) */
        dk = (k1 - k0) / Q_BLK;
        {
            uint32_t inc8 = q_inc((int32_t)v->note * 16 + q.bend), inc4 = inc8 << 1, ph = v->ph;
            int32_t poly = (p8 | p4) && v->note >= split, str = s8 | s4;
            for (i = 0; i < Q_BLK; i++) {
                uint32_t ph8 = ph >> 1;
                int32_t a = 0, b = 0;
                if (hol) {
                    if (s8 | p8) a = q_pulse(ph8, inc8, 0x80000000u) * 2;
                    if (s4 | p4) b = q_pulse(ph, inc4, 0x80000000u) * 2;
                } else {
                    if (s8 | p8) a = q_saw(ph8 + 0x80000000u, inc8);
                    if (s4 | p4) b = q_saw(ph, inc4);
                }
                ph += inc4;
                if (str) sraw[i] += qm15((s8 ? a : 0) + (s4 ? b : 0), k0);
                if (poly) praw[i] += qm15((p8 ? a : 0) + (p4 ? b : 0), k0);
                k0 += dk;
            }
            v->ph = ph;
        }
        v->sg = k1;
    }
    q_mon.voices = (uint8_t)nv;
    {   /* the strings: one AR over the bus */
        int32_t g1 = (s8 | s4) ? qm15(se1 >> 9, 6000) : 0, g0 = q.psg, dg = (g1 - g0) / Q_BLK;
        if (g0 | g1)
            for (i = 0; i < Q_BLK; i++) { q.sbus[i] += qm15(sraw[i], g0); g0 += dg; }
        q.psg = g1;
    }
    {   /* the poly: one VCF (tracking the top key), one VCA */
        int32_t g1 = (p8 | p4) ? qm15(qm15(pe1 >> 9, 30000), trem) : 0, g0 = q.ppg, dg = (g1 - g0) / Q_BLK;
        if (g0 | g1) {
            q_svf_t f1, f2;
            int32_t cut = (P(QP_P_CUT) << 8) + (((pe1 >> 9) * P(QP_P_ENV)) >> 7) +
                          ((((top >= 0 ? top : 60) - 60) * P(QP_P_KBD) * 13) >> 2) + ((lfo * P(QP_P_LFO)) >> 10);
            q_svf_coef(&f1, cut, P(QP_P_RES));
            q_svf_coef(&f2, cut, 0);
            for (i = 0; i < Q_BLK; i++) {
                q.pbus[i] += qm15(q_lp24(&f1, &f2, qclamp(praw[i] >> 3, -60000, 60000), q.pz), g0);
                g0 += dg;
            }
        }
        q.ppg = g1;
    }
}

/* ------------------------------------------------------------------ LEAD --- */
static __attribute__((noinline)) void q_lead(void)
{
    int32_t split = q_split(), i, lo = -1, hi = -1, n = 0;
    int32_t e0 = q.l.env, e1 = e0, retrig = 0, newgate;
    uint8_t notes[Q_NHELD];
    for (i = 0; i < (int32_t)q.nheld; i++)
        if (q.held[i] >= split) {
            notes[n++] = q.held[i];
            if (lo < 0 || q.held[i] < lo) lo = q.held[i];
            if (hi < 0 || q.held[i] > hi) hi = q.held[i];
        }
    newgate = n > 0;
    if (P(QP_ARP) && n) {                       /* the sequencer: the lead's keys in order, over 1..3 octaves */
        uint32_t step = Q_LFO_INC[72 + P(QP_TRSPD) * 55 / 127];
        int32_t k, j, sorted[Q_NHELD], mode = P(QP_ARP), len = n * qclamp(P(QP_ARP_OCT), 1, 3), idx;
        for (k = 0; k < n; k++) sorted[k] = notes[k];
        for (k = 1; k < n; k++)                 /* (insertion sort: a handful) */
            for (j = k; j > 0 && sorted[j - 1] > sorted[j]; j--) { int32_t t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t; }
        if (!q.l.gate) {                        /* the first key: the first step now (DOWN from the top) */
            q.l.arpi = (uint8_t)(mode == 3 ? len - 1 : 0);
            q.l.arpup = 1;
            q.l.arpph = 0;
            retrig = 1;
        }
        int sync = q.clk_run && P(QP_SYNC), adv;
        if (sync) {
            adv = q_clock_step(&q.clk_used, Q_SYNC_DIV[qclamp(P(QP_SYNC), 0, 6)]);
        } else {
            q.l.arpph += step;
            adv = q.l.arpph < step;
        }
        if (adv) {                              /* the next step */
            switch (mode) {
            case 1: q.l.arpi = (uint8_t)((q.l.arpi + 1) % len); break;                       /* UP */
            case 3: q.l.arpi = (uint8_t)(q.l.arpi ? q.l.arpi - 1 : len - 1); break;          /* DOWN */
            case 4: {                                                                         /* RANDOM (not twice) */
                int32_t r = (int32_t)((q_rand(&q.seed) >> 8) % (uint32_t)len);
                if (len > 1 && r == q.l.arpi) r = (r + 1) % len;
                q.l.arpi = (uint8_t)r;
                break;
            }
            default:                                                                          /* UP / DOWN */
                if (len > 1) {
                    if (q.l.arpup && q.l.arpi + 1 >= len) q.l.arpup = 0;
                    else if (!q.l.arpup && q.l.arpi == 0) q.l.arpup = 1;
                    q.l.arpi = (uint8_t)(q.l.arpi + (q.l.arpup ? 1 : -1));
                }
                break;
            }
            retrig = 1;
        }
        if (q.l.arpi >= len) q.l.arpi = 0;
        idx = q.l.arpi;
        lo = hi = sorted[idx % n] + 12 * (idx / n);
        q_mon.arp_step = (uint8_t)idx;
    }
    if (newgate) {
        int32_t base = P(QP_L_OCT) * 192;
        int32_t t1 = ((P(QP_L_2V) ? lo : hi) << 4) + base, t2 = (hi << 4) + base + P(QP_L_INT) * 16;
        if (!q.l.gate)
            retrig = 1;
        else if (P(QP_L_TRIG) && (hi != q.l.note2 || lo != q.l.note1))
            retrig = 1;                         /* MULTI: every new key fires the envelope */
        if (!q.l.gate && !q.l.env) {            /* from silence: no glide in */
            q.l.p1 = t1;
            q.l.p2 = t2;
        }
        q.l.t1 = t1, q.l.t2 = t2;
        q.l.note1 = (uint8_t)lo, q.l.note2 = (uint8_t)hi;
        if (!q.l.gate)
            q.l.vib = 0;
    }
    q.l.gate = (uint8_t)newgate;
    if (retrig)
        q.l.stage = 1;
    if (!q.l.gate && q.l.stage && q.l.stage < 5u)
        q.l.stage = 5;
    {   /* ADS/ASR: 1 attack, 2 the preset decay to DECAY's level, 3 the second attack, 4 full, 5 release;
         * SUS LOW: 2 decays (DECAY the time) to 20 % and stays */
        int32_t slow = P(QP_L_SLOW), sus1 = slow ? (1 << 24) / 5 : (P(QP_L_DEC) << 17) * 3 / 4;
        switch (q.l.stage) {
        case 1:
            e1 = e0 + (int32_t)Q_LIN[P(QP_L_A)];
            if (e1 >= 1 << 24) { e1 = 1 << 24; q.l.stage = 2; }
            break;
        case 2:
            e1 = e0 + qmul16(sus1 - e0, Q_EXP[slow ? 40 + P(QP_L_DEC) * 70 / 127 : 82]);
            if (!slow && e1 - sus1 < 1 << 17) q.l.stage = 3;
            break;
        case 3:
            e1 = e0 + (int32_t)Q_LIN[84];
            if (e1 >= 1 << 24) { e1 = 1 << 24; q.l.stage = 4; }
            break;
        case 4:
            e1 = 1 << 24;
            break;
        case 5:
            e1 = e0 - qmul16(e0, Q_EXP[q.sustain ? 118 : P(QP_L_R)]);
            if (e1 < 1 << 10) { e1 = 0; q.l.stage = 0; }
            break;
        default:
            e1 = 0;
            break;
        }
    }
    q.l.env = e1;
    q_mon.lead_env = (uint16_t)(e1 >> 9);
    q_mon.lead_stage = q.l.stage;
    q_mon.lead_note = q.l.gate ? q.l.note2 : 0;
    {   /* portamento, each VCO its own time per octave (0: none) */
        int32_t *pp[2] = {&q.l.p1, &q.l.p2}, tg[2] = {q.l.t1, q.l.t2}, tm[2] = {P(QP_L_POR1), P(QP_L_POR2)}, k;
        for (k = 0; k < 2; k++) {
            int32_t d = tg[k] - *pp[k], st;
            if (!d) continue;
            if (!tm[k]) { *pp[k] = tg[k]; continue; }
            st = (int32_t)((Q_LIN[qclamp(tm[k] + 40, 0, 127)] * 192u) >> 24);
            if (st < 1) st = 1;
            *pp[k] += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
        }
    }
    if (q.l.gate && q.l.vib < 32767)            /* the vibrato comes in after VDEL */
        q.l.vib = qclamp(q.l.vib + (int32_t)(Q_LIN[P(QP_L_VDEL)] >> 9), 0, 32767);
    {   /* the trill: the interval on and off at the trill speed */
        uint32_t step = Q_LFO_INC[72 + P(QP_TRSPD) * 55 / 127];
        if (q.clk_run && P(QP_SYNC)) {          /* synced: the trill flips at the division */
            if (q_clock_step(&q.clk_trill, Q_SYNC_DIV[qclamp(P(QP_SYNC), 0, 6)]))
                q.l.trph ^= 0x80000000u;
        } else {
            q.l.trph += step;
        }
        q.l.trill_hi = P(QP_L_TRILL) && q.l.gate && !P(QP_ARP) ? (uint8_t)(q.l.trph >> 31) : 0;
    }
    {
        int32_t g1 = qm15(qm15(e1 >> 9, 22000), P(QP_L_TOUCH) == 2 ? 26000 + (q.touch >> 8) * 53 : 32767), g0 = q.l.g, dg;   /* ... and louder */
        if (P(QP_ARP))                          /* the sequencer's short steps: as loud as a held lead */
            g1 = g1 * 2;
        if (!g1 && !g0)
            return;
        dg = (g1 - g0) / Q_BLK;
        {
            int32_t vdep = qclamp(P(QP_L_VIB) + q.modw, 0, 127);
            int32_t vib = qm15(q_sin(q.lfo_ph), q.l.vib > (q.modw << 8) ? q.l.vib : q.modw << 8) * vdep / 127 * 16 >> 15;   /* +- 1 semitone */
            int32_t tr = (q.l.trill_hi ? P(QP_L_TRILL) * 16 : 0) + q.bend + (P(QP_L_TOUCH) == 1 ? (q.touch >> 8) * 32 / 127 : 0);   /* the touch: up to 2 semitones */
            uint32_t inc1 = q_inc(q.l.p1 + vib + tr), inc2 = q_cents(q_inc(q.l.p2 + vib + tr), P(QP_L_DET));
            uint32_t ph1 = q.l.ph1, ph2 = q.l.ph2, pw;
            int32_t wave = P(QP_L_WAVE), two = P(QP_L_VCO2), eq = e1 >> 9, pm = qm15(eq, P(QP_L_PWM) * 258);
            int32_t cut = (P(QP_L_CUT) << 8) + ((eq * P(QP_L_ENV)) >> 7) + (((q.l.p2 >> 4) - 60) * 26 * 4) +
                          (P(QP_L_TOUCH) == 2 ? (q.touch >> 8) * 24 : 0);   /* VOL / BRILL: brighter ... */
            q_svf_t f1, f2;
            switch (wave) {                     /* the pulse width: 10 % mode 25 .. 60 %, 50 % mode 60 .. 80 % */
            case 2: pw = 0x1999999Au; break;
            case 3: pw = 0x40000000u + (uint32_t)pm * 0x2CCCu; break;
            case 4: pw = 0x9999999Au + (uint32_t)pm * 0x1999u; break;
            default: pw = 0x80000000u; break;
            }
            q_svf_coef(&f1, cut, P(QP_L_RES));
            q_svf_coef(&f2, cut, 0);
            for (i = 0; i < Q_BLK; i++) {
                int32_t x = wave ? q_pulse(ph1, inc1, pw) : q_saw(ph1, inc1);
                if (two)
                    x = (x + (wave ? q_pulse(ph2, inc2, pw) : q_saw(ph2, inc2))) >> 1;
                ph1 += inc1;
                ph2 += inc2;
                q.lbus[i] += qm15(q_lp24(&f1, &f2, x >> 1, q.l.z), g0);
                g0 += dg;
            }
            q.l.ph1 = ph1, q.l.ph2 = ph2;
        }
        q.l.g = g1;
    }
}

/* the voices' job (CPU1 with two cores) */
static void q_voices_job(uint32_t a)
{
    (void)a;
    if (P(QP_PARA)) q_voices_para(); else q_voices();
}
#ifndef Q_PAR_RUN
#define Q_PAR_RUN(fn) 0                         /* one core: the host tests, a build without cpu2 */
#define Q_PAR_WAIT() (-1)
#endif

/* ------------------------------------------------------------------ the strings' ensemble --- */
/* the bus (strings + string bass): a tone filter, three delay taps swept 120 degrees apart (0.6 Hz and 6 Hz), stereo */
static __attribute__((noinline)) void q_ensemble(int32_t *l, int32_t *r)
{
    int32_t i, k, d0[3], dd[3], dep = P(QP_S_ENS);
    uint32_t e1 = q.eph1 + 0x001D0000u, e2 = q.eph2 + 0x011D0000u;
    int32_t tone = 8000 + P(QP_S_TONE) * 190;   /* the low-pass's coefficient, Q15 (~1.6 .. 7 kHz) */
    for (k = 0; k < 3; k++) {
        uint32_t o = (uint32_t)k * 0x55555555u;
        int32_t a = (300 << 8) + ((q_sin(q.eph1 + o) * dep) >> 7) * 9 / 16 + ((q_sin(q.eph2 + o) * dep) >> 7) / 14;
        int32_t b = (300 << 8) + ((q_sin(e1 + o) * dep) >> 7) * 9 / 16 + ((q_sin(e2 + o) * dep) >> 7) / 14;
        d0[k] = a;
        dd[k] = (b - a) / Q_BLK;
    }
    q.eph1 = e1, q.eph2 = e2;
    for (i = 0; i < Q_BLK; i++) {
        int32_t s = q.sbus[i], t[3];
        q.tone_hp += s - (q.tone_hp >> 8);      /* ~30 Hz high-pass (its state Q8: no DC left behind) */
        s -= q.tone_hp >> 8;
        q.tone_lp += qm15(s - q.tone_lp, tone);
        s = q.tone_lp;
        q.dl[q.dw & 1023u] = (int16_t)qclamp(s >> 2, -32767, 32767);   /* (>> 2: room for 8 stacked keys) */
        for (k = 0; k < 3; k++) {
            int32_t d = d0[k] >> 8, f = d0[k] & 255;
            int32_t a = q.dl[(q.dw - (uint32_t)d) & 1023u], b = q.dl[(q.dw - (uint32_t)d - 1u) & 1023u];
            t[k] = (a + (((b - a) * f) >> 8)) * 4;
            d0[k] += dd[k];
        }
        q.dw++;
        if (dep) {
            l[i] = (t[0] + (t[1] >> 1) + (s >> 2)) * 3 / 4;
            r[i] = (t[2] + (t[1] >> 1) + (s >> 2)) * 3 / 4;
        } else {
            l[i] = r[i] = s;
        }
    }
}

/* ------------------------------------------------------------------ the phase shifter --- */
static __attribute__((noinline)) void q_phaser(const int32_t *in, int32_t *l, int32_t *r)
{
    int32_t i, k, pos, a0 = q.pa, a1, da, fb = P(QP_PH_RES) * 210, dep = P(QP_PH_DEP), mod = P(QP_PH_MOD);
    uint32_t spd = Q_LFO_INC[P(QP_PH_SPD) * 94 / 127];
    switch (mod) {                              /* where the sweep is, 0..32767 */
    case 1:                                     /* S&H: a new random level at the sweep speed, slewed a little */
        q.sh_ph += spd;
        if (q.sh_ph < spd) q.sh_tgt = (int32_t)(q_rand(&q.seed) >> 17);
        q.sh += (q.sh_tgt - q.sh) >> 2;
        pos = q.sh;
        break;
    case 2:                                     /* ENV: the strings / poly envelope opens it */
        pos = q.ph_env;
        break;
    case 3:                                     /* LFO */
        pos = (q_sin(q.lfo_ph) + 32768) >> 1;
        break;
    default:                                    /* SWEEP: its own triangle */
        q.lfo_tri += spd;
        pos = (int32_t)((q.lfo_tri >> 31 ? ~q.lfo_tri : q.lfo_tri) >> 15);
        break;
    }
    pos = qclamp(pos, 0, 32767);
    q_mon.ph_pos = (uint16_t)pos;
    {   /* the coefficient: the middle of the 64 steps, +- DEPTH */
        int32_t x = 32 * 256 + (((pos - 16384) * dep) >> 13) * 2, j;
        x = qclamp(x, 0, 64 * 256 - 1);
        j = x >> 8;
        a1 = Q_AP[j] + (((Q_AP[j + 1] - Q_AP[j]) * (x & 255)) >> 8);
    }
    if (!a0) a0 = a1;
    da = (a1 - a0) / Q_BLK;
    q.pa = a1;
    {
        int32_t any = 0;
        for (i = 0; i < Q_BLK; i++)
            any |= in[i] > 8 || in[i] < -8;
        q.ph_quiet = any ? 0 : (uint16_t)(q.ph_quiet < 60000u ? q.ph_quiet + 1u : q.ph_quiet);
        if (q.ph_quiet > 300u) {                /* ~0.2 s of silence: the ladder rests */
            if (q.ph_quiet == 301u)
                for (k = 0; k < 14; k++) q.ap[k] = 0, q.apx = q.pfb = 0;
            return;
        }
    }
    for (i = 0; i < Q_BLK; i++) {
        int32_t x = in[i], u = qclamp((x >> 2) + qm15(q.pfb, fb), -40000, 40000), xp = q.apx, y;
        q.apx = u;
        for (k = 0; k < 14; k++) {              /* y = a (x - y1) + x1 */
            int32_t yk = qm15(a0, u - q.ap[k]) + xp;
            xp = q.ap[k];
            q.ap[k] = yk;
            u = yk;
        }
        q.pfb = qclamp(u, -40000, 40000);
        y = u * 4;
        l[i] += (x + y) >> 1;                   /* the two outputs: their notches between each other's, but */
        r[i] += (x - (y >> 2)) >> 1;            /* not opposites, so the FM-1's one speaker (L+R) keeps the sweep */
        a0 += da;
    }
}

/* ------------------------------------------------------------------ the block --- */
#include "q_fx.c"                               /* the delay and the reverb (QUADRO's own) */

static int32_t q_master_tune;                   /* (cents; not used yet) */

/* the bass for a small speaker: the FM-1's speaker gives little below ~200 Hz, so the bass's own level goes up and a
 * driven copy, high-passed (~400 Hz), adds the upper harmonics the ear hears the missing fundamental from */
static int32_t q_blift_lp;
static inline int32_t q_bass_lift(int32_t b)
{
    int32_t a = b < 0 ? -b : b, d, h;
    d = q_tanh(qclamp(a * 2, 0, 65535));                 /* a gentle drive: ~x2 into a soft limit */
    d = b < 0 ? -d : d;
    h = (d - q_blift_lp) * 1870;                        /* a one-pole lowpass ~400 Hz (rounded away from zero, so */
    q_blift_lp += (h + (h > 0 ? 32767 : 0)) >> 15;      /* it lands on d, no residue); d minus it is the highpass */
    h = d - q_blift_lp;
    return b * 3 + h;
}

/* one block of 32 stereo frames into out (Q15, added) */
static void q_block(int32_t *out)
{
    static int32_t sl[Q_BLK], sr[Q_BLK], phin[Q_BLK], dry_l[Q_BLK], dry_r[Q_BLK], wl[Q_BLK], wr[Q_BLK];
    static int32_t dsend[Q_BLK], rsend[Q_BLK];
    int32_t i, gb = q_gain(P(QP_M_B)), gs = q_gain(P(QP_M_S)), gp = q_gain(P(QP_M_P)), gl = q_gain(P(QP_M_L));
    int32_t inb = P(QP_PH_B), ins = P(QP_PH_S), inp = P(QP_PH_P), inl = P(QP_PH_L);
    int32_t rev = P(QP_REV) * 200, dly = P(QP_DLY) * 200;
    uint32_t pk[4] = {0, 0, 0, 0};
    q_events();
    q.samples += Q_BLK;
    q.clk_run = q.clk_last && q.samples - q.clk_last < Q_FS / 2u;
    if (!q.clk_run)
        q.clk_used = q.clk_trill = q.clk_count;            /* (a clock that starts again does not owe old steps) */
    if (q.clk_run && q.clk_span)                           /* the delay follows the clock's tempo */
        qf_bpm10 = (uint16_t)qclamp((int32_t)((uint32_t)Q_FS * 600u / 24u * 256u / q.clk_span), 300, 3000);
    q_mon.bpm10 = q.clk_run ? qf_bpm10 : 0;
    q.touch += (q.touch_tgt * 256 - q.touch) >> 4;   /* the touch, smoothed (Q8, ~20 ms) */
    q.lfo_ph += Q_LFO_INC[38 + P(QP_LFO) * 82 / 127];
    q_mon.lfo = (uint8_t)(q.lfo_ph >> 24);
    for (i = 0; i < Q_BLK; i++)
        q.sbus[i] = q.pbus[i] = q.bbus[i] = q.lbus[i] = q.sbbus[i] = 0;
    {   /* the strings and poly on the second core (if there is one) while this one plays the bass and the lead:
         * they share nothing but the patch (read only) and the keys (written before, in q_events) */
        int par = Q_PAR_RUN(q_voices_job);
        q_bass();
        q_lead();
        if (!par || Q_PAR_WAIT() != 0)
            q_voices_job(0);
        for (i = 0; i < Q_BLK; i++)
            q.sbus[i] += q.sbbus[i];
    }
    q_ensemble(sl, sr);
    for (i = 0; i < Q_BLK; i++) {
        int32_t b = q_bass_lift(qm15(q.bbus[i], gb)), s_l = qm15(sl[i], gs), s_r = qm15(sr[i], gs);
        int32_t p = qm15(q.pbus[i], gp), ld = qm15(q.lbus[i], gl) * 5 / 2, a;
        a = b < 0 ? -b : b; if ((uint32_t)a > pk[0]) pk[0] = (uint32_t)a;
        a = s_l < 0 ? -s_l : s_l; if ((uint32_t)a > pk[1]) pk[1] = (uint32_t)a;
        a = p < 0 ? -p : p; if ((uint32_t)a > pk[2]) pk[2] = (uint32_t)a;
        a = ld < 0 ? -ld : ld; if ((uint32_t)a > pk[3]) pk[3] = (uint32_t)a;
        phin[i] = (inb ? b : 0) + (ins ? (s_l + s_r) >> 1 : 0) + (inp ? p : 0) + (inl ? ld : 0);
        dry_l[i] = (inb ? 0 : b) + (ins ? 0 : s_l) + (inp ? 0 : p) + (inl ? 0 : ld);
        dry_r[i] = (inb ? 0 : b) + (ins ? 0 : s_r) + (inp ? 0 : p) + (inl ? 0 : ld);
    }
    if (inb | ins | inp | inl)
        q_phaser(phin, dry_l, dry_r);
    for (i = 0; i < 4; i++)                     /* the meters fall ~0.3 s from the top */
        q_mon.peak[i] = (uint16_t)(pk[i] > q_mon.peak[i] ? (pk[i] > 32767u ? 32767u : pk[i]) : q_mon.peak[i] - (q_mon.peak[i] >> 4));
    for (i = 0; i < Q_BLK; i++) {
        int32_t m = (dry_l[i] + dry_r[i]) >> 1;
        rsend[i] = qm15(m, rev);
        dsend[i] = qm15(m, dly);
    }
    qf_dtime = (uint8_t)P(QP_DTIME);
    {   /* the reverb and the delay: skipped when nothing has gone in for 3 s (their tails long gone) */
        static uint32_t idle;
        int32_t any = 0;
        if (rev | dly)
            for (i = 0; i < Q_BLK; i++) any |= rsend[i] | dsend[i];
        idle = any ? 0 : idle < 0xFFFFu ? idle + 1u : idle;
        if (idle < 4200u) {
            q_fx(dsend, rsend, wl, wr, Q_BLK);
        } else {
            for (i = 0; i < Q_BLK; i++) wl[i] = wr[i] = 0;
        }
    }
    for (i = 0; i < Q_BLK; i++) {
        out[2 * i] += dry_l[i] + wl[i];
        out[2 * i + 1] += dry_r[i] + wr[i];
    }
    {
        uint32_t lit = 0, k;
        for (k = 0; k < Q_NV; k++)
            if (q.v[k].on && q.v[k].gate && q.v[k].note >= 48 && q.v[k].note < 80)
                lit |= 1u << (q.v[k].note - 48);
        q_mon.keys_lit = lit;
    }
    (void)q_master_tune;
}

/* the master: the mix (Q15) at -6 dB to 16 bits; above 3/4 of full scale it rounds off (the tanh table) instead of
 * clipping */
static inline int32_t q_out(int32_t v)
{
    int32_t a;
    v >>= 1;
    a = v < 0 ? -v : v;
    if (a > 24576) {
        a = 24576 + (q_tanh(qclamp((a - 24576) * 2, 0, 65535)) >> 2);
        if (a > 32767) a = 32767;
        v = v < 0 ? -a : a;
    }
    return v;
}

static void q_init(void)
{
    q.seed = 0x1234567u;
}
