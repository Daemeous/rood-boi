// PS1 SPU reverb unit: clean-room implementation from the psx-spx / nocash
// hardware documentation (see PsyX_SPUReverb.h for scope and provenance).

#include "PsyX_SPUReverb.h"

#include <cstring>

namespace PsyX_SPUReverb {

namespace {

// psx-spx "Reverb Buffer Resampling": the 39-tap low-pass FIR used both to
// decimate the 44100 Hz input to the 22050 Hz reverb rate and to interpolate
// the zero-stuffed 22050 Hz output back to 44100 Hz (with a gain of 2 there).
constexpr int kTaps = 39;
constexpr int32_t kFir[kTaps] = {
    -0x0001,  0x0000,  0x0002,  0x0000, -0x000A,  0x0000,  0x0023,  0x0000,
    -0x0067,  0x0000,  0x010A,  0x0000, -0x0268,  0x0000,  0x0534,  0x0000,
    -0x0B90,  0x0000,  0x2806,  0x4000,  0x2806,  0x0000, -0x0B90,  0x0000,
     0x0534,  0x0000, -0x0268,  0x0000,  0x010A,  0x0000, -0x0067,  0x0000,
     0x0023,  0x0000, -0x000A,  0x0000,  0x0002,  0x0000, -0x0001,
};

int32_t Saturate16(int32_t v)
{
    return v < -0x8000 ? -0x8000 : (v > 0x7FFF ? 0x7FFF : v);
}

// 1.15 fixed-point multiply: sample * volume / 8000h (64-bit: the reflection
// term multiplies a sum of three samples).
int32_t Mul(int32_t sample, int32_t volume)
{
    return static_cast<int32_t>((static_cast<int64_t>(sample) * volume) >> 15);
}

// The documented register sets (psx-spx "SPU Reverb Examples", which are the
// libspu SPU_REV_MODE_* sets), in port order 1F801DC0h..1F801DFEh, with the
// work-area size libspu reserves for each mode. Off: libspu reserves 10h
// bytes; 80h is kept here as a small margin below the sample heap.
struct RawPreset
{
    PresetId id;
    const char* name;
    uint32_t workAreaBytes;
    uint16_t regs[32];
};

// clang-format off
constexpr RawPreset kRawPresets[static_cast<int>(PresetId::Count)] = {
    { PresetId::Off, "Off", 0x80, {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
        0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
        0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000 } },
    { PresetId::Room, "Room", 0x26C0, {
        0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80,
        0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF,
        0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000 } },
    { PresetId::StudioSmall, "Studio Small", 0x1F40, {
        0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00,
        0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266,
        0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7,
        0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000 } },
    { PresetId::StudioMedium, "Studio Medium", 0x4840, {
        0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0,
        0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F, 0x07A2, 0x0616,
        0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7,
        0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000 } },
    { PresetId::StudioLarge, "Studio Large", 0x6FE0, {
        0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680,
        0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973,
        0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2,
        0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000 } },
    { PresetId::Hall, "Hall", 0xADE0, {
        0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000,
        0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1,
        0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD,
        0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000 } },
    { PresetId::SpaceEcho, "Space Echo", 0xF6C0, {
        0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000,
        0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2,
        0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D,
        0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000 } },
    { PresetId::ChaosEcho, "Chaos Echo", 0x18040, {
        0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100,
        0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
        0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000 } },
    { PresetId::Delay, "Delay", 0x18040, {
        0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
        0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000 } },
    { PresetId::HalfEcho, "Half Echo", 0x3C00, {
        0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500,
        0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7,
        0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059,
        0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000 } },
};
// clang-format on

struct PresetTable
{
    Preset presets[static_cast<int>(PresetId::Count)];
    PresetTable()
    {
        for (int i = 0; i < static_cast<int>(PresetId::Count); ++i)
        {
            presets[i].id = kRawPresets[i].id;
            presets[i].name = kRawPresets[i].name;
            presets[i].workAreaBytes = kRawPresets[i].workAreaBytes;
            std::memcpy(&presets[i].regs, kRawPresets[i].regs, sizeof(Registers));
        }
    }
};

} // namespace

const Preset& GetPreset(PresetId id)
{
    static const PresetTable table;
    int index = static_cast<int>(id);
    if (index < 0 || index >= static_cast<int>(PresetId::Count))
        index = static_cast<int>(PresetId::Off);
    return table.presets[index];
}

Reverb::Reverb()
{
    Reset();
}

void Reverb::Reset()
{
    m_regs = Registers{};
    m_mBaseRaw = 0;
    m_base = 0;
    m_vLOUT = m_vROUT = 0;
    m_masterEnable = false;
    ClearDelayState();
}

void Reverb::ClearDelayState()
{
    m_cursor = m_base;
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

// Registers is 32 halfwords in port order (static_assert in the header).
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
    // psx-spx: writing mBASE also sets the current buffer address.
    m_mBaseRaw = mBaseRaw;
    m_base = (static_cast<uint32_t>(mBaseRaw) * 8u) & (kSpuRamBytes - 1u);
    m_cursor = m_base;
}

// Byte address of the buffer word `offsetBytes` after the cursor. The work area
// is the ring [mBASE, 80000h): an address past the end of SPU RAM continues
// from mBASE. Offsets are taken modulo the RAM size (register differences such
// as mAPF - dAPF may wrap below zero).
uint32_t Reverb::BufferAddress(uint32_t offsetBytes) const
{
    uint32_t address = m_cursor + (offsetBytes & (kSpuRamBytes - 2u));
    if (address >= kSpuRamBytes)
        address = address - kSpuRamBytes + m_base;
    return address & (kSpuRamBytes - 2u);
}

// [reg] in the psx-spx formula: the halfword at reg*8 (+ adjustBytes) bytes
// from the cursor.
int32_t Reverb::Load(const uint8_t* ram, uint16_t reg, int32_t adjustBytes) const
{
    int16_t value;
    std::memcpy(&value, ram + BufferAddress(static_cast<uint32_t>(reg) * 8u + static_cast<uint32_t>(adjustBytes)), 2u);
    return value;
}

void Reverb::Store(uint8_t* ram, uint16_t reg, int32_t value) const
{
    if (!m_masterEnable)
        return; // psx-spx "Reverb Disable": no buffer writes
    const int16_t sample = static_cast<int16_t>(Saturate16(value));
    std::memcpy(ram + BufferAddress(static_cast<uint32_t>(reg) * 8u), &sample, 2u);
}

// One 22050 Hz step of the psx-spx "SPU Reverb Formula". `in` is the decimated
// input, `out` the reverb output before vLOUT/vROUT.
void Reverb::Tick22050(uint8_t* ram, const int32_t in[2], int32_t out[2])
{
    const Registers& r = m_regs;

    // vIIR = -8000h: psx-spx notes the product is formed correctly but the
    // result written to memory comes out negated.
    const int32_t reflectSign = (r.vIIR == -0x8000) ? -1 : 1;
    auto reflect = [&](uint16_t dst, int32_t input, uint16_t src) {
        const int32_t previous = Load(ram, dst, -2); // [m?SAME-2] / [m?DIFF-2]
        const int32_t wall = Mul(Load(ram, src), r.vWALL);
        const int32_t value = Mul(input + wall - previous, r.vIIR) + previous;
        Store(ram, dst, reflectSign * Saturate16(value));
    };

    const int32_t lin = Mul(in[0], r.vLIN);
    const int32_t rin = Mul(in[1], r.vRIN);

    // Same side reflection (L-to-L, R-to-R).
    reflect(r.mLSAME, lin, r.dLSAME);
    reflect(r.mRSAME, rin, r.dRSAME);
    // Different side reflection (R-to-L, L-to-R).
    reflect(r.mLDIFF, lin, r.dRDIFF);
    reflect(r.mRDIFF, rin, r.dLDIFF);

    const uint16_t comb[2][4] = {
        { r.mLCOMB1, r.mLCOMB2, r.mLCOMB3, r.mLCOMB4 },
        { r.mRCOMB1, r.mRCOMB2, r.mRCOMB3, r.mRCOMB4 },
    };
    const uint16_t apf1[2] = { r.mLAPF1, r.mRAPF1 };
    const uint16_t apf2[2] = { r.mLAPF2, r.mRAPF2 };
    for (int ch = 0; ch < 2; ++ch)
    {
        // Early echo: four comb taps.
        int32_t v = Mul(Load(ram, comb[ch][0]), r.vCOMB1) + Mul(Load(ram, comb[ch][1]), r.vCOMB2)
                  + Mul(Load(ram, comb[ch][2]), r.vCOMB3) + Mul(Load(ram, comb[ch][3]), r.vCOMB4);

        // Late reverb: two all-pass filters,
        //   out = out - vAPF*[mAPF-dAPF]; [mAPF] = out; out = out*vAPF + [mAPF-dAPF]
        const int32_t delayed1 = Load(ram, static_cast<uint16_t>(apf1[ch] - r.dAPF1));
        v = Saturate16(v - Mul(delayed1, r.vAPF1));
        Store(ram, apf1[ch], v);
        v = Saturate16(Mul(v, r.vAPF1) + delayed1);

        const int32_t delayed2 = Load(ram, static_cast<uint16_t>(apf2[ch] - r.dAPF2));
        v = Saturate16(v - Mul(delayed2, r.vAPF2));
        Store(ram, apf2[ch], v);
        out[ch] = Saturate16(Mul(v, r.vAPF2) + delayed2);
    }

    // BufferAddress = MAX(mBASE, (BufferAddress + 2) AND 7FFFEh)
    const uint32_t next = (m_cursor + 2u) & kWorkAreaEndAddress;
    m_cursor = next < m_base ? m_base : next;
}

void Reverb::Process(uint8_t* ram, uint32_t ramSizeBytes,
                     int32_t inputLeft, int32_t inputRight,
                     int32_t* outLeft, int32_t* outRight)
{
    (void)ramSizeBytes; // addressing always covers the full 512 KiB

    const uint32_t now = m_tick % kHistory;
    m_input[0][now] = static_cast<int16_t>(Saturate16(inputLeft));
    m_input[1][now] = static_cast<int16_t>(Saturate16(inputRight));

    // The reverb unit runs on every second 44100 Hz tick; on the others the
    // zero-stuffed output stream gets a zero.
    if (m_tick & 1u)
    {
        int32_t decimated[2], reverb[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            int32_t acc = 0;
            for (int k = 0; k < kTaps; ++k)
                acc += kFir[k] * m_input[ch][(m_tick - k) % kHistory];
            decimated[ch] = Saturate16(acc >> 15);
        }
        Tick22050(ram, decimated, reverb);
        m_output[0][now] = static_cast<int16_t>(reverb[0]);
        m_output[1][now] = static_cast<int16_t>(reverb[1]);
    }
    else
    {
        m_output[0][now] = 0;
        m_output[1][now] = 0;
    }

    int32_t up[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        int32_t acc = 0;
        for (int k = 0; k < kTaps; ++k)
            acc += kFir[k] * m_output[ch][(m_tick - k) % kHistory];
        up[ch] = Saturate16(acc >> 14); // gain 2 makes up for the zero stuffing
    }
    ++m_tick;

    *outLeft = Mul(up[0], m_vLOUT);
    *outRight = Mul(up[1], m_vROUT);
}

} // namespace PsyX_SPUReverb

