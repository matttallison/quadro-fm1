#!/usr/bin/env python3
# QUADRO's second demo song (app/docs/quadro-demo2.mid): a tour of the 1.1-1.3 sound design and the sequencers, program
# by program on MIDI channel 1: all four sections together, the new bass and lead voices, sync, PWM, the filter types,
# the step sequencer, the arpeggio modes, the new chords, noise, S&H, touch, bend, the mod wheel and the phaser.
# Play it into the FM-1 over USB or Bluetooth (SETUP: MIDI channel OMNI or 1); send MIDI clock and the sequencers lock.
#   python3 tools/make_demo2_mid.py OUT.mid [EVENTS.txt]
import re
import struct
import sys
import os

PPQ, BPM = 480, 110
ev = []
NAMES = re.findall(r'\n    \{"([^"]+)", \{', open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "q_presets.c")).read())


def P(name):
    return NAMES.index(name)


def note(t, n, d, v=100):
    ev.append((t, 1, bytes([0x90, n, v])))
    ev.append((t + d, 0, bytes([0x80, n, 0])))


def chord(t, ns, d, v=96):
    for n in ns:
        note(t, n, d, v)


def hi(ns):                                      # voiced above the split (C4): lower keys play the bass
    return [n + 12 if n < 60 else n for n in ns]


def prog(t, name):
    ev.append((t, 0, bytes([0xC0, P(name)])))


def cc(t, c, v):
    ev.append((t, 0, bytes([0xB0, c, v])))


def bend(t, v):
    v += 8192
    ev.append((t, 0, bytes([0xE0, v & 127, v >> 7])))


def press(t, v):
    ev.append((t, 0, bytes([0xD0, v])))


def reset(t):
    bend(t, 0); press(t, 0); cc(t, 1, 0); cc(t, 64, 0)


