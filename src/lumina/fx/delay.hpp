// Lumina FX: Delay. The second reference insert, written to the quality bar in HANDOFF.md section 4.
//
// Header-only C++17, no dependencies, no allocation after prepare(); safe to share with the MPC plugins (cross-compiles for ARM).
//
// SIGNAL FLOW (per sample, per channel)
//   in -> dry ---------------------------------------------------------------> out
//   in -> delay line (read D samples back, FRACTIONAL) -> wet --------------> out
//        wet -> damping one-pole low-pass -> soft saturation -> DC block -> x feedback -> + in -> write
//   out = dry * (1 - mix) + wet * mix * duckGain
//
// FRACTIONAL READ (why the echo lands on the sample, not near it)
//   The read position is D samples back, D = integer part + fraction f in [0,1).
//   The four samples around the read point (x[-1], x[0], x[1], x[2]) are interpolated with the Catmull-Rom cubic
//   Hermite form (tangents m1 = (x1 - xm1)/2, m2 = (x2 - x0)/2):
//        c1 = m1
//        c2 = 3*(x1 - x0) - 2*m1 - m2
//        c3 = -2*(x1 - x0) + m1 + m2
//        y  = x0 + f*(c1 + f*(c2 + f*c3))
//   At f = 0 this returns x0 and at f = 1 it returns x1, both exactly, so an integer delay time is bit exact and a
//   whole-sample tempo grid never drifts.
//   The truncation error of this interpolator is O(f^3 * d4x/24); for a 1 kHz tone at 48 kHz it is about -134 dB,
//   where a linear read would be about -53 dB (measured in tests/test_delay.cpp).
//
// DAMPING (tone)
//   The feedback path is a one-pole low-pass, TDF-II:
//        y[n] = a*x[n] + (1 - a)*y[n-1],   a = 1 - exp(-2*pi*fc/sr)
//   so the echoes get darker as they repeat (fc = toneHz). The state is flushed below 1e-30 so silence cannot
//   leave a denormal tail behind.
//
// SATURATION IN THE FEEDBACK (why the echo train cannot run away)
//   y = drive * tanh(x / drive) with drive = 1: unity slope at the origin, asymptote at +-1.
//   The loop gain is therefore bounded by feedback (max 0.95) whatever the input does, so the tail is always finite
//   and the harmonics that the clip generates are the "tape" colour of the repeats.
//
// DC BLOCK
//   y[n] = x[n] - x[n-1] + R*y[n-1], R = 1 - 2*pi*5/sr (5 Hz), after the saturator, so the clipped feedback cannot
//   accumulate DC and drift the tail off centre.
//
// DUCKING
//   The input drives a peak follower (instant attack, 250 ms release). Above -30 dBFS the wet path is pulled down
//   by up to duckDb, so a new note does not sit on top of the repeats it just made.
//
// TEMPO SYNC
//   D = beats * 60 / bpm * sr, with beats = the division (1/8 note = 0.5 beats). The time is never stepped: the
//   delay length glides with a 20 ms one-pole, which is also what makes a tempo change click-free.
//
// MEASURED, not assumed: tests/test_delay.cpp checks the echo position against the requested time (free and
// synced), the interpolation error against the analytic ideal, the feedback law, the damping law, the ducking
// depth, click-free time changes, bit-exact bypass, denormal flushing and that the process loop never allocates.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lumina::fx {

struct DelayParams {
    float mix      = 0.0f;      // 0 dry .. 1 wet
    float timeMs   = 250.0f;    // 1 .. 2000 ms (free mode)
    float feedback = 0.35f;     // 0 .. 0.95 (the saturator keeps the loop stable above that)
    float toneHz   = 5000.0f;   // feedback damping, 200 .. 18000 Hz
    float duckDb   = 0.0f;      // 0 = off, else 0 .. 24 dB the wet path is ducked by the input
    float beats    = 0.5f;      // repeat length in beats (0.5 = 1/8 note), used when sync is on
    float bpm      = 120.0f;    // host tempo
    bool  sync     = false;     // true: the time follows the host tempo, false: the time is free
    bool  pingPong = true;      // true: L feeds R (a wide echo), false: each channel repeats on itself
    bool  bypass   = false;
};

