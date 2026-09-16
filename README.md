# PSPATTERN

A music tracker for the Sony PSP.

You build a song from patterns of notes and the PSP generates the sound.
Eight channels, a sampler, a six engine synthesiser, a drum kit built
into the program, a delay and a reverb shared by all channels, a phaser,
a chorus and a distortion on every channel, and MIDI in and out through
the PSP-MIDI adapter. No sample files are required.

PSPATTERN is a fork of [LittleGPTracker](https://github.com/Mdashdotdashn/LittleGPTracker)
by Marc Nostromo, released under the GNU General Public License version 3.

* Website and user guide: [pspattern.hobbychop.com](https://pspattern.hobbychop.com)
* Releases: [github.com/HobbyChop/PSPATTERN/releases](https://github.com/HobbyChop/PSPATTERN/releases)
* Shop: [hobbychop.com](https://hobbychop.com)

## Features

* Eight channels, each playing a sampler, a synth or MIDI out. 128
  sampler, 32 synth and 16 MIDI instruments.
* Six synth engines: TONE, one oscillator with a sub and noise; PDX,
  phase distortion; VAX, up to seven detuned oscillators with hard sync
  and ring modulation; FM, four operators and eight algorithms; VOX, a
  formant engine; HIVE, up to five wavetable voices from one note, so a
  chord is one step.
* A drum kit built into the program, a soft and a hard variant of each
  drum, so a new project has drums with nothing on the card.
* The sampler plays wav files of any common format, soundfont presets
  with their key splits and loop points, or the kit. Loop modes, slicing,
  drive and bit crush, a filter, a feedback stage and an amplitude
  envelope. Importing can fold a file to mono or halve its rate on the
  way in to save memory.
* Two command columns and a velocity column on every step, tables of
  automation running under a note at tick rate, grooves, per step
  probability and vibrato, and arpeggios with a speed control.
* Live mode with chains queued per channel, and bookmarked song rows.
* A tempo synced delay and a reverb on shared sends, with freeze, duck,
  gate, low cut, width, a tone in the delay's feedback, and a drive and
  compressor on the wet bus.
* A ten band master EQ, a clipper with several modes, metering, a scope
  and a live spectrum.
* MIDI out: notes, controllers, pitch bend, program change, channel
  pressure and clock. MIDI in: a keyboard or a pad plays the instruments
  on four voices, while the song runs too, with a kit mode that puts one
  instrument on each key. Controller mapping and clock follow as well.
* Renders the mix to a wav file while the song plays, tails included.
* Autosave, backups with recovery, a low power rest on the power switch,
  and a settings screen that edits the configuration on the device.

## Requirements

A PSP running custom firmware. Tested on 6.61 PRO-C. It also runs on a
PS Vita through its PSP emulator, without the second processor.

The PSP-MIDI adapter is needed only for MIDI. Everything else works
without it.

## Installing

Copy the `PSPATTERN` folder from a release to `ms0:/PSP/GAME/` on the
memory stick and launch it from the PSP game menu.

## Building

Building for the PSP needs the [PSPDEV toolchain](https://pspdev.github.io/)
and a host C++ compiler, `g++` by default. With those installed:

```
./build_psp.sh
```

The result is `dist/PSP/EBOOT.PBP`. The Makefile first builds a small
host program that bakes the drum kit into a blob linked into the
executable, which is why the host compiler is needed. `usbmidi.prx` is
not built by this tree; it belongs to the PSP-MIDI adapter and is
dropped in before packaging.

### The Media Engine library

The default build runs the reverb and delay on the PSP's second core,
the Media Engine, and draws the meters and scope on the GPU. The audio
side links a small vendored library, mcidclan's me-core, whose source
sits under `third_party/me-core/`. Build and install it into your PSPDEV
toolchain once before building PSPATTERN: its `CMakeLists.txt` installs
the `me-core`, `me-core-lib` and `me-core-mapper` archives and their
headers where the link step looks for them. See
`third_party/me-core/README.md` and the repository `NOTICE`.

To build without the second core, drop `-DPSP_ME_OFFLOAD` and
`-DPSP_GU_DISPLAY` from `projects/Makefile.PSP`; both paths stay guarded
and everything falls back to the main core.

This is a PSP release. LittleGPTracker's ports for other machines are
not included, because none of them is needed to build this and carrying
sixteen of them would only obscure what is.

## Documentation

`MANUAL.txt` in the repository root is the full user guide: buttons,
every screen, the six synth engines, the command reference and the
known issues. The same guide is on the web at
[pspattern.hobbychop.com](https://pspattern.hobbychop.com), with
screenshots.

The PSP-MIDI adapter, and the other PSP instruments it works with, are
at [hobbychop.com](https://hobbychop.com).

## License

GPLv3. See `LICENSE`.

This is a modified version of LittleGPTracker. The original authors are
not responsible for it. It derives from the work of Marc Nostromo, with
feature work from the djdiskmachine fork.

The Media Engine audio offload is original work by HobbyChop. The send
effects, a feedback delay and an FDN reverb, run on the PSP's second
Allegrex core; it builds on mcidclan's `psp-media-engine-custom-core`
library (MIT), vendored under `third_party/me-core/`. See the `NOTICE`
file for the full attribution.

`usbmidi.prx` is a separate work, the kernel driver for the PSP-MIDI
adapter. It is not part of this program and communicates with it only
through the system call interface. It is distributed alongside for
convenience.

The drum kit is original work and may be redistributed.
