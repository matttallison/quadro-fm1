/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO's oscillators, filters and LFO, one control at a time: each renders a note with the control set, measures what
 * it must do (harmonics, band energy, beating, the lead's sync) and says ok or FAIL.
 *   cc -O2 -w -Iapp -o build/feature_test tests/feature_test.c -lm && build/feature_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
static void *memset_(void *d, int c, size_t n) { return memset(d, c, n); }
#include "q_params.c"
#include "q_synth.c"
#include "q_presets.c"

#define FS 44100
#define N (FS * 2)
static int16_t buf[N];
static int bad;
static const char *only = "P";                /* the section a test listens to: B S P L (the others are muted) */

/* a patch with only the named sections sounding, filters open: then the caller's overrides (parameter, value, ... -1) */
static void patch(const int *kv)
{
    memset(&q, 0, sizeof q);
    q_init();
    q_patch_init(&qp, "T");
    qp.v[QP_B_EB16] = 0; qp.v[QP_S_8] = qp.v[QP_S_4] = 0; qp.v[QP_P_8] = 0;
    qp.v[QP_M_B] = strchr(only, 'B') ? 100 : 0, qp.v[QP_M_S] = strchr(only, 'S') ? 100 : 0;
    qp.v[QP_M_P] = strchr(only, 'P') ? 100 : 0, qp.v[QP_M_L] = strchr(only, 'L') ? 100 : 0;
    qp.v[QP_PH_S] = 0; qp.v[QP_REV] = 0; qp.v[QP_DLY] = 0; qp.v[QP_S_ENS] = 0;
    qp.v[QP_B_CUT] = qp.v[QP_P_CUT] = qp.v[QP_L_CUT] = 127;
    qp.v[QP_B_RES] = qp.v[QP_P_RES] = qp.v[QP_L_RES] = 0;
    qp.v[QP_B_ENV] = qp.v[QP_P_ENV] = qp.v[QP_L_ENV] = 0;
    qp.v[QP_P_KBD] = 0; qp.v[QP_L_KBD] = 0; qp.v[QP_SPLIT] = 48;
    qp.v[QP_P_A] = 0; qp.v[QP_P_S] = 127; qp.v[QP_L_A] = 0; qp.v[QP_L_DEC] = 127; qp.v[QP_L_VCO2] = 0;
    qp.v[QP_L_VIB] = 0; qp.v[QP_B_DEC] = 127; qp.v[QP_L_SLOW] = 0;
    for (; *kv >= 0; kv += 2) qp.v[kv[0]] = (int8_t)kv[1];
}
static void render(int note)
{
    int n = 0, k;
    q_post(QE_ON, note);
    for (k = 0; k < N / Q_BLK; k++) {
        int32_t o[2 * Q_BLK];
        int i;
        memset(o, 0, sizeof o);
        q_block(o);
        for (i = 0; i < Q_BLK; i++) buf[n++] = (int16_t)qclamp((o[2 * i] + o[2 * i + 1]) >> 1, -32768, 32767);
    }
}
/* the amplitude of frequency f in the second half of the render (a Hann-windowed single-bin DFT) */
static double amp(double f, int from, int len)
{
    double re = 0, im = 0;
    int i;
    for (i = 0; i < len; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / len), a = 2 * M_PI * f * (from + i) / FS;
        re += buf[from + i] * w * cos(a);
        im -= buf[from + i] * w * sin(a);
    }
    return 2 * sqrt(re * re + im * im) / len;
}
static double band(double lo, double hi)          /* energy in a band, steps of 20 Hz */
{
    double e = 0, f;
    for (f = lo; f < hi; f += 20) { double a = amp(f, FS, FS / 2); e += a * a; }
    return e;
}
static double hr(double f0, int h) { return amp(f0 * h, FS, FS) / (amp(f0, FS, FS) + 1e-9); }   /* harmonic h to the first */
static void check(const char *name, int ok, const char *fmt, double a, double b, double c)
{
    char m[96];
    snprintf(m, sizeof m, fmt, a, b, c);
    printf("  %-34s %s   %s\n", name, ok ? "ok  " : "FAIL", m);
    bad += !ok;
}
static double beat_depth(int w0, int w1)          /* the short-term level's swing over the render: (max - min) / mean */
{
    double mn = 1e9, mx = 0, sum = 0;
    int i, j, cnt = 0;
    for (i = w0; i + FS / 50 < w1; i += FS / 50) {
        double e = 0;
        for (j = 0; j < FS / 50; j++) e += (double)buf[i + j] * buf[i + j];
        e = sqrt(e / (FS / 50));
        if (e < mn) mn = e;
        if (e > mx) mx = e;
        sum += e, cnt++;
    }
    return (mx - mn) / (sum / cnt + 1e-9);
}