class Delay {
public:
    static constexpr int kMaxChannels = 2;
    static constexpr float kMaxFeedback = 0.95f;

    /// Allocates (once). Call from a non-audio thread.
    /// maxDelayMs caps the memory: 2 channels * maxDelayMs * sr * 4 bytes (1600 ms at 48 kHz = 614 kB).
    void prepare(double sampleRate, int channels = 2, float maxDelayMs = 1600.0f) {
        sr_ = sampleRate > 0 ? sampleRate : 48000.0;
        ch_ = std::min(std::max(channels, 1), kMaxChannels);
        maxMs_ = std::min(std::max(maxDelayMs, 10.0f), 4000.0f);
        const size_t n = static_cast<size_t>(std::ceil(maxMs_ * 1e-3 * sr_)) + 8;
        for (auto& b : buf_) b.assign(n, 0.0f);
        n_ = n;
        setParams(p_);
        reset();
    }

    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
        // the invariant: rdIdx_ = (wrIdx_ - delayInt_) mod n, so the read is exactly delayInt_ samples back
        wrIdx_ = 0; delayInt_ = 1; rdIdx_ = (n_ > 0) ? (n_ - 1) : 0;
        frac_ = 0.0; delaySamples_ = 1.0;
        lp_[0] = lp_[1] = 0.0; dcX_[0] = dcX_[1] = 0.0; dcY_[0] = dcY_[1] = 0.0;
        env_ = 0.0;
    }

    /// Cheap enough for the audio thread (a few exp() calls); allocation free.
    void setParams(const DelayParams& in) {
        p_ = in;
        p_.mix = clampf(p_.mix, 0.0f, 1.0f);
        p_.timeMs = clampf(p_.timeMs, 1.0f, maxMs_ > 1.0f ? maxMs_ : 4000.0f);
        p_.feedback = clampf(p_.feedback, 0.0f, kMaxFeedback);
        p_.toneHz = clampf(p_.toneHz, 200.0f, 18000.0f);
        p_.duckDb = clampf(p_.duckDb, 0.0f, 24.0f);
        p_.beats = clampf(p_.beats, 0.0625f, 8.0f);
        p_.bpm = clampf(p_.bpm, 20.0f, 300.0f);
        // damping coefficient: a = 1 - exp(-2*pi*fc/sr)
        dampA_ = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * static_cast<double>(p_.toneHz) / sr_);
        // the delay length glides with a 20 ms one-pole: a step would click
        timeCoef_ = 1.0 - std::exp(-1.0 / (0.020 * sr_));
        // the ducking follower releases over 250 ms
        envRel_ = std::exp(-1.0 / (0.250 * sr_));
        // DC blocker: R = 1 - 2*pi*5/sr
        dcR_ = 1.0 - 2.0 * 3.14159265358979323846 * 5.0 / sr_;
        // the delay can never be shorter than one sample
        const double maxSamples = static_cast<double>(n_) - 4.0;
        targetSamples_ = std::min(targetForParams(), maxSamples);
        if (delaySamples_ <= 1.0) delaySamples_ = targetSamples_;
    }

    /// The host tempo (for sync). Call from the audio thread; it only changes the target.
    void setTempo(float bpm) {
        if (bpm < 20.0f || bpm > 300.0f) return;
        p_.bpm = bpm;
        targetSamples_ = std::min(targetForParams(), static_cast<double>(n_) - 4.0);
    }

    const DelayParams& params() const { return p_; }
    /// The delay time actually in use, in milliseconds (it glides toward the target).
    float delayMs() const { return static_cast<float>(delaySamples_ / sr_ * 1000.0); }
    /// The largest absolute value left in the recursive states (denormal probe).
    double maxState() const {
        double m = std::fabs(env_);
        for (int c = 0; c < ch_; ++c) {
            m = std::max(m, std::fabs(lp_[c]));
            m = std::max(m, std::fabs(dcX_[c]));
            m = std::max(m, std::fabs(dcY_[c]));
        }
        return m;
    }

    /// In place. io[c][0..n). channels must match prepare(). Never allocates.
    void process(float* const* io, int channels, int n) {
        if (p_.bypass || n <= 0) return;
        const int C = std::min(channels, ch_);
        const double mix = static_cast<double>(p_.mix);
        const double duckMax = static_cast<double>(p_.duckDb);
        const size_t N = n_;

        for (int i = 0; i < n; ++i) {
            // ---- the delay length glides to its target (also what makes a tempo change click-free) ----
            delaySamples_ += (targetSamples_ - delaySamples_) * timeCoef_;
            // The sample to read is x[n - delaySamples_]. base = n - ceil(delay) and the fraction is
            // ceil(delay) - delay, so the interpolator sits exactly on that sample: f = 0 for a whole
            // number of samples (bit exact), f in (0,1) otherwise.
            const int newInt = static_cast<int>(std::ceil(delaySamples_));
            const double newFrac = static_cast<double>(newInt) - delaySamples_;
            const int d = newInt - delayInt_;
            if (d != 0) {
                long long v = (static_cast<long long>(rdIdx_) - static_cast<long long>(d)) % static_cast<long long>(N);
                if (v < 0) v += static_cast<long long>(N);
                rdIdx_ = static_cast<size_t>(v);
                delayInt_ = newInt;
            }
            frac_ = newFrac;

            float in0 = io[0][i];
            float in1 = (C > 1) ? io[1][i] : in0;
            if (!std::isfinite(in0)) in0 = 0.0f;
            if (!std::isfinite(in1)) in1 = 0.0f;

            // ---- ducking: the input pulls the wet path down ----
            const double a0 = std::fabs(static_cast<double>(in0));
            const double a1 = std::fabs(static_cast<double>(in1));
            double peak = std::max(a0, a1);
            env_ = std::max(peak, env_ * envRel_);
            if (env_ < 1e-30) env_ = 0.0;
            double duck = 0.0;
            if (duckMax > 0.0) {
                const double envDb = 20.0 * std::log10(std::max(env_, 1e-9));
                const double over = (envDb + 30.0) / 30.0;      // 0 at -30 dBFS, 1 at 0 dBFS
                duck = duckMax * (over > 1.0 ? 1.0 : (over < 0.0 ? 0.0 : over));
            }
            const double duckGain = std::pow(10.0, -duck * 0.05);   // -duck dB

            // ---- read the two taps (the write happens after, so the read is always of older data) ----
            const float wet0 = readCubic(buf_[0], N, rdIdx_, frac_);
            const float wet1 = (C > 1) ? readCubic(buf_[1], N, rdIdx_, frac_) : wet0;

            // ---- feedback path: damping, saturation, DC block ----
            double fb0 = feedbackPath(wet0, 0);
            double fb1 = (C > 1) ? feedbackPath(wet1, 1) : fb0;

            if (p_.pingPong && C > 1) {
                // L repeats into R and R repeats into L
                writeSample(buf_[0], N, wrIdx_, static_cast<float>(static_cast<double>(in0) + fb1));
                writeSample(buf_[1], N, wrIdx_, static_cast<float>(static_cast<double>(in1) + fb0));
            } else {
                writeSample(buf_[0], N, wrIdx_, static_cast<float>(static_cast<double>(in0) + fb0));
                if (C > 1) writeSample(buf_[1], N, wrIdx_, static_cast<float>(static_cast<double>(in1) + fb1));
            }
            wrIdx_ = (wrIdx_ + 1 == N) ? 0 : wrIdx_ + 1;
            rdIdx_ = (rdIdx_ + 1 == N) ? 0 : rdIdx_ + 1;

            const double w0 = static_cast<double>(wet0) * mix * duckGain;
            const double w1 = static_cast<double>(wet1) * mix * duckGain;
            io[0][i] = static_cast<float>(static_cast<double>(in0) * (1.0 - mix) + w0);
            if (C > 1) io[1][i] = static_cast<float>(static_cast<double>(in1) * (1.0 - mix) + w1);
        }
    }

