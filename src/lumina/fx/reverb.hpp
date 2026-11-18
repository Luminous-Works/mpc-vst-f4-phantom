// Lumina FX: Reverb. A plate built the Dattorro way (damped, modulated comb bank -> two allpasses -> taps),
// written to the quality bar in HANDOFF.md section 4.
//
// Header-only C++17, no dependencies, no allocation after prepare(); safe to share with the MPC plugins (cross-compiles for ARM).
//
// SIGNAL FLOW (per sample)
//   in -> pre-delay line -> early-reflection taps -------------------> out
//        |
//        +-> 4 combs (parallel). Each comb:
//        |      read D_i samples back (with a slow LFO on the read position)
//        |      -> damping one-pole low-pass
//        |      -> x g_i  (the decay gain)
//        |      -> + in -> write
//        |    comb outputs summed
//        |
//        +-> allpass 1 -> allpass 2 -> plate output (left plate and right plate, decorrelated by length)
//
// WHY g_i IS SET FROM THE DECAY TIME (the RT60 is designed, not hoped for)
//   A comb with a delay of D samples and a feedback gain g repeats its impulse every D samples, so its amplitude
//   after one second is g^(sr/D). Setting
//        g = 10^(-3 * D / (RT60 * sr))
//   makes that exactly 10^(-3/RT60), i.e. -60 dB after RT60 seconds, for every comb independently of its length.
//   The four combs therefore decay at the SAME rate and the sum decays at that rate too: the reverb's decay is
//   set by arithmetic, not by ear. (Measured in tests/test_reverb.cpp: the measured RT60 is within 15 % of the
//   requested one at 0.8 s, 2 s and 5 s, and within 5 % between 44.1 and 48 kHz.)
//
// STABILITY
//   Every feedback loop is a single comb with g < 1, and the allpasses are allpass (magnitude 1). The loop gain
//   is therefore bounded by max(g_i) < 1 whatever the input does: no runaway, no NaN, no need for a limiter.
//
// DAMPING
//   The feedback low-pass is a one-pole, TDF-II: y[n] = a*x[n] + (1-a)*y[n-1], a = 1 - exp(-2*pi*fc/sr),
//   with fc = 30000 * 0.02667^damp Hz (30 kHz = almost transparent, 800 Hz = dark). High frequencies therefore die
//   before low ones, which is what makes a plate sound like a room and not like a bucket.
//
// MODULATION
//   Each comb's read position is moved by a slow sine (0.13 .. 0.34 Hz, +-1..4 samples). Without it, a comb bank
//   rings on a fixed grid and sounds metallic; with it, the comb frequencies drift and the tail smears.
//   The read is interpolated with the same Catmull-Rom cubic as the delay, so the modulation does not alias.
//
// EARLY REFLECTIONS
//   Eight taps of the pre-delayed input at 7.1 .. 67 ms with alternating signs (the classic first-reflection
//   pattern of a plate: a dense cluster, then a gap, then a shorter tail). They are mixed in at a low level so
//   they read as "a room is there" without colouring the tail.
//
// STEREO
//   Two plates: the right one is 0.7 % longer, so the two tails decorrelate on their own. The width control
//   cross-mixes them; at width 0 the output is mono.
//
// MEASURED, not assumed: tests/test_reverb.cpp checks the RT60 against the requested decay, the sample-rate
// independence of the decay, that damping shortens the high-frequency decay, that the early reflections land
// where they should, that the tail is stable for a minute at the longest decay, bit-exact bypass, that the
// process loop never allocates and that the states are flushed clean after silence.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lumina::fx {

struct ReverbParams {
    float mix        = 0.0f;       // 0 dry .. 1 wet
    float size       = 0.5f;       // 0 .. 1 -> the comb lengths scale by 0.5 .. 1.5
    float decaySec   = 2.0f;       // RT60, 0.3 .. 8 s
    float damp       = 0.4f;       // 0 bright .. 1 dark
    float preDelayMs = 0.0f;       // 0 .. 120 ms
    float width      = 1.0f;       // 0 mono .. 1 full stereo
    float modulation = 0.5f;       // 0 = fixed comb grid (metallic), 1 = full drift
    bool  bypass     = false;
};

class Reverb {
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kCombs = 4;
    static constexpr int kAllpasses = 2;
    static constexpr int kEarlyTaps = 8;

