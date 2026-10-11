#include "PsyX_ReferenceReverb.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace PsyX_ReferenceReverb {

namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kInvSqrt8 = 0.35355339059327376220042218105242452;
constexpr double kInternalInputGain = 0.0625;
constexpr double kOutputRecoveryGain = 8.0;
constexpr double kSilenceFloor = 1.0e-24;
constexpr double kSmoothingCoefficient = 1.0 / (0.050 * kSampleRate);
constexpr std::size_t kMaximumPreDelaySamples = static_cast<std::size_t>(kSampleRate / 2) + 2;

constexpr std::array<std::size_t, kFdnLineCount> kDelayLengths = {
    15178, 17902, 21026, 24746, 29074, 34186, 40226, 47326
};

constexpr std::array<double, kFdnLineCount> kInputLeft = {
     1.0,  1.0, -1.0, -1.0,  1.0,  1.0, -1.0, -1.0
};
constexpr std::array<double, kFdnLineCount> kInputRight = {
     1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0, -1.0
};
constexpr std::array<double, kFdnLineCount> kOutputLeft = {
     1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0, -1.0
};
constexpr std::array<double, kFdnLineCount> kOutputRight = {
     1.0, -1.0,  1.0, -1.0,  1.0, -1.0,  1.0, -1.0
};

struct ModePreset
{
    double preDelaySeconds;
    double decaySeconds;
    double dampingHz;
    double wetScale;
    double stereoWidth;
};

constexpr std::array<ModePreset, 10> kModePresets = {{
    {0.000, 0.20,  3500.0, 0.00, 0.00},
    {0.018, 1.85,  5200.0, 0.42, 0.95},
    {0.010, 0.85,  7800.0, 0.34, 0.82},
    {0.016, 1.25,  6800.0, 0.37, 0.88},
    {0.023, 1.75,  5900.0, 0.40, 0.92},
    {0.034, 3.20,  4400.0, 0.45, 1.00},
    {0.052, 4.60,  3600.0, 0.48, 1.08},
    {0.090, 3.50,  4100.0, 0.46, 1.10},
    {0.110, 1.10,  6500.0, 0.38, 0.96},
    {0.007, 2.25,  3100.0, 0.43, 0.72}
}};

double SignedDepth(int16_t value)
{
    return static_cast<double>(value) / 32768.0;
}

} // namespace

static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53,
              "Reference reverb requires IEEE 754 binary64");

Reverb::Reverb()
{
    for (std::size_t i = 0; i < kFdnLineCount; ++i)
        m_delayLines[i].resize(kDelayLengths[i], 0.0);

    m_preDelayLeft.resize(kMaximumPreDelaySamples, 0.0);
    m_preDelayRight.resize(kMaximumPreDelaySamples, 0.0);
    m_outputAllpassLeft.resize(298, 0.0);
    m_outputAllpassRight.resize(422, 0.0);

    PsyQParameters defaults;
    defaults.depthLeft = 0x6000;
    defaults.depthRight = 0x6000;
    SetPsyQParameters(defaults);
    Reset();
}

