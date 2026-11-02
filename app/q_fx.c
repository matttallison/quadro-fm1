/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO's room: a ping-pong tape delay and a plate-like reverb, its own (written for QUADRO, integer, no tables).
 *
 *   DELAY   two lines, left and right, each feeding the other (the echoes cross the stereo field), a note value of the
 *           tempo (the MIDI clock's, QUADRO's own clock's, or 120); a one-pole low-pass and a gentle saturation in the
 *           feedback, so the repeats darken and round off like tape. Feedback ~55 %.
 *   REVERB  per side four damped feedback combs in parallel (lengths mutually prime, the right side a little longer:
 *           width), then two all-pass diffusers in series; a pre-filter darkens what goes in. A medium plate.
 *
 * Sends in (mono), wet out (stereo). Buffers in the pool. */
#define QF_DLEN 32768u                           /* each delay line: 0.74 s (longer note values halve to fit) */
static int16_t qf_dl[2][QF_DLEN] PLAT_POOL_ATTR;
static const uint16_t QF_COMB[8] = {1171, 1277, 1361, 1453, 1193, 1301, 1381, 1481};   /* L 0..3, R 4..7 */
static const uint16_t QF_AP[4] = {563, 439, 587, 461};                                 /* L 0..1, R 2..3 */
#define QF_COMB_TOTAL (1171 + 1277 + 1361 + 1453 + 1193 + 1301 + 1381 + 1481)
#define QF_AP_TOTAL (563 + 439 + 587 + 461)
static int16_t qf_comb[QF_COMB_TOTAL] PLAT_POOL_ATTR;
static int16_t qf_ap[QF_AP_TOTAL] PLAT_POOL_ATTR;
static struct {
    uint32_t dw;                                 /* the delay's write position */
    int32_t dlp[2];                              /* its feedback low-passes */
    uint16_t ci[8], ai[4];                       /* the reverb's positions */
    int32_t clp[8];                              /* the combs' damping */
    int32_t pre;                                 /* the reverb's input low-pass */
} qf;
static uint16_t qf_bpm10 = 1200;                 /* the delay's tempo (x 10) */
static uint8_t qf_dtime = 1;                     /* 0..5: 1/4 1/8 1/16 1/32 1/8T 1/16T */

static uint32_t qf_delay_len(void)
{
    static const uint8_t DIV24[6] = {24, 12, 6, 3, 8, 4};   /* in 24ths of a quarter */
    uint32_t q = (uint32_t)Q_FS * 600u / (qf_bpm10 ? qf_bpm10 : 1200u);   /* a quarter, samples */
    uint32_t n = q * DIV24[qf_dtime % 6u] / 24u;
    while (n >= QF_DLEN) n >>= 1;                /* (too long for the line: the next shorter one an octave down) */
    return n < 64u ? 64u : n;
}

/* a soft limit for the feedback: linear to 3/4, then rounding into 1.0 (Q15) */
/* (values of +-2 and less written back as 0: integer feedback would keep them circulating forever) */
/* x * c / 32768 rounded toward zero (a floor would bias every pass and keep a residue alive) */
static inline int32_t qf_mul(int32_t x, int32_t c) { int32_t p = x * c; return (p + (p < 0 ? 32767 : 0)) >> 15; }
static inline int32_t qf_gate(int32_t x) { return x > 2 || x < -2 ? x : 0; }
static inline int32_t qf_round(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a > 24576) {
        a = 24576 + (q_tanh((a - 24576) * 2) >> 2);
        x = x < 0 ? -a : a;
    }
    return x;
}

static void q_fx(const int32_t *dly, const int32_t *rev, int32_t *wl, int32_t *wr, uint32_t n)
{
    uint32_t i, k, len = qf_delay_len();
    for (i = 0; i < n; i++) {
        int32_t yl, yr, in, rl = 0, rr = 0, o;
        uint32_t rp = (qf.dw - len) & (QF_DLEN - 1u);
        /* ---- delay: each side's echo goes to the other */
        yl = qf_dl[0][rp];
        yr = qf_dl[1][rp];
        qf.dlp[0] += qf_mul(yl - qf.dlp[0], 14000);   /* the repeats darken (~3 kHz) */
        qf.dlp[1] += qf_mul(yr - qf.dlp[1], 14000);
        qf.dlp[0] = qf_gate(qf.dlp[0]);
        qf.dlp[1] = qf_gate(qf.dlp[1]);
        in = dly[i] >> 1;
        qf_dl[0][qf.dw & (QF_DLEN - 1u)] = (int16_t)q_clamp_fx(qf_gate(qf_round(in + qf_mul(qf.dlp[1], 16000))));
        qf_dl[1][qf.dw & (QF_DLEN - 1u)] = (int16_t)q_clamp_fx(qf_gate(qf_round(qf_mul(qf.dlp[0], 16000))));
        qf.dw++;
        wl[i] = yl * 2;
        wr[i] = yr * 2;
        /* ---- reverb */
        qf.pre += qf_mul(rev[i] - qf.pre, 20000);
        qf.pre = qf_gate(qf.pre);
        in = qf.pre >> 3;
        {
            int16_t *c = qf_comb;
            for (k = 0; k < 8u; k++) {          /* combs: y = buf[t - L]; buf[t] = in + damp(y) x 0.84 */
                int32_t y = c[qf.ci[k]];
                qf.clp[k] += qf_mul(y - qf.clp[k], 22000);
                qf.clp[k] = qf_gate(qf.clp[k]);
                c[qf.ci[k]] = (int16_t)q_clamp_fx(qf_gate(in + qf_mul(qf.clp[k], 27500)));
                if (++qf.ci[k] >= QF_COMB[k]) qf.ci[k] = 0;
                if (k < 4u) rl += y; else rr += y;
                c += QF_COMB[k];
            }
        }
        {
            int16_t *a = qf_ap;
            for (k = 0; k < 4u; k++) {          /* all-passes (g = 0.5): v = x + g buf; out = buf - g v; buf = v */
                int32_t x = k < 2u ? rl : rr, b = a[qf.ai[k]], v = x + (b >> 1);
                o = b - (v >> 1);
                a[qf.ai[k]] = (int16_t)q_clamp_fx(qf_gate(v));
                if (++qf.ai[k] >= QF_AP[k]) qf.ai[k] = 0;
                if (k < 2u) rl = o; else rr = o;
                a += QF_AP[k];
            }
        }
        wl[i] += rl * 3;
        wr[i] += rr * 3;
    }
}