#ifdef PSYX_SPUREVERB_STANDALONE_TEST

// Self-test, not part of the library build:
//   g++ -std=c++17 -DPSYX_SPUREVERB_STANDALONE_TEST -Wall -Wextra PsyX_SPUReverb.cpp && ./a.out

#include <cstdio>
#include <vector>

using namespace PsyX_SPUReverb;

static int g_failures;

static void Check(bool ok, const char* what)
{
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    g_failures += !ok;
}

static void Start(Reverb& rv, const Preset& preset)
{
    rv.LoadPreset(preset);
    rv.SetBaseAddress(static_cast<uint16_t>((kSpuRamBytes - preset.workAreaBytes) / 8u));
    rv.SetOutputVolume(0x7FFF, 0x7FFF);
    rv.SetMasterEnable(true);
}

int main()
{
    {
        std::vector<uint8_t> ram(kSpuRamBytes, 0);
        Reverb rv;
        Start(rv, GetPreset(PresetId::Room));
        bool silent = true;
        for (int i = 0; i < 20000 && silent; ++i)
        {
            int32_t l, r;
            rv.Process(ram.data(), kSpuRamBytes, 0, 0, &l, &r);
            silent = l == 0 && r == 0;
        }
        Check(silent, "silence in, silence out");
    }
    {
        std::vector<uint8_t> ram(kSpuRamBytes, 0);
        Reverb rv;
        Start(rv, GetPreset(PresetId::Room));
        bool heard = false;
        for (int i = 0; i < 60000; ++i)
        {
            int32_t l, r;
            rv.Process(ram.data(), kSpuRamBytes, i == 0 ? 0x4000 : 0, i == 0 ? 0x4000 : 0, &l, &r);
            heard |= l != 0 || r != 0;
        }
        Check(heard, "an impulse produces a reverb tail");
    }
    {
        // A small work area at the top of RAM: nothing below mBASE is written.
        std::vector<uint8_t> ram(kSpuRamBytes, 0xCD);
        Preset tiny = GetPreset(PresetId::Off);
        tiny.workAreaBytes = 0x40;
        const uint16_t regs[32] = { 1, 1, 0x4000, 0x2000, 0x2000, 0x1000, 0x1000, 0x4000, 0x4000, 0x4000,
                                    3, 3, 2, 2, 3, 3, 1, 1, 3, 3, 4, 4, 5, 5, 1, 1, 6, 6, 7, 7, 0x4000, 0x4000 };
        std::memcpy(&tiny.regs, regs, sizeof(regs));
        Reverb rv;
        Start(rv, tiny);
        for (int i = 0; i < 200000; ++i)
        {
            int32_t l, r;
            rv.Process(ram.data(), kSpuRamBytes, (i % 977) == 0 ? 0x1234 : 0, (i % 613) == 0 ? -0x4321 : 0, &l, &r);
        }
        bool outside = false;
        for (uint32_t a = 0; a < kSpuRamBytes - tiny.workAreaBytes; ++a)
            outside |= ram[a] != 0xCD;
        Check(!outside, "writes stay inside [mBASE, 80000h)");
    }
    std::printf(g_failures ? "%d failure(s)\n" : "all passed\n", g_failures);
    return g_failures != 0;
}

#endif // PSYX_SPUREVERB_STANDALONE_TEST
