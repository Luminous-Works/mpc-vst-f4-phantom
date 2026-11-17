# F4 Phantom — 8-voice subtractive synthesizer

**Lumina Aerospace LLC** — Retro-Future Mission Control aesthetic.
Roster position: hangar mate to GR-4 Ground.

## Lineage

The name is the McDonnell F-4 Phantom II: a tandem two-seat, twin-engined, all-weather, long-range supersonic interceptor and fighter-bomber, in Navy service from 1961 and later flown by the Marine Corps and the Air Force. 5,195 were built between 1958 and 1981, the most-produced American supersonic military aircraft, with a top speed above Mach 2.2 and more than 18,000 lb of weapons on nine external hardpoints. Like other interceptors of its day it was designed without an internal cannon; some later models gained an internal M61 Vulcan rotary cannon.

F4 Phantom is the **hangar mate** of GR-4 Ground, and it is subtractive on purpose. The VULCAN section is the late-model gun: a six-barrel rotary gate that spools up after every note, in the spirit of the F-4E's internal M61 (Rate is in rounds per minute: GE's 1946 Project Vulcan contract asked for 7,200 rpm, the 1949 T45 ground-fired at 2,500 rpm and reached 4,000 rpm by 1950, and the M61 settled at 6,000 rpm).

## What it is

F4 Phantom is an 8-voice subtractive synthesizer for the Akai MPC (MPC OS 3.10.1+, External plugins enabled). Two PolyBLEP oscillators feed an SVF filter, then dual ADSRs shape amplitude and filter, a tempo-syncable LFO modulates pitch/filter, and a master volume stage sums to stereo.

Signal path per voice:
```
osc1 + osc2 (PolyBLEP, detune, octave)
  -> SVF filter (cutoff + env + LFO + key track, drive)
  -> amp ADSR * velocity
  -> summed -> master volume -> L + R (mono-to-stereo)
```

## Parameters