double Reverb::Clamp(double value, double minimum, double maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

double Reverb::Sanitize(double value)
{
    if (!std::isfinite(value) || std::abs(value) < kSilenceFloor)
        return 0.0;
    return value;
}

RoomParameters Reverb::MapPsyQParameters(const PsyQParameters& parameters)
{
    int mode = parameters.mode & 0xFF;
    if (mode < 0 || mode >= static_cast<int>(kModePresets.size()))
        mode = static_cast<int>(PsyQMode::Off);

    const ModePreset& preset = kModePresets[static_cast<std::size_t>(mode)];
    const double delayControl = Clamp(static_cast<double>(parameters.delay) / 127.0, 0.0, 1.0);
    const double feedbackControl = Clamp(static_cast<double>(parameters.feedback) / 127.0, 0.0, 1.0);

    RoomParameters room;
    room.preDelaySeconds = preset.preDelaySeconds;
    room.decaySeconds = preset.decaySeconds;
    room.dampingHz = preset.dampingHz;
    room.stereoWidth = preset.stereoWidth;
    room.inputGain = parameters.enabled && mode != static_cast<int>(PsyQMode::Off) ? 1.0 : 0.0;

    if (mode == static_cast<int>(PsyQMode::ChaosEcho) ||
        mode == static_cast<int>(PsyQMode::Delay))
    {
        room.preDelaySeconds += delayControl * 0.370;
    }
    else
    {
        room.preDelaySeconds += delayControl * 0.080;
    }

    if (mode == static_cast<int>(PsyQMode::ChaosEcho))
        room.decaySeconds = 0.90 + feedbackControl * 6.10;
    else if (mode == static_cast<int>(PsyQMode::Delay))
        room.decaySeconds = 0.35 + feedbackControl * 2.20;
    else
        room.decaySeconds *= 1.0 + feedbackControl * 0.25;

    room.wetLeft = preset.wetScale * SignedDepth(parameters.depthLeft);
    room.wetRight = preset.wetScale * SignedDepth(parameters.depthRight);
    return room;
}

void Reverb::SetPsyQParameters(const PsyQParameters& parameters)
{
    SetRoomParameters(MapPsyQParameters(parameters));
}

void Reverb::SetRoomParameters(const RoomParameters& parameters)
{
    RoomParameters safe = parameters;
    safe.preDelaySeconds = Clamp(safe.preDelaySeconds, 0.0, 0.5);
    safe.decaySeconds = Clamp(safe.decaySeconds, 0.15, 12.0);
    safe.dampingHz = Clamp(safe.dampingHz, 250.0, 30000.0);
    safe.wetLeft = Clamp(safe.wetLeft, -1.0, 1.0);
    safe.wetRight = Clamp(safe.wetRight, -1.0, 1.0);
    safe.stereoWidth = Clamp(safe.stereoWidth, 0.0, 1.5);
    safe.inputGain = Clamp(safe.inputGain, 0.0, 1.0);
    UpdateTargets(safe);
}

void Reverb::UpdateTargets(const RoomParameters& parameters)
{
    m_target = parameters;
    m_preDelaySamplesTarget = parameters.preDelaySeconds * kSampleRate;
    m_dampingPoleTarget = std::exp(-2.0 * kPi * parameters.dampingHz / kSampleRate);

    for (std::size_t i = 0; i < kFdnLineCount; ++i)
    {
        const double delaySeconds = static_cast<double>(kDelayLengths[i]) / kSampleRate;
        m_feedbackTarget[i] = std::pow(10.0, -3.0 * delaySeconds / parameters.decaySeconds);
        m_feedbackTarget[i] = Clamp(m_feedbackTarget[i], 0.0, 0.9995);
    }
}

void Reverb::Reset()
{
    for (std::size_t i = 0; i < kFdnLineCount; ++i)
    {
        std::fill(m_delayLines[i].begin(), m_delayLines[i].end(), 0.0);
        m_delayPositions[i] = 0;
        m_dampingState[i] = 0.0;
        m_feedbackCurrent[i] = m_feedbackTarget[i];
    }

    std::fill(m_preDelayLeft.begin(), m_preDelayLeft.end(), 0.0);
    std::fill(m_preDelayRight.begin(), m_preDelayRight.end(), 0.0);
    m_preDelayPosition = 0;
    std::fill(m_outputAllpassLeft.begin(), m_outputAllpassLeft.end(), 0.0);
    std::fill(m_outputAllpassRight.begin(), m_outputAllpassRight.end(), 0.0);
    m_outputAllpassLeftPosition = 0;
    m_outputAllpassRightPosition = 0;
    m_current = m_target;
    m_dampingPoleCurrent = m_dampingPoleTarget;
    m_preDelaySamplesCurrent = m_preDelaySamplesTarget;
    m_voiceSendCurrent = m_voiceSendTarget;
}

void Reverb::SetVoiceSend(std::size_t voiceIndex, bool enabled, double amount)
{
    if (voiceIndex >= kVoiceCount)
        return;

    const double normalized = Clamp(amount, 0.0, 1.0);
    m_voiceSendTarget[voiceIndex] = enabled ? normalized * normalized : 0.0;
}

double Reverb::GetVoiceSendTarget(std::size_t voiceIndex) const
{
    return voiceIndex < kVoiceCount ? m_voiceSendTarget[voiceIndex] : 0.0;
}

void Reverb::StepSmoothers()
{
    const auto smooth = [](double current, double target) {
        return current + (target - current) * kSmoothingCoefficient;
    };

    m_current.preDelaySeconds = smooth(m_current.preDelaySeconds, m_target.preDelaySeconds);
    m_current.decaySeconds = smooth(m_current.decaySeconds, m_target.decaySeconds);
    m_current.dampingHz = smooth(m_current.dampingHz, m_target.dampingHz);
    m_current.wetLeft = smooth(m_current.wetLeft, m_target.wetLeft);
    m_current.wetRight = smooth(m_current.wetRight, m_target.wetRight);
    m_current.stereoWidth = smooth(m_current.stereoWidth, m_target.stereoWidth);
    m_current.inputGain = smooth(m_current.inputGain, m_target.inputGain);
    m_preDelaySamplesCurrent = smooth(m_preDelaySamplesCurrent, m_preDelaySamplesTarget);
    m_dampingPoleCurrent = smooth(m_dampingPoleCurrent, m_dampingPoleTarget);

    for (std::size_t i = 0; i < kFdnLineCount; ++i)
    {
        m_feedbackCurrent[i] = smooth(m_feedbackCurrent[i], m_feedbackTarget[i]);
        m_voiceSendCurrent[i] = smooth(m_voiceSendCurrent[i], m_voiceSendTarget[i]);
    }
    for (std::size_t i = kFdnLineCount; i < kVoiceCount; ++i)
        m_voiceSendCurrent[i] = smooth(m_voiceSendCurrent[i], m_voiceSendTarget[i]);
}

double Reverb::ProcessPreDelay(std::vector<double>& buffer, double input)
{
    buffer[m_preDelayPosition] = Sanitize(input);

    double readPosition = static_cast<double>(m_preDelayPosition) - m_preDelaySamplesCurrent;
    while (readPosition < 0.0)
        readPosition += static_cast<double>(buffer.size());

    const std::size_t first = static_cast<std::size_t>(readPosition);
    const std::size_t second = (first + 1) % buffer.size();
    const double fraction = readPosition - static_cast<double>(first);
    return Sanitize(buffer[first] + (buffer[second] - buffer[first]) * fraction);
}

double Reverb::ProcessAllpass(std::vector<double>& buffer, std::size_t& position,
                              double input, double coefficient)
{
    const double delayed = buffer[position];
    const double output = Sanitize(delayed - coefficient * input);
    buffer[position] = Sanitize(input + coefficient * output);
    position = (position + 1) % buffer.size();
    return output;
}

void Reverb::Hadamard8(std::array<double, kFdnLineCount>& values)
{
    for (std::size_t width = 1; width < kFdnLineCount; width *= 2)
    {
        for (std::size_t base = 0; base < kFdnLineCount; base += width * 2)
        {
            for (std::size_t offset = 0; offset < width; ++offset)
            {
                const double a = values[base + offset];
                const double b = values[base + offset + width];
                values[base + offset] = a + b;
                values[base + offset + width] = a - b;
            }
        }
    }

    for (double& value : values)
        value *= kInvSqrt8;
}

void Reverb::Process(double sendLeft, double sendRight, double* wetLeft, double* wetRight)
{
    StepSmoothers();
    ProcessCore(sendLeft, sendRight, wetLeft, wetRight);
}

void Reverb::ProcessVoices(const StereoFrame* voices, std::size_t voiceCount,
                           double* wetLeft, double* wetRight)
{
    StepSmoothers();

    double sendLeft = 0.0;
    double sendRight = 0.0;
    const std::size_t count = std::min(voiceCount, kVoiceCount);
    if (voices)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            sendLeft += Sanitize(voices[i].left) * m_voiceSendCurrent[i];
            sendRight += Sanitize(voices[i].right) * m_voiceSendCurrent[i];
        }
    }
    ProcessCore(sendLeft, sendRight, wetLeft, wetRight);
}

