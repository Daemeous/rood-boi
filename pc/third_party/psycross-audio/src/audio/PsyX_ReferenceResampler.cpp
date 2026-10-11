// PsyX_ReferenceResampler.cpp - see PsyX_ReferenceResampler.h for design
// notes/limitations.

#include "PsyX_ReferenceResampler.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>

namespace PsyX
{

namespace
{
    // Our own constant - avoid relying on M_PI (not guaranteed present by
    // the standard, absent under strict MSVC <cmath> without
    // _USE_MATH_DEFINES) to keep this file free of any platform-specific
    // toggles.
    const double kPi = 3.14159265358979323846;

    // Normalized sinc: sinc(0) == 1, sinc(n) == 0 for every other integer n.
    double Sinc(double x)
    {
        if (std::fabs(x) < 1e-12)
            return 1.0;
        const double px = kPi * x;
        return std::sin(px) / px;
    }

    // Zeroth-order modified Bessel function of the first kind, via its
    // convergent power series. Double precision, deterministic, no
    // external dependency.
    double BesselI0(double x)
    {
        double sum     = 1.0;
        double term    = 1.0;
        const double halfX = x * 0.5;
        for (int k = 1; k <= 48; ++k)
        {
            const double f = halfX / static_cast<double>(k);
            term *= f * f;
            sum += term;
            if (term < sum * 1e-18)
                break;
        }
        return sum;
    }

    // Kaiser window value at integer tap index n of an N-tap window.
    double KaiserWindow(int n, int N, double beta)
    {
        const double M   = (N - 1) / 2.0;
        const double r   = (n - M) / M;
        double       arg = 1.0 - r * r;
        if (arg < 0.0)
            arg = 0.0;
        return BesselI0(beta * std::sqrt(arg)) / BesselI0(beta);
    }