def ramp(t0, t1, f, a, b, step=PPQ // 8):
    t = t0
    while t <= t1:
        f(t, int(a + (b - a) * (t - t0) / max(1, t1 - t0)))
        t += step


Q, E, S, H = PPQ, PPQ // 2, PPQ // 4, 2 * PPQ
BAR = 4 * Q
t = 0
GAP = Q


def section(name):
    global t
    t += GAP
    prog(t, name)
    reset(t)
    t += Q


# 1. QUADRO FULL: the four sections at once, Am F C G
section("QUADRO FULL")
BASS = [45, 41, 48, 43]
CH = [(60, 64, 69), (60, 65, 69), (60, 64, 67), (62, 67, 71)]
mel = [[(76, H), (74, Q), (72, Q)], [(72, H + Q), (69, Q)], [(67, Q), (69, Q), (72, H)], [(71, H), (74, H)]]
for i in range(4):
    b = t + i * BAR
    note(b, BASS[i], H - 30); note(b + H, BASS[i], Q - 30); note(b + H + Q, BASS[i] + 12, Q - 30)
    chord(b, CH[i], BAR - 20)
    x = b
    for n, d in mel[i]:
        note(x, n, d - 20); x += d
t += 4 * BAR

# 2. SAW BASS: a saw through the filter and drive
section("SAW BASS")
riff = [(0, 45), (E, 45), (Q, 57), (Q + E, 45), (H, 48), (H + E, 45), (H + Q, 52), (H + Q + E, 55)]
for i in range(4):
    for o, n in riff:
        note(t + i * BAR + o, n + (3 if i % 2 else 0) - (2 if i == 3 else 0), E - 20, 110)
t += 4 * BAR

# 3. WOBBLE BASS: the LFO on the filter
section("WOBBLE BASS")
note(t, 41, BAR - 30, 110); note(t + BAR, 43, BAR - 30, 110); note(t + 2 * BAR, 45, 2 * BAR - 30, 110)
t += 4 * BAR

# 4. ACID STEP: the step sequencer over one held key, the root changing under it
section("ACID STEP")
for i, n in enumerate([60, 60, 65, 65, 63, 63, 67, 62]):
    note(t + i * BAR, n, BAR - 10)
t += 8 * BAR

# 5. SWING STEP: rests and swing
section("SWING STEP")
for i, n in enumerate([60, 60, 63, 65]):
    note(t + i * BAR, n, BAR - 10)
t += 4 * BAR

# 6. BASS + STEP: a bass line under the held step key
section("BASS + STEP")
chord(t, [60], 4 * BAR - 10)
for i in range(4):
    b = t + i * BAR
    for o, n in [(0, 36), (H, 36), (H + Q, 43)]:
        note(b + o, n + (5 if i == 2 else 0), Q - 20, 110)
t += 4 * BAR

# 7. SYNC LEAD: oscillator sync, the bend and the mod wheel
section("SYNC LEAD")
line = [(69, H), (72, Q), (74, Q), (76, W := 4 * Q), (79, H), (76, H), (74, H + Q), (72, Q), (69, W)]
x = t
for k, (n, d) in enumerate(line):
    note(x, n, d - 10)
    if d >= W:
        ramp(x + Q, x + d - Q, lambda tt, v: cc(tt, 1, v), 0, 90)
        cc(x + d, 1, 0)
    if k == 3:
        ramp(x + 2 * Q, x + 3 * Q, bend, 0, 4096); ramp(x + 3 * Q, x + 4 * Q - 30, bend, 4096, 0)
    x += d
t = x

# 8. PWM POLY then SUPER SAW: the pulse width sweep, then a detuned saw stack
section("PWM POLY")
for i, c in enumerate([(57, 60, 64), (53, 60, 65), (55, 60, 64), (55, 59, 62)]):
    chord(t + i * BAR, hi(c), BAR - 20, 90)
t += 4 * BAR
section("SUPER SAW")
for i, c in enumerate([(57, 64, 69, 72), (53, 65, 69, 72), (55, 64, 67, 72), (55, 62, 67, 71)]):
    chord(t + i * BAR, hi(c), BAR - 20, 90)
t += 4 * BAR

# 9. SINE ORGAN: drawbar footages
section("SINE ORGAN")
for i, c in enumerate([(57, 64, 69), (53, 60, 69), (60, 64, 67), (55, 62, 71)]):
    chord(t + i * BAR, hi(c), BAR - 20, 90)
t += 4 * BAR

# 10. DOM9 STAB: one finger, a ninth chord on the poly (CHORD)
section("DOM9 STAB")
rhy = [(0, E + S), (E + S + S, S), (Q + E, E), (H + E, E), (H + Q, E), (H + Q + E, E)]
for i, r in enumerate([57, 55, 53, 55]):
    for o, d in rhy:
        note(t + i * BAR + o, r, d - 10, 110)
t += 4 * BAR

# 11. MAJ9 PAD: one key, a slow ninth chord on the strings
section("MAJ9 PAD")
for i, r in enumerate([57, 53, 60, 55]):
    note(t + i * BAR, r, BAR - 10, 90)
t += 4 * BAR

# 12. PEDAL ARP and CONVERGE: the new sequencer modes over a held chord
section("PEDAL ARP")
for i, c in enumerate([(60, 64, 67), (60, 65, 69)]):
    chord(t + i * 2 * BAR, c, 2 * BAR - 10)
t += 4 * BAR
section("CONVERGE")
for i, c in enumerate([(60, 64, 67), (62, 65, 69)]):
    chord(t + i * 2 * BAR, c, 2 * BAR - 10)
t += 4 * BAR

# 13. BP WAH and S&H POLY: the band-pass filter on the LFO, sample-and-hold
section("BP WAH")
for i, c in enumerate([(57, 60, 64), (57, 60, 64), (53, 60, 65), (55, 60, 64)]):
    chord(t + i * BAR, hi(c), BAR - 20, 95)
t += 4 * BAR
section("S&H POLY")
for i, c in enumerate([(57, 60, 64), (53, 60, 65)]):
    chord(t + i * 2 * BAR, hi(c), 2 * BAR - 20, 95)
t += 4 * BAR

# 14. WIND NOISE: noise through a band-pass, the touch sensor opens it
section("WIND NOISE")
for i, n in enumerate([60, 67, 64, 72]):
    x = t + i * BAR
    note(x, n, BAR - 10)
    ramp(x, x + BAR - Q, press, 0, 100); ramp(x + BAR - Q, x + BAR, press, 100, 0)
t += 4 * BAR

# 15. TAPE KEYS and TRI FLUTE
section("TAPE KEYS")
for i, c in enumerate([(57, 64, 69), (53, 60, 65), (55, 62, 67), (57, 64, 69)]):
    chord(t + i * BAR, hi(c), H + Q, 85)
    note(t + i * BAR + H + Q, hi(c)[2] + 3, Q - 10, 85)
t += 4 * BAR
section("TRI FLUTE")
fl = [(72, Q), (76, Q), (79, H), (76, Q), (74, Q), (72, H), (69, H + Q), (72, Q), (71, 4 * Q)]
x = t
for n, d in fl:
    note(x, n, d - 15, 90); x += d
t = x

# 16. ONE FINGER and the end: the whole band from one key, then QUADRO FULL's last chord, phased
section("ONE FINGER")
for i, n in enumerate([57, 53, 60, 55]):
    note(t + i * BAR, n, BAR - 20)
t += 4 * BAR
section("STACK PAD")
chord(t, (57,), 4 * BAR, 90)
t += 4 * BAR + 2 * BAR

ev.append((t, 2, None))
ev.sort(key=lambda e: (e[0], e[1]))


def vlq(n):
    b = [n & 127]
    while n > 127:
        n >>= 7
        b.insert(0, (n & 127) | 128)
    return bytes(b)


trk = bytearray()
trk += vlq(0) + b"\xff\x03" + vlq(len(b"QUADRO demo 2")) + b"QUADRO demo 2"
trk += vlq(0) + b"\xff\x51\x03" + struct.pack(">I", 60000000 // BPM)[1:]
trk += vlq(0) + b"\xff\x58\x04\x04\x02\x18\x08"
last = 0
for tk, _, b in ev:
    trk += vlq(tk - last)
    last = tk
    trk += b if b else b"\xff\x2f\x00"
open(sys.argv[1], "wb").write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) + b"MTrk" + struct.pack(">I", len(trk)) + trk)
if len(sys.argv) > 2:
    with open(sys.argv[2], "w") as f:
        for tk, _, b in ev:
            if b:
                f.write("%.5f %s\n" % (tk * 60.0 / BPM / PPQ, " ".join(str(x) for x in b)))
print("%s: %.0f s, %d events" % (sys.argv[1], last * 60.0 / BPM / PPQ, len(ev)))