void Reverb::ProcessCore(double sendLeft, double sendRight, double* wetLeft, double* wetRight)
{
    const double inputLeft = ProcessPreDelay(
        m_preDelayLeft, Sanitize(sendLeft) * m_current.inputGain);
    const double inputRight = ProcessPreDelay(
        m_preDelayRight, Sanitize(sendRight) * m_current.inputGain);
    m_preDelayPosition = (m_preDelayPosition + 1) % m_preDelayLeft.size();

    std::array<double, kFdnLineCount> lineOutputs{};
    std::array<double, kFdnLineCount> feedback{};

    for (std::size_t i = 0; i < kFdnLineCount; ++i)
    {
        lineOutputs[i] = Sanitize(m_delayLines[i][m_delayPositions[i]]);
        m_dampingState[i] = Sanitize(
            (1.0 - m_dampingPoleCurrent) * lineOutputs[i] +
            m_dampingPoleCurrent * m_dampingState[i]);
        feedback[i] = m_dampingState[i];
    }

    Hadamard8(feedback);

    for (std::size_t i = 0; i < kFdnLineCount; ++i)
    {
        const double injection = kInternalInputGain * kInvSqrt8 *
            (inputLeft * kInputLeft[i] + inputRight * kInputRight[i]);
        m_delayLines[i][m_delayPositions[i]] = Sanitize(
            injection + m_feedbackCurrent[i] * feedback[i]);
        m_delayPositions[i] = (m_delayPositions[i] + 1) % m_delayLines[i].size();
    }

    double left = 0.0;
    double right = 0.0;
    for (std::size_t i = 0; i < kFdnLineCount; ++i)
    {
        left += lineOutputs[i] * kOutputLeft[i];
        right += lineOutputs[i] * kOutputRight[i];
    }
    left *= kInvSqrt8;
    right *= kInvSqrt8;
    left = ProcessAllpass(m_outputAllpassLeft, m_outputAllpassLeftPosition, left, 0.57);
    right = ProcessAllpass(m_outputAllpassRight, m_outputAllpassRightPosition, right, 0.63);

    const double mid = 0.5 * (left + right);
    const double side = 0.5 * (left - right) * m_current.stereoWidth;
    left = (mid + side) * kOutputRecoveryGain * m_current.wetLeft;
    right = (mid - side) * kOutputRecoveryGain * m_current.wetRight;

    if (wetLeft)
        *wetLeft = Sanitize(left);
    if (wetRight)
        *wetRight = Sanitize(right);
}

double Reverb::InternalEnergy() const
{
    double energy = 0.0;
    for (const std::vector<double>& line : m_delayLines)
    {
        for (double sample : line)
            energy += sample * sample;
    }
    for (double state : m_dampingState)
        energy += state * state;
    for (double sample : m_outputAllpassLeft)
        energy += sample * sample;
    for (double sample : m_outputAllpassRight)
        energy += sample * sample;
    return std::isfinite(energy) ? energy : std::numeric_limits<double>::infinity();
}

} // namespace PsyX_ReferenceReverb
