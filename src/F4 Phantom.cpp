// F4 Phantom.cpp - an 8-voice subtractive synthesizer for the MPC.
//
// Lumina Aerospace LLC — Retro-Future Mission Control aesthetic.
// Chassis: Space Black (#0A0A0A). Accent: Telemetry Neon Blue (#00F2FF).
// Roster position: hangar mate to GR-4 Ground. Subtractive on purpose.
//
// SPDX-License-Identifier: MIT
//
// What this plugin demonstrates (search for the [N] markers below):
//   [1] declaring parameters in an enum + constructor so indices never drift
//   [2] built-in programs with addProgram()
//   [3] a Voice struct built from framework/dsp pieces and a VoiceAllocator
//   [4] sample-accurate MIDI: render up to each event's offset, then handle it
//   [5] "control rate" updates: expensive maths (tan, pow) every 16 samples
//   [6] tempo-synced LFO via ctx.transport
//   [7] no allocation in process(): fixed-size scratch arrays, chunked rendering
//
// v1.1.0 additions (Lumina Aerospace, 17 Nov 2026) - all appended, so existing parameter indices are unchanged:
//   Sub Level   a sine one octave under Osc1, per voice, for weight
//   Drift       slow random per-voice detune so unison-ish patches breathe like analogue
//   Chorus      stereo chorus on the master (Mix / Depth / Rate): turns the mono-to-stereo into a real stereo image
//   plus a 12-sound factory bank.
//
//   VULCAN      a six-barrel rotary gate, after the M61 Vulcan the F-4E carried: a shot every barrel-cycle, the barrels
//               spooling up from rest after every note and firing in a fixed rotating pattern (Depth / Rate / Spin / Spread)
//
// v1.2.0 additions (Lumina Aerospace, 18 Nov 2026) - all appended, so existing parameter indices are unchanged:
//   Delay        a tempo-syncable stereo delay with a fractional read, a saturated feedback path and ducking
//   Reverb      a Dattorro-style plate: four damped combs whose decay is set from the RT60, two allpasses,
//                eight early reflections and two detuned plates for width
//   both effects are the measured engine code (engine/include/lumina/fx/delay.hpp and reverb.hpp in fl-mcp,
//   vendored into src/lumina/fx/), so their behaviour is documented and tested there, not improvised here.
//
// Signal path per voice:
//   osc1 + osc2 + sub (PolyBLEP, detune, octave, drift) -> SVF (cutoff + env + LFO + key track, drive)
//   -> VULCAN rotary gate -> amp ADSR * velocity  -> summed -> master volume -> stereo chorus
//   -> delay -> reverb -> L + R.
//
// Hard rules honoured:
//   - Parameter 0 is master volume (host-nudged on every load; harmless, no side effects).
//   - No allocation, locks, logging or I/O in process().
//   - uid ("F4PH") matches PluginInfo.uniqueId = MPC_FOURCC('F','4','P','H').

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/oscillator.h"
#include "dsp/svf.h"
#include "dsp/adsr.h"
#include "dsp/lfo.h"
#include "dsp/smoother.h"
#include "dsp/voice.h"
#include "dsp/tempo.h"
#include "dsp/midi_cc.h"
#include "dsp/delay_line.h"   // v1.1.0 stereo chorus
#include "dsp/noise.h"        // v1.1.0 Xorshift32 for per-voice drift
#include "lumina/fx/delay.hpp"   // v1.2.0 delay
#include "lumina/fx/reverb.hpp"  // v1.2.0 reverb

using namespace mpc::dsp;

