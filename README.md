# Ravel

Ravel is a custom firmware for the M-VAVE FM-1. It turns the FM-1 into a pocket groovebox for sound design. It has 8 tracks that synthesize, catch what they play onto a shared tape, and cut it up and rebuild it, and the tracks can play, excite and reshape each other.

<p align="center">
  <img src="docs/signal-flow.svg" alt="Signal flow: synth, strike and tape tracks feed the mixer and buses; the mixer feeds the capture tape, which plays back into the tape tracks; an influence band connects every track to every other" width="760">
</p>

**Status: playable in the emulator; the FM-1 build does not fit yet.** The whole instrument runs in the browser emulator and the desktop simulator, from the same C sources the firmware is built from. The FM-1 build (on M-VAVE's official FM1 Open SDK) compiles and links, but the app image is about 20 KB larger than the FM-1's app area, so there is no installable package yet. It has not been run on an FM-1.

## What it is

- **Synth tracks** (T1 to T4), six engines: **VA** (saw to pulse, supersaw, sync, sub, noise, through-zero FM), **PD** (Casio-style phase distortion, 8 waves, foam sidebands, ring), **FM** (4 operators, 12 algorithms, a wave per operator), **PHYS** (string, bowed, blown, membrane), **FORMANT** (vowel filter bank or FOF grains), **PARTICLE** (pulsar emitters that pull into sync).
- **Strike tracks** (T5, T6): an exciter (click, noise, metal, FM burst, dust, rain, bow, breath, or the audio input) strikes a body (ping filters, modal bars, a struck disc or coupled strings), with a transient layer, ring mod and a pitch drop.
- **Tape tracks** (T7, T8): no oscillator. They play the shared 120 KB capture tape as **MLR** playheads (four rows, jumps on the keys, rates from -2× to 4×), through the **Beads** grain engine, or as **slices** found in what was caught.
- **Capture tape:** records the master, one track, the buses or the audio input. **Catch** keeps what is on it, **resample** records a bar (or 2 or 4) onto row 1 on the downbeat, erosion and "dirty" half-rate µ-law for more time.
- **Influence:** an 8 × 8 grid where any track can modulate, trigger, excite, ring, jump the tape of, or clock any other.
- **Articulation:** onset, continuant and termination stages with curve, overshoot, a loop, a tail and **Give** (a spring that lets the level lag and bounce), two destinations, and a 256-shape envelope library that steps can recall.
- **Sequencer:** each track runs in **STEPS** or **GRAPH** mode. Steps: length, rate and direction, parameter locks, microtiming, ratchets with curves, conditions, chance. Graph (from Silt): cords set where a playhead goes next, two playheads walk the same graph, each playing notes, only the gate, or only the pitch (so rhythm and melody can take different paths), head 2 can play another track, and each head has a function generator (rise, fall, arch, decay, swell, or an LFO or dust) that moves its speed, times or plus the clock. Plus density arcs, a sieve, scale quantizing, René-style clocking from LFOs or dust, and a two-oscillator master clock.
- **Modulation:** two LFOs, dust, an 8-slot matrix and 4 macros with 8 destinations each (curves and windows).
- **Buses:** a stereo delay (spread, wobble, freeze), a space reverb (motion, diffusion, low cut), a feedback bus (filter, comb, smear, shift, fold or crush, returning to the master or into a track), a filter pair (low, band or high pass each; series, parallel or feedback, with two LFOs), an 8-band vocoder (its body is dust, drops, noise, the tape or a track; attack, release, shift, width), and master drive with microcuts and punch-in effects on the keys. Each bus page shows the current track's send into it.
- **Song:** patterns A and B per track, 16 scenes, a 32-row arrangement, mute and solo, tap tempo, 8 project slots.

## Try it

**In a browser:** open `ravel-emulator.html` (one file; build it with the steps below, or take it from the CI artifact). Sound starts on the first click or key. The page is an FM-1 on screen: MASTER, SELECT, PRESETS and ALGORITHM on the left, the screen, KNOB 1 to 4, the two button rows and the keys, with a side panel for the project, transport, tracks, sounds, audio input and the controls guide. Computer keys A to ; play notes, [ and ] change the octave, Space plays, 1 to 8 pick the track, arrows turn SELECT. Right-click (or shift-click) a button to hold it.

**How the panel is laid out:** each button opens a section (HOME, VOICE, ARTICULATION, MOD, BUSES, SCALE, TAPE, SEQ, SONG, SAVE); SELECT walks its pages, ALGORITHM picks the track, PRESETS browses what the page holds, KNOB 1 to 4 set the four values shown. Holding a button gives its layer: SEQ turns the keys into steps (hold a step and turn a knob to lock), LFO puts the macros on the knobs, FX puts punch-in effects on the keys, ARP the tape, GLO mute and solo, SAVE stores scenes, SCL sets the root, REC + OCT- catches and REC + OCT+ resamples.

## What's here

| Path | What |
| --- | --- |
| `firmware/app/` | Ravel: the engine (`rv_engine.c`), synth engines (`rv_synth.c`), strike (`rv_strike.c`), tape (`rv_tape.c`), buses (`rv_bus.c`), sequencer (`rv_seq.c`), modulation and clock (`rv_mod.c`), parameters, presets, projects, and the interface (`rv_ui.c`, `rv_gfx.c`). Portable C99, single-precision float, no C library. |
| `firmware/board/` | The FM-1 side: `ravel_board.c` (Ravel's platform on the SDK's API, its hooks, the audio block) and board code from FM1 Open (audio interrupt, panel, screen, input wiring) |
| `core/beads/` | Beads' grain engine rewritten in integer C (MIT, see `core/beads/LICENSE-beads`) |
| `host/` | The simulator: the whole app on a desktop, driven by scripts, writing screenshots and WAV files |
| `web/emu/` | The browser emulator: the same code compiled to WebAssembly, on a drawn FM-1 panel |
| `tests/beads/` | Renders 13 scenarios through the original Beads and the port, and compares them |
| `tests/scenarios/` | Simulator scripts that play the firmware and check it (`tour.rvl` visits every page) |
| `tools/` | The FM-1 build (`build_fm1.py`, `fm1pack.py`), SDK and toolchain download, font and table generators |

## Building

The simulator (any Unix with a C compiler, and Python with Pillow):

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

The FM-1 firmware, on Linux or macOS (JieLi's clang for the AC791N, and M-VAVE's FM1 Open SDK):

```
sh tools/get_toolchain.sh ~/.jieli
sh tools/get_sdk.sh                                         # build/fm1_open, at a fixed commit
JIELI_TOOLCHAIN=~/.jieli/toolchain python3 tools/build_fm1.py
```

`build_fm1.py` does what the SDK's own `Build.ps1` does: it checks the SDK against its manifest, compiles with the SDK's flags, links against the SDK's library and linker script, checks the heap, SRAM placement and entry point, and packages `build/fm1/Ravel-FM1.fwsc` with `tools/fm1pack.py` (a Python port of the SDK's `Package.cs`). The update identity is `FM-1_8960002`.

