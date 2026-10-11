#ifndef PSYX_REFERENCEREVERB_H
#define PSYX_REFERENCEREVERB_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PsyX_ReferenceReverb {

constexpr int kSampleRate = 352800;
constexpr std::size_t kFdnLineCount = 8;
constexpr std::size_t kVoiceCount = 24;

enum class PsyQMode : int
{
    Off = 0,
    Room,
    StudioSmall,
    StudioMedium,
    StudioLarge,
    Hall,
    SpaceEcho,
    ChaosEcho,
    Delay,
    HalfEcho
};

struct PsyQParameters
{
    int mode = static_cast<int>(PsyQMode::Room);
    int16_t depthLeft = 0;
    int16_t depthRight = 0;
    int delay = 0;
    int feedback = 0;
    bool enabled = true;
};

struct RoomParameters
{
    double preDelaySeconds = 0.018;
    double decaySeconds = 1.85;
    double dampingHz = 5200.0;
    double wetLeft = 0.0;
    double wetRight = 0.0;
    double stereoWidth = 0.95;
    double inputGain = 1.0;
};

struct StereoFrame
{
    double left;
    double right;
};

class Reverb
{
public:
    Reverb();

    void Reset();
    void SetPsyQParameters(const PsyQParameters& parameters);
    void SetRoomParameters(const RoomParameters& parameters);

    static RoomParameters MapPsyQParameters(const PsyQParameters& parameters);

    void SetVoiceSend(std::size_t voiceIndex, bool enabled, double amount = 1.0);
    double GetVoiceSendTarget(std::size_t voiceIndex) const;

    void Process(double sendLeft, double sendRight, double* wetLeft, double* wetRight);
    void ProcessVoices(const StereoFrame* voices, std::size_t voiceCount,
                       double* wetLeft, double* wetRight);

    double InternalEnergy() const;
    const RoomParameters& GetTargetRoomParameters() const { return m_target; }

private:
    static double Clamp(double value, double minimum, double maximum);
    static double Sanitize(double value);
    static void Hadamard8(std::array<double, kFdnLineCount>& values);

    void UpdateTargets(const RoomParameters& parameters);
    void StepSmoothers();
    void ProcessCore(double sendLeft, double sendRight, double* wetLeft, double* wetRight);
    double ProcessPreDelay(std::vector<double>& buffer, double input);
    static double ProcessAllpass(std::vector<double>& buffer, std::size_t& position,
                                 double input, double coefficient);

    std::array<std::vector<double>, kFdnLineCount> m_delayLines;
    std::array<std::size_t, kFdnLineCount> m_delayPositions{};
    std::array<double, kFdnLineCount> m_dampingState{};
    std::array<double, kFdnLineCount> m_feedbackCurrent{};
    std::array<double, kFdnLineCount> m_feedbackTarget{};

    std::vector<double> m_preDelayLeft;
    std::vector<double> m_preDelayRight;
    std::size_t m_preDelayPosition = 0;
    std::vector<double> m_outputAllpassLeft;
    std::vector<double> m_outputAllpassRight;
    std::size_t m_outputAllpassLeftPosition = 0;
    std::size_t m_outputAllpassRightPosition = 0;

    RoomParameters m_current{};
    RoomParameters m_target{};
    double m_dampingPoleCurrent = 0.0;
    double m_dampingPoleTarget = 0.0;
    double m_preDelaySamplesCurrent = 0.0;
    double m_preDelaySamplesTarget = 0.0;

    std::array<double, kVoiceCount> m_voiceSendCurrent{};
    std::array<double, kVoiceCount> m_voiceSendTarget{};
};

} // namespace PsyX_ReferenceReverb

#endif // PSYX_REFERENCEREVERB_H
