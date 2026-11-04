# Changelog

## 1.6

- Phaser MOD ADD page: S&H and ENV can stack on top of the chosen sweep source, as on the original.
- INTERVAL WRITE (SETUP > TOOLS, knob 4): play two keys and their distance becomes VCO 2's interval (range now +-60 semitones, 10 octaves).
- FLAT footswitch: MIDI CC 68 lowers the lead a semitone while held.
- HOLD footswitch: MIDI CC 69 locks the lead gate open after the key is released (pitch follows new keys).
- TRILL WRITE: SETUP > TOOLS, knob 4 turned left; play one key, its distance from the lowest key is the trill interval.
- Poly keeps to 3 octaves above the bass split, with no overlap.
- TOUCH > BEND now bends up to one semitone (was two); the MIDI pitch wheel is unchanged at +-2.

## 1.5

- The ARP Quadra's own sixteen factory voices (programs 90-105), read from the patch sheets in its owner's manual.
- The Quadra's sixteen are now programs 1-16; the rest follow, 17-105. (If you saved programs, they stay in the slots they were in, so the
  factory program there is a different one now: check your saved ones.)
- MIDI in: velocity sets each section's level and the bass, poly and lead cutoff (GLO > VELOCITY, VEL FILTER); the scale lock and
  transpose can apply to MIDI keys (SCALE page, MIDI); CC 71-74, 91, 93, 94 and 20-23 are mapped; SETUP page 3 gives each section its own MIDI channel.
- Sequencer: LATCH, RANGE up to 4 octaves, and an ACCENT or TIE for each of the 16 steps.
- Lead UNISON (FILTER page): two detuned copies of VCO 1.
- SETUP page 4: TAP tempo, COMPARE (the stored program against your edit) and a RANDOM sound.
- Flash: a store part that is full keeps the programs that fit instead of overrunning.
- ANALOG DRIFT: each oscillator's pitch wanders a few cents over a second or two, as the original's free-running VCOs did (GLO > ANALOG, DRIFT; 0 is off).

## 1.4

- Master effects on the whole mix: DRIVE, BASS and TREBLE shelves, a stereo CHORUS, stereo WIDTH and a compressor (GLO pages).
- The room extended: reverb DECAY, DAMP, SIZE (room to cathedral) and PRE-DELAY; delay FEEDBACK, TONE, PINGPONG or STEREO
  mode, and four more note values (dotted 1/8 and 1/16, 4T, 32T).
- Scale lock: set the keyboard to a KEY and SCALE (SEL > SCALE), plus TRANSPOSE.
- Fixed: a section added to a program that had it off would not sound. Turning a footage on now brings its level up, and
  raising a level from zero turns its first footage on. The 16' footages count for a section being on.
- Program 89, INIT: a plain starting point for a new sound.

## 1.3

- Six new sequencer modes: PEDAL, THUMB, CONV, DIVRG, PLAYED (the order the keys went down) and SKIP.
- Fourteen new chords, 24 in all: ADD9, MIN6, MAJ6, DIM, AUG, SUS2, 7SUS, DOM9, MAJ9, MIN11, M7B5, OCT, 4THS, M7#11. Chords can have five notes.
- 4 new programs (85-88), 88 in all.
- A second demo song ([quadro-demo2.mp3](app/docs/quadro-demo2.mp3), [.mid](app/docs/quadro-demo2.mid)) touring the new sounds. BP WAH and WIND NOISE are louder.

## 1.2

- A step sequencer: SEQ = STEP plays a 16-step pattern (semitones from the lowest lead key, or REST) with LENGTH, GATE
  and SWING. GATE works in every sequencer mode. Press ARP to step through its pages; the screen draws the pattern.
- 4 new programs (81-84), 84 in all.
- The factory programs rechecked for loudness: the poly is about 6 dB louder, and the quiet programs (pads, plucks, wah)
  were brought up to match the rest.
- Key velocity out over MIDI is flatter (72-127), and TOUCH needs a firmer press at every sensitivity.

## 1.1

Sound design from scratch.

- Waveform choice (SAW, SQR, PULSE, TRI, SINE) for the bass, strings, poly and both lead oscillators, with pulse width and PWM.
- Filter types LP24, LP12, BP and HP, with drive and key tracking, on the bass, poly and lead.
- Lead: oscillator sync, sub-oscillator and noise. Poly: detune and vibrato. Strings and poly: true 16' / 8' / 4' footages.
- LFO shapes SINE, TRI, SAW, SQR and S&H, routable to the bass and poly filters.
- 12 new programs (69-80), 80 in all ([app/docs/PRESETS.md](app/docs/PRESETS.md)).
- Fixes: 8' is now at the key's own pitch (strings and poly sound an octave lower than in 1.0); the lead's DET is in real cents (1.0's was about ten times too wide).
- Programs are stored in a new format across more flash. Programs saved with 1.0 are not carried over; SETUP settings are.
- Package identity FM-1_52.

## 1.0.1

- A package identity of QUADRO's own, FM-1_52 (1.0's FM-1_96 is the start of other firmwares').
- The sequencer louder: the lead's steps as loud as a held lead; BERLIN SEQ and SEQUENCE fuller, the sequence programs' steps longer.
- A demo song: [app/docs/quadro-demo.mid](app/docs/quadro-demo.mid) to play into QUADRO, and how it sounds ([quadro-demo.mp3](app/docs/quadro-demo.mp3)).

## 1.0

The first release.

- Four sections at once (bass, strings, poly, lead), a 14-stage phase shifter, an output mixer, a reverb and a
  ping-pong delay that are QUADRO's own.
- 68 factory programs in nine banks ([app/docs/PRESETS.md](app/docs/PRESETS.md)). Program 1 is QUADRO.
- CHORD (nine shapes) and CH BASS for one-finger playing; PARA for the original's paraphonic strings and poly.
- The lead's sequencer: UP, UP/DN, DOWN, RAND over 1–3 octaves. It locks to MIDI clock (1/4 to 1/32, with triplets).
- MIDI clock out, with tempo and START / STOP in SETUP.
- TOUCH: key pressure bends the lead or opens its filter, with sensitivity 1–6 (0 off).
- MIDI in and out over USB, Bluetooth and TRS: notes, bend, mod wheel, sustain, CC 74, pressure, program change.
- The strings and poly run on the FM-1's second core. If the second core fails, QUADRO falls back to one.
- Bass tuned for the FM-1: an octave above the original's (BASS OCTAVE −1 for the original register), with a light
  lift for the small speaker.
- Longer DECAY ranges for the bass (about 0.1 s to 8 s, or HOLD) and the poly, shown on screen in real time.
- The phaser keeps its sweep on the FM-1's single speaker.
- Updating from a 0.x test build keeps your settings and loads the 1.0 factory programs.

## 0.1 – 0.9

Test builds.