The Beads comparison:

```
cd tests/beads
make fetch    # clones the original Beads and stmlib for the reference
make test
```

## The FM-1 build

Ravel runs as the app on M-VAVE's FM1 Open SDK. The SDK's control task calls Ravel's hooks (`groove_screen_init`, `groove_screen_control`); the audio interrupt asks Ravel for 128 frames at a time; controls, LEDs, the screen, flash and USB audio go through the SDK's API. The SDK's library brings the RTOS, USB, Bluetooth and the update path.

- **Flash:** the SDK takes about 371 KB of the 454 KB app area, which leaves Ravel about 82 KB. Ravel is about 102 KB at `-Oz` with LTO, so it is **about 20 KB over**. The build stops with the size and keeps the ELF and map; the CI job reports it without failing.
- **RAM:** Ravel uses about 257 KB of main RAM (the 120 KB tape is most of it) and the cache RAM for the delay line, leaving the SDK the 48 KB heap it needs.
- **Not checked on hardware:** CPU load (the DSP at `-Oz` is slower than at `-O2`), the FPU state between the audio interrupt and the control task, and the package itself (`fm1pack.py` has not been compared byte for byte with `Package.cs` output).
- **Not in this version:** Bluetooth audio and MIDI (Bluetooth is switched off), MIDI over USB, and tape punch-in. USB audio input feeds the tape and the strike exciter; the output is sent to USB as well.
- **Beads on the FM-1:** grain mode only (no delay mode, which the delay bus covers), reading the shared tape. The emulator and simulator use the same options.

## Licence

GPL-3.0 only (see `LICENSE`). Ravel's firmware sources also carry a permission to link with M-VAVE's FM1 binary SDK, the same one M-VAVE gives for its own app code (see `LICENSE-EXCEPTION.md`); a firmware package must include `firmware/board/FM1-APP-LICENSE.md` and `firmware/board/FM1-THIRD_PARTY.md`. The Beads port keeps Mutable Instruments' MIT licence. Third-party code is listed in `THIRD_PARTY.md`.

## Credits

- Mutable Instruments (Émilie Gillet) for Beads.
- M-VAVE for [FM1 Open](https://github.com/m-vave/fm1_open): the SDK, its board code and its packager.
- [X0X](https://github.com/charlesvestal/fm1-x0x), a Felucca fork, for the simulator's design, its PNG writer and the toolchain script.
- The authors of Felucca, JIANT, SLOOP and sloopDX for opening up the FM-1.