| Param | Range | Default | Unit | Page |
|---|---|---|---|---|
| Volume | -40 .. 6 | -8 | dB | OSC (param 0 — host-nudged) |
| Osc1 Wave | Sine/Triangle/Saw/Square | Saw | — | OSC |
| Osc1 Octave | -2 .. 2 | 0 | — | OSC |
| Osc1 Level | 0 .. 100 | 100 | % | OSC |
| Osc2 Wave | Sine/Triangle/Saw/Square | Saw | — | OSC |
| Osc2 Octave | -2 .. 2 | 0 | — | OSC |
| Osc2 Semi | -12 .. 12 | 0 | st | OSC |
| Osc2 Detune | -50 .. 50 | 8 | ct | OSC |
| Osc2 Level | 0 .. 100 | 60 | % | OSC |
| Pulse Width | 5 .. 95 | 50 | % | OSC |
| Glide | 0 .. 1000 | 0 | ms | OSC |
| Bend Range | 0 .. 24 | 2 | st | OSC |
| Cutoff | 20 .. 18000 | 4000 | Hz | FILTER |
| Resonance | 0 .. 100 | 20 | % | FILTER |
| Filter Mode | LP/BP/HP/Notch | LP | — | FILTER |
| Drive | 0 .. 100 | 0 | % | FILTER |
| Env Amount | -100 .. 100 | 40 | % | FILTER |
| Key Track | 0 .. 100 | 50 | % | FILTER |
| Filt Attack/Decay/Release | 1 .. 5000 | 5/300/300 | ms | FILTER |
| Filt Sustain | 0 .. 100 | 20 | % | FILTER |
| Amp Attack/Decay/Release | 1 .. 5000 | 5/200/200 | ms | AMP |
| Amp Sustain | 0 .. 100 | 80 | % | AMP |
| Velocity | 0 .. 100 | 60 | % | AMP |
| LFO Shape | LFO shape list | Sine | — | LFO |
| LFO Rate | 0.05 .. 30 | 5 | Hz | LFO |
| LFO Sync | Off/On | Off | — | LFO |
| LFO Div | division list | Quarter | — | LFO |
| LFO>Pitch | 0 .. 12 | 0 | st | LFO |
| LFO>Filter | -100 .. 100 | 0 | % | LFO |
| Wheel Vib | 0 .. 12 | 0.5 | st | LFO |
| Sub Level | 0 .. 100 | 0 | % | OSC (a sine one octave under Osc1) |
| Drift | 0 .. 100 | 0 | % | OSC (slow random per-voice detune, up to 8 ct) |
| Chorus Mix | 0 .. 100 | 0 | % | FX |
| Chorus Depth | 0 .. 100 | 50 | % | FX |
| Chorus Rate | 0.1 .. 4 | 0.6 | Hz | FX |
| Vulcan Depth | 0 .. 100 | 0 | % | FX (rotary gate; 0 = off) |
| Vulcan Rate | 240 .. 7200 | 3000 | rpm | FX (rounds per minute, the gun's own unit) |
| Vulcan Spin | 0 .. 2000 | 400 | ms | FX (spool-up time after each note) |
| Vulcan Spread | 0 .. 100 | 35 | % | FX (how unevenly the six barrels fire) |

Parameter 0 is master volume. The MPC host nudges parameter 0 on every load, so it is deliberately side-effect-free.

## Built-in programs

- **Init Saw** — factory defaults.
- **Fat Bass** — saw + square, octave-down osc2, detuned, resonant LPF, glide, a little sub.
- **Soft Pad** — triangle + saw, detuned, slow filter/env, subtle LFO on filter, drift and chorus.
- **Pluck** — square + saw, high key track, short decay, velocity-sensitive.
- **Phantom Lead** — saw + square, glide, mod-wheel vibrato, a touch of chorus.
- **Neon Strings** — detuned saws, slow attack, drift, wide chorus.
- **Deep Sub** — sine plus sub, closed filter, a little glide.
- **Glass Keys** — triangle + octave-up sine, bell-ish decay, chorus.
- **Retro Brass** — detuned saws, filter envelope swell, drive.
- **Ghost Wobble** — tempo-synced (1/8) triangle LFO on a resonant filter, sub-heavy.
- **Wide Saws** — two saws 22 ct apart, drift, deep slow chorus.
- **Vulcan Rotary** — saw + square, the rotary gate at 6,000 rpm with a 700 ms spool-up.
- **F-4E Gun Run** — low saws and sub, the gate at 1,320 rpm with a 1.3 s spool-up: the gun winding up.
- **Acid Line** — saw, resonant low-pass, hard envelope, glide.

## UI

Space Black theme matching the Lumina Aerospace style bible:
- Chassis `#0A0A0A`, accent Telemetry Neon Blue `#00F2FF`, text `#C8C8C8`, parameter names `#8C97A8` (v1.1: brighter, they were too dim to read across a room).
- Five pages (OSC / FILTER / AMP / LFO / FX), each a block of four controls so the Q-Link focus rectangle encloses them cleanly. FX holds the chorus on its first row and VULCAN on its second.
- v1.1: faders are the Lumina **Electron Tube Slider** as film strips (`assets/fader_h_uni.png`, `fader_h_bi.png`, made by `tools/make_es_fader.py`): a glass tube with a glowing filament fill and a large bead as the touch handle. The MPC is a touchscreen, so the whole 248 x 36 strip takes the drag.
- Knobs use vertical drag. Switches, faders, sliders with menu overlays.

### Pizzazz: the logo and the ghost dial (v1.1)

`skins/` is a hand-tuned copy of the generated screen (`tools/make_skin.py` builds it; the pipeline is in the script's header).
The decal sits faintly behind every page, and **Cutoff** is the Phantom itself: as you open the filter its glow brightens, its
eyes slide from left to right and it rises a little inside the cyan arc. It reacts because **you** turn the dial; the MPC screen
format has no meter or animation channel, so nothing is animated by the plugin. If ui.json changes, rerun the pipeline.

## CPU

Aiming for a few percent of one ARM core per instance. No per-sample transcendental functions in the inner loop; expensive math (`tan`, `pow`) runs at control rate (every 16 samples). `process()` performs no allocation.

## Files

- `plugin.json` — manifest (name/vendor/uid must match the compiled `PluginInfo`).
- `src/F4 Phantom.cpp` — DSP implementation against `framework/mpc_plugin.h` + `framework/dsp/*.h`.
- `ui.json` — touch UI pages + Space Black style block (read by `tools/gen_ui.py`).

## Build & install

Requires MPC OS 3.10.1 or later, and **External plugins** enabled on the device (Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support).

```sh
# From the SDK root:
python tools/build.py "plugins/F4 Phantom"
python tools\validate.py "build\Lumina Aerospace LLC - VST - F4 Phantom"
```

The output folder `build/Lumina Aerospace LLC - VST - F4 Phantom/` contains `plugin.json`, `version.xml`, `VST/f4-phantom-arm32.so`, `VST/f4-phantom-arm64.so`, and `Plugin Skins/`. Copy that folder into a folder named `VST2 Plugins` on your MPC's drive.

To test on this computer first:
```sh
python tools\host_sim "plugins/F4 Phantom\build\native\f4-phantom.so" test --notes 60,64,67 --wav out.wav
```

## License

MIT. DSP building blocks (`framework/dsp/*) are part of the MPC Plugin SDK.

## Notes

- `uid` ("F4PH") is permanent once users have projects — do not change it.
- Append new parameters at the end; never reorder or remove existing ones.
- 8 voices (0 inputs, 2 outputs). Stereo comes from the chorus; with Chorus Mix at 0 the output is mono on both channels.
- A soft limiter on the master rounds off chords on hot patches instead of hard-clipping.
- Version 1.1.0 added Sub Level, Drift, Chorus and VULCAN (all appended) and the 14-sound bank; indices 0-33 are unchanged from 1.0.0.

## Contact

owner@luminousworksllc.com (Lumina Aerospace LLC). Reports of what works and what does not on your MPC are welcome: device, MPC OS version, what happened.
