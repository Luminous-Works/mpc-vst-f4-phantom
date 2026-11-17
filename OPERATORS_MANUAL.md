# F4 PHANTOM — OPERATOR'S MANUAL

**LUMINA AEROSPACE LLC**  
**Unit: F4 Phantom**  
**Aesthetic: Retro-Future Mission Control**  
**Document: Operator's Manual v1.1.0**

---

## 1. SYSTEM CHECK

On initialization, each DSP module reports status line by line. Final line:

```
SYSTEM CHECK: NOMINAL. F4 PHANTOM READY FOR LIFTOFF.
```

If any module reports otherwise, see §10 (Offline — Re-calibrate).

---

## 2. UNIT OVERVIEW

### Lineage

The name is the McDonnell F-4 Phantom II: a tandem two-seat, twin-engined, all-weather, long-range supersonic interceptor and fighter-bomber, in Navy service from 1961 and later flown by the Marine Corps and the Air Force. 5,195 were built between 1958 and 1981, the most-produced American supersonic military aircraft, with a top speed above Mach 2.2 and more than 18,000 lb of weapons on nine external hardpoints. Like other interceptors of its day it was designed without an internal cannon; some later models gained an internal M61 Vulcan rotary cannon.

F4 Phantom is the **hangar mate** of GR-4 Ground, and it is subtractive on purpose. The VULCAN section is the late-model gun: a six-barrel rotary gate that spools up after every note, in the spirit of the F-4E's internal M61 (Rate is in rounds per minute: GE's 1946 Project Vulcan contract asked for 7,200 rpm, the 1949 T45 ground-fired at 2,500 rpm and reached 4,000 rpm by 1950, and the M61 settled at 6,000 rpm).


**F4 Phantom** is an 8-voice subtractive synthesizer unit for the Akai MPC (MPC OS 3.10.1 or later, External plugins enabled). Roster position: hangar mate to GR-4 Ground. Subtractive on purpose.

Signal path per voice:

```
OSC1 + OSC2 (PolyBLEP, detune, octave)
  -> SVF Filter (cutoff + env + LFO + key track, drive)
  -> AMP ADSR * velocity
  -> summed -> Master Volume -> L + R (mono-to-stereo)
```

Tempo-synced LFO modulates pitch and/or filter. Glide smooths note transitions linearly in pitch.

---

## 3. INSTALLATION

1. Copy the folder **`Lumina Aerospace LLC - VST - F4 Phantom/`** into a folder named **`VST2 Plugins`** on your MPC's drive.
2. On the MPC: Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support.
3. Rescan plugins. The unit appears under **Synth**.

**Requirements:** MPC OS 3.10.1+. External plugin support must be on. The unit will not load on older firmware.

---

## 4. PARAMETERS

Parameter 0 is Master Volume. The host nudges parameter 0 on every load; it is deliberately side-effect-free.

### OSC Page

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Volume | -40 .. 6 | -8 | dB | Master volume (param 0) |
| Osc1 Wave | Sine / Triangle / Saw / Square | Saw | — | PolyBLEP oscillator 1 |
| Osc1 Octave | -2 .. 2 | 0 | — | Osc 1 transpose |
| Osc1 Level | 0 .. 100 | 100 | % | Osc 1 gain |
| Osc2 Wave | Sine / Triangle / Saw / Square | Saw | — | PolyBLEP oscillator 2 |
| Osc2 Octave | -2 .. 2 | 0 | — | Osc 2 transpose |
| Osc2 Semi | -12 .. 12 | 0 | st | Osc 2 semitone offset |
| Osc2 Detune | -50 .. 50 | 8 | ct | Osc 2 fine detune |
| Osc2 Level | 0 .. 100 | 60 | % | Osc 2 gain |
| Pulse Width | 5 .. 95 | 50 | % | Square wave pulse width |
| Glide | 0 .. 1000 | 0 | ms | Portamento time, linear in pitch |
| Bend Range | 0 .. 24 | 2 | st | Pitch bend range |
| Sub Level | 0 .. 100 | 0 | % | A sine one octave under Osc1 |
| Drift | 0 .. 100 | 0 | % | Slow random per-voice detune (up to 8 ct) so stacked oscillators breathe |