private:
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    double targetForParams() const {
        if (p_.sync) {
            const double bpm = static_cast<double>(p_.bpm);
            return static_cast<double>(p_.beats) * 60.0 / bpm * sr_;
        }
        return static_cast<double>(p_.timeMs) * 1e-3 * sr_;
    }

    /// Catmull-Rom cubic Hermite between x0 and x1 (t = frac in [0,1)).
    static float readCubic(const std::vector<float>& buf, size_t N, size_t i0, double t) {
        const size_t im = (i0 == 0) ? (N - 1) : (i0 - 1);
        const size_t ip1 = (i0 + 1 == N) ? 0 : (i0 + 1);
        const size_t ip2 = (i0 + 2 >= N) ? (i0 + 2 - N) : (i0 + 2);
        double xm1 = buf[im], x0 = buf[i0], x1 = buf[ip1], x2 = buf[ip2];
        if (std::fabs(xm1) < 1e-30) xm1 = 0.0;    // never let a denormal into the interpolation
        if (std::fabs(x0)  < 1e-30) x0 = 0.0;
        if (std::fabs(x1)  < 1e-30) x1 = 0.0;
        if (std::fabs(x2)  < 1e-30) x2 = 0.0;
        const double m1 = 0.5 * (x1 - xm1);
        const double m2 = 0.5 * (x2 - x0);
        const double c1 = m1;
        const double c2 = 3.0 * (x1 - x0) - 2.0 * m1 - m2;
        const double c3 = -2.0 * (x1 - x0) + m1 + m2;
        return static_cast<float>(x0 + t * (c1 + t * (c2 + t * c3)));
    }

    static void writeSample(std::vector<float>& buf, size_t N, size_t idx, float v) {
        buf[idx] = (std::fabs(v) < 1e-30f) ? 0.0f : v;   // flush denormals on the way in
        (void)N;
    }

    /// damping one-pole -> saturation -> DC block -> x feedback
    double feedbackPath(float wet, int c) {
        const double y = dampA_ * static_cast<double>(wet) + (1.0 - dampA_) * lp_[c];
        lp_[c] = y;
        if (std::fabs(lp_[c]) < 1e-30) lp_[c] = 0.0;
        const double sat = std::tanh(y);                      // drive = 1: unity slope, asymptote +-1
        const double dc = sat - dcX_[c] + dcR_ * dcY_[c];
        dcX_[c] = sat; dcY_[c] = dc;
        if (std::fabs(dcY_[c]) < 1e-30) dcY_[c] = 0.0;
        if (std::fabs(dcX_[c]) < 1e-30) dcX_[c] = 0.0;
        return dc * static_cast<double>(p_.feedback);
    }

    DelayParams p_{};
    double sr_ = 48000.0;
    int ch_ = 2;
    float maxMs_ = 1600.0f;
    size_t n_ = 0;
    std::vector<float> buf_[kMaxChannels];
    size_t wrIdx_ = 0, rdIdx_ = 0;
    int delayInt_ = 1;
    double frac_ = 0.0;
    double delaySamples_ = 1.0;
    double targetSamples_ = 12000.0;
    double dampA_ = 0.5, timeCoef_ = 0.01, envRel_ = 0.999, dcR_ = 0.999;
    double lp_[kMaxChannels] = {0.0, 0.0};
    double dcX_[kMaxChannels] = {0.0, 0.0};
    double dcY_[kMaxChannels] = {0.0, 0.0};
    double env_ = 0.0;
};

} // namespace lumina::fx
