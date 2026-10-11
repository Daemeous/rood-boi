// Floating-point PS1 SPU reverb (see PsyX_IdealReverb.h). Clean-room
// implementation from the psx-spx / nocash hardware documentation.

#include "PsyX_IdealReverb.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace PsyX_IdealReverb {

namespace {

// psx-spx "Reverb Buffer Resampling" 39-tap FIR, as fractions of 8000h.
constexpr int kTaps = 39;
constexpr double kFir[kTaps] = {
    -0x0001 / 32768.0,  0.0,  0x0002 / 32768.0,  0.0, -0x000A / 32768.0,  0.0,  0x0023 / 32768.0,  0.0,
    -0x0067 / 32768.0,  0.0,  0x010A / 32768.0,  0.0, -0x0268 / 32768.0,  0.0,  0x0534 / 32768.0,  0.0,
    -0x0B90 / 32768.0,  0.0,  0x2806 / 32768.0,  0.5,  0x2806 / 32768.0,  0.0, -0x0B90 / 32768.0,  0.0,
     0x0534 / 32768.0,  0.0, -0x0268 / 32768.0,  0.0,  0x010A / 32768.0,  0.0, -0x0067 / 32768.0,  0.0,
     0x0023 / 32768.0,  0.0, -0x000A / 32768.0,  0.0,  0x0002 / 32768.0,  0.0, -0x0001 / 32768.0,
};

double Volume(int16_t raw)
{
    return raw / 32768.0;
}

} // namespace

static_assert(std::numeric_limits<double>::is_iec559, "Ideal reverb requires IEEE 754 doubles");

Reverb::Reverb(uint32_t internalSampleRate)
    : m_storage(kSpuRamHalfwords, 0.0)
{
    SetInternalSampleRate(internalSampleRate);
    Reset();
}

bool Reverb::SetInternalSampleRate(uint32_t sampleRate)
{
    if (sampleRate != kRate176400 && sampleRate != kRate352800)
        return false;
    m_internalSampleRate = sampleRate;
    m_oversampleFactor = sampleRate / kLogicalSampleRate;
    m_internalPhase = 0;
    m_inputAccumulator[0] = m_inputAccumulator[1] = 0.0;
    m_lastLogicalOutput = {};
    return true;
}

void Reverb::Reset()
{
    m_regs = Registers{};
    m_mBaseRaw = 0;
    m_base = 0;
    m_depthLeft = m_depthRight = 0;
    m_masterEnable = false;
    ClearDelayState();
}

void Reverb::ClearDelayStorage()
{
    std::fill(m_storage.begin(), m_storage.end(), 0.0);
}

void Reverb::ClearDelayState()
{
    ClearDelayStorage();
    m_cursor = m_base;
    m_internalPhase = 0;
    m_inputAccumulator[0] = m_inputAccumulator[1] = 0.0;
    m_lastLogicalOutput = {};
    std::memset(m_input, 0, sizeof(m_input));
    std::memset(m_output, 0, sizeof(m_output));
    m_tick = 0;
}

void Reverb::LoadPreset(PresetId id)
{
    LoadPreset(GetPreset(id));
}

void Reverb::LoadPreset(const Preset& preset)
{
    m_regs = preset.regs;
}

void Reverb::SetPsyQParameters(const PsyQParameters& parameters)
{
    int mode = parameters.mode & 0xFF;
    if (mode >= static_cast<int>(PresetId::Count))
        mode = static_cast<int>(PresetId::Off);
    const Preset& preset = GetPreset(static_cast<PresetId>(mode));
    LoadPreset(preset);
    SetBaseAddress(static_cast<uint16_t>((kSpuRamBytes - preset.workAreaBytes) / 8u));
    SetOutputDepth(parameters.depthLeft, parameters.depthRight);
    SetMasterEnable(parameters.enabled && mode != static_cast<int>(PresetId::Off));
}

void Reverb::SetRegisterRaw(unsigned index, uint16_t value)
{
    if (index < 32u)
        std::memcpy(reinterpret_cast<uint8_t*>(&m_regs) + index * 2u, &value, 2u);
}

uint16_t Reverb::GetRegisterRaw(unsigned index) const
{
    uint16_t value = 0;
    if (index < 32u)
        std::memcpy(&value, reinterpret_cast<const uint8_t*>(&m_regs) + index * 2u, 2u);
    return value;
}

void Reverb::SetBaseAddress(uint16_t mBaseRaw)
{
    m_mBaseRaw = mBaseRaw;
    m_base = (static_cast<uint32_t>(mBaseRaw) * 8u) & (kSpuRamBytes - 1u);
    m_cursor = m_base;
}

void Reverb::SetOutputDepth(int16_t left, int16_t right)
{
    m_depthLeft = left;
    m_depthRight = right;
}

// Same ring addressing as PsyX_SPUReverb::Reverb::BufferAddress, as a halfword
// index into m_storage.
uint32_t Reverb::BufferIndex(uint32_t offsetBytes) const
{
    uint32_t address = m_cursor + (offsetBytes & (kSpuRamBytes - 2u));
    if (address >= kSpuRamBytes)
        address = address - kSpuRamBytes + m_base;
    return (address & (kSpuRamBytes - 2u)) / 2u;
}