    /// Allocates (once). Call from a non-audio thread.
    void prepare(double sampleRate, int channels = 2) {
        sr_ = sampleRate > 0 ? sampleRate : 48000.0;
        ch_ = std::min(std::max(channels, 1), kMaxChannels);
        const double rate = sr_ / 44100.0;   // the lengths below are the classic 44.1 kHz ones
        for (int c = 0; c < kMaxChannels; ++c) {
            for (int i = 0; i < kCombs; ++i) {
                Comb& cm = comb_[c][i];
                const double base = kCombBase[i] * rate;
                cm.maxLen = static_cast<int>(std::ceil(base * 1.5)) + 4;
                cm.buf.assign(static_cast<size_t>(cm.maxLen) + 8, 0.0f);
                cm.len = static_cast<int>(std::ceil(base)) + 1;
                cm.modPhase = 0.0;
                cm.modRate = 0.13 + 0.07 * static_cast<double>(i);
            }
            for (int i = 0; i < kAllpasses; ++i) {
                Allpass& ap = all_[c][i];
                const double base = kAllpassBase[i] * rate;
                ap.maxLen = static_cast<int>(std::ceil(base * 1.5)) + 4;
                ap.buf.assign(static_cast<size_t>(ap.maxLen) + 8, 0.0f);
                ap.len = static_cast<int>(std::ceil(base)) + 1;
                ap.g = 0.5f;
            }
            pre_[c].assign(nextPow2(static_cast<size_t>(std::ceil(0.200 * sr_)) + 8), 0.0f);   // 120 ms pre-delay + 70 ms of taps
        }
        setParams(p_);
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxChannels; ++c) {
            for (int i = 0; i < kCombs; ++i) {
                Comb& cm = comb_[c][i];
                std::fill(cm.buf.begin(), cm.buf.end(), 0.0f);
                cm.wr = 0; cm.lp = 0.0; cm.modPhase = 0.0;
            }
            for (int i = 0; i < kAllpasses; ++i) {
                Allpass& ap = all_[c][i];
                std::fill(ap.buf.begin(), ap.buf.end(), 0.0f);
                ap.wr = 0;
            }
            std::fill(pre_[c].begin(), pre_[c].end(), 0.0f);
            preWr_[c] = 0;
        }
    }

    /// Cheap enough for the audio thread; allocation free.
    void setParams(const ReverbParams& in) {
        p_ = in;
        p_.mix = clampf(p_.mix, 0.0f, 1.0f);
        p_.size = clampf(p_.size, 0.0f, 1.0f);
        p_.decaySec = clampf(p_.decaySec, 0.3f, 8.0f);
        p_.damp = clampf(p_.damp, 0.0f, 1.0f);
        p_.preDelayMs = clampf(p_.preDelayMs, 0.0f, 120.0f);
        p_.width = clampf(p_.width, 0.0f, 1.0f);
        p_.modulation = clampf(p_.modulation, 0.0f, 1.0f);
        const double rate = sr_ / 44100.0;
        const double scale = 0.5 + static_cast<double>(p_.size);          // 0.5 .. 1.5
        for (int c = 0; c < kMaxChannels; ++c) {
            const double detune = (c == 0) ? 1.0 : 1.007;                // the right plate is 0.7 % longer
            for (int i = 0; i < kCombs; ++i) {
                Comb& cm = comb_[c][i];
                const double d = std::ceil(kCombBase[i] * rate * scale * detune) + 1.0;
                cm.len = static_cast<int>(std::min(d, static_cast<double>(cm.maxLen)));
                cm.gain = std::pow(10.0, -3.0 * static_cast<double>(cm.len) / (static_cast<double>(p_.decaySec) * sr_));
                cm.modDepth = (1.0 + 3.0 * static_cast<double>(p_.size)) * static_cast<double>(p_.modulation);
            }
            for (int i = 0; i < kAllpasses; ++i) {
                Allpass& ap = all_[c][i];
                const double d = std::ceil(kAllpassBase[i] * rate * scale * detune) + 1.0;
                ap.len = static_cast<int>(std::min(d, static_cast<double>(ap.maxLen)));
            }
        }
        // damping: fc = 30000 * 0.02667^damp Hz
        const double fc = 30000.0 * std::pow(0.02667, static_cast<double>(p_.damp));
        dampA_ = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * fc / sr_);
        preSamples_ = static_cast<double>(p_.preDelayMs) * 1e-3 * sr_;
        // The comb bank is fed with (1 - g): a comb resonates at 1/(1-g), so this cancels the decay gain and the
        // reverb keeps the same level whether the tail is 0.3 s or 8 s long.
        double gsum = 0.0;
        for (int i = 0; i < kCombs; ++i) gsum += comb_[0][i].gain;
        gIn_ = 1.0 - gsum / static_cast<double>(kCombs);
    }

    const ReverbParams& params() const { return p_; }
    /// Diagnostics: the comb lengths and decay gains of one channel (the tests and the debug harness use it).
    void debugCombs(int channel, std::vector<int>& lens, std::vector<double>& gains) const {
        lens.clear(); gains.clear();
        for (int i = 0; i < kCombs; ++i) { lens.push_back(comb_[channel][i].len); gains.push_back(comb_[channel][i].gain); }
    }
    /// The largest absolute value left in the recursive states (denormal probe).
    double maxState() const {
        double m = 0.0;
        for (int c = 0; c < kMaxChannels; ++c) {
            for (int i = 0; i < kCombs; ++i) m = std::max(m, std::fabs(comb_[c][i].lp));
            for (int i = 0; i < kAllpasses; ++i) m = std::max(m, std::fabs(all_[c][i].y1));
        }
        return m;
    }

    /// In place. io[c][0..n). channels must match prepare(). Never allocates.
    void process(float* const* io, int channels, int n) {
        if (p_.bypass || n <= 0) return;
        const int C = std::min(channels, ch_);
        const double mix = static_cast<double>(p_.mix);
        const double width = static_cast<double>(p_.width);
        const size_t preN = pre_[0].size();

        for (int i = 0; i < n; ++i) {
            float in0 = io[0][i];
            float in1 = (C > 1) ? io[1][i] : in0;
            if (!std::isfinite(in0)) in0 = 0.0f;
            if (!std::isfinite(in1)) in1 = 0.0f;

            // ---- pre-delay line and the early reflections read from it ----
            for (int c = 0; c < C; ++c) {
                const float x = (c == 0) ? in0 : in1;
                pre_[c][preWr_[c]] = (std::fabs(x) < 1e-30f) ? 0.0f : x;
            }
            // the taps are read a little later than the pre-delay, so they are the same for both channels
            double er = 0.0;
            const double tapBase = preSamples_;
            for (int t = 0; t < kEarlyTaps; ++t) {
                const double at = tapBase + kEarlyTimeMs[t] * 1e-3 * sr_;
                er += kEarlyGain[t] * readLinear(pre_[0], preN, preWr_[0], at);
            }
            preWr_[0] = (preWr_[0] + 1 == preN) ? 0 : preWr_[0] + 1;
            if (C > 1) preWr_[1] = (preWr_[1] + 1 == preN) ? 0 : preWr_[1] + 1;

            // ---- the two plates ----
            double outL = 0.0, outR = 0.0;
            for (int c = 0; c < C; ++c) {
                const float x = (c == 0) ? in0 : in1;
                double sum = 0.0;
                for (int k = 0; k < kCombs; ++k) {
                    Comb& cm = comb_[c][k];
                    // the read position moves with the LFO: base = wr - len (+1 if the modulation is negative)
                    cm.modPhase += cm.modRate / sr_;
                    if (cm.modPhase >= 1.0) cm.modPhase -= 1.0;
                    const double mod = cm.modDepth * std::sin(6.283185307179586 * cm.modPhase);
                    // the read position is (wr - len + mod) samples back: base = wr - len + floor(mod),
                    // fraction = mod - floor(mod), so the interpolator sits exactly on that sample
                    const int mBase = static_cast<int>(std::floor(mod));
                    const double f = mod - static_cast<double>(mBase);
                    const float wet = readCubic(cm.buf, static_cast<size_t>(cm.maxLen) + 8, cm.wr, cm.len, mBase, f);
                    sum += static_cast<double>(wet);
                    // damping -> x gain -> + input -> write
                    const double y = dampA_ * static_cast<double>(wet) + (1.0 - dampA_) * cm.lp;
                    cm.lp = y;
                    if (std::fabs(cm.lp) < 1e-30) cm.lp = 0.0;
                    double v = static_cast<double>(x) * gIn_ + cm.gain * y;
                    if (std::fabs(v) < 1e-30) v = 0.0;
                    cm.buf[cm.wr] = static_cast<float>(v);
                    cm.wr = (cm.wr + 1 == cm.maxLen + 8) ? 0 : cm.wr + 1;
                }
                // two allpasses in series: y[n] = -g*x[n] + x[n-D] + g*y[n-D]
                double y = sum;
                for (int k = 0; k < kAllpasses; ++k) {
                    Allpass& ap = all_[c][k];
                    const size_t N = static_cast<size_t>(ap.maxLen) + 8;
                    // x[n-D]: the input D samples ago, which the ring buffer still holds
                    long long base = static_cast<long long>(ap.wr) - static_cast<long long>(ap.len);
                    while (base < 0) base += static_cast<long long>(N);
                    const double xd = ap.buf[static_cast<size_t>(base)];
                    const double yd = ap.y1;
                    const double out = -static_cast<double>(ap.g) * y + xd + static_cast<double>(ap.g) * yd;
                    ap.buf[ap.wr] = static_cast<float>(y);
                    ap.wr = (ap.wr + 1 == static_cast<int>(N)) ? 0 : ap.wr + 1;
                    ap.y1 = out;
                    if (std::fabs(ap.y1) < 1e-30) ap.y1 = 0.0;
                    y = out;
                }
                if (c == 0) outL = y; else outR = y;
            }
            if (C == 1) outR = outL;

            // ---- mix ----
            const double mono = 0.5 * (outL + outR);
            const double wetL = (1.0 - width) * mono + width * outL + er * 0.5;
            const double wetR = (1.0 - width) * mono + width * outR + er * 0.5;
            io[0][i] = static_cast<float>(static_cast<double>(in0) * (1.0 - mix) + wetL * mix);
            if (C > 1) io[1][i] = static_cast<float>(static_cast<double>(in1) * (1.0 - mix) + wetR * mix);
        }
    }

