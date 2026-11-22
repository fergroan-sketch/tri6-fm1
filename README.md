# TRI-6 FM-1

A Trigon-6 style polysynth firmware for the M-VAVE FM-1, built on
[fm1-base](https://github.com/charlesvestal/fm1-base) (commit `2bb8e74`). Unofficial: not affiliated with or endorsed
by Sequential or M-VAVE. The engine is written from scratch from the Trigon-6 user's guide and MIDI implementation:
what each control does and its range. It contains no code or data from Sequential's firmware, and the factory patches
are original designs.

**Status: 0.2, pre-release.** It builds for the FM-1 and passes fm1-base's host simulator checks. It has not yet been
tested on a real FM-1: install it at your own risk, after a backup.

## The sound

Six voices. Per voice:

- **Three oscillators**, each with triangle, saw and pulse at once in any mix (OSC 3 adds a reverse saw), octave
  -2..+2, pulse width from silent through square to narrow. OSCs 2 and 3 have a fine pitch of +-7 semitones in cents.
  OSC 2 hard-syncs to OSC 1. OSC 3 has a LO range (an LFO that follows the keys or not) and can leave the keyboard.
- **Noise**, and one **FDBK / DRIVE** knob: left of centre feeds the voice's output back into the filter (harmonic to
  chaotic), right of centre drives the filter's input.
- **Ladder low-pass**, four or two pole, resonant to self-oscillation, keyboard tracking off / half / full, a bipolar
  envelope amount, velocity.
