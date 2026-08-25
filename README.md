# PSPATTERN

A music tracker for the Sony PSP.

You build a song from patterns of notes and the PSP generates the sound.
Eight channels, a sampler, a four engine synthesiser, a drum kit that is
generated at load time, a shared delay and reverb, and MIDI in and out
through the PSP-MIDI adapter. No sample files are required.

PSPATTERN is a fork of [LittleGPTracker](https://github.com/Mdashdotdashn/LittleGPTracker)
by Marc Nostromo, released under the GNU General Public License version 3.

* Website and user guide: [pspattern.hobbychop.com](https://pspattern.hobbychop.com)
* Shop: [hobbychop.com](https://hobbychop.com)

## Features

* Eight channels, each running a sampler, a synth or MIDI out
* Four synth engines: a single oscillator, phase distortion, a virtual
  analogue stack with hard sync and ring modulation, and four operator FM
* A drum kit synthesised at load time, so it costs no disk space and can
  be redistributed freely
* Per step velocity, written in decimal rather than hexadecimal
* Tables: sixteen rows of automation running under a note at tick rate
* A tempo synced delay and a reverb, on sends shared by all channels
* Arpeggios with a speed control, so the classic every tick trill is one
  setting rather than the only option
* MIDI notes, controllers, pitch bend, program change and clock out;
  controller mapping and external clock follow in
* Stereo and per channel rendering to wav

## Requirements

A PSP running custom firmware. Tested on 6.61 PRO-C.

The PSP-MIDI adapter is needed only for MIDI. Everything else works
without it.

## Installing

Copy the `PSPATTERN` folder from a release to `ms0:/PSP/GAME/` on the
memory stick and launch it from the PSP game menu.

## Building

Building for the PSP needs the [PSPDEV toolchain](https://pspdev.github.io/).
With that installed:

```
./build_psp.sh
```

The result is `dist/PSP/EBOOT.PBP`. `usbmidi.prx` is not built by this
tree; it belongs to the PSP-MIDI adapter and is dropped in before
packaging.

This is a PSP release. LittleGPTracker's ports for other machines are
not included, because none of them is needed to build this and carrying
sixteen of them would only obscure what is.

## Documentation

`MANUAL.txt` in the repository root is the full user guide: buttons,
every screen, all four synth engines, the command reference and the
known issues. The same guide is on the web at
[pspattern.hobbychop.com](https://pspattern.hobbychop.com), with
screenshots.

The PSP-MIDI adapter, and the other PSP instruments it works with, are
at [hobbychop.com](https://hobbychop.com).

## Licence

GPLv3. See `LICENSE`.

This is a modified version of LittleGPTracker. The original authors are
not responsible for it. It derives from the work of Marc Nostromo, with
feature work from the djdiskmachine fork.

`usbmidi.prx` is a separate work, the kernel driver for the PSP-MIDI
adapter. It is not part of this program and communicates with it only
through the system call interface. It is distributed alongside for
convenience.

The drum kit and the demo songs are original work and may be
redistributed.
