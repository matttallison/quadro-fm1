/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO's sound on the host: each factory program through a phrase (bass keys, a chord, a lead line), checks
 * (sound, no clipping, quiet at the end, voices free) and WAVs; the cost a sample. With -DHOLD: one key (A3) held
 * 1.5 s, and each program must still sound after 300 ms (no program a click).
 *   cc -O2 -w -Iapp -o build/audio_test tests/audio_test.c -lm && build/audio_test build/audio
 *   cc -O2 -w -Iapp -DHOLD -o build/hold_test tests/audio_test.c -lm && DRY=1 build/hold_test build/hold */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
static void *memset_(void *d, int c, size_t n) { return memset(d, c, n); }
#include "q_params.c"
#include "q_synth.c"
#include "q_presets.c"

typedef struct { float t; uint8_t note, on; } ev_t;
#ifdef HOLD
static const ev_t PHRASE[] = {{0.0f, 57, 1}, {1.5f, 57, 0}};
#define SECS 2.0f
#else
static const ev_t PHRASE[] = {
    {0.00f, 48, 1}, {0.00f, 60, 1}, {0.00f, 64, 1}, {0.00f, 67, 1}, {0.00f, 72, 1},
    {0.60f, 72, 0}, {0.65f, 74, 1}, {1.20f, 74, 0}, {1.25f, 76, 1},
    {1.00f, 48, 0}, {1.05f, 55, 1}, {1.90f, 55, 0}, {1.95f, 53, 1},
    {2.30f, 76, 0}, {2.35f, 79, 1},
    {3.00f, 53, 0}, {3.00f, 60, 0}, {3.00f, 64, 0}, {3.00f, 67, 0}, {3.00f, 79, 0},
};
#define SECS 7.0f
#endif

static void wav(const char *path, const int16_t *s, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t v;
    uint16_t a = 1, ch = 2, ba = 4, bits = 16;
    uint32_t sr = Q_FS, br = Q_FS * 4;
    if (!f) return;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
    fwrite(s, 4, frames, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host/audio";
    uint32_t frames = (uint32_t)(SECS * Q_FS) / Q_BLK * Q_BLK, k, bad = 0, pi;
    int16_t *buf = malloc(frames * 4u);
    char path[256];
    snprintf(path, sizeof path, "mkdir -p %s", dir);
    if (system(path)) {}
    for (pi = 0; pi < sizeof FACTORY / sizeof FACTORY[0]; pi++) {
        uint32_t e = 0, clip = 0, i, left = 0;
        int32_t o[2 * Q_BLK];
        double ns = 0, pk = 0, tail = 0, side = 0, mid = 0, bpk;
        static double blk[8u * 48000u / Q_BLK];         /* (8 s: more than SECS) */
        uint32_t last = 0;
        struct timespec t0, t1;
        memset_(&q, 0, sizeof q);
        memset_(&qf, 0, sizeof qf);
        memset_(qf_dl, 0, sizeof qf_dl);
        memset_(qf_comb, 0, sizeof qf_comb);
        memset_(qf_ap, 0, sizeof qf_ap);
        
        q_init();
        q_factory(&qp, pi);
        if (getenv("DRY")) qp.v[QP_REV] = qp.v[QP_DLY] = 0;
        for (k = 0; k < frames / Q_BLK; k++) {
            float now = (float)(k * Q_BLK) / Q_FS;
            while (e < sizeof PHRASE / sizeof PHRASE[0] && PHRASE[e].t <= now) {
                q_post(PHRASE[e].on ? QE_ON : QE_OFF, PHRASE[e].note);
                e++;
            }
            memset_(o, 0, sizeof o);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            q_block(o);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
            bpk = 0;
            for (i = 0; i < 2u * Q_BLK; i++) {
                int32_t v = q_out(o[i]);
                double a = fabs((double)v);
                buf[2u * k * Q_BLK + i] = (int16_t)v;
                if (a >= 32767) clip++;
                if (a > pk) pk = a;
                if (a > bpk) bpk = a;
                if (k * Q_BLK > frames - Q_FS / 2u && a > tail) tail = a;
            }
            blk[k] = bpk;
            for (i = 0; i < Q_BLK; i++) {
                double l = o[2 * i], r = o[2 * i + 1];
                side += (l - r) * (l - r);
                mid += (l + r) * (l + r);
            }
        }
        for (i = 0; i < Q_NV; i++) left += q.v[i].on;
        {
            double pdb = 20 * log10(pk / 32768 + 1e-9), tdb = 20 * log10(tail / 32768 + 1e-9);
            for (i = 0; i < frames / Q_BLK; i++)
                if (blk[i] > pk / 10) last = i;           /* the last block within 20 dB of the peak */
#ifdef HOLD
            int ok = pdb > -40 && !clip && last * Q_BLK * 1000u / Q_FS >= 300u;
            printf("  held: %4u ms within 20 dB\n", last * Q_BLK * 1000u / Q_FS);
#else
            int ok = pdb > -24 && !clip && tdb < -55 && !left;
#endif
            bad += !ok;
            printf("%-10s peak %6.1f dB  tail %6.1f dB  width %4.2f  clipped %u  voices left %u  %4.0f ns/sample  %s\n",
                   FACTORY[pi].name, pdb, tdb, sqrt(side / (mid + 1)), clip, left, ns / frames, ok ? "ok" : "FAIL");
        }
        snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, pi, FACTORY[pi].name);
        for (char *c = path + strlen(dir) + 1; *c; c++) if (*c == ' ') *c = '_';
        wav(path, buf, frames);
    }
    printf("%s\n", bad ? "QUADRO audio: FAIL" : "QUADRO audio: all ok");
    return bad != 0;
}
