/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO's master effects, on the finished stereo mix (dry and room together), in this order:
 *   DRIVE    a soft saturation (the tanh table): the input x1 to x13 into it, the ceiling lowered as it rises
 *   BASS / TREBLE   two shelves (one-pole, ~200 Hz and ~3 kHz), -12 .. +12 dB
 *   CHORUS   two modulated taps a side, 90 degrees apart (a stereo chorus), mixed in
 *   WIDTH    the side signal scaled: 0 mono, 64 as it is, 127 wide
 *   COMP     a feed-forward compressor (4:1, the threshold falls with the knob) with its make-up gain
 * With every control neutral the stage is skipped: the sound is exactly what it was without it. */
#define QX_N 1024u                               /* the chorus lines: 23 ms */
static int32_t qx_dl[2][QX_N] PLAT_POOL_ATTR;
static const int16_t QX_SHELF[25] = {-3067, -2942, -2801, -2643, -2465, -2266, -2043, -1793, -1512, -1196, -842, -445, 0, 500, 1061, 1690, 2396, 3188, 4077, 5074, 6193, 7448, 8857, 10437, 12210};   /* 10^(dB/20) - 1, Q12, -12 .. +12 dB */
static struct {
    uint32_t w, ph;                              /* the chorus' write position, LFO phase */
    int32_t lo[2], hi[2];                        /* the shelves' one-pole states */
    int32_t env, gain;                           /* the compressor's level (peak, falling) and gain (Q15) */
} qx = {0, 0, {0, 0}, {0, 0}, 0, 32768 << 9};   /* (the gain in Q24, make-up included) */

static int qx_neutral(void)
{
    return !P(QP_X_DRIVE) && !P(QP_X_BASS) && !P(QP_X_TREB) && !P(QP_X_CHR) && !P(QP_X_COMP) && P(QP_X_WIDTH) == 64;
}
static void q_master(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t drv = P(QP_X_DRIVE), kb = QX_SHELF[qclamp(P(QP_X_BASS), -12, 12) + 12], kt = QX_SHELF[qclamp(P(QP_X_TREB), -12, 12) + 12];
    int32_t chr = P(QP_X_CHR), dep = P(QP_X_CHRDEP), wid = P(QP_X_WIDTH), comp = P(QP_X_COMP);
    uint32_t inc = Q_LFO_INC[qclamp(14 + P(QP_X_CHRATE) * 56 / 127, 0, 127)] / Q_BLK, i, c;
    int32_t thr = 24000 - comp * 180, makeup = 256 + comp * 2;
    int32_t pk = 0;
    for (i = 0; i < n; i++) {
        int32_t x[2];
        x[0] = l[i];
        x[1] = r[i];
        for (c = 0; c < 2u; c++) {
            int32_t v = qclamp(x[c], -131071, 131071);
            if (drv) {                           /* DRIVE: tanh of (v / 2) * (1 .. 9), back x2, a little under at the top */
                int32_t u = qclamp((v * (256 + drv * 24)) >> 8, -65535, 65535), a = u < 0 ? -u : u, y = q_tanh(a);
                y = (u < 0 ? -y : y) * 2;                 /* (the input x 1 .. 13 into the tanh: 1.0 = 32768) */
                v = y * 256 / (256 + drv * 4);            /* the ceiling falls as the drive rises */
            }
            if (kb | kt) {                       /* the shelves */
                qx.lo[c] += qm15(v - qx.lo[c], 934);
                qx.hi[c] += qm15(v - qx.hi[c], 14000);
                v += ((qx.lo[c] >> 2) * kb >> 10) + (((v - qx.hi[c]) >> 2) * kt >> 10);
            }
            x[c] = v;
        }
        if (chr) {                               /* CHORUS */
            uint32_t ph = qx.ph + (i + 1u) * inc;
            int32_t m = chr * 2, d0 = 420 * 256 + ((q_sin(ph) * dep * 3) >> 7), d1 = 420 * 256 + ((q_sin(ph + 0x40000000u) * dep * 3) >> 7);
            int32_t dl[2] = {d0, d1};
            for (c = 0; c < 2u; c++) {
                int32_t pos = dl[c] >> 8, fr = dl[c] & 255;
                uint32_t i0 = (qx.w - (uint32_t)pos) & (QX_N - 1u), i1 = (i0 - 1u) & (QX_N - 1u);
                int32_t tap = (qx_dl[c][i0] * (256 - fr) + qx_dl[c][i1] * fr) >> 8;
                qx_dl[c][qx.w & (QX_N - 1u)] = x[c];
                x[c] = (x[c] * (256 - (m >> 1)) + tap * m) >> 8;
            }
            qx.w++;
        }
        if (wid != 64) {                         /* WIDTH */
            int32_t mid = (x[0] + x[1]) >> 1, side = ((x[0] - x[1]) >> 1) * wid / 64;
            x[0] = mid + side;
            x[1] = mid - side;
        }
        if (comp) {                              /* COMPRESSOR: the peak follows (fast up, slow down), the gain eases to it */
            int32_t a0 = x[0] < 0 ? -x[0] : x[0], a1 = x[1] < 0 ? -x[1] : x[1], a = a0 > a1 ? a0 : a1, g;
            qx.env = a > qx.env ? qx.env + ((a - qx.env) >> 3) : qx.env - (qx.env >> 11);
            g = (32768 << 9) / 256 * makeup;
            if (qx.env > thr) {
                int32_t r = thr + (qx.env - thr) / 4;
                g = (int32_t)((((uint32_t)r << 14) / (uint32_t)qx.env) << 10) / 256 * makeup;   /* r / env x make-up, Q24 */
            }
            qx.gain += (g - qx.gain) >> (g < qx.gain ? 6 : 13);
            x[0] = ((x[0] >> 2) * (qx.gain >> 10)) >> 12;
            x[1] = ((x[1] >> 2) * (qx.gain >> 10)) >> 12;
        }
        l[i] = x[0];
        r[i] = x[1];
        (void)pk;
    }
    qx.ph += (uint32_t)n * inc;
}
static void q_master_reset(void)
{
    uint32_t i;
    for (i = 0; i < QX_N; i++) qx_dl[0][i] = qx_dl[1][i] = 0;
    qx.w = qx.ph = 0;
    qx.lo[0] = qx.lo[1] = qx.hi[0] = qx.hi[1] = 0;
    qx.env = 0;
    qx.gain = 32768 << 9;
}
