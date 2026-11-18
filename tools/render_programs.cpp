// render_programs.cpp - render every factory program of F4 Phantom and MEASURE it.
//
// The friend who tested the first fourteen patches liked their warmth and width but said fourteen is too thin.
// The library now carries one voice of every family in every genre, so every one of those patches has to earn
// its place: it must make a sound, it must sit at a sensible level, and it must not run into the limiter.
//
// This tool loads the native build, walks every program the plugin offers, plays a chord into it and reports
// the peak and the RMS of each one. A patch that is silent, or that slams the safety limiter, is a failure.
//
// Build (from the SDK root, after tools/build.py has made build/native/f4-phantom.dll):
//   g++ -std=c++17 -O2 -I framework tools/render_programs.cpp -o build/render_programs.exe
//   build/render_programs.exe "<F4 Phantom>/build/native/f4-phantom.dll"
#include "mpc_vst2_abi.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const double kSr = 44100.0;
static const int kBlock = 256;

static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) {
    if (op == audioMasterVersion) return 2400;
    return 0;
}

// One MIDI event: a note on or off.
struct MidiBuf {
    VstEvent ev[8];
    VstEvents events;
    void begin() { events.numEvents = 0; events.reserved = 0; events.events[0] = ev; }
    void add(int offset, int status, int d1, int d2) {
        VstMidiEvent* m = (VstMidiEvent*)&ev[events.numEvents];
        m->type = 1;                                  // kVstMidiType
        m->byteSize = sizeof(VstMidiEvent);
        m->deltaFrames = offset;
        m->flags = 0;
        m->noteLength = 0;
        m->noteOffset = 0;
        m->midiData[0] = (char)status;
        m->midiData[1] = (char)d1;
        m->midiData[2] = (char)d2;
        m->midiData[3] = 0;
        m->detune = 0;
        m->noteOffVelocity = 0;
        m->reserved1 = 0;
        m->reserved2 = 0;
        ++events.numEvents;
    }
};

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { std::printf("usage: render_programs <plugin.dll>\n"); return 2; }
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { std::printf("cannot load %s\n", argv[1]); return 2; }
    auto mainFn = (AEffect * (*)(audioMasterCallback))GetProcAddress(h, "VSTPluginMain");
    if (!mainFn) { std::printf("no VSTPluginMain\n"); return 2; }
    std::printf("loaded\n");
    AEffect* e = mainFn(host);
    std::printf("got the effect\n");
    e->dispatcher(e, effOpen, 0, 0, nullptr, 0);
    std::printf("opened\n");
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, (float)kSr);
    e->dispatcher(e, effSetBlockSize, 0, kBlock, nullptr, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);
    std::printf("opened, %d programs\n", e->numPrograms);
    const int nPrograms = e->numPrograms;
    std::vector<float> L(kBlock), R(kBlock);
    std::printf("%d programs, %d parameters\n", nPrograms, e->numParams);
    float* out[2] = {L.data(), R.data()};
    MidiBuf midi;

    int silent = 0, hot = 0, nan = 0;
    double worstQuiet = 0.0, loudest = -200.0;
    std::string worstQuietName, loudestName;

    for (int p = 0; p < nPrograms; ++p) {
        char nameBuf[128] = {0};
        e->dispatcher(e, effGetProgramNameIndexed, p, 0, nameBuf, 0);
        std::string name(nameBuf);
        e->dispatcher(e, effSetProgram, p, p, nullptr, (float)p);   // this SDK reads the index from `value`

        // a chord: C3, E3, G3, C4 - enough to show a patch's level and its width
        const int notes[4] = {48, 52, 55, 60};
        midi.begin();
        for (int i = 0; i < 4; ++i) midi.add(0, 0x90, notes[i], 100);
        e->dispatcher(e, effProcessEvents, 0, 0, &midi.events, 0);

        double peak = 0.0, sumsq = 0.0;
        long long n = 0;
        const int blocks = (int)(kSr * 2.0 / kBlock);
        for (int b = 0; b < blocks; ++b) {
            e->processReplacing(e, nullptr, out, kBlock);
            for (int i = 0; i < kBlock; ++i) {
                const double a = std::fabs((double)L[i]);
                if (!(a == a)) { ++nan; }
                if (a > peak) peak = a;
                sumsq += (double)L[i] * (double)L[i];
                ++n;
            }
        }
        // all notes off, so the next program starts clean
        midi.begin();
        for (int i = 0; i < 4; ++i) midi.add(0, 0x80, notes[i], 0);
        e->dispatcher(e, effProcessEvents, 0, 0, &midi.events, 0);
        for (int b = 0; b < 8; ++b) e->processReplacing(e, nullptr, out, kBlock);

        const double rms = std::sqrt(sumsq / (double)n);
        const double peakDb = 20.0 * std::log10(peak > 1e-9 ? peak : 1e-9);
        const double rmsDb = 20.0 * std::log10(rms > 1e-9 ? rms : 1e-9);
        bool bad = false;
        if (peakDb < -45.0) { ++silent; bad = true; }
        if (peakDb > -0.05) { ++hot; bad = true; }
        if (rmsDb < -50.0 && peakDb < -45.0) { ++silent; }
        if (peakDb < worstQuiet) { worstQuiet = peakDb; worstQuietName = name; }
        if (peakDb > loudest) { loudest = peakDb; loudestName = name; }
        std::printf("%-14s peak %7.2f dBFS  rms %7.2f dBFS%s\n", name.c_str(), peakDb, rmsDb, bad ? "   <-- CHECK" : "");
    }
    std::printf("\n%d programs: %d silent, %d at the limiter, %d non-finite samples\n", nPrograms, silent, hot, nan);
    std::printf("quietest: %s at %.2f dBFS; loudest: %s at %.2f dBFS\n",
                worstQuietName.c_str(), worstQuiet, loudestName.c_str(), loudest);
    return (silent || hot || nan) ? 1 : 0;
}
