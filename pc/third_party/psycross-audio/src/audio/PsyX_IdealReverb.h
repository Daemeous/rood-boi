#ifndef PSYX_IDEALREVERB_H
#define PSYX_IDEALREVERB_H

// Floating-point ("ideal") variant of the PS1 SPU reverb for the Ideal
// renderer: the same documented network as PsyX_SPUReverb (psx-spx "SPU Reverb
// Formula" and "Reverb Buffer Resampling"), computed in double precision with
// no 16-bit storage, rounding or saturation, in its own delay storage rather
// than SPU RAM. Input is accepted at 176400 or 352800 Hz and box-averaged down
// to the unit's 44100 Hz input rate.
//
// Clean-room implementation from the public hardware documentation only (see
// PsyX_SPUReverb.h); MIT, see ../../LICENSE.

#include "PsyX_SPUReverb.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace PsyX_IdealReverb {

constexpr uint32_t kSpuRamBytes = PsyX_SPUReverb::kSpuRamBytes;
constexpr uint32_t kSpuRamHalfwords = kSpuRamBytes / 2u;
constexpr uint32_t kWorkAreaEndAddress = PsyX_SPUReverb::kWorkAreaEndAddress;
constexpr uint32_t kLogicalSampleRate = 44100u;
constexpr uint32_t kReverbSampleRate = 22050u;
constexpr uint32_t kRate176400 = 176400u;
constexpr uint32_t kRate352800 = 352800u;

using Registers = PsyX_SPUReverb::Registers;
using PresetId = PsyX_SPUReverb::PresetId;
using Preset = PsyX_SPUReverb::Preset;

struct PsyQParameters
{
    int mode = static_cast<int>(PresetId::Room);
    int16_t depthLeft = 0;
    int16_t depthRight = 0;
    bool enabled = true;
};

struct StereoFrame
{
    double left;
    double right;
};

using PsyX_SPUReverb::GetPreset;

class Reverb
{
public:
    explicit Reverb(uint32_t internalSampleRate = kRate176400);

    // 176400 or 352800; anything else is refused.
    bool SetInternalSampleRate(uint32_t sampleRate);
    uint32_t GetInternalSampleRate() const { return m_internalSampleRate; }
    uint32_t GetOversampleFactor() const { return m_oversampleFactor; }

    void Reset();
    void ClearDelayStorage();
    void ClearDelayState();

    void LoadPreset(PresetId id);
    void LoadPreset(const Preset& preset);
    // Mode, base address, output depth and enable as libspu would set them.
    void SetPsyQParameters(const PsyQParameters& parameters);

    void SetRegisters(const Registers& regs) { m_regs = regs; }
    const Registers& GetRegisters() const { return m_regs; }
    void SetRegisterRaw(unsigned index, uint16_t value);
    uint16_t GetRegisterRaw(unsigned index) const;

    void SetBaseAddress(uint16_t mBaseRaw);
    uint16_t GetBaseAddressRaw() const { return m_mBaseRaw; }
    uint32_t GetBaseAddressHalfword() const { return m_base / 2u; }
    uint32_t GetCurrentAddressHalfword() const { return m_cursor / 2u; }

    void SetOutputDepth(int16_t left, int16_t right);
    int16_t GetOutputDepthLeft() const { return m_depthLeft; }
    int16_t GetOutputDepthRight() const { return m_depthRight; }

    void SetMasterEnable(bool enabled) { m_masterEnable = enabled; }
    bool GetMasterEnable() const { return m_masterEnable; }

    // One tick at the internal rate; the output holds between 44100 Hz steps.
    void Process(double inputLeft, double inputRight, double* outputLeft, double* outputRight);
    StereoFrame Process(double inputLeft, double inputRight);

    double ReadStorageHalfword(uint32_t index) const;
    void WriteStorageHalfword(uint32_t index, double value);
    // Sum of squares of every stored value (storage and filter history).
    double InternalEnergy() const;

private:
    static constexpr unsigned kHistory = 64;

    uint32_t BufferIndex(uint32_t offsetBytes) const;
    double Load(uint16_t reg, int32_t adjustBytes = 0) const;
    void Store(uint16_t reg, double value);
    StereoFrame Step44100(double inputLeft, double inputRight);
    void Tick22050(const double in[2], double out[2]);

    Registers m_regs{};
    std::vector<double> m_storage; // one value per SPU RAM halfword
    uint32_t m_internalSampleRate = kRate176400;
    uint32_t m_oversampleFactor = 4;
    uint32_t m_internalPhase = 0;
    double m_inputAccumulator[2]{};
    StereoFrame m_lastLogicalOutput{};

    uint16_t m_mBaseRaw = 0;
    uint32_t m_base = 0;   // bytes
    uint32_t m_cursor = 0; // bytes
    int16_t m_depthLeft = 0;
    int16_t m_depthRight = 0;
    bool m_masterEnable = false;

    double m_input[2][kHistory]{};
    double m_output[2][kHistory]{};
    uint32_t m_tick = 0;
};

} // namespace PsyX_IdealReverb

#endif
