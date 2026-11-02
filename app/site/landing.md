# QUADRO

QUADRO is a four-section performance synthesizer for the M-VAVE FM-1, after the ARP Quadra. It has a bass, a string
ensemble, a poly synth and a two-voice lead, all at once across a split keyboard. They go through a 14-stage phase
shifter, reverb and delay, with chords from one key, a sequencer that syncs to MIDI clock, and 68 programs.

![QUADRO on the FM-1's screen: the output mixer, CHORD+BASS, the bass filter, the sequencer](manual/img/hero.png "QUADRO on the FM-1's screen")

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): the sections, the controls, SETUP, MIDI.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 fm1_install.py {{PKG}}`.
- [Source](https://github.com/matttallison/matttallison-quadro-fm1): GPL-3.0. Built on fm1-base and felucca-bsp, from Felucca by
  Leo Kuroshita, Hügelton Instruments.

ARP, Quadra, M-VAVE and FM-1 are trademarks of their owners. QUADRO is not affiliated with them.
