# Ravel

Ravel is a custom firmware for the M-VAVE FM-1. It turns the FM-1 into a pocket groovebox for sound design. It has 8 tracks that synthesize, catch what they play onto a shared tape, and cut it up and rebuild it, and the tracks can play, excite and reshape each other.

<p align="center">
  <img src="docs/signal-flow.svg" alt="Signal flow: synth, strike and tape tracks feed the mixer and buses; the mixer feeds the capture tape, which plays back into the tape tracks; an influence band connects every track to every other" width="760">
</p>

**Status: early, and playable.** The firmware builds for the FM-1 and runs in a desktop simulator and a browser emulator. It has one synth track (T1), one tape track (T7) with a fixed-point port of Mutable Instruments Beads, and a mixer. It has not been run on an FM-1 yet.

## What it is

- **Synth tracks** (T1 to T4): virtual analog with supersaw, phase distortion, FM, physical modelling and formant engines.
- **Strike tracks** (T5, T6): percussion synthesis. An exciter (click, noise, metallic noise, FM burst or another track's audio) strikes ping filters, modal bodies, a struck disc or coupled strings, followed by ring mod and fold.
- **Tape tracks** (T7, T8): no oscillator. They play a shared 192 KB capture tape with MLR-style playheads, through the Beads grain engine, or as slices played from the sequencer.
- **Influence:** an 8 × 8 grid where any track can modulate, trigger, excite, jump the tape of, or clock any other track.
- **Envelope-first articulation:** onset, continuant and termination states drive level, filter and more from one gesture, plus a library of envelope shapes steps can recall.
- **Sequencer:** parameter locks, microtiming, ratchets, a graph mode where cords set the playhead's path, René-style clocking, and a two-oscillator master clock.
- **Song building:** patterns, scenes, clip launching and an arrangement.

## What's here so far

| Path | What |
| --- | --- |
| `firmware/src/app/` | Ravel itself: the engine (`engine.c`), the screen and controls (`ui.c`) |
| `firmware/src/platform/`, `firmware/src/*.c`, `firmware/hal/`, `firmware/loader/` | The FM-1 platform layer, update loader and drivers, from X0X (see Credits) |
| `core/beads/` | Beads' grain and delay engine, rewritten in integer C for the FM-1 (MIT, see `LICENSE-beads`) |
| `host/` | The simulator: the whole app on a desktop, driven by scripts, writing screenshots and WAV files |
| `web/emu/` | The browser emulator: the same code compiled to WebAssembly, on a drawn FM-1 panel |
| `tests/beads/` | Renders 13 test scenarios through the original float Beads and the port, and compares them |
| `tests/scenarios/` | Simulator scripts that play the firmware and check what it does |
| `tools/` | Firmware build (`build.py`), toolchain download, packaging and install tools, table and font generators |
| `docs/` | The signal-flow diagram |

### What plays now

- **T1, synth:** four-voice virtual analog. A saw-to-square oscillator with detune, sub and noise, drive, and a lowpass filter. Articulation states (onset, continuant, termination) shape each note.
- **T7, tape:** Beads on a 192 KB tape that records T1 or the master mix. 2.2 seconds clean (16-bit), longer at lower quality. **REC** catches the tape so the grains keep playing what it holds.
- **Mixer:** level and pan for T1 and T7, master drive.
- **Screen:** the track list with meters, the oscillator scope, the filter's response, the articulation path, and the tape's waveform with its grains moving over it.
- **Saving:** **SAVE** keeps the sound in flash; it comes back on the next start.

### The Beads port

- **Coverage:** all of Beads' grain mode (32 grains, Time, Size, Shape, Pitch, Density with its regular, tapped, random and clocked modes, Random, freeze, feedback) and its delay mode (synced and free multitap delay, frozen slices). The tape can be 8-bit µ-law, 12-bit or 16-bit, mono or stereo.
- **Left out:** the reverb, wow and flutter, and the wavetable synth mode, which this firmware doesn't use.
- **Maths:** integers only. No floating point and no C library, so it builds for any 32-bit embedded target as it is.
- **Memory:** about 21 KB of code and 6 KB of tables in flash, 3 KB of state, plus the tape buffer.
- **Accuracy:** checked against the original, unchanged Beads code on every scenario. Level, loudness envelope and brightness all match; see `tests/beads/compare.py` for the thresholds.
- **Cost:** about 600 to 1,600 instructions per sample on typical settings (measured on x86). A dense cloud of 32 overlapping grains costs about 6,000, which is the next thing to optimize.

## Building and running

The simulator (any Unix with a C compiler and Python with Pillow):

```
sh host/build_host.sh
./build/host/ravel_host tests/scenarios/tour.rvl build/out    # screenshots and audio in build/out
```

The browser emulator (clang with the wasm32 target and wasm-ld; no emscripten):

```
sh web/emu/build.sh          # web/emu/ravel.wasm
node web/emu/test.mjs        # smoke test
python3 web/emu/bundle.py    # build/web/ravel-emulator.html, one file to open in a browser
```

The firmware (JieLi's clang for the AC791N):

```
sh tools/get_toolchain.sh ~/.jieli
JIELI_TOOLCHAIN=~/.jieli/toolchain python3 tools/build.py
```

`build.py` builds the app (`build/ravel.bin`) and the update loader, and checks them: image size, RAM and pool use, no double-precision maths, hardware registers touched only in `hal/`. Making the installable package needs JieLi's AC79 SDK as well (`AC79_SDK=...`); without it the build stops before packaging.

The Beads comparison:

```
cd tests/beads
make fetch    # clones the original Beads and stmlib for the reference
make test
```

The renders land in `tests/beads/out/` as WAV files: `<scenario>_ref.wav` is the original and `<scenario>_port.wav` the port.

## Hardware

The FM-1 has a JieLi AC791N with two cores and a single-precision FPU (float maths is fast; double is not available), 96 KB of main RAM, a 336 KB memory pool and 29 KB of low SRAM, 1 MB of flash, a 240 × 240 screen, 27 keys, 14 buttons and 7 endless encoders. Audio runs at 44,118 Hz. The current build uses 96 KB of flash for the app, 34 KB of RAM and 262 KB of the pool (the tape is most of that).

## Licence

GPL-3.0 (see `LICENSE`), as required by the Felucca-family code it builds on. The Beads port in `core/beads/` keeps Mutable Instruments' MIT licence (see `core/beads/LICENSE-beads`). Files taken from other projects keep their original copyright notices.

## Credits

- Mutable Instruments (Émilie Gillet) for Beads.
- [X0X](https://github.com/charlesvestal/fm1-x0x), a Felucca fork, for the platform layer, hardware drivers, update loader, safe mode, build and packaging tools, and the simulator's design.
- The authors of Felucca, JIANT, SLOOP and sloopDX for opening up the FM-1.
