#!/usr/bin/env python3
# QUADRO's demo song (app/docs/quadro-demo.mid): one MIDI track on channel 1 that walks through what QUADRO does,
# program by program (program changes), with the split, CHORD + CH BASS, the sequencer, the touch and the phaser.
# Play it into the FM-1 over USB or Bluetooth from any DAW or MIDI player (SETUP: MIDI channel OMNI or 1); send MIDI
# clock too and the sequencer locks to the song's tempo. Also writes the event list the preview renderer reads.
#   python3 tools/make_demo_mid.py OUT.mid [EVENTS.txt]
import re
import os
import struct
import sys

PPQ, BPM = 480, 110
BAR = 4 * PPQ
ev = []                                          # (tick, order, bytes)


def note(t, n, d, v=100):
    ev.append((t, 1, bytes([0x90, n, v])))
    ev.append((t + d, 0, bytes([0x80, n, 0])))


def chord(t, ns, d, v=96):
    for n in ns:
        note(t, n, d, v)


NAMES = re.findall(r'\n    \{"([^"]+)", \{', open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "q_presets.c")).read())


def prog(t, name):
    ev.append((t, 0, bytes([0xC0, NAMES.index(name)])))   # by name: the numbers move when programs are added or removed


def cc(t, c, v):
    ev.append((t, 0, bytes([0xB0, c, v])))


def bend(t, v):                                  # -8192..8191
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


E, Q, H, W = PPQ // 2, PPQ, 2 * PPQ, 4 * PPQ
S = PPQ // 4
# A minor: Am F C G. Bass below the split (C4), chords from C4 up, the lead on the top key
BASS = [45, 41, 48, 43]
CHORDS = [(60, 64, 69), (60, 65, 69), (60, 64, 67), (62, 67, 71)]
ROOT = [57, 53, 60, 55]                          # one key a chord (CHORD + CH BASS)
t = 0

# 1. QUADRO (program 1): the whole Quadra, split: bass, strings and poly chords, the lead singing on top
prog(t, "QUADRO"); reset(t); t += Q
mel = [[(76, H), (74, Q), (72, Q)], [(72, H + Q), (69, Q)], [(67, Q), (69, Q), (72, H)], [(71, H), (74, H)]]
for rep in range(2):
    for i in range(4):
        b = t + (rep * 4 + i) * BAR
        note(b, BASS[i], H - 30); note(b + H, BASS[i], Q - 30); note(b + H + Q, BASS[i] + 12, Q - 30)
        chord(b, CHORDS[i], BAR - 20)
        x = b
        for n, d in mel[i]:
            note(x, n + 12 * (rep == 1 and i == 3), d - 20); x += d
t += 8 * BAR

# 2. CHORD+BASS (57): one finger, a minor-seventh chord and its root on the bass
t += Q; prog(t, "CHORD+BASS"); reset(t); t += Q
rhythm = [(0, E + S), (E + S + S, S), (Q + E, E), (H + E, E), (H + Q, E), (H + Q + E, E)]
for rep in range(2):
    for i in range(4):
        b = t + (rep * 4 + i) * BAR
        for o, d in rhythm:
            note(b + o, ROOT[i], d - 10, 110)
t += 8 * BAR

# 3. ONE FINGER (59): CHORD + CH BASS + PARA + the sequencer, the whole band from one key held
t += Q; prog(t, "ONE FINGER"); reset(t); t += Q
for i, n in enumerate([57, 53, 60, 55, 57, 53, 55, 57]):
    note(t + i * BAR, n, BAR - 20)
t += 8 * BAR

# 4. BERLIN SEQ (46): the lead's sequencer up and down over the chord held, the strings under it
t += Q; prog(t, "BERLIN SEQ"); reset(t); t += Q
for i in range(8):
    chord(t + i * BAR, [n - 3 if n > 60 else n for n in CHORDS[i % 4]] + [72 + (i % 2) * 2], BAR - 20)
    note(t + i * BAR, BASS[i % 4], BAR - 20)
t += 8 * BAR

# 5. SPACE LEAD (31): the touch sensor (pressure opens the filter), the mod wheel's vibrato, the bend, glide
t += Q; prog(t, "SPACE LEAD"); reset(t); t += Q
line = [(69, H), (72, H), (76, W), (74, H), (72, Q), (71, Q), (69, W),
        (64, H), (67, H), (72, W), (71, H + Q), (69, Q), (69, W)]
x = t
for k, (n, d) in enumerate(line):
    note(x, n, d - 10)
    if d >= W:
        ramp(x + Q, x + d - Q, press, 0, 110)
        ramp(x + d - Q, x + d, press, 110, 0, PPQ // 16)
        ramp(x + H, x + d - 40, cc_ := (lambda tt, v: cc(tt, 1, v)), 0, 70)
        cc(x + d, 1, 0)
    if k == 9:
        ramp(x + Q, x + H, bend, 0, 4096); ramp(x + H, x + H + Q, bend, 4096, 0)
    x += d
t = x

# 6. SPLIT FUNK (28): a funk bass under, clav stabs through the phaser over
t += Q; prog(t, "SPLIT FUNK"); reset(t); t += Q
fb = [(0, 45, S), (E, 45, S), (E + S, 57, S), (Q + S, 45, S), (H, 48, S), (H + S, 50, S), (H + E, 52, S), (H + Q + E, 43, S)]
st = [(Q, (64, 69, 72)), (Q + E + S, (64, 69, 72)), (H + Q, (65, 69, 72)), (H + Q + E + S, (64, 67, 71))]
for i in range(8):
    b = t + i * BAR
    for o, n, d in fb:
        note(b + o, n + (5 if i % 4 == 2 else 0), d - 5, 112)
    for o, c in st:
        chord(b + o, c, S, 100)
t += 8 * BAR

# 7. ARENA STR (18): the ensemble, wide, with the phaser on the strings
t += Q; prog(t, "ARENA STR"); reset(t); t += Q
for i, c in enumerate([(57, 60, 64, 69), (53, 60, 65, 69), (48, 55, 64, 67), (55, 62, 67, 71)]):
    chord(t + i * 2 * BAR, [n + 12 if n < 60 else n for n in c], 2 * BAR - 20, 90)
t += 8 * BAR

# 8. THUNDER (67): the end, the string bass and the strings low and wide
t += Q; prog(t, "THUNDER"); reset(t); t += Q
note(t, 45, 3 * BAR); chord(t, (64, 69, 72, 76), 3 * BAR, 90)
t += 4 * BAR + 2 * BAR                           # the release and the room

ev.append((t, 2, None))                          # the end of the track
ev.sort(key=lambda e: (e[0], e[1]))


def vlq(n):
    b = [n & 127]
    while n > 127:
        n >>= 7
        b.insert(0, (n & 127) | 128)
    return bytes(b)


trk = bytearray()
trk += vlq(0) + b"\xff\x03" + vlq(len(b"QUADRO demo")) + b"QUADRO demo"
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