int main(void)
{
    static const char *WN[5] = {"SAW", "SQR", "PULSE", "TRI", "SINE"};
    int w;
    double f60 = 261.626;

    printf("poly: the footages (note C4 = 261.6 Hz)\n");
    only = "P";
    {
        int k16[] = {QP_P_16, 1, QP_P_8, 0, -1}, k8[] = {-1}, k4[] = {QP_P_8, 0, QP_P_4, 1, -1};
        patch(k16); render(60); check("16' is an octave under", amp(f60 / 2, FS, FS) > 1.4 * amp(f60, FS, FS), "130 Hz %.0f  261 Hz %.0f", amp(f60 / 2, FS, FS), amp(f60, FS, FS), 0);
        patch(k8); qp.v[QP_P_8] = 1; render(60); check("8' is the key's pitch", amp(f60, FS, FS) > 1.4 * amp(f60 * 2, FS, FS) && amp(f60 / 2, FS, FS) < 0.05 * amp(f60, FS, FS), "261 Hz %.0f  523 Hz %.0f  130 Hz %.0f", amp(f60, FS, FS), amp(f60 * 2, FS, FS), amp(f60 / 2, FS, FS));
        patch(k4); render(60); check("4' is an octave over", amp(f60 * 2, FS, FS) > 1.4 * amp(f60 * 4, FS, FS) && amp(f60, FS, FS) < 0.05 * amp(f60 * 2, FS, FS), "523 Hz %.0f  261 Hz %.0f", amp(f60 * 2, FS, FS), amp(f60, FS, FS), 0);
    }
    printf("poly and strings: the waves (harmonic / first)\n");
    for (w = 0; w < 5; w++) {
        int k[] = {QP_P_8, 1, QP_P_WAVE, w, QP_P_PW, 64, -1};
        double h2, h3;
        only = "P";
        patch(k);
        render(60);
        h2 = hr(f60, 2), h3 = hr(f60, 3);
        switch (w) {
        case 0: check("poly SAW", h2 > 0.35 && h3 > 0.22, "h2 %.2f h3 %.2f (0.50 0.33)", h2, h3, 0); break;
        case 1: check("poly SQR", h2 < 0.08 && h3 > 0.25, "h2 %.2f h3 %.2f (0 0.33)", h2, h3, 0); break;
        case 2: check("poly PULSE", h2 > 0.30, "h2 %.2f h3 %.2f (a narrow pulse)", h2, h3, 0); break;
        case 3: check("poly TRI", h2 < 0.08 && h3 < 0.2 && h3 > 0.04, "h2 %.2f h3 %.2f (0 0.11)", h2, h3, 0); break;
        default: check("poly SINE", h2 < 0.06 && h3 < 0.06, "h2 %.2f h3 %.2f (0 0)", h2, h3, 0); break;
        }
        {
            int ks[] = {QP_S_8, 1, QP_S_4, 0, QP_P_8, 0, QP_S_WAVE, w, QP_S_PW, 64, QP_S_ATK, 24, -1};
            double s2, s3;
            only = "S";
            patch(ks);
            render(60);
            s2 = hr(f60, 2), s3 = hr(f60, 3);
            check(w == 0 ? "strings SAW" : w == 1 ? "strings SQR" : w == 2 ? "strings PULSE" : w == 3 ? "strings TRI" : "strings SINE",
                  fabs(s2 - h2) < 0.12 && fabs(s3 - h3) < 0.12, "same as the poly: h2 %.2f h3 %.2f", s2, s3, 0);
        }
    }
    printf("bass: the waves (note F3 = 174.6 Hz; the bass an octave up)\n");
    only = "B";
    {
        double f = 174.614;
        for (w = 0; w < 5; w++) {
            int k[] = {QP_B_EB16, 1, QP_B_WAVE, w, QP_B_PW, 64, QP_M_B, 127, QP_SPLIT, 72, -1};
            double h2, h3;
            patch(k);
            render(53);
            h2 = hr(f, 2), h3 = hr(f, 3);
            check(w == 0 ? "bass SQR" : w == 1 ? "bass SAW" : w == 2 ? "bass PULSE" : w == 3 ? "bass TRI" : "bass SINE",
                  w == 1 ? h2 > 0.3 : w == 2 ? h2 > 0.25 : w == 4 ? h2 < 0.06 && h3 < 0.06 : h2 < 0.1,
                  "h2 %.2f h3 %.2f f %.0f", h2, h3, f);
        }
    }
    printf("filters: TYPE (poly saw C4, cutoff ~1 kHz): low / high band energy\n");
    only = "P";
    {
        double lo[4], hi[4];
        int m;
        for (m = 0; m < 4; m++) {
            int k[] = {QP_P_8, 1, QP_P_4, 1, QP_P_WAVE, 0, QP_P_CUT, 76, QP_P_FMODE, m, QP_P_RES, 20, -1};
            patch(k);
            render(60);
            lo[m] = band(200, 700), hi[m] = band(2500, 6000);
        }
        check("LP24 passes lows, cuts highs", lo[0] > 20 * hi[0], "low/high %.0f", lo[0] / (hi[0] + 1e-9), 0, 0);
        check("LP12 cuts less than LP24", hi[1] > 2 * hi[0] && lo[1] > 20 * hi[1] * 0.2, "high 12 dB / 24 dB %.1f", hi[1] / (hi[0] + 1e-9), 0, 0);
        check("BP: lows cut, highs cut less than HP", lo[2] < lo[0] * 0.5 && hi[2] < hi[3], "low %.2f of LP24, high %.2f of HP", lo[2] / lo[0], hi[2] / hi[3], 0);
        check("HP passes highs, cuts lows", hi[3] > 3 * lo[3], "high/low %.1f", hi[3] / (lo[3] + 1e-9), 0, 0);
    }
    printf("unison, PWM, vibrato\n");
    only = "P";
    {
        int off[] = {QP_P_8, 1, QP_P_WAVE, 0, -1}, on[] = {QP_P_8, 1, QP_P_WAVE, 0, QP_P_DET, 30, -1};
        double a, b;
        patch(off); render(60); a = beat_depth(FS / 2, N);
        patch(on); render(60); b = beat_depth(FS / 2, N);
        check("DET beats against the voice", b > 0.25 && a < 0.1, "swing %.2f (off %.2f)", b, a, 0);
    }
    {
        int off[] = {QP_P_8, 1, QP_P_WAVE, 2, QP_P_PW, 50, QP_LFO, 70, -1}, on[] = {QP_P_8, 1, QP_P_WAVE, 2, QP_P_PW, 50, QP_P_PWM, 100, QP_LFO, 70, -1};
        double a[8], mn, mx, sa, sb;
        int i;
        patch(off); render(60);
        for (i = 0, mn = 1e9, mx = 0; i < 8; i++) { a[i] = amp(f60 * 2, FS / 4 + i * (FS / 8), FS / 8); if (a[i] < mn) mn = a[i]; if (a[i] > mx) mx = a[i]; }
        sa = (mx - mn) / (mx + 1e-9);
        patch(on); render(60);
        for (i = 0, mn = 1e9, mx = 0; i < 8; i++) { a[i] = amp(f60 * 2, FS / 4 + i * (FS / 8), FS / 8); if (a[i] < mn) mn = a[i]; if (a[i] > mx) mx = a[i]; }
        sb = (mx - mn) / (mx + 1e-9);
        check("PWM moves the harmonics", sb > 0.3 && sa < 0.15, "h2 swing %.2f (off %.2f)", sb, sa, 0);
    }
    {
        int off[] = {QP_P_8, 1, QP_P_WAVE, 4, QP_LFO, 76, -1}, on[] = {QP_P_8, 1, QP_P_WAVE, 4, QP_P_VIB, 100, QP_LFO, 76, -1};
        double pa, pb;
        int i, z;
        patch(off); render(60);
        for (i = FS / 2, z = 0; i < N - 1; i++) z += (buf[i] < 0) != (buf[i + 1] < 0);
        pa = z / 2.0 / ((N - FS / 2) / (double)FS);
        patch(on); render(60);
        {   /* the pitch each 100 ms: its swing */
            double mn = 1e9, mx = 0;
            for (i = FS / 2; i + FS / 10 < N; i += FS / 10) {
                int j, zz = 0;
                for (j = 0; j < FS / 10 - 1; j++) zz += (buf[i + j] < 0) != (buf[i + j + 1] < 0);
                { double f = zz / 2.0 * 10; if (f < mn) mn = f; if (f > mx) mx = f; }
            }
            pb = (mx - mn) / f60;
        }
        check("VIB bends the pitch with the LFO", pb > 0.015 && fabs(pa - f60) < 6, "swing %.3f of the pitch, steady f %.0f", pb, pa, 0);
    }
    printf("the LFO's shapes (a cycle's values)\n");
    only = "P";
    {
        int s;
        static const char *SN[5] = {"SINE", "TRI", "SAW", "SQR", "S&H"};
        for (s = 0; s < 5; s++) {
            int k[] = {QP_LFO_W, s, QP_LFO, 100, -1}, i, distinct = 0, last = 99999, ch = 0;
            int32_t mn = 99999, mx = -99999;
            patch(k);
            for (i = 0; i < 4000; i++) {                       /* a few cycles (7 Hz: 0.14 s a cycle, 43 blocks) */
                int32_t o[2 * Q_BLK];
                memset(o, 0, sizeof o);
                q_block(o);
                if (q.lfo_v < mn) mn = q.lfo_v;
                if (q.lfo_v > mx) mx = q.lfo_v;
                if (abs(q.lfo_v - last) > 40) ch++;
                last = q.lfo_v;
            }
            (void)distinct;
            check(SN[s], s == 4 ? mx - mn > 20000 && ch < 400 && ch > 5 : mx > 32000 && mn < -32000,
                  "range %.0f .. %.0f, changes %.0f", mn, mx, ch);
        }
    }
    printf("the lead: waves, VCO 2, sub, noise, sync, drive\n");
    only = "L";
    {
        double f = 440.0;
        for (w = 0; w < 7; w += 1) {
            int k[] = {QP_L_WAVE, w, -1};
            double h2, h3;
            if (w == 2 || w == 3 || w == 4) continue;
            patch(k);
            render(69);
            h2 = hr(f, 2), h3 = hr(f, 3);
            check(w == 0 ? "lead SAW" : w == 1 ? "lead SQR" : w == 5 ? "lead TRI" : "lead SINE",
                  w == 0 ? h2 > 0.35 : w == 1 ? h2 < 0.1 && h3 > 0.15 : w == 5 ? h2 < 0.08 && h3 < 0.2 : h2 < 0.06 && h3 < 0.06,
                  "h2 %.2f h3 %.2f", h2, h3, 0);
        }
        {
            int k[] = {QP_L_WAVE, 4, QP_L_VCO2, 1, QP_L_W2, 4, QP_L_INT, 12, QP_L_DET, 0, -1};      /* VCO 1 square-ish, VCO 2 a sine an octave up */
            double a1, a2;
            patch(k);
            render(69);
            a1 = amp(440, FS, FS), a2 = amp(880, FS, FS);
            check("VCO 2 has its own wave", a2 > 0.2 * a1, "880 Hz %.0f vs 440 Hz %.0f", a2, a1, 0);
        }
        {
            int off[] = {QP_L_WAVE, 5, -1}, on[] = {QP_L_WAVE, 5, QP_L_SUB, 127, -1};
            double a, b;
            patch(off); render(69); a = amp(220, FS, FS);
            patch(on); render(69); b = amp(220, FS, FS);
            check("SUB adds the octave below", b > 8 * (a + 1), "220 Hz %.0f (off %.0f)", b, a, 0);
        }
        {
            int off[] = {QP_L_WAVE, 5, -1}, on[] = {QP_L_WAVE, 5, QP_L_NOISE, 127, -1};
            double a, b;
            patch(off); render(69); a = band(5000, 9000);
            patch(on); render(69); b = band(5000, 9000);
            check("NOISE fills the spectrum", b > 30 * (a + 1), "5-9 kHz energy %.0f (off %.0f)", b, a, 0);
        }
        {   /* sync: VCO 2 (+7 st) restarts with VCO 1: its spectrum is the harmonics of VCO 1 only */
            int off[] = {QP_L_WAVE, 0, QP_L_VCO2, 1, QP_L_W2, 1, QP_L_INT, 7, QP_L_DET, 0, QP_L_SYNC, 0, -1};
            int on[] = {QP_L_WAVE, 0, QP_L_VCO2, 1, QP_L_W2, 1, QP_L_INT, 7, QP_L_DET, 0, QP_L_SYNC, 1, -1};
            double eh[2], et[2];
            int s;
            for (s = 0; s < 2; s++) {
                double fr, e = 0, h = 0;
                patch(s ? on : off);
                render(69);
                for (fr = 300; fr < 4000; fr += 10) {
                    double a = amp(fr, FS, FS), r = fmod(fr, 440.0);
                    a *= a;
                    e += a;
                    if (r < 20 || r > 420) h += a;
                }
                eh[s] = h, et[s] = e;
            }
            check("SYNC locks VCO 2 to VCO 1", eh[1] / et[1] > 0.95 && eh[0] / et[0] < 0.88, "harmonic share %.2f (free %.2f)", eh[1] / et[1], eh[0] / et[0], 0);
        }
        {
            int off[] = {QP_L_WAVE, 6, -1}, on[] = {QP_L_WAVE, 6, QP_L_DRIVE, 120, -1};
            double a, b;
            patch(off); render(69); a = hr(440, 3);
            patch(on); render(69); b = hr(440, 3);
            check("lead DRIVE adds harmonics to a sine", b > a + 0.08, "h3 %.2f (off %.2f)", b, a, 0);
        }
    }
    printf("the bass filter: drive and LFO\n");
    only = "B";
    {
        double f = 174.614;
        int off[] = {QP_B_EB16, 1, QP_B_WAVE, 4, QP_B_CUT, 127, QP_SPLIT, 72, QP_M_B, 127, -1}, on[] = {QP_B_EB16, 1, QP_B_WAVE, 4, QP_B_CUT, 127, QP_B_DRIVE, 110, QP_SPLIT, 72, QP_M_B, 127, -1};
        double a, b;
        patch(off); render(53); a = hr(f, 3);
        patch(on); render(53); b = hr(f, 3);
        check("bass DRIVE adds harmonics to a sine", b > a + 0.05, "h3 %.2f (off %.2f)", b, a, 0);
    }
    {
        int off[] = {QP_B_EB16, 1, QP_B_WAVE, 1, QP_B_CUT, 50, QP_B_RES, 40, QP_LFO, 76, QP_SPLIT, 72, QP_M_B, 127, -1}, on[] = {QP_B_EB16, 1, QP_B_WAVE, 1, QP_B_CUT, 50, QP_B_RES, 40, QP_B_LFO, 127, QP_LFO, 76, QP_SPLIT, 72, QP_M_B, 127, -1};
        double a, b;
        patch(off); render(53); a = beat_depth(FS / 2, N);
        patch(on); render(53); b = beat_depth(FS / 2, N);
        check("bass LFO > VCF wobbles", b > 0.15 && a < 0.06, "swing %.2f (off %.2f)", b, a, 0);
    }
    printf("%s\n", bad ? "QUADRO features: FAIL" : "QUADRO features: all ok");
    return bad != 0;
}