    // Rabiner/Kaiser empirical beta-from-stopband-attenuation formula.
    double KaiserBeta(double attenuationDb)
    {
        if (attenuationDb > 50.0)
            return 0.1102 * (attenuationDb - 8.7);
        if (attenuationDb >= 21.0)
            return 0.5842 * std::pow(attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
        return 0.0;
    }

    // Target stopband attenuation used for every bandlimit bin's Kaiser
    // window. Higher taps or lower attenuation would narrow/widen the
    // transition band - see the header's "Filter design" notes.
    const double kStopbandAttenDb = 100.0;

    // Extra safety margin applied to the anti-alias cutoff of buckets
    // above unity pitch (bucket 0 uses full bandwidth, no margin needed -
    // see header notes on why R<=1 can never alias).
    const double kAntiAliasMargin = 0.80;
}

ReferenceResampler::ReferenceResampler()
    : m_historyStart(0)
    , m_position(0)
    , m_cachedStep(~uint64_t(0))
    , m_cachedBucket(0)
    , m_cachedBank(nullptr)
{
}

void ReferenceResampler::Reset()
{
    m_history.clear();
    m_historyStart = 0;
    m_position     = 0;
    m_cachedStep   = ~uint64_t(0);
    m_cachedBucket = 0;
    m_cachedBank   = nullptr;
}

void ReferenceResampler::PushSamples(const int16_t* samples, size_t count)
{
    if (!samples || count == 0)
        return;
    m_history.insert(m_history.end(), samples, samples + count);
    TrimHistory();
}

void ReferenceResampler::TrimHistory()
{
    // Never discard samples still reachable by a filter window centered
    // anywhere at or after the current read position (kHalfTaps lookback,
    // plus a small safety margin for in-flight modulation jitter).
    const int64_t posInt   = static_cast<int64_t>(m_position >> kPosFracBits);
    const int64_t keepFrom = posInt - kHalfTaps - 4;
    if (keepFrom > m_historyStart)
    {
        int64_t drop = keepFrom - m_historyStart;
        if (drop > static_cast<int64_t>(m_history.size()))
            drop = static_cast<int64_t>(m_history.size());
        if (drop >= 1024)
        {
            m_history.erase(m_history.begin(), m_history.begin() + static_cast<size_t>(drop));
            m_historyStart += drop;
        }
    }
}

int16_t ReferenceResampler::SampleAt(int64_t absoluteIndex) const
{
    // Before the start of the stream or beyond what has been pushed so
    // far: treat as silence. This is the conventional, deterministic
    // zero-padding convention for finite-length streams (matches e.g. the
    // implicit silence before/after a sound effect starts/ends).
    if (absoluteIndex < m_historyStart)
        return 0;
    const int64_t rel = absoluteIndex - m_historyStart;
    if (rel >= static_cast<int64_t>(m_history.size()))
        return 0;
    return m_history[static_cast<size_t>(rel)];
}

int ReferenceResampler::BucketForStep(uint64_t step)
{
    const uint64_t unityStep = PitchToStep(kSpuPitchUnity);
    if (step <= unityStep)
        return 0;

    static const std::array<uint64_t, kMaxBandlimitBucket + 1> upperSteps = [unityStep] {
        std::array<uint64_t, kMaxBandlimitBucket + 1> result{};
        result[0] = unityStep;
        for (int bin = 1; bin <= kMaxBandlimitBucket; ++bin)
        {
            const double ratio = std::exp2(
                static_cast<double>(bin) /
                static_cast<double>(kBandlimitBinsPerOctave));
            result[bin] = static_cast<uint64_t>(
                std::ceil(static_cast<double>(unityStep) * ratio));
        }
        return result;
    }();
    const auto found = std::lower_bound(
        upperSteps.begin() + 1, upperSteps.end(), step);
    return found == upperSteps.end()
        ? kMaxBandlimitBucket
        : static_cast<int>(found - upperSteps.begin());
}

double ReferenceResampler::DesignCutoffForBucket(int bucket)
{
    if (bucket <= 0)
        return 1.0; // full source Nyquist - see header notes (R<=1 never aliases)

    if (bucket > kMaxBandlimitBucket)
        bucket = kMaxBandlimitBucket;
    const double ratioBound = std::exp2(
        static_cast<double>(bucket) /
        static_cast<double>(kBandlimitBinsPerOctave));
    return kAntiAliasMargin / ratioBound;
}

void ReferenceResampler::BuildBank(Bank& bank, double normalizedCutoff)
{
    const double beta = KaiserBeta(kStopbandAttenDb);
    const double nc   = normalizedCutoff;

    for (int p = 0; p < kPhases; ++p)
    {
        const double frac = static_cast<double>(p) / static_cast<double>(kPhases);
        double       sum  = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const int    off    = i - (kHalfTaps - 1); // -31 .. +32 for kTaps==64
            const double sample = nc * Sinc(nc * (static_cast<double>(off) - frac)) * KaiserWindow(i, kTaps, beta);
            bank.coeffs[p][i]   = sample;
            sum += sample;
        }

        // Unity DC normalization: force this phase row to sum to exactly
        // 1.0, regardless of window/sinc truncation error.
        if (sum != 0.0)
        {
            const double invSum = 1.0 / sum;
            for (int i = 0; i < kTaps; ++i)
                bank.coeffs[p][i] *= invSum;
        }
    }
}

const ReferenceResampler::Bank& ReferenceResampler::GetBank(int bucket)
{
    // Coefficient banks are pure functions of (bucket) and are shared
    // process-wide - lazily built once per distinct bucket and cached for
    // every ReferenceResampler instance/call thereafter. Function-local
    // statics give us thread-safe, exactly-once construction per bucket
    // (C++11 magic statics) without pulling in any extra synchronization
    // primitives or process-wide init-order dependencies.
    if (bucket < 0)
        bucket = 0;
    if (bucket > kMaxBandlimitBucket)
        bucket = kMaxBandlimitBucket;

    static std::array<std::once_flag, kMaxBandlimitBucket + 1> once;
    static std::array<std::unique_ptr<Bank>, kMaxBandlimitBucket + 1> banks;
    std::call_once(once[bucket], [bucket] {
        std::unique_ptr<Bank> bank(new Bank());
        BuildBank(*bank, DesignCutoffForBucket(bucket));
        banks[bucket] = std::move(bank);
    });
    return *banks[bucket];
}

void ReferenceResampler::WarmUpAllBanks()
{
    for (int b = 0; b <= kMaxBandlimitBucket; ++b)
        GetBank(b);
}

double ReferenceResampler::RenderSample(uint64_t position, const Bank& bank) const
{
    const int64_t  n        = static_cast<int64_t>(position >> kPosFracBits);
    const uint64_t fracMask = (uint64_t(1) << kPosFracBits) - 1u;
    const uint64_t fracBits = position & fracMask;
    const int      phase    = static_cast<int>(fracBits >> (kPosFracBits - kPhaseBits)) & (kPhases - 1);
    const int64_t first = n - (kHalfTaps - 1) - m_historyStart;
    const double* coefficients = bank.coeffs[phase];

    double acc = 0.0;
    if (first >= 0 &&
        first + kTaps <= static_cast<int64_t>(m_history.size()))
    {
        const int16_t* samples = m_history.data() + first;
        for (int i = 0; i < kTaps; ++i)
            acc += coefficients[i] * static_cast<double>(samples[i]);
        return acc;
    }

    for (int i = 0; i < kTaps; ++i)
    {
        const int     off    = i - (kHalfTaps - 1);
        const int16_t sample = SampleAt(n + off);
        acc += coefficients[i] * static_cast<double>(sample);
    }
    return acc;
}

double ReferenceResampler::ProcessDouble(uint64_t step)
{
    if (step != m_cachedStep)
    {
        m_cachedStep = step;
        m_cachedBucket = BucketForStep(step);
        m_cachedBank = &GetBank(m_cachedBucket);
    }

    const double sample = RenderSample(m_position, *m_cachedBank);

    m_position += step;
    TrimHistory();
    return sample;
}

void ReferenceResampler::Advance(uint64_t step)
{
    m_position += step;
    TrimHistory();
}

int64_t ReferenceResampler::ProcessInt64(uint64_t step)
{
    const double sample = ProcessDouble(step);
    // Round-half-away-from-zero; intentionally not saturated/clamped
    // here (see header: callers get a faithful reconstruction and are
    // responsible for any final quantization/clamping policy).
    return static_cast<int64_t>(sample >= 0.0 ? std::floor(sample + 0.5) : std::ceil(sample - 0.5));
}

void ReferenceResampler::ProcessBlockDouble(const uint64_t* steps, size_t count, double* outSamples)
{
    for (size_t i = 0; i < count; ++i)
        outSamples[i] = ProcessDouble(steps[i]);
}

void ReferenceResampler::ProcessBlockInt64(const uint64_t* steps, size_t count, int64_t* outSamples)
{
    for (size_t i = 0; i < count; ++i)
        outSamples[i] = ProcessInt64(steps[i]);
}

void ReferenceResampler::ProcessBlockDouble(uint64_t constantStep, size_t count, double* outSamples)
{
    for (size_t i = 0; i < count; ++i)
        outSamples[i] = ProcessDouble(constantStep);
}

void ReferenceResampler::ProcessBlockInt64(uint64_t constantStep, size_t count, int64_t* outSamples)
{
    for (size_t i = 0; i < count; ++i)
        outSamples[i] = ProcessInt64(constantStep);
}

// ---------------------------------------------------------------------------
// HalfBandDecimator - see PsyX_ReferenceResampler.h "Half-band output
// decimation" section for the design rationale.
// ---------------------------------------------------------------------------

HalfBandDecimator::HalfBandDecimator()
{
}

void HalfBandDecimator::Reset()
{
    m_history.clear();
    m_historyStart = 0;
    m_inputCount = 0;
}

double HalfBandDecimator::SampleAt(int64_t absoluteIndex) const
{
    // Same deterministic zero-padding convention as
    // ReferenceResampler::SampleAt() - before the start of the stream or
    // beyond what has been pushed so far reads as silence.
    if (absoluteIndex < m_historyStart)
        return 0.0;
    const int64_t rel = absoluteIndex - m_historyStart;
    if (rel >= static_cast<int64_t>(m_history.size()))
        return 0.0;
    return m_history[static_cast<size_t>(rel)];
}

void HalfBandDecimator::TrimHistory()
{
    // The oldest index this or any future call could still read is
    // (newest - 2*kMaxOffset): Process() centers its filter window
    // kMaxOffset behind the newest pushed sample, and reaches another
    // kMaxOffset further back from that center.
    const int64_t newest = m_inputCount - 1;
    const int64_t keepFrom = newest - 2 * kMaxOffset;
    if (keepFrom > m_historyStart)
    {
        int64_t drop = keepFrom - m_historyStart;
        if (drop > static_cast<int64_t>(m_history.size()))
            drop = static_cast<int64_t>(m_history.size());
        if (drop > 0)
        {
            m_history.erase(m_history.begin(), m_history.begin() + static_cast<size_t>(drop));
            m_historyStart += drop;
        }
    }
}

void HalfBandDecimator::BuildCoefficients(Coefficients& coefficients)
{
    // Half-band lowpass at nc == 0.5 (this file's nc/Sinc() convention -
    // see BuildBank() above): h[n] = 0.5*Sinc(0.5*n), which is exactly
    // zero for every nonzero even n - only the center tap and odd offsets
    // up to +-kMaxOffset are ever computed/stored (see header).
    const double beta = KaiserBeta(kStopbandAttenDb);
    const int    denseLength = 2 * kMaxOffset + 1; // odd, symmetric around the center tap
    const double nc = 0.5;

    coefficients.center = nc * Sinc(0.0) * KaiserWindow(kMaxOffset, denseLength, beta);
    double totalSum = coefficients.center;

    for (int k = 0; k < kSideTaps; ++k)
    {
        const int    offset = 2 * k + 1;
        const double window = KaiserWindow(kMaxOffset + offset, denseLength, beta);
        const double tap    = nc * Sinc(nc * static_cast<double>(offset)) * window;
        coefficients.side[k] = tap;
        totalSum += 2.0 * tap; // mirrored +-offset both contribute this same weight
    }

    // Unity DC normalization - matches BuildBank()'s own per-phase
    // renormalization convention exactly (see header "Filter design").
    if (totalSum != 0.0)
    {
        const double invSum = 1.0 / totalSum;
        coefficients.center *= invSum;
        for (int k = 0; k < kSideTaps; ++k)
            coefficients.side[k] *= invSum;
    }
}

const HalfBandDecimator::Coefficients& HalfBandDecimator::GetCoefficients()
{
    // Process-wide, lazily-built, shared cache - same function-local
    // static "exactly-once construction" idiom as
    // ReferenceResampler::GetBank() (coefficients are a pure function of
    // the fixed nc==0.5 design, with no per-instance state).
    static Coefficients coefficients;
    static bool built = false;
    if (!built)
    {
        BuildCoefficients(coefficients);
        built = true;
    }
    return coefficients;
}

double HalfBandDecimator::Process(double in0, double in1)
{
    m_history.push_back(in0);
    m_history.push_back(in1);
    m_inputCount += 2;

    const int64_t newest = m_inputCount - 1; // absolute index of in1
    const int64_t center = newest - kMaxOffset;

    const Coefficients& c = GetCoefficients();
    double acc = c.center * SampleAt(center);
    for (int k = 0; k < kSideTaps; ++k)
    {
        const int64_t offset = 2 * k + 1;
        acc += c.side[k] * (SampleAt(center - offset) + SampleAt(center + offset));
    }

    TrimHistory();
    return acc;
}

// ---------------------------------------------------------------------------
// ReferenceOutputDecimator - cascades 0-3 HalfBandDecimator stereo stages.
// ---------------------------------------------------------------------------

int ReferenceOutputDecimator::StagesForOutputRate(uint32_t outputRate)
{
    switch (outputRate)
    {
    case 352800: return 0;
    case 176400: return 1;
    case 88200:  return 2;
    case 44100:  return 3;
    default:     return -1; // not a direct-family rate - never an arbitrary resample
    }
}

void ReferenceOutputDecimator::Reset(int stageCount)
{
    m_stageCount = stageCount < 0 ? 0 : (stageCount > 3 ? 3 : stageCount);
    for (Stage& stage : m_stages)
    {
        stage.left.Reset();
        stage.right.Reset();
        stage.havePending  = false;
        stage.pendingLeft  = 0.0;
        stage.pendingRight = 0.0;
    }
}

bool ReferenceOutputDecimator::Process(double inLeft, double inRight, double* outLeft, double* outRight)
{
    double curLeft  = inLeft;
    double curRight = inRight;
    for (int i = 0; i < m_stageCount; ++i)
    {
        Stage& stage = m_stages[i];
        if (!stage.havePending)
        {
            // First half of this stage's next input pair - nothing new to
            // offer downstream yet (see header doc comment).
            stage.pendingLeft  = curLeft;
            stage.pendingRight = curRight;
            stage.havePending  = true;
            return false;
        }
        curLeft  = stage.left.Process(stage.pendingLeft, curLeft);
        curRight = stage.right.Process(stage.pendingRight, curRight);
        stage.havePending = false;
        // curLeft/curRight now hold this stage's OUTPUT - feed to the next stage.
    }

    if (outLeft)
        *outLeft = curLeft;
    if (outRight)
        *outRight = curRight;
    return true;
}

} // namespace PsyX
