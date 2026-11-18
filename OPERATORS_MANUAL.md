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

### FX Page (v1.2)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Delay Mix | 0 .. 100 | 0 | % | Delay on the master, after the chorus |
| Delay Time | 10 .. 2000 | 350 | ms | The repeat length when Delay Sync is off |
| Delay Feedback | 0 .. 95 | 35 | % | How much of each repeat comes back. The feedback path is saturated, so the tail cannot run away |
| Delay Tone | 200 .. 18000 | 5000 | Hz | The damping of the repeats: the tail gets darker as it repeats |
| Delay Sync | Off / On | Off | — | Lock the repeat to the host tempo |
| Delay Div | Division list | 1/8 | — | The musical division used when Delay Sync is on |
| Delay Duck | 0 .. 24 | 0 | dB | The input pulls the repeats down, so a new note does not sit on the ones it just made |
| Delay Ping | Off / On | On | — | Ping-pong: the echo crosses from left to right |
| Reverb Mix | 0 .. 100 | 0 | % | Reverb on the master, after the delay |
| Reverb Size | 0 .. 100 | 50 | % | The size of the room: the comb lengths scale with it |
| Reverb Decay | 0.3 .. 8 | 2 | s | The RT60: the time the tail takes to fall 60 dB |
| Reverb Damp | 0 .. 100 | 40 | % | How fast the top of the tail dies against the bottom |
| Reverb Pre | 0 .. 120 | 10 | ms | The gap between the dry sound and the room |
| Chorus Mix | 0 .. 100 | 0 | % | Stereo chorus on the master; also widens the output to true stereo |
| Chorus Depth | 0 .. 100 | 50 | % | Modulation depth |
| Chorus Rate | 0.1 .. 4 | 0.6 | Hz | Modulation rate |

### VULCAN Page (v1.2)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Vulcan Depth | 0 .. 100 | 0 | % | The rotary gate. 0 = off |
| Vulcan Rate | 240 .. 7200 | 3000 | rpm | Rounds per minute at full spin: 2,500 (T45, 1949), 4,000 (T45, 1950), 6,000 (M61), 7,200 (Project Vulcan, 1946) |
| Vulcan Spin | 0 .. 2000 | 400 | ms | Spool-up: the barrels start nearly still after each note and accelerate to Rate |
| Vulcan Spread | 0 .. 100 | 35 | % | How unevenly the six barrels fire; more spread, more ratchet |

**DELAY.** The repeat length is read with a cubic interpolation, so it can land on a fraction of a sample and the echo still arrives exactly where the music says it should. The length glides when you
change it, so a change never clicks. The feedback path is filtered, saturated and DC-blocked: the repeats get darker as they go, the saturation is what stops a long tail from running away, and the DC block is what
keeps the tail centred.

**REVERB.** A plate: four comb filters whose decay gain is calculated from the RT60 you asked for, two allpasses to smear the early reflections, eight early-reflection taps in front, and a slow drift on
every comb so the tail does not ring on a fixed grid. The level does not change with the decay time: a long tail is a long tail, not a louder one.

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

### The genre library (v1.2)

The fourteen configurations above are the originals. On top of them, v1.2 carries **180 patches**: every genre the synth is played in, and in each genre one voice of every family.

| Family | What it is |
|---------|-----------|
| LEAD | a cutting, wide lead; the one that plays the melody |
| BASS | saw plus square and sub, filtered down, monophonic in feel |
| KEY | triangle over an octave-up sine: the electric-piano voice |
| PAD | slow attack, detuned saws, the widest of the six |
| STRINGS | saws with a slow swell and a touch of vibrato |
| BRASS | a filtered saw swell with drive, for stabs and horn-like lines |

The genres: **8BIT, ACID, AMBIENT, BREAKBEAT, COUNTRY, DISCO, DISCO HOUSE, DNB, DUBSTEP, EDM, ELECTRO, FUNK, GOSPEL, HARDSTYLE, HIPHOP, HOUSE, INDIE, INDUSTRIAL, JAZZ, LATIN, LOFI, POP, PSYTRANCE, RNB, ROCK, SYNTHWAVE, TECHNO, TRANCE, TRAP, UKG**.

Every patch is named `<GENRE> <FAMILY>` - `EDM LEAD`, `HOUSE PAD`, `ACID BASS` - so the MPC's own preset list groups them by genre down the page, the way an MPC's own 808 or 909 kits do. The MPC sorts a plugin's programs by name, so
the library reads as a shelf of genres rather than a wall of patches.

Each family starts from one archetype: the warmth and the width (chorus on, a little drive, the delay and the reverb in the mix) are the same in all 30 genres. The genre line is what changes it - Hardstyle's bass is heavy and
saturated, Psytrance moves, Industrial is harsh and resonant, Country is clean and twangy, Gospel has the tremolo, UK Garage is bouncy and shuffled, Disco House is bright with a lot of chorus.

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