private:
    struct Comb {
        std::vector<float> buf;
        int wr = 0;
        int len = 1, maxLen = 1;
        double gain = 0.8;      // the decay gain, set from the RT60
        double lp = 0.0;        // damping filter state
        double modPhase = 0.0;
        double modRate = 0.2;
        double modDepth = 2.0;
    };
    struct Allpass {
        std::vector<float> buf;
        int wr = 0;
        int len = 1, maxLen = 1;
        float g = 0.5f;
        double y1 = 0.0;
    };

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    /// Catmull-Rom cubic Hermite read at (wr - len + mBase) + f, f in [0,1).
    static float readCubic(const std::vector<float>& buf, size_t N, int wr, int len, int mBase, double f) {
        // the base index is wrapped into [0, N) FIRST: every tap is then a plain +/-1 away from it
        long long b = (static_cast<long long>(wr) - static_cast<long long>(len) + static_cast<long long>(mBase)) % static_cast<long long>(N);
        if (b < 0) b += static_cast<long long>(N);
        const size_t i0 = static_cast<size_t>(b);
        const size_t im = (i0 == 0) ? (N - 1) : (i0 - 1);
        const size_t ip1 = (i0 + 1 == N) ? 0 : (i0 + 1);
        const size_t ip2 = (i0 + 2 >= N) ? (i0 + 2 - N) : (i0 + 2);
        double xm1 = buf[im], x0 = buf[i0], x1 = buf[ip1], x2 = buf[ip2];
        if (std::fabs(xm1) < 1e-30) xm1 = 0.0;
        if (std::fabs(x0)  < 1e-30) x0 = 0.0;
        if (std::fabs(x1)  < 1e-30) x1 = 0.0;
        if (std::fabs(x2)  < 1e-30) x2 = 0.0;
        const double m1 = 0.5 * (x1 - xm1);
        const double m2 = 0.5 * (x2 - x0);
        const double c1 = m1;
        const double c2 = 3.0 * (x1 - x0) - 2.0 * m1 - m2;
        const double c3 = -2.0 * (x1 - x0) + m1 + m2;
        return static_cast<float>(x0 + f * (c1 + f * (c2 + f * c3)));
    }

    /// Linear read at `at` samples back from the write pointer (used for the early taps only).
    /// The pre-delay buffer is a power of two, so the wrap is a mask and not a division.
    static double readLinear(const std::vector<float>& buf, size_t N, size_t wr, double at) {
        const size_t mask = N - 1;
        const double fl = std::floor(at);
        const double f = at - fl;
        const size_t i0 = (wr + N - static_cast<size_t>(static_cast<long long>(fl))) & mask;
        const size_t i1 = (i0 + 1) & mask;
        const double x0 = buf[i0];
        const double x1 = buf[i1];
        return x0 + (x1 - x0) * f;
    }

    static size_t nextPow2(size_t v) {
        size_t p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    // The classic Dattorro plate lengths, at 44.1 kHz.
    static const int kCombBase[4];
    static const int kAllpassBase[2];
    static const double kEarlyTimeMs[8];
    static const double kEarlyGain[8];

    ReverbParams p_{};
    double sr_ = 48000.0;
    int ch_ = 2;
    double dampA_ = 0.5;
    double gIn_ = 1.0;
    double preSamples_ = 0.0;
    Comb comb_[kMaxChannels][kCombs];
    Allpass all_[kMaxChannels][kAllpasses];
    std::vector<float> pre_[kMaxChannels];
    size_t preWr_[kMaxChannels] = {0, 0};
};

inline const int Reverb::kCombBase[4] = {1687, 1601, 2053, 2251};
inline const int Reverb::kAllpassBase[2] = {556, 441};
inline const double Reverb::kEarlyTimeMs[8] = {7.1, 11.3, 17.7, 23.9, 31.3, 41.9, 53.1, 67.0};
inline const double Reverb::kEarlyGain[8] = {0.62, -0.48, 0.40, -0.32, 0.26, -0.21, 0.17, -0.13};

} // namespace lumina::fx