class F4Phantom : public mpc::Plugin
{
public:
    // [1] One enum entry per parameter, in the order they are added below.
    // kVolume is first: the host nudges parameter 0 on every load, so it must be
    // harmless master volume with no side effects.
    enum Params
    {
        kVolume,
        kOsc1Wave, kOsc1Oct, kOsc1Level,
        kOsc2Wave, kOsc2Oct, kOsc2Semi, kOsc2Detune, kOsc2Level,
        kPulseWidth, kGlide, kBendRange,
        kCutoff, kReso, kFilterMode, kEnvAmount, kDrive, kKeyTrack,
        kFAttack, kFDecay, kFSustain, kFRelease,
        kAttack, kDecay, kSustain, kRelease, kVelSens,
        kLfoShape, kLfoRate, kLfoSync, kLfoDiv, kLfoPitch, kLfoFilter, kWheelVib,
        // v1.1.0 (appended: indices above never move)
        kSubLevel, kDrift, kChorusMix, kChorusDepth, kChorusRate,
        kVulcanDepth, kVulcanRate, kVulcanSpin, kVulcanSpread,
        // v1.2.0 (appended: the indices above never move)
        kDelayMix, kDelayTime, kDelayFeedback, kDelayTone, kDelaySync, kDelayDiv, kDelayDuck, kDelayPing,
        kReverbMix, kReverbSize, kReverbDecay, kReverbDamp, kReverbPre,
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "F4 Phantom";
        i.vendor = "Lumina Aerospace LLC";
        i.uniqueId = MPC_FOURCC('F', '4', 'P', 'H');   // must match "uid" in plugin.json
        i.version = 1200;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    F4Phantom() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> waves = {"Sine", "Triangle", "Saw", "Square"};   // == dsp::Wave order
        // Parameter 0: master volume (host-nudged on load — harmless).
        addParam(floatParam("vol",      "Volume", -40.0f, 6.0f, -8.0f, "dB", 1));
        // OSC page
        addParam(choiceParam("osc1wave", "Osc1 Wave", waves, 2));
        addParam(intParam  ("osc1oct",  "Osc1 Octave", -2, 2, 0));
        addParam(floatParam("osc1lvl",  "Osc1 Level", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(choiceParam("osc2wave", "Osc2 Wave", waves, 2));
        addParam(intParam  ("osc2oct",  "Osc2 Octave", -2, 2, 0));
        addParam(intParam  ("osc2semi", "Osc2 Semi", -12, 12, 0, "st"));
        addParam(floatParam("osc2det",  "Osc2 Detune", -50.0f, 50.0f, 8.0f, "ct", 0));
        addParam(floatParam("osc2lvl",  "Osc2 Level", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("pw",       "Pulse Width", 5.0f, 95.0f, 50.0f, "%", 0));
        addParam(floatParam("glide",    "Glide", 0.0f, 1000.0f, 0.0f, "ms", 0, 0.5f));
        addParam(intParam  ("bend",     "Bend Range", 0, 24, 2, "st"));
        // FILTER page
        addParam(logParam  ("cutoff",   "Cutoff", 20.0f, 18000.0f, 4000.0f, "Hz", 0));
        addParam(floatParam("reso",     "Resonance", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(choiceParam("fmode",   "Filter Mode", {"LP", "BP", "HP", "Notch"}, 0));
        addParam(floatParam("envamt",   "Env Amount", -100.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("drive",    "Drive", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("keytrk",   "Key Track", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(logParam  ("fatk",     "Filt Attack", 1.0f, 5000.0f, 5.0f, "ms", 0));
        addParam(logParam  ("fdec",     "Filt Decay", 1.0f, 5000.0f, 300.0f, "ms", 0));
        addParam(floatParam("fsus",     "Filt Sustain", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(logParam  ("frel",     "Filt Release", 1.0f, 5000.0f, 300.0f, "ms", 0));
        // AMP page
        addParam(logParam  ("atk",      "Amp Attack", 1.0f, 5000.0f, 5.0f, "ms", 0));
        addParam(logParam  ("dec",      "Amp Decay", 1.0f, 5000.0f, 200.0f, "ms", 0));
        addParam(floatParam("sus",      "Amp Sustain", 0.0f, 100.0f, 80.0f, "%", 0));
        addParam(logParam  ("rel",      "Amp Release", 1.0f, 5000.0f, 200.0f, "ms", 0));
        addParam(floatParam("velsens",  "Velocity", 0.0f, 100.0f, 60.0f, "%", 0));
        // LFO page
        addParam(choiceParam("lfoshape", "LFO Shape", LFO::shapeNames(), 0));
        addParam(logParam  ("lforate",  "LFO Rate", 0.05f, 30.0f, 5.0f, "Hz", 2));
        addParam(boolParam ("lfosync",  "LFO Sync", false));
        addParam(choiceParam("lfodiv",  "LFO Div", divisionNames(), kDivisionQuarter));
        addParam(floatParam("lfopitch", "LFO>Pitch", 0.0f, 12.0f, 0.0f, "st", 2, 0.5f));
        addParam(floatParam("lfofilt",  "LFO>Filter", -100.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("wheelvib", "Wheel Vib", 0.0f, 12.0f, 0.5f, "st", 2, 0.5f));
        // v1.1.0: weight, life and width
        addParam(floatParam("sublvl",   "Sub Level", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("drift",    "Drift", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("chmix",    "Chorus Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("chdepth",  "Chorus Depth", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(logParam  ("chrate",   "Chorus Rate", 0.1f, 4.0f, 0.6f, "Hz", 2));
        // VULCAN: Rate is in rounds per minute like the gun: 2,500 (T45, 1949), 4,000 (T45, 1950), 6,000 (M61), 7,200 (Project Vulcan, 1946)
        addParam(floatParam("vdepth",   "Vulcan Depth", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(logParam  ("vrate",    "Vulcan Rate", 240.0f, 7200.0f, 3000.0f, "rpm", 0));
        addParam(floatParam("vspin",    "Vulcan Spin", 0.0f, 2000.0f, 400.0f, "ms", 0, 0.5f));
        addParam(floatParam("vspread",  "Vulcan Spread", 0.0f, 100.0f, 35.0f, "%", 0));
        // v1.2.0 FX: the delay and the reverb. Both are the measured engine code; the defaults are dry, so a
        // 1.1 patch still sounds exactly as it did.
        addParam(floatParam("dlmix",   "Delay Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(logParam  ("dltime",  "Delay Time", 10.0f, 2000.0f, 350.0f, "ms", 0));
        addParam(floatParam("dlfb",    "Delay Feedback", 0.0f, 95.0f, 35.0f, "%", 0));
        addParam(logParam  ("dltone",  "Delay Tone", 200.0f, 18000.0f, 5000.0f, "Hz", 0));
        addParam(boolParam ("dlsync",  "Delay Sync", false));
        addParam(choiceParam("dldiv",  "Delay Div", divisionNames(), kDivisionEighth));
        addParam(floatParam("dlduck",  "Delay Duck", 0.0f, 24.0f, 0.0f, "dB", 0));
        addParam(boolParam ("dlping",  "Delay Ping", true));
        addParam(floatParam("rvmix",   "Reverb Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("rvsize",  "Reverb Size", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(logParam  ("rvdecay", "Reverb Decay", 0.3f, 8.0f, 2.0f, "s", 0));
        addParam(floatParam("rvdamp",  "Reverb Damp", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(logParam  ("rvpre",   "Reverb Pre", 0.0f, 120.0f, 10.0f, "ms", 0));

        { uint32_t seed = 0x1F4A7C15u; for (auto& v : voices_) { v.rng.reseed(seed); seed = seed * 1664525u + 1013904223u; } }
        if (numParams() != kNumParams) mpc::log("F4 Phantom: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);

        // [2] Built-in programs. Start from the defaults and override a few values.
        auto program = [this](const char* name, const std::vector<std::pair<int, float>>& overrides)
        {
            std::vector<float> v((size_t)numParams());
            for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
            for (const auto& o : overrides) v[(size_t)o.first] = o.second;
            addProgram(name, std::move(v));
        };
        // Factory bank (12). Values are plain units, listed by parameter. Choice params take their index:
        // waves 0 Sine 1 Triangle 2 Saw 3 Square; LFO shapes 0 Sine 1 Triangle 2 Saw 3 Square 4 S&H; filter 0 LP 1 BP 2 HP 3 Notch.
        program("Init Saw", {});
        program("Fat Bass", {{kOsc1Wave, 2}, {kOsc2Wave, 3}, {kOsc2Oct, -1}, {kOsc2Detune, 12}, {kOsc2Level, 80},
                             {kCutoff, 900}, {kReso, 35}, {kEnvAmount, 70}, {kDrive, 30}, {kFDecay, 250}, {kFSustain, 5},
                             {kDecay, 300}, {kSustain, 70}, {kRelease, 120}, {kGlide, 60}, {kSubLevel, 35}, {kVolume, -6}});
        program("Soft Pad", {{kOsc1Wave, 2}, {kOsc2Wave, 1}, {kOsc2Detune, -10}, {kOsc2Level, 90},
                             {kCutoff, 1500}, {kReso, 10}, {kEnvAmount, 30}, {kFAttack, 800}, {kFDecay, 1500}, {kFSustain, 50},
                             {kAttack, 700}, {kDecay, 1000}, {kSustain, 90}, {kRelease, 1200},
                             {kLfoShape, 0}, {kLfoRate, 0.4f}, {kLfoFilter, 25}, {kDrift, 25}, {kChorusMix, 40}, {kVolume, -10}});
        program("Pluck", {{kOsc1Wave, 3}, {kPulseWidth, 25}, {kOsc2Wave, 2}, {kOsc2Oct, 1}, {kOsc2Level, 40},
                          {kCutoff, 600}, {kReso, 30}, {kEnvAmount, 90}, {kKeyTrack, 100}, {kFDecay, 120}, {kFSustain, 0},
                          {kDecay, 400}, {kSustain, 0}, {kRelease, 150}, {kVelSens, 100}, {kChorusMix, 20}, {kVolume, -7}});
        program("Phantom Lead", {{kOsc1Wave, 2}, {kOsc2Wave, 3}, {kPulseWidth, 45}, {kOsc2Detune, 7}, {kOsc2Level, 70},
                                 {kCutoff, 3200}, {kReso, 25}, {kEnvAmount, 45}, {kKeyTrack, 70}, {kFDecay, 400}, {kFSustain, 40},
                                 {kAttack, 3}, {kDecay, 300}, {kSustain, 85}, {kRelease, 250}, {kGlide, 40},
                                 {kLfoRate, 5.5f}, {kLfoPitch, 0.15f}, {kWheelVib, 1.0f}, {kDrift, 20}, {kChorusMix, 30}, {kVolume, -8}});
        program("Neon Strings", {{kOsc1Wave, 2}, {kOsc2Wave, 2}, {kOsc2Detune, 14}, {kOsc2Level, 90},
                                 {kCutoff, 2800}, {kReso, 8}, {kEnvAmount, 15}, {kFAttack, 350}, {kFDecay, 900}, {kFSustain, 60},
                                 {kAttack, 350}, {kDecay, 800}, {kSustain, 85}, {kRelease, 700}, {kVelSens, 40},
                                 {kDrift, 30}, {kChorusMix, 65}, {kChorusDepth, 60}, {kChorusRate, 0.5f}, {kVolume, -10}});
        program("Deep Sub", {{kOsc1Wave, 0}, {kOsc2Level, 0}, {kSubLevel, 80},
                             {kCutoff, 400}, {kReso, 0}, {kEnvAmount, 10}, {kKeyTrack, 20}, {kFDecay, 200}, {kFSustain, 80},
                             {kAttack, 4}, {kDecay, 400}, {kSustain, 90}, {kRelease, 150}, {kGlide, 20}, {kVolume, -8}});
        program("Glass Keys", {{kOsc1Wave, 1}, {kOsc2Wave, 0}, {kOsc2Oct, 1}, {kOsc2Detune, 3}, {kOsc2Level, 45},
                               {kCutoff, 5000}, {kReso, 12}, {kEnvAmount, 35}, {kKeyTrack, 60}, {kFDecay, 500}, {kFSustain, 10},
                               {kDecay, 900}, {kSustain, 25}, {kRelease, 500}, {kVelSens, 90},
                               {kChorusMix, 45}, {kChorusDepth, 40}, {kChorusRate, 0.8f}, {kVolume, -8}});
        program("Retro Brass", {{kOsc1Wave, 2}, {kOsc2Wave, 2}, {kOsc2Detune, 6}, {kOsc2Level, 85},
                                {kCutoff, 700}, {kReso, 15}, {kEnvAmount, 60}, {kDrive, 20}, {kKeyTrack, 40},
                                {kFAttack, 120}, {kFDecay, 500}, {kFSustain, 55}, {kFRelease, 250},
                                {kAttack, 60}, {kDecay, 400}, {kSustain, 85}, {kRelease, 180},
                                {kDrift, 15}, {kChorusMix, 25}, {kVolume, -8}});
        program("Ghost Wobble", {{kOsc1Wave, 3}, {kPulseWidth, 40}, {kOsc2Wave, 2}, {kOsc2Oct, -1}, {kOsc2Level, 70}, {kSubLevel, 30},
                                 {kCutoff, 500}, {kReso, 55}, {kEnvAmount, 10},
                                 {kLfoShape, 1}, {kLfoSync, 1}, {kLfoDiv, 7}, {kLfoFilter, 80},
                                 {kAttack, 4}, {kSustain, 90}, {kChorusMix, 20}, {kVolume, -10}});
        program("Wide Saws", {{kOsc1Wave, 2}, {kOsc2Wave, 2}, {kOsc2Detune, 22}, {kOsc2Level, 100},
                              {kCutoff, 6500}, {kReso, 5}, {kEnvAmount, 20}, {kFDecay, 600}, {kFSustain, 60},
                              {kAttack, 15}, {kDecay, 600}, {kSustain, 80}, {kRelease, 400},
                              {kDrift, 40}, {kChorusMix, 80}, {kChorusDepth, 70}, {kChorusRate, 0.35f}, {kVolume, -10}});
        program("Vulcan Rotary", {{kOsc1Wave, 2}, {kOsc2Wave, 3}, {kPulseWidth, 40}, {kOsc2Detune, 9}, {kOsc2Level, 70},
                                  {kCutoff, 5200}, {kReso, 22}, {kEnvAmount, 25}, {kDrive, 25}, {kKeyTrack, 60},
                                  {kAttack, 2}, {kSustain, 90}, {kRelease, 120},
                                  {kVulcanDepth, 90}, {kVulcanRate, 6000}, {kVulcanSpin, 700}, {kVulcanSpread, 45}, {kChorusMix, 15}, {kVolume, -13}});
        program("F-4E Gun Run", {{kOsc1Wave, 2}, {kOsc2Wave, 2}, {kOsc2Oct, -1}, {kOsc2Detune, 5}, {kOsc2Level, 90}, {kSubLevel, 40},
                                 {kCutoff, 1400}, {kReso, 30}, {kEnvAmount, 55}, {kFDecay, 600}, {kFSustain, 20},
                                 {kAttack, 2}, {kDecay, 500}, {kSustain, 85}, {kRelease, 160},
                                 {kVulcanDepth, 100}, {kVulcanRate, 1320}, {kVulcanSpin, 1300}, {kVulcanSpread, 60}, {kVolume, -14}});
        program("Acid Line", {{kOsc1Wave, 2}, {kOsc2Level, 0},
                              {kCutoff, 450}, {kReso, 75}, {kEnvAmount, 85}, {kDrive, 35}, {kKeyTrack, 30},
                              {kFDecay, 180}, {kFSustain, 0}, {kAttack, 3}, {kDecay, 200}, {kSustain, 85}, {kRelease, 90},
                              {kGlide, 70}, {kVolume, -10}});

        // -------------------------------------------------------------------------------------------
        // v1.2.0: THE GENRE LIBRARY.
        //
        // The fourteen factory sounds above stay exactly as they are: they are the batch a professional
        // musician tested and liked, and their warmth and width are the standard everything here is judged
        // against.
        //
        // On top of them: every genre the synth is played in, and in each genre one voice of every family.
        // The NAMES are what make the MPC's own preset list work like a drum machine's: the MPC sorts a
        // plugin's programs by name, so "EDM LEAD", "EDM BASS", "HOUSE PAD" and so on group themselves by
        // genre down the list, the way an MPC's own "909" or "808" kits do.
        //
        // The families are the six archetypes below. An archetype says what the VOICE is (a lead, a bass,
        // keys, a pad, strings, brass); the genre line says what that genre does to it. The archetype always
        // starts from the same warm, wide place: chorus on, a little drive, the new delay and reverb in the
        // mix. That is the floor - the quality the tested batch set.
        // -------------------------------------------------------------------------------------------
        enum Arch { kLead, kBass, kKey, kPad, kStrings, kBrass };

        auto arch = [](int a) {
            std::vector<std::pair<int, float>> v;
            switch (a) {
            case kLead:
                v = {{kOsc1Wave, 2}, {kOsc2Wave, 2}, {kOsc2Detune, 7}, {kOsc2Level, 70}, {kPulseWidth, 50},
                     {kCutoff, 3000}, {kReso, 20}, {kEnvAmount, 40}, {kDrive, 15}, {kKeyTrack, 50},
                     {kAttack, 5}, {kDecay, 400}, {kSustain, 85}, {kRelease, 200},
                     {kDrift, 15}, {kChorusMix, 45}, {kChorusDepth, 55}, {kChorusRate, 0.45f},
                     {kDelayMix, 18}, {kDelayFeedback, 30}, {kDelayTime, 350}, {kDelayTone, 5000},
                     {kReverbMix, 16}, {kReverbDecay, 1.8f}, {kReverbSize, 50}, {kVolume, -9}};
                break;
            case kBass:
                v = {{kOsc1Wave, 2}, {kOsc2Wave, 3}, {kOsc2Oct, -1}, {kOsc2Level, 45}, {kSubLevel, 45},
                     {kCutoff, 700}, {kReso, 30}, {kEnvAmount, 70}, {kDrive, 25}, {kKeyTrack, 40},
                     {kAttack, 3}, {kDecay, 250}, {kSustain, 70}, {kRelease, 120},
                     {kDrift, 8}, {kChorusMix, 0}, {kDelayMix, 0}, {kReverbMix, 8}, {kReverbDecay, 1.2f},
                     {kVolume, -10}};
                break;
            case kKey:
                v = {{kOsc1Wave, 1}, {kOsc2Wave, 0}, {kOsc2Oct, 1}, {kOsc2Detune, 4}, {kOsc2Level, 45},
                     {kCutoff, 2200}, {kReso, 10}, {kEnvAmount, 25}, {kDrive, 8}, {kKeyTrack, 40},
                     {kAttack, 5}, {kDecay, 700}, {kSustain, 55}, {kRelease, 350},
                     {kDrift, 12}, {kChorusMix, 30}, {kChorusDepth, 45}, {kChorusRate, 0.5f},
                     {kDelayMix, 12}, {kDelayFeedback, 25}, {kDelayTime, 300},
                     {kReverbMix, 28}, {kReverbDecay, 2.4f}, {kReverbSize, 50}, {kVolume, -9}};
                break;
            case kPad:
                v = {{kOsc1Wave, 2}, {kOsc2Wave, 2}, {kOsc2Detune, -12}, {kOsc2Level, 85}, {kSubLevel, 20},
                     {kCutoff, 1400}, {kReso, 12}, {kEnvAmount, 20}, {kFAttack, 900}, {kFDecay, 1200},
                     {kFSustain, 85}, {kFRelease, 1500},
                     {kAttack, 900}, {kDecay, 1200}, {kSustain, 85}, {kRelease, 1500},
                     {kLfoShape, 0}, {kLfoRate, 0.35f}, {kLfoFilter, 20},
                     {kDrift, 30}, {kChorusMix, 70}, {kChorusDepth, 60}, {kChorusRate, 0.4f},
                     {kDelayMix, 12}, {kDelayFeedback, 25}, {kDelayTime, 500},
                     {kReverbMix, 45}, {kReverbDecay, 3.5f}, {kReverbSize, 60}, {kVolume, -11}};
                break;
            case kStrings:
                v = {{kOsc1Wave, 2}, {kOsc2Wave, 2}, {kOsc2Detune, 9}, {kOsc2Level, 80}, {kPulseWidth, 50},
                     {kCutoff, 2400}, {kReso, 8}, {kEnvAmount, 18}, {kFAttack, 350}, {kFDecay, 900},
                     {kFSustain, 85}, {kFRelease, 700},
                     {kAttack, 350}, {kDecay, 900}, {kSustain, 85}, {kRelease, 700},
                     {kLfoShape, 0}, {kLfoRate, 0.7f}, {kLfoPitch, 0.04f},
                     {kDrift, 25}, {kChorusMix, 55}, {kChorusDepth, 50}, {kChorusRate, 0.45f},
                     {kDelayMix, 15}, {kDelayFeedback, 25}, {kDelayTime, 400},
                     {kReverbMix, 35}, {kReverbDecay, 3.0f}, {kReverbSize, 55}, {kVolume, -10}};
                break;
            case kBrass:
                v = {{kOsc1Wave, 2}, {kOsc2Wave, 2}, {kOsc2Detune, 6}, {kOsc2Level, 75}, {kPulseWidth, 45},
                     {kCutoff, 1800}, {kReso, 18}, {kEnvAmount, 55}, {kDrive, 22}, {kKeyTrack, 40},
                     {kFAttack, 120}, {kFDecay, 500}, {kFSustain, 55}, {kFRelease, 250},
                     {kAttack, 70}, {kDecay, 450}, {kSustain, 80}, {kRelease, 250},
                     {kDrift, 15}, {kChorusMix, 35}, {kChorusDepth, 45}, {kChorusRate, 0.5f},
                     {kDelayMix, 12}, {kDelayFeedback, 25}, {kDelayTime, 350},
                     {kReverbMix, 28}, {kReverbDecay, 2.2f}, {kReverbSize, 50}, {kVolume, -9}};
                break;
            }
            return v;
        };

        // A patch is an archetype plus the handful of values that make it that genre's voice.
        auto patch = [&program, &arch](const char* name, int a, std::initializer_list<std::pair<int, float>> extra) {
            std::vector<std::pair<int, float>> v = arch(a);
            for (const auto& e : extra) {
                bool replaced = false;
                for (auto& p : v) if (p.first == e.first) { p.second = e.second; replaced = true; break; }
                if (!replaced) v.push_back(e);
            }
            program(name, v);
        };

        // ---- 8BIT: square waves, no filter, fast decay ----
        patch("8BIT LEAD", kLead, {{kOsc1Wave, 3}, {kOsc2Wave, 3}, {kOsc2Level, 40}, {kCutoff, 12000}, {kReso, 0}, {kEnvAmount, 10}, {kChorusMix, 0}, {kReverbMix, 8}});
        patch("8BIT BASS", kBass, {{kOsc1Wave, 3}, {kOsc2Wave, 3}, {kOsc2Level, 30}, {kCutoff, 10000}, {kReso, 0}, {kEnvAmount, 10}, {kDecay, 150}, {kSubLevel, 30}, {kReverbMix, 5}});
        patch("8BIT KEY", kKey, {{kOsc1Wave, 3}, {kOsc2Wave, 3}, {kOsc2Level, 35}, {kCutoff, 11000}, {kReso, 0}, {kChorusMix, 20}, {kReverbMix, 10}});
        patch("8BIT PAD", kPad, {{kOsc1Wave, 3}, {kPulseWidth, 25}, {kOsc2Wave, 3}, {kOsc2Level, 30}, {kCutoff, 9000}, {kReso, 0}, {kChorusMix, 35}, {kReverbMix, 15}});
        patch("8BIT STRINGS", kStrings, {{kOsc1Wave, 3}, {kOsc2Wave, 3}, {kOsc2Level, 30}, {kCutoff, 10000}, {kReso, 0}, {kChorusMix, 30}, {kReverbMix, 12}});
        patch("8BIT BRASS", kBrass, {{kOsc1Wave, 3}, {kOsc2Wave, 3}, {kOsc2Level, 35}, {kCutoff, 9000}, {kReso, 0}, {kDecay, 250}, {kSustain, 40}, {kChorusMix, 25}});

        // ---- ACID: the 303, all resonance and glide ----
        patch("ACID LEAD", kLead, {{kCutoff, 2400}, {kReso, 80}, {kEnvAmount, 90}, {kDrive, 40}, {kGlide, 120}, {kDelayMix, 12}});
        patch("ACID BASS", kBass, {{kCutoff, 600}, {kReso, 85}, {kEnvAmount, 95}, {kDrive, 45}, {kDecay, 150}, {kSustain, 20}, {kGlide, 80}});
        patch("ACID KEY", kKey, {{kCutoff, 2600}, {kReso, 60}, {kEnvAmount, 70}, {kDecay, 250}, {kSustain, 30}});
        patch("ACID PAD", kPad, {{kCutoff, 1800}, {kReso, 45}, {kEnvAmount, 60}, {kFAttack, 400}, {kChorusMix, 60}, {kReverbMix, 35}});
        patch("ACID STRINGS", kStrings, {{kCutoff, 2200}, {kReso, 40}, {kGlide, 60}, {kAttack, 250}});
        patch("ACID BRASS", kBrass, {{kCutoff, 2000}, {kReso, 55}, {kEnvAmount, 75}, {kDrive, 35}});

        // ---- AMBIENT: slow, huge, soft ----
        patch("AMBIENT LEAD", kLead, {{kCutoff, 2400}, {kAttack, 400}, {kChorusMix, 70}, {kDelayMix, 25}, {kReverbMix, 55}, {kReverbDecay, 6.0f}});
        patch("AMBIENT BASS", kBass, {{kCutoff, 500}, {kSubLevel, 60}, {kAttack, 200}, {kReverbMix, 40}, {kReverbDecay, 5.0f}});
        patch("AMBIENT KEY", kKey, {{kCutoff, 2200}, {kAttack, 300}, {kChorusMix, 60}, {kReverbMix, 50}, {kReverbDecay, 5.0f}});
        patch("AMBIENT PAD", kPad, {{kCutoff, 1400}, {kAttack, 1200}, {kChorusMix, 85}, {kReverbMix, 70}, {kReverbDecay, 7.0f}});
        patch("AMBIENT STRINGS", kStrings, {{kCutoff, 2000}, {kAttack, 600}, {kChorusMix, 75}, {kReverbMix, 60}, {kReverbDecay, 6.0f}});
        patch("AMBIENT BRASS", kBrass, {{kCutoff, 1800}, {kAttack, 500}, {kChorusMix, 60}, {kReverbMix, 55}, {kReverbDecay, 5.5f}});

        // ---- BREAKBEAT: grit and punch ----
        patch("BREAKBEAT LEAD", kLead, {{kCutoff, 3400}, {kReso, 35}, {kDrive, 35}, {kDelayMix, 22}, {kReverbMix, 22}});
        patch("BREAKBEAT BASS", kBass, {{kCutoff, 650}, {kReso, 45}, {kDrive, 45}, {kSubLevel, 60}, {kDecay, 200}, {kChorusMix, 10}});
        patch("BREAKBEAT KEY", kKey, {{kCutoff, 2400}, {kChorusMix, 45}, {kReverbMix, 25}});
        patch("BREAKBEAT PAD", kPad, {{kCutoff, 1300}, {kChorusMix, 65}, {kReverbMix, 45}});
        patch("BREAKBEAT STRINGS", kStrings, {{kCutoff, 2200}, {kChorusMix, 55}, {kReverbMix, 35}});
        patch("BREAKBEAT BRASS", kBrass, {{kCutoff, 2100}, {kDrive, 40}, {kChorusMix, 40}});
        // ---- COUNTRY: clean and twangy ----
        patch("COUNTRY LEAD", kLead, {{kCutoff, 3600}, {kChorusMix, 35}, {kDelayMix, 18}});
        patch("COUNTRY BASS", kBass, {{kCutoff, 800}, {kSubLevel, 50}, {kDecay, 220}, {kChorusMix, 12}});
        patch("COUNTRY KEY", kKey, {{kCutoff, 2800}, {kChorusMix, 45}, {kReverbMix, 22}});
        patch("COUNTRY PAD", kPad, {{kCutoff, 1600}, {kChorusMix, 60}, {kReverbMix, 35}});
        patch("COUNTRY STRINGS", kStrings, {{kCutoff, 2800}, {kChorusMix, 55}, {kReverbMix, 30}});
        patch("COUNTRY BRASS", kBrass, {{kCutoff, 2800}, {kChorusMix, 50}, {kReverbMix, 25}});

        // ---- DISCO: lush, bright, chorus everywhere ----
        patch("DISCO LEAD", kLead, {{kCutoff, 4200}, {kChorusMix, 60}, {kDelayMix, 25}});
        patch("DISCO BASS", kBass, {{kCutoff, 800}, {kDrive, 30}, {kChorusMix, 25}, {kReverbMix, 14}});
        patch("DISCO KEY", kKey, {{kCutoff, 3200}, {kChorusMix, 70}, {kReverbMix, 35}});
        patch("DISCO PAD", kPad, {{kCutoff, 2400}, {kChorusMix, 85}, {kReverbMix, 50}, {kLfoRate, 0.5f}});
        patch("DISCO STRINGS", kStrings, {{kCutoff, 3200}, {kChorusMix, 70}, {kReverbMix, 40}});
        patch("DISCO BRASS", kBrass, {{kCutoff, 2800}, {kChorusMix, 50}, {kReverbMix, 32}});

        // ---- DISCO HOUSE: the nu-disco shine ----
        patch("DISCO HOUSE LEAD", kLead, {{kCutoff, 4000}, {kChorusMix, 55}, {kDelayMix, 22}, {kDelaySync, 1}, {kDelayDiv, 7}});
        patch("DISCO HOUSE BASS", kBass, {{kCutoff, 850}, {kDrive, 30}, {kSubLevel, 45}, {kDecay, 200}, {kChorusMix, 15}});
        patch("DISCO HOUSE KEY", kKey, {{kCutoff, 3200}, {kChorusMix, 70}, {kReverbMix, 30}});
        patch("DISCO HOUSE PAD", kPad, {{kCutoff, 2200}, {kChorusMix, 80}, {kReverbMix, 45}});
        patch("DISCO HOUSE STRINGS", kStrings, {{kCutoff, 3000}, {kChorusMix, 70}, {kReverbMix, 35}});
        patch("DISCO HOUSE BRASS", kBrass, {{kCutoff, 2800}, {kDrive, 25}, {kChorusMix, 50}});

        // ---- DNB: the Reese bass and the wide pad ----
        patch("DNB LEAD", kLead, {{kCutoff, 4200}, {kReso, 25}, {kDrive, 25}, {kDelayMix, 20}, {kDelaySync, 1}, {kDelayDiv, 4}, {kReverbMix, 20}});
        patch("DNB BASS", kBass, {{kCutoff, 700}, {kReso, 40}, {kDrive, 45}, {kSubLevel, 65}, {kOsc2Detune, 14}, {kGlide, 40}, {kChorusMix, 20}});
        patch("DNB KEY", kKey, {{kCutoff, 2600}, {kChorusMix, 50}, {kDelayMix, 15}});
        patch("DNB PAD", kPad, {{kCutoff, 1600}, {kChorusMix, 75}, {kReverbMix, 50}, {kReverbDecay, 4.0f}});
        patch("DNB STRINGS", kStrings, {{kCutoff, 2600}, {kChorusMix, 60}, {kReverbMix, 40}});
        patch("DNB BRASS", kBrass, {{kCutoff, 2200}, {kDrive, 30}, {kDelayMix, 18}});

        // ---- DUBSTEP: big, dark, detuned ----
        patch("DUBSTEP LEAD", kLead, {{kCutoff, 3000}, {kOsc2Detune, 20}, {kChorusMix, 70}, {kReverbMix, 30}});
        patch("DUBSTEP BASS", kBass, {{kCutoff, 500}, {kReso, 55}, {kDrive, 50}, {kSubLevel, 75}, {kDecay, 400}, {kSustain, 55}});
        patch("DUBSTEP KEY", kKey, {{kCutoff, 1800}, {kChorusMix, 45}, {kReverbMix, 30}});
        patch("DUBSTEP PAD", kPad, {{kCutoff, 1200}, {kChorusMix, 80}, {kReverbMix, 60}, {kReverbDecay, 5.0f}});
        patch("DUBSTEP STRINGS", kStrings, {{kCutoff, 2000}, {kChorusMix, 70}, {kReverbMix, 45}});
        patch("DUBSTEP BRASS", kBrass, {{kCutoff, 1700}, {kDrive, 40}, {kChorusMix, 40}});

        // ---- EDM: the main-stage ----
        patch("EDM LEAD", kLead, {{kCutoff, 4200}, {kDrive, 25}, {kChorusMix, 55}, {kDelayMix, 22}, {kDelaySync, 1}, {kDelayDiv, 7}, {kReverbMix, 15}});
        patch("EDM BASS", kBass, {{kCutoff, 900}, {kDrive, 35}, {kSubLevel, 55}, {kDecay, 180}, {kChorusMix, 15}});
        patch("EDM KEY", kKey, {{kCutoff, 3000}, {kChorusMix, 45}, {kReverbMix, 25}});
        patch("EDM PAD", kPad, {{kCutoff, 2200}, {kChorusMix, 75}, {kReverbMix, 50}, {kReverbDecay, 4.5f}});
        patch("EDM STRINGS", kStrings, {{kCutoff, 3000}, {kChorusMix, 60}, {kDelayMix, 18}, {kReverbMix, 28}});
        patch("EDM BRASS", kBrass, {{kCutoff, 2600}, {kDrive, 30}, {kAttack, 40}, {kChorusMix, 45}});

        // ---- ELECTRO: the 80s machine ----
        patch("ELECTRO LEAD", kLead, {{kCutoff, 3600}, {kReso, 30}, {kLfoShape, 2}, {kLfoRate, 6.0f}, {kLfoPitch, 0.12f}, {kDelayMix, 25}});
        patch("ELECTRO BASS", kBass, {{kCutoff, 800}, {kDrive, 35}, {kDecay, 200}, {kSubLevel, 50}});
        patch("ELECTRO KEY", kKey, {{kCutoff, 3000}, {kChorusMix, 55}, {kReverbMix, 28}});
        patch("ELECTRO PAD", kPad, {{kCutoff, 2000}, {kChorusMix, 70}, {kReverbMix, 45}});
        patch("ELECTRO STRINGS", kStrings, {{kCutoff, 2600}, {kChorusMix, 60}, {kReverbMix, 32}});
        patch("ELECTRO BRASS", kBrass, {{kCutoff, 2800}, {kReso, 25}, {kDrive, 30}});

        // ---- FUNK: percussive and resonant ----
        patch("FUNK LEAD", kLead, {{kCutoff, 2800}, {kReso, 40}, {kEnvAmount, 70}, {kDecay, 200}, {kGlide, 50}});
        patch("FUNK BASS", kBass, {{kCutoff, 700}, {kReso, 50}, {kEnvAmount, 80}, {kDecay, 150}, {kSustain, 40}, {kGlide, 60}});
        patch("FUNK KEY", kKey, {{kCutoff, 2600}, {kEnvAmount, 60}, {kDecay, 250}, {kChorusMix, 45}});
        patch("FUNK PAD", kPad, {{kCutoff, 1600}, {kChorusMix, 60}, {kReverbMix, 35}});
        patch("FUNK STRINGS", kStrings, {{kCutoff, 2200}, {kDecay, 300}, {kAttack, 200}});
        patch("FUNK BRASS", kBrass, {{kCutoff, 2000}, {kEnvAmount, 70}, {kAttack, 40}});

        // ---- GOSPEL: organ-like, full, tremolo ----
        patch("GOSPEL LEAD", kLead, {{kCutoff, 3000}, {kLfoShape, 0}, {kLfoRate, 5.0f}, {kLfoPitch, 0.08f}, {kChorusMix, 45}, {kReverbMix, 25}});
        patch("GOSPEL BASS", kBass, {{kCutoff, 550}, {kSubLevel, 60}, {kDrive, 25}, {kChorusMix, 15}});
        patch("GOSPEL KEY", kKey, {{kCutoff, 2600}, {kChorusMix, 60}, {kReverbMix, 30}});
        patch("GOSPEL PAD", kPad, {{kCutoff, 1600}, {kChorusMix, 75}, {kReverbMix, 45}});
        patch("GOSPEL STRINGS", kStrings, {{kCutoff, 2400}, {kChorusMix, 65}, {kReverbMix, 40}});
        patch("GOSPEL BRASS", kBrass, {{kCutoff, 2400}, {kDrive, 25}, {kChorusMix, 55}});

        // ---- HARDSTYLE: massive and distorted ----
        patch("HARDSTYLE LEAD", kLead, {{kCutoff, 3200}, {kDrive, 45}, {kChorusMix, 55}, {kReverbMix, 20}});
        patch("HARDSTYLE BASS", kBass, {{kCutoff, 450}, {kReso, 60}, {kDrive, 60}, {kSubLevel, 80}, {kDecay, 500}, {kSustain, 80}});
        patch("HARDSTYLE KEY", kKey, {{kCutoff, 2200}, {kDrive, 35}, {kChorusMix, 40}});
        patch("HARDSTYLE PAD", kPad, {{kCutoff, 1200}, {kChorusMix, 70}, {kReverbMix, 45}});
        patch("HARDSTYLE STRINGS", kStrings, {{kCutoff, 2200}, {kDrive, 40}, {kChorusMix, 55}});
        patch("HARDSTYLE BRASS", kBrass, {{kCutoff, 2000}, {kDrive, 50}, {kChorusMix, 35}});

        // ---- HIPHOP: filtered, deep, wide ----
        patch("HIPHOP LEAD", kLead, {{kCutoff, 2200}, {kReso, 25}, {kChorusMix, 40}, {kReverbMix, 22}});
        patch("HIPHOP BASS", kBass, {{kCutoff, 500}, {kSubLevel, 70}, {kDrive, 25}, {kDecay, 180}, {kChorusMix, 10}});
        patch("HIPHOP KEY", kKey, {{kCutoff, 1800}, {kChorusMix, 50}, {kReverbMix, 30}, {kReverbDecay, 2.8f}});
        patch("HIPHOP PAD", kPad, {{kCutoff, 1200}, {kChorusMix, 60}, {kReverbMix, 40}});
        patch("HIPHOP STRINGS", kStrings, {{kCutoff, 1800}, {kChorusMix, 55}, {kReverbMix, 35}});
        patch("HIPHOP BRASS", kBrass, {{kCutoff, 1600}, {kDrive, 25}, {kReverbMix, 25}});

        // ---- HOUSE: warm and filtered, with the chord stab ----
        patch("HOUSE LEAD", kLead, {{kCutoff, 2400}, {kChorusMix, 40}, {kDelayMix, 22}, {kDelaySync, 1}, {kDelayDiv, 7}});
        patch("HOUSE BASS", kBass, {{kCutoff, 800}, {kDrive, 30}, {kSubLevel, 50}, {kChorusMix, 15}});
        patch("HOUSE KEY", kKey, {{kCutoff, 2400}, {kChorusMix, 50}, {kReverbMix, 25}});
        patch("HOUSE PAD", kPad, {{kCutoff, 1500}, {kChorusMix, 65}, {kReverbMix, 45}, {kReverbDecay, 3.5f}});
        patch("HOUSE STRINGS", kStrings, {{kCutoff, 2000}, {kReverbMix, 40}, {kChorusMix, 50}});
        patch("HOUSE BRASS", kBrass, {{kCutoff, 1800}, {kDrive, 25}, {kChorusMix, 40}});

        // ---- INDIE: warm, a little detuned, a lot of chorus ----
        patch("INDIE LEAD", kLead, {{kCutoff, 2600}, {kChorusMix, 55}, {kDelayMix, 20}});
        patch("INDIE BASS", kBass, {{kCutoff, 750}, {kChorusMix, 20}, {kReverbMix, 12}});
        patch("INDIE KEY", kKey, {{kCutoff, 2200}, {kChorusMix, 55}, {kReverbMix, 35}});
        patch("INDIE PAD", kPad, {{kCutoff, 1500}, {kChorusMix, 70}, {kReverbMix, 45}});
        patch("INDIE STRINGS", kStrings, {{kCutoff, 2200}, {kChorusMix, 60}, {kReverbMix, 35}});
        patch("INDIE BRASS", kBrass, {{kCutoff, 2200}, {kChorusMix, 45}, {kReverbMix, 30}});

        // ---- INDUSTRIAL: harsh and mechanical ----
        patch("INDUSTRIAL LEAD", kLead, {{kCutoff, 2800}, {kReso, 55}, {kDrive, 55}, {kLfoShape, 2}, {kLfoRate, 9.0f}, {kLfoPitch, 0.16f}, {kDelayMix, 25}});
        patch("INDUSTRIAL BASS", kBass, {{kCutoff, 600}, {kReso, 60}, {kDrive, 55}, {kSubLevel, 60}, {kChorusMix, 10}});
        patch("INDUSTRIAL KEY", kKey, {{kCutoff, 2000}, {kReso, 45}, {kChorusMix, 40}});
        patch("INDUSTRIAL PAD", kPad, {{kCutoff, 1100}, {kReso, 40}, {kChorusMix, 60}, {kReverbMix, 45}});
        patch("INDUSTRIAL STRINGS", kStrings, {{kCutoff, 1800}, {kDrive, 40}, {kChorusMix, 45}});
        patch("INDUSTRIAL BRASS", kBrass, {{kCutoff, 1700}, {kReso, 50}, {kDrive, 50}, {kChorusMix, 35}});

        // ---- JAZZ: rounded, with vibrato ----
        patch("JAZZ LEAD", kLead, {{kCutoff, 2600}, {kChorusMix, 35}, {kLfoShape, 0}, {kLfoRate, 5.0f}, {kLfoPitch, 0.06f}});
        patch("JAZZ BASS", kBass, {{kCutoff, 600}, {kChorusMix, 25}, {kDecay, 250}, {kReverbMix, 14}});
        patch("JAZZ KEY", kKey, {{kCutoff, 2400}, {kChorusMix, 45}, {kReverbMix, 30}});
        patch("JAZZ PAD", kPad, {{kCutoff, 1500}, {kChorusMix, 60}, {kReverbMix, 40}});
        patch("JAZZ STRINGS", kStrings, {{kCutoff, 2200}, {kChorusMix, 55}, {kReverbMix, 32}});
        patch("JAZZ BRASS", kBrass, {{kCutoff, 2200}, {kDrive, 20}, {kChorusMix, 40}});

        // ---- LATIN: bright and percussive ----
        patch("LATIN LEAD", kLead, {{kCutoff, 4200}, {kChorusMix, 45}, {kDelayMix, 20}});
        patch("LATIN BASS", kBass, {{kCutoff, 700}, {kSubLevel, 55}, {kDecay, 200}, {kChorusMix, 12}});
        patch("LATIN KEY", kKey, {{kCutoff, 3000}, {kChorusMix, 50}, {kReverbMix, 25}});
        patch("LATIN PAD", kPad, {{kCutoff, 1800}, {kChorusMix, 65}, {kReverbMix, 40}});
        patch("LATIN STRINGS", kStrings, {{kCutoff, 3000}, {kChorusMix, 55}, {kReverbMix, 30}});
        patch("LATIN BRASS", kBrass, {{kCutoff, 3000}, {kDrive, 25}, {kChorusMix, 45}});

        // ---- LOFI: soft, wobbly, filtered ----
        patch("LOFI LEAD", kLead, {{kCutoff, 1600}, {kReso, 20}, {kLfoShape, 2}, {kLfoRate, 5.0f}, {kLfoPitch, 0.08f}, {kChorusMix, 45}, {kReverbMix, 25}});
        patch("LOFI BASS", kBass, {{kCutoff, 450}, {kSubLevel, 60}, {kDrive, 20}, {kReverbMix, 12}});
        patch("LOFI KEY", kKey, {{kCutoff, 1500}, {kChorusMix, 50}, {kReverbMix, 30}});
        patch("LOFI PAD", kPad, {{kCutoff, 900}, {kLfoRate, 0.5f}, {kChorusMix, 70}, {kReverbMix, 45}});
        patch("LOFI STRINGS", kStrings, {{kCutoff, 1400}, {kChorusMix, 55}, {kReverbMix, 35}});
        patch("LOFI BRASS", kBrass, {{kCutoff, 1400}, {kDrive, 25}, {kChorusMix, 45}});

        // ---- POP: clean, bright, wide ----
        patch("POP LEAD", kLead, {{kCutoff, 4000}, {kChorusMix, 50}, {kDelayMix, 20}});
        patch("POP BASS", kBass, {{kCutoff, 900}, {kSubLevel, 45}, {kChorusMix, 12}});
        patch("POP KEY", kKey, {{kCutoff, 2800}, {kChorusMix, 45}, {kReverbMix, 25}});
        patch("POP PAD", kPad, {{kCutoff, 2000}, {kChorusMix, 65}, {kReverbMix, 40}});
        patch("POP STRINGS", kStrings, {{kCutoff, 2800}, {kChorusMix, 55}, {kReverbMix, 30}});
        patch("POP BRASS", kBrass, {{kCutoff, 2400}, {kDrive, 20}, {kChorusMix, 45}});

        // ---- PSYTRANCE: the moving one ----
        patch("PSYTRANCE LEAD", kLead, {{kCutoff, 4200}, {kReso, 45}, {kLfoShape, 2}, {kLfoRate, 7.0f}, {kLfoPitch, 0.14f}, {kDelayMix, 25}, {kDelaySync, 1}, {kDelayDiv, 4}, {kReverbMix, 25}});
        patch("PSYTRANCE BASS", kBass, {{kCutoff, 900}, {kReso, 45}, {kDrive, 35}, {kGlide, 50}, {kChorusMix, 15}});
        patch("PSYTRANCE KEY", kKey, {{kCutoff, 2800}, {kChorusMix, 55}, {kReverbMix, 30}});
        patch("PSYTRANCE PAD", kPad, {{kCutoff, 1800}, {kLfoShape, 2}, {kLfoRate, 5.0f}, {kLfoFilter, 0.45f}, {kChorusMix, 75}, {kReverbMix, 50}});
        patch("PSYTRANCE STRINGS", kStrings, {{kCutoff, 2600}, {kLfoShape, 2}, {kLfoRate, 6.0f}, {kLfoPitch, 0.10f}, {kChorusMix, 60}});
        patch("PSYTRANCE BRASS", kBrass, {{kCutoff, 2400}, {kReso, 35}, {kDrive, 35}, {kChorusMix, 45}});

        // ---- RNB: smooth, warm, slow ----
        patch("RNB LEAD", kLead, {{kCutoff, 2400}, {kLfoShape, 0}, {kLfoRate, 4.5f}, {kLfoPitch, 0.05f}, {kChorusMix, 50}, {kReverbMix, 25}});
        patch("RNB BASS", kBass, {{kCutoff, 520}, {kSubLevel, 60}, {kDrive, 20}, {kDecay, 300}, {kChorusMix, 10}});
        patch("RNB KEY", kKey, {{kCutoff, 1900}, {kChorusMix, 55}, {kReverbMix, 35}});
        patch("RNB PAD", kPad, {{kCutoff, 1300}, {kChorusMix, 70}, {kReverbMix, 45}});
        patch("RNB STRINGS", kStrings, {{kCutoff, 2000}, {kChorusMix, 60}, {kReverbMix, 38}});
        patch("RNB BRASS", kBrass, {{kCutoff, 1800}, {kDrive, 18}, {kChorusMix, 45}});

        // ---- ROCK: driven and raw ----
        patch("ROCK LEAD", kLead, {{kCutoff, 3600}, {kDrive, 60}, {kReso, 30}, {kChorusMix, 25}, {kReverbMix, 18}});
        patch("ROCK BASS", kBass, {{kCutoff, 650}, {kDrive, 45}, {kSubLevel, 50}});
        patch("ROCK KEY", kKey, {{kCutoff, 2400}, {kDrive, 35}, {kChorusMix, 30}});
        patch("ROCK PAD", kPad, {{kCutoff, 1400}, {kDrive, 40}, {kChorusMix, 55}, {kReverbMix, 30}});
        patch("ROCK STRINGS", kStrings, {{kCutoff, 2600}, {kDrive, 40}, {kChorusMix, 40}});
        patch("ROCK BRASS", kBrass, {{kCutoff, 2600}, {kDrive, 55}, {kChorusMix, 30}});

        // ---- SYNTHWAVE: the 80s, chorus and a slow filter ----
        patch("SYNTHWAVE LEAD", kLead, {{kCutoff, 3200}, {kChorusMix, 60}, {kDelayMix, 25}});
        patch("SYNTHWAVE BASS", kBass, {{kCutoff, 700}, {kChorusMix, 25}, {kReverbMix, 12}});
        patch("SYNTHWAVE KEY", kKey, {{kCutoff, 2600}, {kChorusMix, 65}, {kReverbMix, 35}});
        patch("SYNTHWAVE PAD", kPad, {{kCutoff, 1800}, {kChorusMix, 85}, {kLfoRate, 0.25f}, {kReverbMix, 50}});
        patch("SYNTHWAVE STRINGS", kStrings, {{kCutoff, 2400}, {kChorusMix, 70}, {kReverbMix, 40}});
        patch("SYNTHWAVE BRASS", kBrass, {{kCutoff, 2400}, {kChorusMix, 50}, {kReverbMix, 30}});

        // ---- TECHNO: dark and metallic ----
        patch("TECHNO LEAD", kLead, {{kCutoff, 2800}, {kReso, 45}, {kDrive, 35}, {kDelayMix, 25}, {kDelaySync, 1}, {kDelayDiv, 4}});
        patch("TECHNO BASS", kBass, {{kCutoff, 650}, {kReso, 40}, {kDrive, 40}, {kSubLevel, 55}, {kChorusMix, 10}});
        patch("TECHNO KEY", kKey, {{kCutoff, 2000}, {kReso, 35}, {kChorusMix, 45}});
        patch("TECHNO PAD", kPad, {{kCutoff, 1200}, {kChorusMix, 70}, {kReverbMix, 50}, {kReverbDecay, 4.5f}});
        patch("TECHNO STRINGS", kStrings, {{kCutoff, 1800}, {kDrive, 25}, {kReverbMix, 38}});
        patch("TECHNO BRASS", kBrass, {{kCutoff, 1900}, {kReso, 30}, {kDrive, 35}, {kChorusMix, 40}});

        // ---- TRANCE: soaring and wide ----
        patch("TRANCE LEAD", kLead, {{kCutoff, 5200}, {kChorusMix, 70}, {kDelayMix, 30}, {kDelaySync, 1}, {kDelayDiv, 10}, {kReverbMix, 25}});
        patch("TRANCE BASS", kBass, {{kCutoff, 1000}, {kSubLevel, 45}, {kAttack, 5}, {kChorusMix, 15}});
        patch("TRANCE KEY", kKey, {{kCutoff, 3200}, {kChorusMix, 60}, {kReverbMix, 30}});
        patch("TRANCE PAD", kPad, {{kCutoff, 2600}, {kChorusMix, 80}, {kReverbMix, 55}, {kReverbDecay, 5.0f}});
        patch("TRANCE STRINGS", kStrings, {{kCutoff, 3400}, {kChorusMix, 65}, {kReverbMix, 35}});
        patch("TRANCE BRASS", kBrass, {{kCutoff, 2800}, {kDrive, 30}, {kChorusMix, 45}});

        // ---- TRAP: dark, snappy, wide ----
        patch("TRAP LEAD", kLead, {{kCutoff, 3600}, {kOsc2Detune, 12}, {kChorusMix, 60}, {kDelayMix, 22}, {kReverbMix, 20}});
        patch("TRAP BASS", kBass, {{kCutoff, 620}, {kSubLevel, 70}, {kDrive, 40}, {kGlide, 30}, {kChorusMix, 10}});
        patch("TRAP KEY", kKey, {{kCutoff, 2400}, {kChorusMix, 55}, {kReverbMix, 25}});
        patch("TRAP PAD", kPad, {{kCutoff, 1400}, {kChorusMix, 75}, {kReverbMix, 45}});
        patch("TRAP STRINGS", kStrings, {{kCutoff, 2200}, {kChorusMix, 65}, {kReverbMix, 32}});
        patch("TRAP BRASS", kBrass, {{kCutoff, 1900}, {kDrive, 30}, {kReverbMix, 25}});

        // ---- UKG: bouncy, shuffled, warm ----
        patch("UKG LEAD", kLead, {{kCutoff, 2600}, {kChorusMix, 45}, {kDelayMix, 20}, {kDelaySync, 1}, {kDelayDiv, 4}, {kReverbMix, 20}});
        patch("UKG BASS", kBass, {{kCutoff, 750}, {kReso, 40}, {kEnvAmount, 75}, {kDecay, 180}, {kSubLevel, 45}, {kChorusMix, 15}});
        patch("UKG KEY", kKey, {{kCutoff, 2400}, {kChorusMix, 55}, {kReverbMix, 25}});
        patch("UKG PAD", kPad, {{kCutoff, 1400}, {kChorusMix, 65}, {kReverbMix, 40}});
        patch("UKG STRINGS", kStrings, {{kCutoff, 2000}, {kChorusMix, 55}, {kReverbMix, 35}});
        patch("UKG BRASS", kBrass, {{kCutoff, 2200}, {kReso, 30}, {kDrive, 30}, {kChorusMix, 45}});

    }

    // ----- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        for (auto& v : voices_)
        {
            v.osc1.setSampleRate(sampleRate); v.osc2.setSampleRate(sampleRate);
            v.filter.setSampleRate(sampleRate);
            v.ampEnv.setSampleRate(sampleRate); v.filtEnv.setSampleRate(sampleRate);
            v.sub.setSampleRate(sampleRate); v.sub.setWave(Wave::Sine);
        }
        sr_ = (float)sampleRate; invSr_ = 1.0f / sr_;
        chorusL_.prepare((int)(sampleRate * 0.04) + 4);   // 40 ms: allocation happens here, never in process()
        chorusR_.prepare((int)(sampleRate * 0.04) + 4);
        // v1.2.0: the delay line and the plate allocate here too, never in process()
        delay_.prepare(sampleRate, 2, 1600.0f);           // 1600 ms of delay, 2 channels
        reverb_.prepare(sampleRate, 2);
        lfo_.setSampleRate(sampleRate);
        controllers_.prepare(sampleRate, 10.0f);
        volume_.setTime(10.0f, sampleRate);
        volume_.snap(dbToGain(get(kVolume)));
    }

    void reset() override
    {
        for (auto& v : voices_) { v.ampEnv.reset(); v.filtEnv.reset(); v.filter.reset(); v.note = -1; }
        alloc_.reset();
        chorusL_.reset(); chorusR_.reset(); chorusPhase_ = 0.0f;
        delay_.reset(); reverb_.reset();
        lfo_.reset();
        controllers_.reset();
        volume_.snap(dbToGain(get(kVolume)));
    }

    // ----- audio ------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        updateBlockSettings(ctx);

        // [4] Split the block at MIDI event offsets so notes start on the right sample.
        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            if (ev.sampleOffset > pos) { render(ctx, pos, ev.sampleOffset); pos = ev.sampleOffset; }
            handleMidi(ev);
        }
        render(ctx, pos, ctx.numFrames);

        // Mono voice mix is in outputs[0]; apply master volume and copy to the right channel.
        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const float chMix = s_.chorusMix;
        const float chInc = s_.chorusRate / sr_;
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const float g = volume_.process();
            const float dry = L[i] * g;
            if (R && chMix > 0.001f)
            {
                // Stereo chorus: two modulated delays (L and R run a quarter turn apart) around a 14 ms centre.
                const float ph = chorusPhase_;
                const float modL = std::sin(6.2831853f * ph), modR = std::sin(6.2831853f * (ph + 0.25f));
                const float centre = 0.014f * sr_, swing = (0.002f + 0.006f * s_.chorusDepth) * sr_;
                const float wetL = chorusL_.readAllpass(centre + swing * modL);
                const float wetR = chorusR_.readAllpass(centre + swing * modR);
                // -300 dB alternating dither keeps the allpass-interpolated delay out of denormal range when the input is silent
                denorm_ = -denorm_;
                chorusL_.write(dry + denorm_); chorusR_.write(dry + denorm_);
                chorusPhase_ += chInc; if (chorusPhase_ >= 1.0f) chorusPhase_ -= 1.0f;
                const float dryG = 1.0f - 0.45f * chMix, wetG = 0.75f * chMix;
                L[i] = dry * dryG + wetL * wetG;
                R[i] = dry * dryG + wetR * wetG;
            }
            else
            {
                L[i] = dry;
                if (R) R[i] = L[i];
            }
        }

        // v1.2.0: the delay and the reverb, in series, on the stereo master.
        if (R && (s_.delayMix > 0.0f || s_.reverbMix > 0.0f))
        {
            float* io[2] = {L, R};
            if (s_.delayMix > 0.0f) delay_.process(io, 2, ctx.numFrames);
            if (s_.reverbMix > 0.0f) reverb_.process(io, 2, ctx.numFrames);
        }

        // The safety limiter is last, so a hot reverb tail is rounded off instead of clipping the MPC's output.
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            L[i] = softLimit(L[i]);
            if (R) R[i] = softLimit(R[i]);
        }
    }

private:
    static constexpr int kNumVoices = 8;
    static constexpr int kChunk = 64;        // [7] max samples rendered per inner loop (fixed scratch size)
    static constexpr int kCtrlInterval = 16; // [5] samples between coefficient updates
    static constexpr int kDriftInterval = 1100;   // control ticks between new drift targets (~0.4 s at 44.1 kHz)

    // [3] Everything one note needs.
    struct Voice
    {
        PolyBlepOsc osc1, osc2, sub;
        SVF filter;
        ADSR ampEnv, filtEnv;
        Smoother glide;          // smooths the MIDI note number (so glide is linear in pitch)
        int note = -1;
        float velGain = 1.0f;
        int ctrl = 0;            // control-rate counter
        float oscGain1 = 1.0f, oscGain2 = 0.0f;
        // v1.1.0 analogue drift: a slowly wandering random detune, in cents, different for every voice
        Xorshift32 rng;
        float drift = 0.0f, driftTarget = 0.0f;
        int driftCount = 0;
        // VULCAN rotary gate
        float vPhase = 0.0f, vSpin = 0.0f;   // shot phase 0..1, spool-up 0..1
        int vBarrel = 0;                     // which of the six barrels fires this cycle
    };

    // Per-block parameter snapshot (read once; parameters are atomics that the UI thread writes).
    struct Settings
    {
        int wave1, wave2, oct1, oct2, semi2, mode;
        float detune2, lvl1, lvl2, pw, glideMs, cutoff, reso, envAmt, drive, keyTrack, velSens, lfoPitch, lfoFilter, wheelVib;
        float sub, drift, chorusMix, chorusDepth, chorusRate;   // v1.1.0
        float vDepth, vRate, vSpinInc, vSpread;                  // VULCAN
        float delayMix, reverbMix;                          // v1.2.0 FX
    } s_{};

    void updateBlockSettings(mpc::ProcessContext& ctx)
    {
        s_.wave1 = getInt(kOsc1Wave); s_.wave2 = getInt(kOsc2Wave);
        s_.oct1 = getInt(kOsc1Oct);   s_.oct2 = getInt(kOsc2Oct); s_.semi2 = getInt(kOsc2Semi);
        s_.detune2 = get(kOsc2Detune); s_.lvl1 = get(kOsc1Level) * 0.01f; s_.lvl2 = get(kOsc2Level) * 0.01f;
        s_.pw = get(kPulseWidth) * 0.01f; s_.glideMs = get(kGlide);
        s_.cutoff = get(kCutoff); s_.reso = get(kReso) * 0.01f; s_.mode = getInt(kFilterMode);
        s_.envAmt = get(kEnvAmount) * 0.01f; s_.drive = get(kDrive) * 0.01f; s_.keyTrack = get(kKeyTrack) * 0.01f;
        s_.velSens = get(kVelSens) * 0.01f;
        s_.lfoPitch = get(kLfoPitch); s_.lfoFilter = get(kLfoFilter) * 0.01f; s_.wheelVib = get(kWheelVib);
        s_.sub = get(kSubLevel) * 0.01f; s_.drift = get(kDrift) * 0.01f;
        s_.vDepth = get(kVulcanDepth) * 0.01f; s_.vRate = get(kVulcanRate) * (1.0f / 60.0f);   // rounds per minute -> shots per second; s_.vSpread = get(kVulcanSpread) * 0.01f;
        { const float ms = get(kVulcanSpin); s_.vSpinInc = ms > 0.5f ? 1.0f / (ms * 0.001f * (float)ctx.sampleRate) : 1.0f; }
        s_.chorusMix = get(kChorusMix) * 0.01f; s_.chorusDepth = get(kChorusDepth) * 0.01f; s_.chorusRate = get(kChorusRate);

        // v1.2.0: the delay and the reverb follow the panel. The delay's time is either free or locked to the
        // host tempo; the reverb's decay is its RT60 in seconds.
        {
            lumina::fx::DelayParams dp;
            dp.mix = get(kDelayMix) * 0.01f;
            dp.timeMs = get(kDelayTime);
            dp.feedback = get(kDelayFeedback) * 0.01f;
            dp.toneHz = get(kDelayTone);
            dp.duckDb = get(kDelayDuck);
            dp.sync = getBool(kDelaySync);
            dp.beats = divisionBeats(getInt(kDelayDiv));
            dp.bpm = safeBpm(ctx.transport.bpm);
            dp.pingPong = getBool(kDelayPing);
            delay_.setParams(dp);

            lumina::fx::ReverbParams rp;
            rp.mix = get(kReverbMix) * 0.01f;
            rp.size = get(kReverbSize) * 0.01f;
            rp.decaySec = get(kReverbDecay);
            rp.damp = get(kReverbDamp) * 0.01f;
            rp.preDelayMs = get(kReverbPre);
            rp.width = 1.0f;
            reverb_.setParams(rp);
            s_.delayMix = dp.mix;
            s_.reverbMix = rp.mix;
        }

        alloc_.setBendRange((float)getInt(kBendRange));
        volume_.setTarget(dbToGain(get(kVolume)));

        // [6] LFO: free running in Hz, or locked to the song position when synced and playing.
        lfo_.setShape(getInt(kLfoShape));
        if (getBool(kLfoSync))
        {
            lfo_.setTempoSync(ctx.transport.bpm, divisionBeats(getInt(kLfoDiv)));
            if (ctx.transport.playing && ctx.transport.ppqValid) lfo_.syncToPpq(ctx.transport.ppqPos);
        }
        else lfo_.setFrequency(get(kLfoRate));

        const float fa = get(kFAttack), fd = get(kFDecay), fs = get(kFSustain) * 0.01f, fr = get(kFRelease);
        const float aa = get(kAttack),  ad = get(kDecay),  as = get(kSustain) * 0.01f,  ar = get(kRelease);
        for (auto& v : voices_)
        {
            v.ampEnv.set(aa, ad, as, ar);
            v.filtEnv.set(fa, fd, fs, fr);
            v.glide.setTime(s_.glideMs, ctx.sampleRate);
            v.osc1.setWave(s_.wave1); v.osc2.setWave(s_.wave2);
            v.osc1.setPulseWidth(s_.pw); v.osc2.setPulseWidth(s_.pw);
            v.filter.setDrive(1.0f + s_.drive * 9.0f);
        }
    }

    void handleMidi(const mpc::MidiEvent& ev)
    {
        if (ev.isNoteOn())
        {
            const int lastNote = alloc_.lastNote();
            const int vi = alloc_.noteOn(ev.note(), ev.velocity());
            Voice& v = voices_[vi];
            const bool wasSounding = v.ampEnv.isActive();
            v.note = ev.note();
            v.velGain = lerp(1.0f, ev.velocity() / 127.0f, s_.velSens);
            // Glide from the previous note if glide is on, otherwise jump.
            if (s_.glideMs > 0.0f && lastNote >= 0) { if (!wasSounding) v.glide.snap((float)lastNote); v.glide.setTarget((float)ev.note()); }
            else v.glide.snap((float)ev.note());
            if (!wasSounding) { v.osc1.resetPhase(); v.osc2.resetPhase(0.25f); v.sub.resetPhase(); v.filter.reset(); v.vPhase = 0.0f; v.vSpin = 0.0f; v.vBarrel = 0; }
            v.ampEnv.noteOn(); v.filtEnv.noteOn();
            v.ctrl = 0;   // force a coefficient update on the first sample
        }
        else if (ev.isNoteOff())
        {
            const int vi = alloc_.noteOff(ev.note());
            if (vi >= 0) releaseVoice(vi);
        }
        else if (ev.isAllNotesOff())
        {
            alloc_.allNotesOff([this](int vi) { releaseVoice(vi); });
        }
        else if (ev.isController() && ev.controller() == cc::Sustain)
        {
            controllers_.handle(ev);
            alloc_.setSustain(ev.controllerValue() >= 64, [this](int vi) { releaseVoice(vi); });
        }
        else if (ev.isPitchBend())
        {
            alloc_.setPitchBend(ev.pitchBend());
        }
        else controllers_.handle(ev);   // mod wheel, aftertouch, other CCs
    }

    void releaseVoice(int vi) { voices_[vi].ampEnv.noteOff(); voices_[vi].filtEnv.noteOff(); }

    // Render [from, to) of the block for all voices into outputs[0].
    void render(mpc::ProcessContext& ctx, int from, int to)
    {
        float* out = ctx.outputs[0];
        while (from < to)
        {
            const int n = (to - from) < kChunk ? (to - from) : kChunk;
            // Global modulators for this chunk.
            for (int i = 0; i < n; ++i) { controllers_.tick(); lfoBuf_[i] = lfo_.process(); }
            const float vib = s_.lfoPitch + s_.wheelVib * controllers_.modWheel();   // semitones of pitch LFO

            for (int vi = 0; vi < kNumVoices; ++vi)
            {
                Voice& v = voices_[vi];
                if (!v.ampEnv.isActive()) continue;
                renderVoice(v, out + from, n, vib);
                if (!v.ampEnv.isActive()) alloc_.voiceFinished(vi);   // release finished: free the slot
            }
            from += n;
        }
    }

    void renderVoice(Voice& v, float* out, int n, float vibSemitones)
    {
        for (int i = 0; i < n; ++i)
        {
            const float fenv = v.filtEnv.process();
            const float aenv = v.ampEnv.process();

            // [5] Control rate: pitch, cutoff and filter coefficients every kCtrlInterval samples.
            if ((v.ctrl++ % kCtrlInterval) == 0)
            {
                const float note = v.glide.value();
                const float lfo = lfoBuf_[i];
                // Analogue drift: pick a new random target every ~0.4 s, glide to it (about 0.45 s time constant). +-8 cents at 100 %.
                if (++v.driftCount >= kDriftInterval) { v.driftCount = 0; v.driftTarget = v.rng.nextBipolar(); }
                v.drift += (v.driftTarget - v.drift) * 0.0008f;
                const float driftRatio = s_.drift > 0.0f ? centsToRatio(v.drift * s_.drift * 8.0f) : 1.0f;
                const float hz = alloc_.noteToHz(note) * semitonesToRatio(lfo * vibSemitones) * driftRatio;
                v.osc1.setFrequency(hz * semitonesToRatio(12.0f * (float)s_.oct1));
                v.osc2.setFrequency(hz * semitonesToRatio(12.0f * (float)s_.oct2 + (float)s_.semi2) * centsToRatio(s_.detune2));
                v.sub.setFrequency(hz * semitonesToRatio(12.0f * (float)(s_.oct1 - 1)));   // one octave under Osc1

                float octaves = s_.envAmt * fenv * 8.0f                 // envelope: up to +-8 octaves
                              + s_.lfoFilter * lfo * 4.0f               // LFO: up to +-4 octaves
                              + s_.keyTrack * (note - 60.0f) / 12.0f;   // key tracking around middle C
                v.filter.set(s_.cutoff * std::pow(2.0f, clamp(octaves, -10.0f, 10.0f)), s_.reso);
            }
            // The glide smoother runs per sample so it is accurate even with short glides.
            v.glide.process();

            float mix = v.osc1.process() * s_.lvl1 + v.osc2.process() * s_.lvl2;
            if (s_.sub > 0.0f) mix += v.sub.process() * s_.sub * 0.8f;
            const float filtered = v.filter.process(mix, s_.mode);
            // VULCAN: one shot per barrel-cycle. The spool-up eases in (barrels start nearly still), a shot is a 45 %-duty
            // parabolic pulse (continuous at both ends, so no clicks), and the six barrels fire in a fixed, slightly
            // uneven pattern, which is what makes it ratchet rather than buzz evenly.
            float vg = 1.0f;
            if (s_.vDepth > 0.0f)
            {
                if (v.vSpin < 1.0f) { v.vSpin += s_.vSpinInc; if (v.vSpin > 1.0f) v.vSpin = 1.0f; }
                v.vPhase += s_.vRate * (0.05f + 0.95f * v.vSpin * v.vSpin) * invSr_;
                if (v.vPhase >= 1.0f) { v.vPhase -= 1.0f; v.vBarrel = v.vBarrel >= 5 ? 0 : v.vBarrel + 1; }
                const float u = v.vPhase * (1.0f / 0.45f);
                const float shot = u < 1.0f ? 4.0f * u * (1.0f - u) : 0.0f;
                const float barrel = 1.0f - s_.vSpread * vDip_[v.vBarrel];
                vg = (1.0f - s_.vDepth * (1.0f - shot * barrel)) * (1.0f + s_.vDepth * 1.4f);   // makeup for the gated-off time
            }
            out[i] += filtered * aenv * v.velGain * vg * 0.5f;
        }
    }

    Voice voices_[kNumVoices];
    VoiceAllocator<kNumVoices> alloc_;
    LFO lfo_;
    MidiControllers controllers_;
    Smoother volume_;
    DelayLine chorusL_, chorusR_;      // v1.1.0 stereo chorus (allocated in prepare())
    lumina::fx::Delay delay_;          // v1.2.0 delay (allocated in prepare())
    lumina::fx::Reverb reverb_;        // v1.2.0 plate (allocated in prepare())
    float chorusPhase_ = 0.0f;
    float denorm_ = 1e-15f;
    // Safety limiter on the master: transparent below 0.7, then a tanh knee that approaches (never reaches) full scale,
    // so a chord on a hot patch is rounded off instead of hard-clipping on the MPC's output.
    static float softLimit(float x)
    {
        const float a = x < 0.0f ? -x : x;
        if (a < 0.7f) return x;
        const float y = 0.7f + 0.3f * std::tanh((a - 0.7f) * (1.0f / 0.3f));
        return x < 0.0f ? -y : y;
    }
    float sr_ = 44100.0f, invSr_ = 1.0f / 44100.0f;
    const float vDip_[6] = {0.00f, 0.18f, 0.07f, 0.30f, 0.12f, 0.40f};   // VULCAN: how far each barrel's shot falls short at full Spread
    float lfoBuf_[kChunk] = {0};
};

MPC_PLUGIN_ENTRY(F4Phantom)
