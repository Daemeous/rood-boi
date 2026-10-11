#ifndef PSYX_REFERENCERESAMPLER_H
#define PSYX_REFERENCERESAMPLER_H

// PsyX_ReferenceResampler - backend-neutral, deterministic, high-quality
// variable-rate reconstruction module.
//
// This is a from-scratch, standalone polyphase windowed-sinc resampler. It
// has no knowledge of SPUCore, OpenAL/SDL/WASAPI, or any other subsystem: it
// only consumes decoded s16 PCM samples plus a fixed-point source position/
// step and produces reconstructed samples (double or int64_t). It does not
// depend on anything outside the standard library (<stdint.h>, <stddef.h>,
// <vector>, <cmath>) - no FFT/DSP third-party libraries, no arbitrary sample
// rates, no OS/audio-backend headers.
//
// -----------------------------------------------------------------------
// Fixed output clock
// -----------------------------------------------------------------------
// The reconstruction runs on a fixed, integer-ratio 8x oversampled clock:
//   kOutputSampleRate = kSourceSampleRate * kOutputOversample = 44100 * 8
//                     = 352800 Hz
// This is not a generic "any sample rate" resampler - it only ever produces
// samples on this one fixed 352800Hz tick, exactly 8 output ticks per
// original 44100Hz source sample at unity pitch. 8x (rather than a
// seemingly-simpler 4x) is deliberate: the maximum supported PMON pitch
// ratio pitch-shifts source-Nyquist content up to the *oversampled* bus's
// own Nyquist, so the bus itself must sit a full octave above the worst
// case to leave a genuine transition band - see kMaxBandlimitBucket/
// DesignCutoffForBucket() below, which already bandlimits per-bucket, but
// still needs real headroom above the highest in-bucket ratio to have
// anywhere to put a non-brickwall transition band. Downstream code that
// needs a lower final output rate is expected to do its own (separate,
// simple) rate conversion from this fixed 352800Hz stream - see
// HalfBandDecimator/ReferenceOutputDecimator further down, which do
// exactly that via direct-family (352800/2^n) decimation only, never an
// arbitrary sample rate (see task requirement: "no arbitrary sample
// rates").
//
// -----------------------------------------------------------------------
// Source position / step (variable SPU pitch, including modulation)
// -----------------------------------------------------------------------
// The source read position is a Q32.32 fixed-point absolute sample index
// (kPosFracBits == 32 fractional bits). Callers advance it once per output
// tick by a `step` value returned by PitchToStep(), which mirrors the PS1
// SPU's documented 4.12 fixed-point VxPitch register format (0x1000 ==
// 1.0x / unity, see PsyX_SPUCore.h's kPitchUnity): given the fixed 8x
// output oversample ratio, PitchToStep() is an EXACT (no rounding error)
// left-shift because kSpuPitchUnity (2^12) times kOutputOversample (2^3)
// is itself a power of two (2^15):
//
//   step = (uint64_t)spuPitch << (kPosFracBits - kSpuPitchFracBits - kOversampleBits)
//        = (uint64_t)spuPitch << 17
//
// Because `step` is just an integer (not a rate), pitch MODULATION is
// supported trivially: the caller may pass a different `step` to every
// single Process*() call (e.g. recomputed every output tick, or every 8th
// tick to match the original 44100Hz PMON modulation cadence) - the
// resampler carries no per-call assumption that step is constant.
//
// -----------------------------------------------------------------------
// Filter design
// -----------------------------------------------------------------------
// Polyphase windowed-sinc (Kaiser window), kTaps (>=64) taps, kPhases
// (1024) phases. Each phase's tap row is generated once via double
// precision math (sin/sqrt from <cmath>, an internally implemented
// Bessel I0 power series for the Kaiser window, no external DSP
// dependency) and cached (lazily, per bandlimit bin - see below) so
// repeated Process() calls are cheap table lookups. Coefficient
// generation is a pure function of (phase, tap, target cutoff): given the
// same build/toolchain it reproduces bit-identical tables every run - see
// WarmUpAllBanks()/GetBucketDesignCutoff() for tests that check this.
//
// Every phase row is explicitly renormalized so its taps sum to exactly
// 1.0 (unity DC gain) - this both fixes up the Kaiser window's inherent
// finite-precision truncation error and guarantees DC content passes
// through with no gain/attenuation regardless of cutoff or window
// rounding.
//
// Bandlimit vs. pitch (1/32-octave bins): let
//   R = step / PitchToStep(kSpuPitchUnity)
// be the current playback ratio relative to the original recorded pitch
// (R == 1 at unity). Any R > 1 (higher pitch = faster read = smaller
// output-tick-to-source-tick spacing) pitch-shifts the source content's
// frequencies up by a factor of R. Since a real sampled source signal is
// inherently already bandlimited to its own Nyquist (kSourceSampleRate/2),
// R <= 1 can never produce content above the (8x oversampled) output
// Nyquist and needs no extra bandlimiting - the filter is designed with
// cutoff == full source Nyquist (nc == 1.0) for R <= 1, which as a
// side-effect makes the interpolator an EXACT passthrough at
// non-fractional (phase == 0) positions (sinc(integer) == 0 for every
// tap except the center one, see the unit tests). For R > 1, content
// above (source Nyquist / R) would alias once pitch-shifted, so the
// design cutoff is reduced by (a conservative fraction of) 1/R. Ratios are
// quantized upward to 1/32-octave boundaries, keeping the cached bank count
// bounded while limiting cutoff error to about 2.2%. Each bank is generated
// once and reused for every step in its bin; using the bin's upper edge keeps
// the design conservative without the severe power-of-two over-filtering.
//
// -----------------------------------------------------------------------
// Continuous history across ADPCM blocks
// -----------------------------------------------------------------------
// PushSamples() appends newly decoded PCM (e.g. one 28-sample SPU-ADPCM
// block at a time, or any other chunk size) to an internal history buffer
// addressed by a persistent absolute sample index. Nothing is reset
// between PushSamples() calls (only Reset() does that), so a filter
// window that straddles a block boundary sees the same continuous sample
// history whether the caller pushes 1 giant buffer or many small ADPCM-
// sized chunks - there is no per-block re-priming/discontinuity. Old
// history no longer reachable by any future filter window is trimmed
// automatically to bound memory use.
// -----------------------------------------------------------------------
// Half-band output decimation (HalfBandDecimator / ReferenceOutputDecimator)
// -----------------------------------------------------------------------
// Separate, much simpler utility classes (below, same translation unit so
// they can reuse Sinc()/KaiserWindow()/KaiserBeta() without duplicating any
// math) that step the fixed 352800Hz engine rate DOWN to one of the
// device's direct-family rates (352800/176400/88200/44100) via cascaded
// exact 2:1 decimation stages - never an arbitrary resample. Each stage is
// a classic linear-phase half-band FIR lowpass at exactly half its input
// Nyquist: in this file's nc/Sinc() convention (nc == 1.0 is full Nyquist,
// see BuildBank() above), that is nc == 0.5, i.e. h[n] = 0.5*Sinc(0.5*n).
// This specific cutoff has a well-known structural property: Sinc(0.5*n)
// is EXACTLY zero for every nonzero EVEN n (0.5*n is then a nonzero
// integer, and Sinc(integer) == 0), so only the center tap and the ODD-
// offset taps are ever nonzero - HalfBandDecimator stores/computes only
// those, about half of an equivalent dense FIR, with no approximation
// involved (this is exact, not a shortcut).
// -----------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace PsyX
{

class ReferenceResampler
{
public:
    // -- Filter shape (task requirements: >=64 taps, 1024 phases) --
    static const int kTaps      = 64;         // FIR length (even, centered)
    static const int kHalfTaps  = kTaps / 2;  // 32
    static const int kPhaseBits = 10;
    static const int kPhases    = 1 << kPhaseBits; // 1024 polyphase positions

    // -- Fixed, integer-ratio output clock --
    static const uint32_t kSourceSampleRate = 44100u;
    static const uint32_t kOutputOversample = 8u;                                  // integer ratio, NOT an arbitrary rate
    static const uint32_t kOutputSampleRate = kSourceSampleRate * kOutputOversample; // 352800 Hz, fixed

    // -- SPU VxPitch fixed-point format (matches PsyX_SPUCore.h kPitchUnity) --
    static const uint32_t kSpuPitchUnity    = 0x1000u; // 4.12 fixed point, 1.0x == 0x1000
    static const int      kSpuPitchFracBits = 12;
    static const int      kOversampleBits   = 3;       // log2(kOutputOversample)

    // -- Source position fixed point (Q32.32 absolute sample index) --
    static const int kPosFracBits = 32;
    static const int kStepShift   = kPosFracBits - kSpuPitchFracBits - kOversampleBits; // == 17

    // One PS1 SPU-ADPCM block's decoded sample count (for tests/callers
    // that want to simulate feeding decode output one block at a time).
    static const int kAdpcmBlockSamples = 28;

    static const int kBandlimitBinsPerOctave = 32;
    static const int kMaxBandlimitOctaves = 4;
    static const int kMaxBandlimitBucket =
        kBandlimitBinsPerOctave * kMaxBandlimitOctaves;

    ReferenceResampler();

    // Clears all history and resets the read position to 0. Coefficient
    // banks (which are process-wide, lazily-built caches - see GetBank())
    // are NOT affected by Reset(); they are pure functions of pitch ratio
    // and independent of any one resampler instance's state.
    void Reset();

    // Appends newly decoded PCM. May be called with any chunk size,
    // including one SPU-ADPCM block (kAdpcmBlockSamples) at a time;
    // history is continuous across calls (see file header comment).
    void PushSamples(const int16_t* samples, size_t count);

    // Converts a raw SPU VxPitch register value (0..0xFFFF, or any wider
    // post-PMON-modulation step magnitude) into the exact Q32.32 fixed-
    // point per-output-tick step consumed by Process*().
    static uint64_t PitchToStep(uint32_t spuPitch)
    {
        return static_cast<uint64_t>(spuPitch) << kStepShift;
    }

    // Renders one output sample at the current read position using the
    // supplied step's bandlimit bucket, then advances the read position by
    // `step`. `step` may differ on every call (pitch modulation).
    double  ProcessDouble(uint64_t step);
    int64_t ProcessInt64(uint64_t step);
    void Advance(uint64_t step);

    // Convenience bulk variants.
    void ProcessBlockDouble(const uint64_t* steps, size_t count, double* outSamples);
    void ProcessBlockInt64(const uint64_t* steps, size_t count, int64_t* outSamples);
    void ProcessBlockDouble(uint64_t constantStep, size_t count, double* outSamples);
    void ProcessBlockInt64(uint64_t constantStep, size_t count, int64_t* outSamples);

    uint64_t GetPosition() const { return m_position; }
    void     SetPosition(uint64_t pos) { m_position = pos; }

    // Absolute index range currently retained in the history buffer
    // (diagnostics/tests only).
    int64_t GetHistoryStartIndex() const { return m_historyStart; }
    int64_t GetHistoryEndIndex() const { return m_historyStart + static_cast<int64_t>(m_history.size()); }

    // Forces every bandlimit bucket's coefficient bank to be built right
    // now (rather than lazily, on first use). Coefficient generation is
    // deterministic and side-effect-free, so this is purely useful to (a)
    // make first-use latency predictable and (b) let tests/callers ensure
    // banks exist before latency-sensitive rendering begins. Lazy bank
    // construction is synchronized independently per bin.
    static void WarmUpAllBanks();

    // Maps a step to its 1/32-octave bandlimit bin (0..kMaxBandlimitBucket)
    // and returns the normalized design cutoff (fraction of source
    // Nyquist, 1.0 == full bandwidth) used for that bucket. Exposed for
    // tests/diagnostics.
    static int    BucketForStep(uint64_t step);
    static double DesignCutoffForBucket(int bucket);

private:
    struct Bank
    {
        double coeffs[kPhases][kTaps];
    };

    static const Bank& GetBank(int bucket);
    static void        BuildBank(Bank& bank, double normalizedCutoff);

    int16_t SampleAt(int64_t absoluteIndex) const;
    double  RenderSample(uint64_t position, const Bank& bank) const;
    void    TrimHistory();

    std::vector<int16_t> m_history;
    int64_t               m_historyStart; // absolute sample index of m_history[0]
    uint64_t              m_position;     // Q32.32 absolute read position
    uint64_t              m_cachedStep;
    int                   m_cachedBucket;
    const Bank*           m_cachedBank;
};

// Single exact 2:1 half-band decimation stage - see file header "Half-band
// output decimation" section for the design rationale. Mono (like
// ReferenceResampler itself); ReferenceOutputDecimator below pairs two
// instances per cascade stage for stereo.
//
// Consumes samples two at a time (Process(in0, in1)) and returns one
// output sample per call, at half the input rate. There is no separate
// "not ready yet" case - unlike ReferenceOutputDecimator's cascade, a
// single stage always has exactly one output per pair of inputs.
//
// Group delay is a fixed kMaxOffset input samples (a harmless, constant,
// deterministic latency - the same kind of accepted startup transient as
// ReferenceResampler's own pre-stream silence padding, see SampleAt()).
class HalfBandDecimator
{
public:
    // Dense-equivalent FIR length is 2*kMaxOffset+1 (odd, symmetric around
    // the center tap) - kMaxOffset itself must stay odd so every nonzero
    // side tap lands on an odd offset (see file header). kSideTaps is the
    // number of nonzero taps on ONE side (offsets 1,3,...,kMaxOffset).
    static const int kMaxOffset = 31;
    static const int kSideTaps  = (kMaxOffset + 1) / 2; // 16

    HalfBandDecimator();

    // Clears all history/position state. Coefficients (a process-wide,
    // lazily-built cache - see GetCoefficients()) are unaffected, exactly
    // like ReferenceResampler::Reset() vs. its coefficient banks.
    void Reset();

    // Consumes exactly one new pair of input samples (at this stage's
    // input rate, in0 immediately before in1) and returns one output
    // sample at half that rate. Every call advances by exactly 2 input
    // samples - there is no meaningful partial call for a 2:1 decimator.
    double Process(double in0, double in1);

private:
    struct Coefficients
    {
        double center;
        double side[kSideTaps]; // side[k] == weight for offsets +-(2k+1)
    };
    static const Coefficients& GetCoefficients();
    static void                BuildCoefficients(Coefficients& coefficients);

    double SampleAt(int64_t absoluteIndex) const;
    void   TrimHistory();

    std::vector<double> m_history;
    int64_t m_historyStart = 0; // absolute sample index of m_history[0]
    int64_t m_inputCount   = 0; // total samples ever pushed via Process()
};

// Cascades 0-3 HalfBandDecimator stereo stages to step the Reference
// engine's fixed 352800Hz internal render rate down to whichever
// direct-family device rate is negotiated - see file header. Never an
// arbitrary ratio: stage count is always exactly log2(352800 / deviceRate)
// for one of the 4 legal device rates (352800/176400/88200/44100), i.e.
// 0/1/2/3 stages respectively.
class ReferenceOutputDecimator
{
public:
    // Returns the exact stage count for outputRate, or -1 if outputRate is
    // not one of the 4 legal direct-family rates (never an arbitrary
    // resample - see file header).
    static int StagesForOutputRate(uint32_t outputRate);

    // (Re)configures the cascade to run stageCount stages (clamped to
    // 0..3) and clears all stage history/pending state.
    void Reset(int stageCount);

    // Feeds one 352800Hz input stereo pair. Returns true and writes
    // *outLeft/*outRight exactly once per 2^stageCount calls (the output
    // rate's own tick); otherwise returns false and leaves
    // *outLeft/*outRight untouched (this stage of the cascade has not yet
    // accumulated enough input to produce a new output). stageCount == 0
    // is pure passthrough (352800Hz direct, no decimation): every call
    // returns true immediately.
    bool Process(double inLeft, double inRight, double* outLeft, double* outRight);

private:
    struct Stage
    {
        HalfBandDecimator left;
        HalfBandDecimator right;
        bool   havePending  = false;
        double pendingLeft  = 0.0;
        double pendingRight = 0.0;
    };

    int   m_stageCount = 0;
    Stage m_stages[3];
};

} // namespace PsyX

#endif // PSYX_REFERENCERESAMPLER_H