### FILTER Page

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Cutoff | 20 .. 18000 | 4000 | Hz | SVF filter cutoff |
| Resonance | 0 .. 100 | 20 | % | Filter resonance |
| Filter Mode | LP / BP / HP / Notch | LP | — | Filter mode |
| Drive | 0 .. 100 | 0 | % | Filter saturation drive |
| Env Amount | -100 .. 100 | 40 | % | Filter envelope depth |
| Key Track | 0 .. 100 | 50 | % | Keyboard tracking around middle C |
| Filt Attack | 1 .. 5000 | 5 | ms | Filter envelope attack |
| Filt Decay | 1 .. 5000 | 300 | ms | Filter envelope decay |
| Filt Sustain | 0 .. 100 | 20 | % | Filter envelope sustain |
| Filt Release | 1 .. 5000 | 300 | ms | Filter envelope release |

### AMP Page

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Amp Attack | 1 .. 5000 | 5 | ms | Amplitude envelope attack |
| Amp Decay | 1 .. 5000 | 200 | ms | Amplitude envelope decay |
| Amp Sustain | 0 .. 100 | 80 | % | Amplitude envelope sustain |
| Amp Release | 1 .. 5000 | 200 | ms | Amplitude envelope release |
| Velocity | 0 .. 100 | 60 | % | Velocity sensitivity |

### LFO Page

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| LFO Shape | Sine / Triangle / Saw / Square | Sine | — | LFO waveform |
| LFO Rate | 0.05 .. 30 | 5 | Hz | LFO frequency (unsynced) |
| LFO Sync | Off / On | Off | — | Lock LFO to song position |
| LFO Div | Division list | Quarter | — | Tempo division (synced) |
| LFO>Pitch | 0 .. 12 | 0 | st | LFO pitch modulation depth |
| LFO>Filter | -100 .. 100 | 0 | % | LFO filter modulation depth |
| Wheel Vib | 0 .. 12 | 0.5 | st | Mod wheel vibrato depth |

### FX Page (v1.1)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Chorus Mix | 0 .. 100 | 0 | % | Stereo chorus on the master; also widens the output to true stereo |
| Chorus Depth | 0 .. 100 | 50 | % | Modulation depth |
| Chorus Rate | 0.1 .. 4 | 0.6 | Hz | Modulation rate |
| Vulcan Depth | 0 .. 100 | 0 | % | The rotary gate. 0 = off |
| Vulcan Rate | 240 .. 7200 | 3000 | rpm | Rounds per minute at full spin: 2,500 (T45, 1949), 4,000 (T45, 1950), 6,000 (M61), 7,200 (Project Vulcan, 1946) |
| Vulcan Spin | 0 .. 2000 | 400 | ms | Spool-up: the barrels start nearly still after each note and accelerate to Rate |
| Vulcan Spread | 0 .. 100 | 35 | % | How unevenly the six barrels fire; more spread, more ratchet |

**VULCAN** is a six-barrel rotary gate after the filter: one shot per barrel cycle, six barrels firing in a fixed rotating pattern, spooling up from rest on every note. At 6,000 rpm it is the cannon's buzz; at 500 to 1,500 rpm with a long spin it is a gun winding up.

Parameter IDs and order are permanent. Do not reorder or remove; append new parameters at the end only.

---

## 5. CONFIGURATIONS (PRESETS)

