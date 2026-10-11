#ifndef PSYX_SPUREVERB_H
#define PSYX_SPUREVERB_H

// PS1 SPU reverb unit (integer, hardware-style model).
//
// Clean-room implementation, written for rood-boi from the public PlayStation
// hardware documentation only: the "Sound Processing Unit (SPU)" chapter of
// psx-spx / nocash PSX specifications
// (https://psx-spx.consoledev.net/soundprocessingunitspu/), sections "SPU Reverb
// Registers", "SPU Reverb Formula", "SPU Reverb Examples" and "Reverb Buffer
// Resampling". No emulator source code was consulted or used. Released under
// the same MIT licence as the rest of this directory (see ../../LICENSE).
//
// What the documentation specifies, and this file implements:
//   - 32 reverb registers (1F801DC0h..1F801DFEh), mBASE (1F801DA2h) and the
//     output volume vLOUT/vROUT (1F801D84h/86h).
//   - The reverb work area: from mBASE*8 up to the fixed end of SPU RAM. All
//     buffer offsets are relative to a cursor ("BufferAddress") that advances
//     by one halfword per reverb tick and wraps inside the work area.
//   - The formula: same-side and cross-side reflections (vIIR/vWALL), the four
//     comb taps, two all-pass filters, output volume.
//   - The unit runs at 22050 Hz: the 44100 Hz input is decimated, and the
//     output interpolated back, with the documented 39-tap FIR.
//   - SPUCNT bit 7 (reverb master enable) only gates writes to the buffer.
//
// The caller owns the 512 KiB SPU RAM and passes it to every Process() call, so
// voice data and the reverb buffer alias exactly as on the console.

#include <cstddef>
#include <cstdint>

namespace PsyX_SPUReverb {

// SPU RAM size in bytes.
constexpr uint32_t kSpuRamBytes = 512u * 1024u;

// Last halfword of SPU RAM (bytes): the fixed top of the reverb work area.
constexpr uint32_t kWorkAreaEndAddress = 0x7FFFEu;

// The 32 reverb registers in port order (field 0 = 1F801DC0h ... field 31 =
// 1F801DFEh), named as in psx-spx. Address registers (m*, d*) are in units of
// 8 bytes; volume registers (v*) are signed 1.15 fixed point.
struct Registers
{
    uint16_t dAPF1, dAPF2;
    int16_t  vIIR;
    int16_t  vCOMB1, vCOMB2, vCOMB3, vCOMB4;
    int16_t  vWALL;
    int16_t  vAPF1, vAPF2;
    uint16_t mLSAME, mRSAME;
    uint16_t mLCOMB1, mRCOMB1;
    uint16_t mLCOMB2, mRCOMB2;
    uint16_t dLSAME, dRSAME;
    uint16_t mLDIFF, mRDIFF;
    uint16_t mLCOMB3, mRCOMB3;
    uint16_t mLCOMB4, mRCOMB4;
    uint16_t dLDIFF, dRDIFF;
    uint16_t mLAPF1, mRAPF1;
    uint16_t mLAPF2, mRAPF2;
    int16_t  vLIN, vRIN;
};

static_assert(sizeof(Registers) == 32u * sizeof(uint16_t),
              "Registers must be 32 packed halfwords, in port order");

// The ten libspu reverb modes (SPU_REV_MODE_OFF .. SPU_REV_MODE_PIPE), which
// are the ten example register sets published in psx-spx.
enum class PresetId : int
{
    Off = 0,      // SPU_REV_MODE_OFF
    Room,         // SPU_REV_MODE_ROOM
    StudioSmall,  // SPU_REV_MODE_STUDIO_A
    StudioMedium, // SPU_REV_MODE_STUDIO_B
    StudioLarge,  // SPU_REV_MODE_STUDIO_C
    Hall,         // SPU_REV_MODE_HALL
    SpaceEcho,    // SPU_REV_MODE_SPACE
    ChaosEcho,    // SPU_REV_MODE_ECHO
    Delay,        // SPU_REV_MODE_DELAY
    HalfEcho,     // SPU_REV_MODE_PIPE
    Count
};

struct Preset
{
    PresetId id;
    const char* name;
    uint32_t workAreaBytes; // size of the work area at the top of SPU RAM
    Registers regs;
};

// The register set for a mode (an out-of-range id gives Off).
const Preset& GetPreset(PresetId id);

class Reverb
{
public:
    Reverb();

    // Clears registers, addresses, volumes, enable and filter history. Does
    // not touch SPU RAM.
    void Reset();
    // Rewinds the cursor to mBASE and clears the filter history.
    void ClearDelayState();

    void LoadPreset(PresetId id);
    void LoadPreset(const Preset& preset);

    void SetRegisters(const Registers& regs) { m_regs = regs; }
    const Registers& GetRegisters() const { return m_regs; }

    // Register access by port index (0 = 1F801DC0h ... 31 = 1F801DFEh).
    void SetRegisterRaw(unsigned index, uint16_t value);
    uint16_t GetRegisterRaw(unsigned index) const;

    // mBASE in port units (8 bytes). Writing it also moves the cursor there.
    void SetBaseAddress(uint16_t mBaseRaw);
    uint16_t GetBaseAddressRaw() const { return m_mBaseRaw; }

    // vLOUT / vROUT.
    void SetOutputVolume(int16_t vLOUT, int16_t vROUT) { m_vLOUT = vLOUT; m_vROUT = vROUT; }

    // SPUCNT bit 7. When clear the buffer is not written, but it is still read
    // and the unit keeps producing output from what it holds.
    void SetMasterEnable(bool enabled) { m_masterEnable = enabled; }
    bool GetMasterEnable() const { return m_masterEnable; }

    // One 44100 Hz tick. `inputLeft/Right` are the sums of every source with
    // its reverb bit set; they are saturated to 16 bits here. `ram` is the
    // live SPU RAM (at least kSpuRamBytes). Outputs are after vLOUT/vROUT.
    void Process(uint8_t* ram, uint32_t ramSizeBytes,
                 int32_t inputLeft, int32_t inputRight,
                 int32_t* outLeft, int32_t* outRight);

private:
    // 44100 Hz history length for the 39-tap resampling filter (power of two).
    static constexpr unsigned kHistory = 64;

    uint32_t BufferAddress(uint32_t offsetBytes) const;
    int32_t Load(const uint8_t* ram, uint16_t reg, int32_t adjustBytes = 0) const;
    void Store(uint8_t* ram, uint16_t reg, int32_t value) const;
    void Tick22050(uint8_t* ram, const int32_t in[2], int32_t out[2]);

    Registers m_regs{};
    uint16_t m_mBaseRaw = 0;
    uint32_t m_base = 0;   // work area start, bytes
    uint32_t m_cursor = 0; // BufferAddress, bytes (even)
    int16_t m_vLOUT = 0;
    int16_t m_vROUT = 0;
    bool m_masterEnable = false;

    // Last kHistory input samples (decimator) and zero-stuffed output samples
    // (interpolator) per channel, indexed by m_tick modulo kHistory.
    int16_t m_input[2][kHistory]{};
    int16_t m_output[2][kHistory]{};
    uint32_t m_tick = 0;
};

} // namespace PsyX_SPUReverb

#endif // PSYX_SPUREVERB_H
