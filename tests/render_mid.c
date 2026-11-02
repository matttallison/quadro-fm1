/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO's sound for an event list (tools/make_demo_mid.py's: seconds and MIDI bytes a line), with MIDI clock at
 * BPM as a DAW would send it: a WAV of what the FM-1 plays for the song.
 *   cc -O2 -w -Iapp -o build/render_mid tests/render_mid.c -lm && build/render_mid EVENTS.txt BPM OUT.wav */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static void *memset_(void *d, int c, size_t n) { return memset(d, c, n); }
#include "q_params.c"
#include "q_synth.c"
#include "q_presets.c"

int main(int argc, char **argv)
{
    FILE *in = fopen(argv[1], "r"), *out = fopen(argv[3], "wb");
    double bpm = atof(argv[2]), t = 0, next_clk = 0, et = -1, end = 0;
    unsigned b[3], nb = 0, frames = 0, v;
    char line[128];
    int32_t o[2 * Q_BLK];
    if (!in || !out) return 1;
    q_init();
    q_factory(&qp, 0);
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0", 1, 24, out);
    v = Q_FS; fwrite(&v, 4, 1, out); v = Q_FS * 4; fwrite(&v, 4, 1, out);
    fwrite("\x04\0\x10\0data\0\0\0\0", 1, 12, out);
    q_clock(0xFA);
    line[0] = 0;
    if (fgets(line, sizeof line, in)) nb = sscanf(line, "%lf %u %u %u", &et, &b[0], &b[1], &b[2]) - 1;
    while (et >= 0 || t < end + 4.0) {
        while (et >= 0 && et <= t) {
            switch (b[0] & 0xF0) {
            case 0x90: q_key((uint8_t)b[1], b[2] != 0); break;
            case 0x80: q_key((uint8_t)b[1], 0); break;
            case 0xE0: q.bend = ((int32_t)(b[1] | b[2] << 7) - 8192) * 32 / 8192; break;
            case 0xD0: q.touch_tgt = (int32_t)b[1]; break;
            case 0xC0: q_factory(&qp, b[1] % 68u); break;
            case 0xB0: if (b[1] == 1) q.modw = (int32_t)b[2]; else if (b[1] == 64) q.sustain = b[2] >= 64; break;
            }
            end = et;
            et = -1;
            if (fgets(line, sizeof line, in)) nb = sscanf(line, "%lf %u %u %u", &et, &b[0], &b[1], &b[2]) - 1;
        }
        while (next_clk <= t) { q_clock(0xF8); next_clk += 60.0 / bpm / 24.0; }
        memset(o, 0, sizeof o);
        q_block(o);
        for (unsigned i = 0; i < 2u * Q_BLK; i++) { int16_t s = (int16_t)q_out(o[i]); fwrite(&s, 2, 1, out); }
        frames += Q_BLK;
        t = (double)frames / Q_FS;
    }
    (void)nb;
    v = frames * 4 + 36; fseek(out, 4, SEEK_SET); fwrite(&v, 4, 1, out);
    v = frames * 4; fseek(out, 40, SEEK_SET); fwrite(&v, 4, 1, out);
    fclose(out);
    printf("%s: %.1f s\n", argv[3], t);
    return 0;
}