| Configuration | Description |
|---------------|-------------|
| Init Saw | factory defaults. |
| Fat Bass | saw + square, octave-down osc2, detuned, resonant LPF, glide, a little sub. |
| Soft Pad | triangle + saw, detuned, slow filter/env, subtle LFO on filter, drift and chorus. |
| Pluck | square + saw, high key track, short decay, velocity-sensitive. |
| Phantom Lead | saw + square, glide, mod-wheel vibrato, a touch of chorus. |
| Neon Strings | detuned saws, slow attack, drift, wide chorus. |
| Deep Sub | sine plus sub, closed filter, a little glide. |
| Glass Keys | triangle + octave-up sine, bell-ish decay, chorus. |
| Retro Brass | detuned saws, filter envelope swell, drive. |
| Ghost Wobble | tempo-synced (1/8) triangle LFO on a resonant filter, sub-heavy. |
| Wide Saws | two saws 22 ct apart, drift, deep slow chorus. |
| Vulcan Rotary | saw + square, the rotary gate at 6,000 rpm with a 700 ms spool-up. |
| F-4E Gun Run | low saws and sub, the gate at 1,320 rpm with a 1.3 s spool-up: the gun winding up. |
| Acid Line | saw, resonant low-pass, hard envelope, glide. |

Configurations are stored per-project on the MPC. Parameter ID permanence means old projects recall correctly even after unit updates.

---

## 6. MIDI & CONTROL

- **Note On / Note Off:** 8-voice polyphony with voice stealing.
- **Pitch Bend:** Range set by Bend Range parameter.
- **CC 64 (Sustain):** Sustains notes; releases on CC value < 64.
- **Mod Wheel:** Adds vibrato per Wheel Vib depth.
- **Aftertouch / Other CC:** Routed through the MIDI controller layer.

The unit cannot send MIDI to the MPC. It is an instrument that makes its own sound.

---

## 7. TELEMETRY VIEW

Toggle between:

- **State A:** Sleek modern GUI — panels, knobs, pitch trace, meters (default).
- **State B:** Raw Telemetry — scrolling ASCII terminal, real-time DSP math output.

Toggle via the TELEMETRY button in the header. State B displays per-voice oscillator frequency, filter cutoff, envelope values, and LFO phase in mission-control telemetry format.

---

## 8. STATUS INDICATORS

| State | Dot Color | Status Text |
|-------|-----------|-------------|
| Nominal | Sovereign Green | `NOMINAL` |
| Processing | Deep Amber | `SIGNAL ACTIVE` |
| Complete | Sovereign Green | `READY — ROUTED TO OUTPUT` |
| Clipping | Pulsing Amber | `SIGNAL UNSTABLE` |
| Offline/Stasis | Strobe Red-Grey | `OFFLINE — RE-CALIBRATE` |

---

## 9. METERING

Radar-style vertical bars, not vintage VU.

| Meter | Label | Function |
|-------|-------|----------|
| IN | Input level | Pre-filter signal |
| GR | Gain reduction | Filter resonance intensity |
| OUT | Output level | Post-envelope signal |
| DEV | Deviation | Cents deviation from target |

Hot detection: bar fill switches to amber/orange gradient above 75% (IN) or 80% (OUT).

---

## 10. OFFLINE / RE-CALIBRATE

If the unit reports `SOVEREIGN LOCK — RE-CALIBRATE`:

1. Verify MPC OS is 3.10.1 or later.
2. Verify External plugins are enabled (Preferences → Enhancements (Beta)).
3. Confirm the output folder is inside a folder named `VST2 Plugins` on the MPC drive.
4. Confirm `plugin.json` name, manufacturer, and uid match the compiled `PluginInfo` exactly:
   - name: **F4 Phantom**
   - manufacturer: **Lumina Aerospace LLC**
   - uid: **F4PH**

If the problem persists, the unit's `uid` may have changed. `uid` is permanent once users have projects — do not change it.

---

## 11. LICENSE & CREDITS

**F4 Phantom** — Lumina Aerospace LLC. MIT License.

DSP building blocks (`framework/dsp/*.h`) are part of the MPC Plugin SDK.

Stablemate to GR-4 Ground.  
Retro-Future Mission Control.  
**Liftoff nominal.**