double Reverb::Load(uint16_t reg, int32_t adjustBytes) const
{
    return m_storage[BufferIndex(static_cast<uint32_t>(reg) * 8u + static_cast<uint32_t>(adjustBytes))];
}

void Reverb::Store(uint16_t reg, double value)
{
    if (m_masterEnable)
        m_storage[BufferIndex(static_cast<uint32_t>(reg) * 8u)] = value;
}

void Reverb::Process(double inputLeft, double inputRight, double* outputLeft, double* outputRight)
{
    m_inputAccumulator[0] += inputLeft;
    m_inputAccumulator[1] += inputRight;
    if (++m_internalPhase == m_oversampleFactor)
    {
        const double scale = 1.0 / m_oversampleFactor;
        m_lastLogicalOutput = Step44100(m_inputAccumulator[0] * scale, m_inputAccumulator[1] * scale);
        m_inputAccumulator[0] = m_inputAccumulator[1] = 0.0;
        m_internalPhase = 0;
    }
    if (outputLeft)
        *outputLeft = m_lastLogicalOutput.left;
    if (outputRight)
        *outputRight = m_lastLogicalOutput.right;
}

StereoFrame Reverb::Process(double inputLeft, double inputRight)
{
    StereoFrame frame{};
    Process(inputLeft, inputRight, &frame.left, &frame.right);
    return frame;
}

StereoFrame Reverb::Step44100(double inputLeft, double inputRight)
{
    const uint32_t now = m_tick % kHistory;
    m_input[0][now] = inputLeft;
    m_input[1][now] = inputRight;
    if (m_tick & 1u)
    {
        double decimated[2], reverb[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            double acc = 0.0;
            for (int k = 0; k < kTaps; ++k)
                acc += kFir[k] * m_input[ch][(m_tick - k) % kHistory];
            decimated[ch] = acc;
        }
        Tick22050(decimated, reverb);
        m_output[0][now] = reverb[0];
        m_output[1][now] = reverb[1];
    }
    else
    {
        m_output[0][now] = m_output[1][now] = 0.0;
    }
    double up[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        double acc = 0.0;
        for (int k = 0; k < kTaps; ++k)
            acc += kFir[k] * m_output[ch][(m_tick - k) % kHistory];
        up[ch] = 2.0 * acc;
    }
    ++m_tick;
    return { up[0] * Volume(m_depthLeft), up[1] * Volume(m_depthRight) };
}

// psx-spx "SPU Reverb Formula" without rounding or saturation.
void Reverb::Tick22050(const double in[2], double out[2])
{
    const Registers& r = m_regs;
    const double vIIR = Volume(r.vIIR), vWALL = Volume(r.vWALL);
    auto reflect = [&](uint16_t dst, double input, uint16_t src) {
        const double previous = Load(dst, -2);
        Store(dst, (input + Load(src) * vWALL - previous) * vIIR + previous);
    };
    const double lin = in[0] * Volume(r.vLIN);
    const double rin = in[1] * Volume(r.vRIN);
    reflect(r.mLSAME, lin, r.dLSAME);
    reflect(r.mRSAME, rin, r.dRSAME);
    reflect(r.mLDIFF, lin, r.dRDIFF);
    reflect(r.mRDIFF, rin, r.dLDIFF);

    const uint16_t comb[2][4] = {
        { r.mLCOMB1, r.mLCOMB2, r.mLCOMB3, r.mLCOMB4 },
        { r.mRCOMB1, r.mRCOMB2, r.mRCOMB3, r.mRCOMB4 },
    };
    const uint16_t apf1[2] = { r.mLAPF1, r.mRAPF1 };
    const uint16_t apf2[2] = { r.mLAPF2, r.mRAPF2 };
    const double vAPF1 = Volume(r.vAPF1), vAPF2 = Volume(r.vAPF2);
    for (int ch = 0; ch < 2; ++ch)
    {
        double v = Load(comb[ch][0]) * Volume(r.vCOMB1) + Load(comb[ch][1]) * Volume(r.vCOMB2)
                 + Load(comb[ch][2]) * Volume(r.vCOMB3) + Load(comb[ch][3]) * Volume(r.vCOMB4);
        const double delayed1 = Load(static_cast<uint16_t>(apf1[ch] - r.dAPF1));
        v -= delayed1 * vAPF1;
        Store(apf1[ch], v);
        v = v * vAPF1 + delayed1;
        const double delayed2 = Load(static_cast<uint16_t>(apf2[ch] - r.dAPF2));
        v -= delayed2 * vAPF2;
        Store(apf2[ch], v);
        out[ch] = v * vAPF2 + delayed2;
    }

    const uint32_t next = (m_cursor + 2u) & kWorkAreaEndAddress;
    m_cursor = next < m_base ? m_base : next;
}

double Reverb::ReadStorageHalfword(uint32_t index) const
{
    return m_storage[index % kSpuRamHalfwords];
}

void Reverb::WriteStorageHalfword(uint32_t index, double value)
{
    m_storage[index % kSpuRamHalfwords] = value;
}

double Reverb::InternalEnergy() const
{
    double energy = 0.0;
    for (double v : m_storage)
        energy += v * v;
    for (const auto& channel : m_input)
        for (double v : channel)
            energy += v * v;
    for (const auto& channel : m_output)
        for (double v : channel)
            energy += v * v;
    return energy;
}

} // namespace PsyX_IdealReverb