- **Filter envelope and amp envelope** (ADSR), amp envelope amount (0 gives an organ's gate) and velocity.
- **Poly mod**: OSC 3 (at audio rate) and the filter envelope, each bipolar, to the pitch of OSCs 1, 2 and 3, the pulse
  width of OSCs 1 and 2, the feedback and the cutoff. FM, sync sweeps, audio-rate filter modulation.
- **LFO**: triangle, saw, reverse saw, square, random (noise at the top of FREQ); 0.05 to 100 Hz, or synced to the
  clock (4 bars to 1/32), or reset by the first key; to pitch and pulse width (per oscillator), filter and amp. The mod
  wheel adds to its amount.
- **Mod matrix**: four routes a patch, each a source, a destination and a bipolar amount (MOD MATRIX, below).
- **Vintage** (voice-to-voice differences in pitch, cutoff, envelopes and pulse width, and a slow wander), **pan
  spread**, **glide** (fixed rate or fixed time, each also legato only), bend range.

After the voices: stereo **distortion**, then **two effects in series**: A is one of BBD delay, digital delay, chorus,
three phasers, ring modulator (its pitch from a knob or from the lowest note) and two flangers; B is any of those or
hall, room, plate or spring reverb. The delays can sync to the clock (dotted and triplet values); the longest delay is
0.74 s and a synced time longer than that is halved until it fits, as the Trigon-6 does at its 1 s.

**Unison** stacks 1 to 6 voices on one note with a key mode (low, high, last, each with or without retrigger).
**Chord memory**: press PLAY, play a chord (up to six notes), and each key then plays it; UNISON shows CHORD and
the chord is saved with the patch. **Arpeggiator**: up, down, up and down, random, as played; one to three octaves;
ten divisions including dotted, swung and triplet; BPM 30 to 250 or MIDI clock; hold (a new chord replaces the held
one, notes added while a key stays down). The arpeggiator's settings and BPM are part of the patch.

Not yet: the 64-step sequencer, NRPN, MPE, alternative tunings.

## The controls

| Button | Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- |
| HOME | the patch | CUTOFF | RES | ENV AMT | FDBK/DRV |
| EDIT | OSC 1 | OCT | WAVE | PW | LEVEL |
| EDIT 2nd | OSC 2 | OCT | PITCH | WAVE | LEVEL |
| EDIT 3rd | OSC 3 | OCT (LO) | PITCH | WAVE | LEVEL |
| EDIT 4th | OSC MORE | PW 2 | SYNC 2 | PW 3 | KEY 3 |
| SEL | FILTER | CUTOFF | RES | ENV AMT | KEY |
| SEL 2nd | FILTER 2 / MIX | POLES | VEL | NOISE | FDBK/DRV |
| ENV | FILTER ENV | A | D | S | R |
| ENV 2nd | AMP ENV | A | D | S | R |
| ENV 3rd | AMP | ENV AMT | VEL | | |
| LFO | LFO | FREQ | SHAPE | AMOUNT | SYNC |
| LFO 2nd | LFO DEST | PITCH (1 2 3) | PW (1 2 3) | FILTER | AMP |
| SEQ | POLY MOD | OSC 3 | FIL ENV | PITCH (1 2 3) | FILTER |
| SEQ 2nd | POLY MOD 2 | PW (1 2) | FDBK | | |
| SEQ 3rd | MOD MATRIX | SLOT (1-4) | SOURCE | DEST | AMOUNT |
| FX | EFFECT A | TYPE | MIX | P1 | P2 |
| FX 2nd | EFFECT B | TYPE | MIX | P1 | P2 |
| FX 3rd | EFFECTS | ON | A SYNC | B SYNC | |
| FX 4th | CHARACTER | DISTORT | VINTAGE | PAN | VOLUME |
| GLO | VOICE | UNISON | KEY MODE | GLIDE | RATE |
| GLO 2nd | GLIDE / PLAY | MODE | BEND | VEL CURVE | VOICES |
| GLO 3rd | SETUP (shows the CPU load) | CLOCK | MIDI CH | LOCAL | CORES |
| ARP | ARPEGGIATOR (each press: off, on, hold) | MODE | OCT | DIVIDE | BPM |

P1 and P2 are named for the effect chosen: TIME / FDBK for the delays, RATE / DEPTH for chorus, phasers and flangers,
FREQ / TRACK for the ring modulator, TIME / EARLY for hall, room and plate, DECAY / TONE for the spring. WAVE is
written as its waves: T(ri), S(aw), P(ulse), R(everse saw), for example S+P.

PRESET or SELECT: next/previous patch. ALGO: ten patches at a time. SAVE: store the patch (lit when edited).
OCT-/OCT+: octave. REC held a second: Bluetooth on/off. 128 patches, 000 to 127; 000 to 065 are factory patches made
for this firmware (see PRESETS.md), the rest start as INIT.

## The mod matrix

SEQ's third page. SLOT picks one of the four routes; SOURCE, DEST and AMOUNT (bipolar, 0 is none) set it. The title
line counts the routes in use. The routes are saved with the patch and go in its SysEx dump; a patch saved before the
matrix existed has all four OFF.

- Sources: LFO (bipolar, after its shape), FIL ENV, AMP ENV, VELO, WHEEL, PRESSURE (the FM-1's keys, the hardest one
  held, or MIDI channel / poly pressure; the keys' pressure is also sent as channel pressure), KEY (the note, -1 at
  C-1 .. +1 at C9, 0 at middle C), RANDOM (a new value each note), BEND.
- Destinations, per voice: PITCH (all three oscillators), PITCH 1 / 2 / 3 (+-2 octaves at full amount, on a square law
  so small amounts are fine), PW 1 / 2 / 3, LEVEL 1 / 2 / 3, NOISE, CUTOFF (+-8 octaves), RES, FB/DRV, AMP, PAN.
- Destinations shared by every voice: LFO RATE (+-4 octaves), LFO AMT, FXA MIX, FXB MIX. A per-voice source (an
  envelope, velocity, key, random) there is the newest note's.
- Routes add: two on one destination sum. Everything runs at the control rate (0.73 ms); OSC 3 at audio rate is the
  poly mod's.

## The boot screen

A striped triangle in 70s sunset colours (a trigon), six small ones for the six voices, the name and a little
keyboard between walnut cheeks (`app_splash`, app.c). The same picture at twice the size is `card.png`, the
firmware's picture for firmware hubs (fm1.toml screenshot).

## The second core

With CORES at 2 (the default) the FM-1's second core renders voices 4 to 6. If it fails, the firmware carries on with
one core until it is switched off (`cpu2_fallback`). CORES 1 keeps everything on the first core; VOICES lowers the
voice count if the load is too high. The SETUP page shows the load.

## MIDI

In (USB, Bluetooth, TRS) and out (USB, Bluetooth):

- Notes, pitch bend, mod wheel, sustain, CC 74 (brightness, added to the cutoff), CC 120/123, program change 0..127.
- The Trigon-6's controller numbers from its MIDI implementation, for the parameters it has a CC for: 3 BPM, 5 glide
  rate, 9 distortion, 12 volume, 40/41 amp env amount and velocity, 43-46 amp ADSR, 47 filter env amount, 50-53
  filter ADSR, 58-60 and 62 arpeggiator, 65 glide, 67-73 OSC 1, 75-81 OSC 2, 82-89 OSC 3 (the wave switches set
  WAVE's bits), 102-107 cutoff, resonance, key, velocity, drive, 2-pole, 122 local control. Turning a knob sends its CC.
- MIDI clock, Start, Stop (arpeggiator, LFO and delay sync).
- SysEx over USB, TRI-6 FM-1's own (non-commercial ID 7D): `F0 7D 54 36 01 pp <224 nibbles> F7` a patch into slot pp,
  `02 <224 nibbles>` into the edit buffer, `03 pp` asks for slot pp (answered with 01).

## Layout

    app/tri6.c, tri6.h   the engine (pure C, float; runs on the FM-1 and on a computer)
    app/app.c, app.h     the FM-1 app: pages, knobs, patches, MIDI
    app/factory.h        the factory bank (from tools/gen_factory.py)
    app/fm1.toml         build settings
    app/tests/           host simulator scripts (basic.txt and features.txt check; demo.txt renders every factory patch)
    host/render.c        a standalone engine renderer for quick listening tests

## Building

TRI-6 FM-1 builds with [fm1-base](https://github.com/charlesvestal/fm1-base) at commit `2bb8e74`, checked out beside
this repository with its toolchain set up (fm1-base's README: `tools/setup_toolchain.sh`; Docker on a Mac). From the
folder that holds both `fm1-base/` and `tri6-fm1/`:

    sh tri6-fm1/build.sh host     # the host simulator and its checks
    sh tri6-fm1/build.sh          # the firmware: tri6-fm1/app/build/tri6fm1.fwsc

A release: `python3 fm1-base/tools/build.py tri6-fm1/app --release X.Y` (identity FM-1_62 and XXYYZZ).

## Playing it in the browser

[fergroan-sketch.github.io/tri6-fm1/play/](https://fergroan-sketch.github.io/tri6-fm1/play/) runs the released
package unchanged on [fm1-emu](https://joakimeriksson.github.io/fm1-emu/), Joakim Eriksson's FM-1 emulator: no FM-1
needed. The site (front page, web installer, player) is the gh-pages branch, made by
`sh tri6-fm1/tools/make_pages.sh X.Y` after a release build.

## Installing

The `.fwsc` is attached to each [release](https://github.com/fergroan-sketch/tri6-fm1/releases). Back up your FM-1 first. Installing TRI-6 FM-1 over JUNO FM-1 replaces JUNO's saved patches (both keep them in the
same place in the flash). fm1-base's web installer (Chrome or Edge, USB cable) installs a `.fwsc` and can put the stock
M-VAVE firmware back. Do not unplug during an install.
