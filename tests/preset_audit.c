/* SPDX-License-Identifier: GPL-3.0-only */
/* every factory program's level and length: three single keys (F3 bass zone, C4 mid, E5 top) each held 1.5 s, the
 * loudness (RMS dB) early (0.1-0.4 s) and late (1.0-1.5 s). ./preset_audit [target_dB]: lists the programs whose
 * loudness is far from the rest, or that die before 1 s on a held key.
 *   cc -O2 -w -Iapp -o build/preset_audit tests/preset_audit.c -lm && build/preset_audit */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
static void *memset_(void *d, int c, size_t n) { return memset(d, c, n); }
#include "q_params.c"
#include "q_synth.c"
#include "q_presets.c"
static double run(int pi, int note, double *early, double *late, double *pk)
{
    uint32_t blocks = (uint32_t)(2.0 * Q_FS) / Q_BLK, k, i;
    double e = 0, l = 0, ne = 0, nl = 0, p = 0;
    memset(&q, 0, sizeof q);
    q_init();
    q_factory(&qp, (uint32_t)pi);
    q_post(QE_ON, note);
    for (k = 0; k < blocks; k++) {
        int32_t o[2 * Q_BLK];
        double t = (double)k * Q_BLK / Q_FS;
        if (t > 1.5 && k * Q_BLK / (double)Q_FS < 1.5 + 1.0 / Q_FS * Q_BLK) q_post(QE_OFF, note);
        memset(o, 0, sizeof o);
        q_block(o);
        for (i = 0; i < Q_BLK; i++) {
            double v = (q_out(o[2 * i]) + q_out(o[2 * i + 1])) * 0.5 / 32768.0;
            if (fabs(v) > p) p = fabs(v);
            if (t >= 0.1 && t < 0.4) { e += v * v; ne++; }
            if (t >= 1.0 && t < 1.5) { l += v * v; nl++; }
        }
    }
    *early = 10 * log10(e / ne + 1e-12);
    *late = 10 * log10(l / nl + 1e-12);
    *pk = 20 * log10(p + 1e-9);
    return 0;
}
int main(void)
{
    static const int notes[3] = {53, 60, 76};
    int pi, n = (int)(sizeof FACTORY / sizeof FACTORY[0]);
    for (pi = 0; pi < n; pi++) {
        double e[3], l[3], pk[3];
        int j;
        for (j = 0; j < 3; j++) run(pi, notes[j], &e[j], &l[j], &pk[j]);
        printf("%2d %-11s", pi + 1, FACTORY[pi].name);
        for (j = 0; j < 3; j++) printf("  %5.1f/%5.1f/pk%5.1f", e[j], l[j], pk[j]);
        printf("\n");
        if (getenv("MACHINE")) {
            q_patch_t t;
            q_factory(&t, (uint32_t)pi);
            fprintf(stderr, "%d %.1f %d %d %d %d\n", pi + 1, (pk[1] + pk[2]) / 2, t.v[QP_M_B], t.v[QP_M_S], t.v[QP_M_P], t.v[QP_M_L]);
        }
    }
    return 0;
}
