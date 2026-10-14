// PsyX_SPUCore.cpp - see PsyX_SPUCore.h for design notes/limitations.

#include "PsyX_SPUCore.h"

#include "PsyX/PsyX_public.h" /* g_PsyX_SfxOverride */

#include <string.h>
#include <algorithm>
#include <type_traits>
#include <cmath>

namespace PsyX
{

// ---------------------------------------------------------------------------
// Documented 512-entry, 8bit-indexed 4-point Gaussian interpolation table.
//
// Verbatim from the SPU hardware documentation (values as originally
// reverse-engineered and published in the "SPU ADPCM Pitch / 4-Point
// Gaussian Interpolation" section of the public nocash/psx-spx PS1 hardware
// specification). This is a fixed data table describing real silicon
// behavior (like a CRC table), not creative content.
// ---------------------------------------------------------------------------
static const int16_t s_gaussTable[512] =
{
    -0x0001,-0x0001,-0x0001,-0x0001,-0x0001,-0x0001,-0x0001,-0x0001,
    -0x0001,-0x0001,-0x0001,-0x0001,-0x0001,-0x0001,-0x0001,-0x0001,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
     0x0001, 0x0001, 0x0001, 0x0002, 0x0002, 0x0002, 0x0003, 0x0003,
     0x0003, 0x0004, 0x0004, 0x0005, 0x0005, 0x0006, 0x0007, 0x0007,
     0x0008, 0x0009, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
     0x000F, 0x0010, 0x0011, 0x0012, 0x0013, 0x0015, 0x0016, 0x0018,
     0x0019, 0x001B, 0x001C, 0x001E, 0x0020, 0x0021, 0x0023, 0x0025,
     0x0027, 0x0029, 0x002C, 0x002E, 0x0030, 0x0033, 0x0035, 0x0038,
     0x003A, 0x003D, 0x0040, 0x0043, 0x0046, 0x0049, 0x004D, 0x0050,
     0x0054, 0x0057, 0x005B, 0x005F, 0x0063, 0x0067, 0x006B, 0x006F,
     0x0074, 0x0078, 0x007D, 0x0082, 0x0087, 0x008C, 0x0091, 0x0096,
     0x009C, 0x00A1, 0x00A7, 0x00AD, 0x00B3, 0x00BA, 0x00C0, 0x00C7,
     0x00CD, 0x00D4, 0x00DB, 0x00E3, 0x00EA, 0x00F2, 0x00FA, 0x0101,
     0x010A, 0x0112, 0x011B, 0x0123, 0x012C, 0x0135, 0x013F, 0x0148,
     0x0152, 0x015C, 0x0166, 0x0171, 0x017B, 0x0186, 0x0191, 0x019C,
     0x01A8, 0x01B4, 0x01C0, 0x01CC, 0x01D9, 0x01E5, 0x01F2, 0x0200,
     0x020D, 0x021B, 0x0229, 0x0237, 0x0246, 0x0255, 0x0264, 0x0273,
     0x0283, 0x0293, 0x02A3, 0x02B4, 0x02C4, 0x02D6, 0x02E7, 0x02F9,
     0x030B, 0x031D, 0x0330, 0x0343, 0x0356, 0x036A, 0x037E, 0x0392,
     0x03A7, 0x03BC, 0x03D1, 0x03E7, 0x03FC, 0x0413, 0x042A, 0x0441,
     0x0458, 0x0470, 0x0488, 0x04A0, 0x04B9, 0x04D2, 0x04EC, 0x0506,
     0x0520, 0x053B, 0x0556, 0x0572, 0x058E, 0x05AA, 0x05C7, 0x05E4,
     0x0601, 0x061F, 0x063E, 0x065C, 0x067C, 0x069B, 0x06BB, 0x06DC,
     0x06FD, 0x071E, 0x0740, 0x0762, 0x0784, 0x07A7, 0x07CB, 0x07EF,
     0x0813, 0x0838, 0x085D, 0x0883, 0x08A9, 0x08D0, 0x08F7, 0x091E,
     0x0946, 0x096F, 0x0998, 0x09C1, 0x09EB, 0x0A16, 0x0A40, 0x0A6C,
     0x0A98, 0x0AC4, 0x0AF1, 0x0B1E, 0x0B4C, 0x0B7A, 0x0BA9, 0x0BD8,
     0x0C07, 0x0C38, 0x0C68, 0x0C99, 0x0CCB, 0x0CFD, 0x0D30, 0x0D63,
     0x0D97, 0x0DCB, 0x0E00, 0x0E35, 0x0E6B, 0x0EA1, 0x0ED7, 0x0F0F,
     0x0F46, 0x0F7F, 0x0FB7, 0x0FF1, 0x102A, 0x1065, 0x109F, 0x10DB,
     0x1116, 0x1153, 0x118F, 0x11CD, 0x120B, 0x1249, 0x1288, 0x12C7,
     0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
     0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700,
     0x1747, 0x1790, 0x17D8, 0x1821, 0x186B, 0x18B5, 0x1900, 0x194B,
     0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
     0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37,
     0x1E89, 0x1EDC, 0x1F2F, 0x1F82, 0x1FD6, 0x202A, 0x207F, 0x20D4,
     0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
     0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653,
     0x26AD, 0x2708, 0x2763, 0x27BE, 0x281A, 0x2876, 0x28D2, 0x292E,
     0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
     0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D,
     0x2F6C, 0x2FCC, 0x302B, 0x308B, 0x30EA, 0x314A, 0x31AA, 0x3209,
     0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
     0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807,
     0x3867, 0x38C6, 0x3926, 0x3985, 0x39E4, 0x3A43, 0x3AA2, 0x3B00,
     0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,
     0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0,
     0x412A, 0x4185, 0x41DF, 0x4239, 0x4292, 0x42EB, 0x4344, 0x439C,
     0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
     0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9,
     0x493A, 0x498A, 0x49D9, 0x4A29, 0x4A77, 0x4AC5, 0x4B13, 0x4B5F,
     0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
     0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7,
     0x5019, 0x505A, 0x509A, 0x50DA, 0x5118, 0x5156, 0x5194, 0x51D0,
     0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
     0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529,
     0x5558, 0x5585, 0x55B2, 0x55DE, 0x5609, 0x5632, 0x565B, 0x5684,
     0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
     0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886,
     0x589E, 0x58B5, 0x58CB, 0x58E0, 0x58F4, 0x5907, 0x5919, 0x592A,
     0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
     0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3,
};

const int16_t* SPUCore::GaussTable() { return s_gaussTable; }

double SPUCore::EvaluateIdealGaussian(const int16_t taps[4], uint32_t pitchCounter)
{
    const int i = static_cast<int>((pitchCounter >> 4) & 0xFFu);
    return (static_cast<double>(s_gaussTable[0x0FF - i]) * taps[3] +
            static_cast<double>(s_gaussTable[0x1FF - i]) * taps[2] +
            static_cast<double>(s_gaussTable[0x100 + i]) * taps[1] +
            static_cast<double>(s_gaussTable[0x000 + i]) * taps[0]) / 32768.0;
}

// SPU/XA-ADPCM filter coefficients (K0,K1), scaled by 64 (>>6 after the
// multiply-accumulate). This is the same "same as for CD-XA" filter table
// referenced by the SPU-ADPCM sample format documentation.
static const int32_t s_adpcmFilterK0[5] = {   0,  60, 115,  98, 122 };
static const int32_t s_adpcmFilterK1[5] = {   0,   0, -52, -55, -60 };

static inline int16_t SaturateS16(int32_t v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

// ---------------------------------------------------------------------------
// Construction / reset
// ---------------------------------------------------------------------------

SPUCore::SPUCore()
{
    Reset();
}

void SPUCore::ResetVoice(SPUVoiceState& v)
{
    memset(&v, 0, sizeof(v));
    v.attr.volmode.left  = SPU_VOICE_DIRECT16;
    v.attr.volmode.right = SPU_VOICE_DIRECT16;
    v.envPhase = EnvPhase::Off;
}

void SPUCore::Reset()
{
    memset(m_ram, 0, sizeof(m_ram));

    for (int i = 0; i < kNumVoices; ++i)
    {
        ResetVoice(m_voices[i]);
        m_voices[i].attr.voice = SPU_VOICECH(i);
    }

    m_malloc.initialized = false;
    m_malloc.heapBase = 0;
    m_malloc.heapEnd = kSpuRamSize;
    m_malloc.maxRecords = 0;
    m_malloc.blocks.clear();

    m_transferStartAddr = 0;
    m_transferCurAddr = 0;
    m_transferMode = TransferMode::Stop;

    m_masterVolL = m_masterVolR = 0;
    m_masterVolModeL = m_masterVolModeR = SPU_VOICE_DIRECT16;
    m_curMasterVolL = m_curMasterVolR = 0;
    m_masterSweepCounterL = m_masterSweepCounterR = 0;
    m_muted = SPU_OFF;

    m_reverbMasterEnable = false;
    memset(&m_reverbAttr, 0, sizeof(m_reverbAttr));
    m_reverb.Reset();
    m_idealReverb.Reset();
    m_referenceReverb.Reset();
    m_noiseClock = 0;
    m_noiseTimer = 0;
    m_noiseLevel = 1;

    m_cdEnable = false;
    m_cdReverb = false;
    m_cdVolL = m_cdVolR = 0;
    m_xaMasterGainQ16 = 65536u;
    m_xaMasterGain = 1.0;
    m_cdQueue.clear();
    m_cdQueueDouble.clear();
    m_lastCdFrame.l = m_lastCdFrame.r = 0;
    m_nativePhase = 0;
    m_referenceSincProcessCount = 0;
    for (int i = 0; i < kNumVoices; ++i)
    {
        m_referenceVoices[i].resampler.Reset();
        m_referenceVoices[i].processCount = 0;
        m_referenceVoices[i].lastBandlimitBucket = 0;
        m_highRateVoiceSamples[i] = 0.0;
        m_highRateVoiceSteps[i] = 0;
    }
}

// ---------------------------------------------------------------------------
// Sound RAM Data Transfer port
// ---------------------------------------------------------------------------

uint32_t SPUCore::SetTransferStartAddr(uint32_t addr)
{
    m_transferStartAddr = addr & (kSpuRamSize - 1u);
    m_transferCurAddr = m_transferStartAddr;
    return m_transferStartAddr;
}

uint32_t SPUCore::GetTransferStartAddr() const
{
    return m_transferStartAddr;
}

uint32_t SPUCore::GetCurrentTransferAddr() const
{
    return m_transferCurAddr;
}

void SPUCore::SetTransferMode(TransferMode mode)
{
    m_transferMode = mode;
}

TransferMode SPUCore::GetTransferMode() const
{
    return m_transferMode;
}

u_int SPUCore::Write(const u_char* src, u_int sizeBytes)
{
    if (m_transferMode == TransferMode::Stop)
        return 0;

    u_int remaining = (m_transferCurAddr < kSpuRamSize) ? (kSpuRamSize - m_transferCurAddr) : 0;
    u_int n = (sizeBytes < remaining) ? sizeBytes : remaining;
    if (n > 0)
    {
        memcpy(m_ram + m_transferCurAddr, src, n);
        m_transferCurAddr += n;
    }
    return n;
}

u_int SPUCore::Read(u_char* dst, u_int sizeBytes)
{
    u_int remaining = (m_transferCurAddr < kSpuRamSize) ? (kSpuRamSize - m_transferCurAddr) : 0;
    u_int n = (sizeBytes < remaining) ? sizeBytes : remaining;
    if (n > 0)
    {
        memcpy(dst, m_ram + m_transferCurAddr, n);
        m_transferCurAddr += n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// SPU RAM allocator (SDK convenience, see header comment on SPUMallocState)
// ---------------------------------------------------------------------------

void SPUCore::InitMalloc(int numRecords, uint32_t heapBaseAddr)
{
    m_malloc.initialized = true;
    m_malloc.maxRecords = numRecords;
    m_malloc.heapBase = heapBaseAddr;
    m_malloc.heapEnd = kSpuRamSize;
    m_malloc.blocks.clear();
    if (heapBaseAddr < kSpuRamSize)
    {
        SPUMallocState::Block b;
        b.addr = heapBaseAddr;
        b.size = kSpuRamSize - heapBaseAddr;
        b.free = true;
        m_malloc.blocks.push_back(b);
    }
}

uint32_t SPUCore::Malloc(uint32_t sizeBytes)
{
    if (!m_malloc.initialized)
        return 0;

    // 8-byte alignment, matching SPU address granularity.
    uint32_t alignedSize = (sizeBytes + 7u) & ~7u;

    for (size_t i = 0; i < m_malloc.blocks.size(); ++i)
    {
        SPUMallocState::Block& b = m_malloc.blocks[i];
        if (!b.free || b.size < alignedSize)
            continue;
        if (b.addr + alignedSize > m_malloc.heapEnd)
            continue;

        uint32_t addr = b.addr;
        if (b.size > alignedSize)
        {
            SPUMallocState::Block rest;
            rest.addr = b.addr + alignedSize;
            rest.size = b.size - alignedSize;
            rest.free = true;
            b.size = alignedSize;
            b.free = false;
            m_malloc.blocks.insert(m_malloc.blocks.begin() + i + 1, rest);
        }
        else
        {
            b.free = false;
        }
        return addr;
    }
    return 0; // SPU_NULL: no fitting free block
}

uint32_t SPUCore::MallocWithStartAddr(uint32_t addr, uint32_t sizeBytes)
{
    if (!m_malloc.initialized)
        return 0;

    uint32_t alignedSize = (sizeBytes + 7u) & ~7u;
    if (addr + alignedSize > m_malloc.heapEnd)
        return 0;

    for (size_t i = 0; i < m_malloc.blocks.size(); ++i)
    {
        SPUMallocState::Block& b = m_malloc.blocks[i];
        if (!b.free || addr < b.addr || addr + alignedSize > b.addr + b.size)
            continue;

        // Split off [b.addr, addr) as free, [addr, addr+alignedSize) as used,
        // and the remainder tail as free.
        std::vector<SPUMallocState::Block> newBlocks;
        if (addr > b.addr)
        {
            SPUMallocState::Block pre;
            pre.addr = b.addr; pre.size = addr - b.addr; pre.free = true;
            newBlocks.push_back(pre);
        }
        SPUMallocState::Block mid;
        mid.addr = addr; mid.size = alignedSize; mid.free = false;
        newBlocks.push_back(mid);
        uint32_t tailStart = addr + alignedSize;
        uint32_t blockEnd = b.addr + b.size;
        if (tailStart < blockEnd)
        {
            SPUMallocState::Block post;
            post.addr = tailStart; post.size = blockEnd - tailStart; post.free = true;
            newBlocks.push_back(post);
        }
        m_malloc.blocks.erase(m_malloc.blocks.begin() + i);
        m_malloc.blocks.insert(m_malloc.blocks.begin() + i, newBlocks.begin(), newBlocks.end());
        return addr;
    }
    return 0;
}

void SPUCore::Free(uint32_t addr)
{
    for (size_t i = 0; i < m_malloc.blocks.size(); ++i)
    {
        if (m_malloc.blocks[i].addr == addr)
        {
            m_malloc.blocks[i].free = true;
            break;
        }
    }

    // Coalesce adjacent free blocks.
    for (size_t i = 0; i + 1 < m_malloc.blocks.size(); )
    {
        SPUMallocState::Block& a = m_malloc.blocks[i];
        SPUMallocState::Block& b = m_malloc.blocks[i + 1];
        if (a.free && b.free && a.addr + a.size == b.addr)
        {
            a.size += b.size;
            m_malloc.blocks.erase(m_malloc.blocks.begin() + i + 1);
        }
        else
        {
            ++i;
        }
    }
}

// ---------------------------------------------------------------------------
// ADSR field packing helpers (adsr1/adsr2 <-> discrete fields), per the
// documented 32bit ADSR register layout.
// ---------------------------------------------------------------------------

// NOTE on `ar`/`sr`: the documented ADSR register packs each of these as a
// combined (shift<<2 | step) field (7 bits: 5-bit shift + 2-bit step), and
// that is exactly the bit pattern SpuVoiceAttr::ar / ::sr hold here - i.e.
// ar/sr are NOT plain shift values, they are the raw 7-bit register
// subfield, matching how adsr1/adsr2 (which are unquestionably the raw
// register halves) are packed. `dr`/`rr` have no step subfield on real
// hardware (fixed step -8), so they hold the plain shift value only.
//
// NOTE on `s_mode`: SpuVoiceAttr has no dedicated "sustain direction" field,
// yet the hardware ADSR register has one (bit30, distinct from bit31's
// linear/exponential mode) because Sustain - unlike Attack/Decay/Release -
// can run in either direction. We expose that 2nd bit by packing it into
// s_mode: bit0 = curve (0=Linear,1=Exponential), bit1 = direction
// (0=Increase,1=Decrease). a_mode/r_mode stay plain 0/1 since Attack is
// hardwired to increase and Release/Decay are hardwired to decrease.
static void UnpackAdsr(SpuVoiceAttr& a)
{
    unsigned short w1 = a.adsr1;
    unsigned short w2 = a.adsr2;

    a.a_mode = (w1 >> 15) & 1;
    a.ar     = (w1 >> 8) & 0x7F;     // bits 14-8: attack shift(5)+step(2) combined field
    a.dr     = (w1 >> 4) & 0xF;      // bits 7-4: decay shift
    a.sl     = w1 & 0xF;             // bits 3-0: sustain level nibble

    a.s_mode = ((w2 >> 15) & 1) | (((w2 >> 14) & 1) << 1); // bit0=mode, bit1=direction
    a.sr     = (w2 >> 6) & 0x7F;     // bits 22-16 relative: shift(5)+step(2)
    a.r_mode = (w2 >> 5) & 1;        // release mode
    a.rr     = w2 & 0x1F;            // release shift
}

static void PackAdsr(SpuVoiceAttr& a)
{
    unsigned short w1 = 0;
    w1 |= (a.a_mode & 1) << 15;
    w1 |= (a.ar & 0x7F) << 8;
    w1 |= (a.dr & 0xF) << 4;
    w1 |= (a.sl & 0xF);
    a.adsr1 = w1;

    unsigned short w2 = 0;
    w2 |= (a.s_mode & 1) << 15;
    w2 |= ((a.s_mode >> 1) & 1) << 14;
    w2 |= (a.sr & 0x7F) << 6;
    w2 |= (a.r_mode & 1) << 5;
    w2 |= (a.rr & 0x1F);
    a.adsr2 = w2;
}

// ---------------------------------------------------------------------------
// Voice attribute set/get
// ---------------------------------------------------------------------------

void SPUCore::ApplyVoiceAttrFields(SPUVoiceState& v, const SpuVoiceAttr& src, uint32_t mask)
{
    SpuVoiceAttr& a = v.attr;

    if (mask & SPU_VOICE_VOLL)      a.volume.left  = src.volume.left;
    if (mask & SPU_VOICE_VOLR)      a.volume.right = src.volume.right;
    if (mask & SPU_VOICE_VOLMODEL)  a.volmode.left  = src.volmode.left;
    if (mask & SPU_VOICE_VOLMODER)  a.volmode.right = src.volmode.right;
    if (mask & SPU_VOICE_PITCH)     a.pitch = src.pitch;
    if (mask & SPU_VOICE_NOTE)      a.note = src.note;
    if (mask & SPU_VOICE_SAMPLE_NOTE) a.sample_note = src.sample_note;
    if (mask & SPU_VOICE_WDSA)
    {
        a.addr = src.addr;
    }
    if (mask & SPU_VOICE_LSAX)
    {
        a.loop_addr = src.loop_addr;
        v.repeatAddr = src.loop_addr & (kSpuRamSize - 1u);
    }

    if (mask & SPU_VOICE_ADSR_ADSR1) a.adsr1 = src.adsr1;
    if (mask & SPU_VOICE_ADSR_ADSR2) a.adsr2 = src.adsr2;
    if (mask & (SPU_VOICE_ADSR_ADSR1 | SPU_VOICE_ADSR_ADSR2))
        UnpackAdsr(a);

    if (mask & SPU_VOICE_ADSR_AMODE) a.a_mode = src.a_mode;
    if (mask & SPU_VOICE_ADSR_SMODE) a.s_mode = src.s_mode;
    if (mask & SPU_VOICE_ADSR_RMODE) a.r_mode = src.r_mode;
    if (mask & SPU_VOICE_ADSR_AR)    a.ar = src.ar;
    if (mask & SPU_VOICE_ADSR_DR)    a.dr = src.dr;
    if (mask & SPU_VOICE_ADSR_SR)    a.sr = src.sr;
    if (mask & SPU_VOICE_ADSR_RR)    a.rr = src.rr;
    if (mask & SPU_VOICE_ADSR_SL)    a.sl = src.sl;

    const bool touchedDiscrete =
        (mask & (SPU_VOICE_ADSR_AMODE | SPU_VOICE_ADSR_SMODE |
                 SPU_VOICE_ADSR_RMODE | SPU_VOICE_ADSR_AR |
                 SPU_VOICE_ADSR_DR | SPU_VOICE_ADSR_SR |
                 SPU_VOICE_ADSR_RR | SPU_VOICE_ADSR_SL)) != 0;
    if (touchedDiscrete)
        PackAdsr(a);
}

void SPUCore::SetVoiceAttr(const SpuVoiceAttr& attr)
{
    for (int i = 0; i < kNumVoices; ++i)
    {
        if ((attr.voice & SPU_VOICECH(i)) == 0)
            continue;
        ApplyVoiceAttrFields(m_voices[i], attr, attr.mask);
    }
}

void SPUCore::GetVoiceAttr(SpuVoiceAttr& attr) const
{
    for (int i = 0; i < kNumVoices; ++i)
    {
        if (attr.voice != static_cast<unsigned long>(SPU_VOICECH(i)))
            continue;
        attr = m_voices[i].attr;
        attr.voice = SPU_VOICECH(i);
        return;
    }
    memset(&attr, 0, sizeof(attr));
}

void SPUCore::SetVoiceAttrIndexed(int voiceIndex, const SpuVoiceAttr& attr)
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    ApplyVoiceAttrFields(m_voices[voiceIndex], attr, attr.mask);
}

void SPUCore::GetVoiceAttrIndexed(int voiceIndex, SpuVoiceAttr& attr) const
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
    {
        memset(&attr, 0, sizeof(attr));
        return;
    }
    attr = m_voices[voiceIndex].attr;
}

void SPUCore::SetVoiceVolume(int voiceIndex, int16_t volL, int16_t volR)
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    m_voices[voiceIndex].attr.volume.left = volL;
    m_voices[voiceIndex].attr.volume.right = volR;
    m_voices[voiceIndex].attr.volmode.left = SPU_VOICE_DIRECT16;
    m_voices[voiceIndex].attr.volmode.right = SPU_VOICE_DIRECT16;
}

void SPUCore::GetVoiceVolume(int voiceIndex, int16_t* volL, int16_t* volR) const
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    if (volL) *volL = m_voices[voiceIndex].attr.volume.left;
    if (volR) *volR = m_voices[voiceIndex].attr.volume.right;
}

void SPUCore::GetVoiceVolumeX(int voiceIndex, int16_t* volXL, int16_t* volXR) const
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    if (volXL) *volXL = m_voices[voiceIndex].attr.volumex.left;
    if (volXR) *volXR = m_voices[voiceIndex].attr.volumex.right;
}

void SPUCore::SetVoicePitch(int voiceIndex, uint16_t pitch)
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    m_voices[voiceIndex].attr.pitch = pitch;
}

void SPUCore::GetVoicePitch(int voiceIndex, uint16_t* pitch) const
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    if (pitch) *pitch = m_voices[voiceIndex].attr.pitch;
}

void SPUCore::SetVoiceStartAddr(int voiceIndex, uint32_t startAddr)
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    SPUVoiceState& v = m_voices[voiceIndex];
    v.attr.addr = startAddr;
}

void SPUCore::SetVoiceLoopStartAddr(int voiceIndex, uint32_t lsa)
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    SPUVoiceState& v = m_voices[voiceIndex];
    v.attr.loop_addr = lsa;
    v.repeatAddr = lsa & (kSpuRamSize - 1u);
}

void SPUCore::GetVoiceEnvelope(int voiceIndex, int16_t* envx) const
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;
    if (envx) *envx = static_cast<int16_t>(m_voices[voiceIndex].envLevel);
}

int32_t SPUCore::GetVoiceLastOutputX(int voiceIndex) const
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return 0;
    return m_voices[voiceIndex].lastOutX;
}

// ---------------------------------------------------------------------------
// Key on/off + 4-state key status
// ---------------------------------------------------------------------------

void SPUCore::KeyOnVoice(SPUVoiceState& v, int voiceIndex)
{
    v.curAddr = v.attr.addr & (kSpuRamSize - 1u);
    v.repeatAddr = v.attr.loop_addr & (kSpuRamSize - 1u);
    v.repeatAddrLatchedByBlock = false;
    v.adpcmHist1 = 0;
    v.adpcmHist2 = 0;
    memset(v.blockSamples, 0, sizeof(v.blockSamples));
    v.blockSamplePos = kAdpcmBlockSamples; // force a decode on first tick
    v.blockValid = false;
    v.interpTaps[0] = v.interpTaps[1] = v.interpTaps[2] = v.interpTaps[3] = 0;
    v.pitchCounter = 0;
    v.reachedLoopEnd = false;   // Key On clears ENDX for this voice
    v.blockLoopEnd = false;
    v.forceReleaseOnBlockEnd = false;

    v.envPhase = EnvPhase::Attack;
    v.envLevel = 0;             // Key On always resets envelope to 0
    v.envCounter = 0;
    v.everKeyedOn = true;

    // Loose per-sound replacement, the same registry the OpenAL voice path uses
    // (g_PsyX_SfxOverride -> pc_sfx_override.c), keyed on the SPU address this
    // voice plays from. Without this the software SPU just decodes the original
    // ADPCM and every sound mod is silently ignored -- which is what happened to
    // all of them when the software SPU became the default renderer.
    v.ovrPcm = nullptr;
    v.ovrCount = 0;
    v.ovrPos = 0;
    v.ovrStep = 0x10000u;
    {
        const short* modPcm = nullptr;
        int modCount = 0;
        int modRate = 0;

        if (g_PsyX_SfxOverride != nullptr &&
            g_PsyX_SfxOverride(static_cast<int>(v.attr.addr), &modPcm, &modCount, &modRate) &&
            modCount > 0 && modPcm != nullptr)
        {
            // A replacement whose rate is KNOWN plays at the rate it was AUTHORED
            // at. A voice consumes decoded samples at (pitch/4096) per 44.1kHz
            // tick, so advancing the source by rate*4096/(44100*pitch) per emitted
            // sample lands it at its own rate for this Key On. Pitch the game
            // applies later still scales relative to that latch, so a modulated
            // sound keeps its modulation -- the same contract the OpenAL path
            // documents.
            //
            // An UNKNOWN rate (modRate 0) falls back to native semantics instead:
            // one source sample per (pitch/4096) tick, exactly what the original
            // ADPCM at this address would have done. That is every sample lifted
            // out of a whole-bank SND/<BANK>.VAB replacement -- ADPCM carries no
            // rate, so the registry reports 0 and the game's own pitch is the only
            // thing that knows how fast the sound should run. Latching a 44100
            // baseline there played such a bank at a flat 44.1kHz however low the
            // game keyed it, which is what broke the replaced weapon sounds:
            // PISTOL and SHOTGUN are whole-bank mods. The OpenAL path draws the
            // same line -- overridePitchBase stays 0 unless modRate > 0, leaving
            // plain pitch/4096 semantics in place.
            uint64_t step = 0x10000ull;

            if (modRate > 0 && v.attr.pitch > 0)
            {
                const uint64_t pitch = static_cast<uint64_t>(v.attr.pitch);
                const uint64_t rate = static_cast<uint64_t>(modRate);

                step = (rate * 4096ull * 65536ull) / (44100ull * pitch);
            }

            if (step == 0) step = 1;
            if (step > 0xFFFFFFFFull) step = 0xFFFFFFFFull;

            v.ovrPcm = reinterpret_cast<const int16_t*>(modPcm);
            v.ovrCount = static_cast<uint32_t>(modCount);
            v.ovrStep = static_cast<uint32_t>(step);
        }
    }
    if (voiceIndex >= 0 && voiceIndex < kNumVoices)
    {
        m_referenceVoices[voiceIndex].resampler.Reset();
        if (m_renderer == RendererMode::Reference)
        {
            const int16_t preRoll[ReferenceResampler::kHalfTaps]{};
            m_referenceVoices[voiceIndex].resampler.PushSamples(
                preRoll, ReferenceResampler::kHalfTaps);
        }
        m_referenceVoices[voiceIndex].processCount = 0;
        m_referenceVoices[voiceIndex].lastBandlimitBucket = 0;
    }
}

void SPUCore::KeyOffVoice(SPUVoiceState& v)
{
    v.envPhase = EnvPhase::Release;
    v.envCounter = 0;
}

void SPUCore::SetKey(int onOffCmd, uint32_t voiceBitmask)
{
    for (int i = 0; i < kNumVoices; ++i)
    {
        if ((voiceBitmask & SPU_VOICECH(i)) == 0)
            continue;

        SPUVoiceState& v = m_voices[i];
        switch (onOffCmd)
        {
        case KeyCmd_On:
            KeyOnVoice(v, i);
            break;
        case KeyCmd_Off:
            KeyOffVoice(v);
            break;
        case KeyCmd_OffEnvOn:
            // Silence the voice's audible output but leave the envelope
            // generator running (matches libspu.h's SPU_OFF_ENV_ON, see
            // KeyCommand doc). We model "silenced" by forcing envLevel to 0
            // while explicitly NOT touching envPhase/envCounter, so the
            // generator keeps ticking exactly where it left off.
            v.envLevel = 0;
            break;
        case KeyCmd_OnEnvOff:
            // Resume/keep audible output without perturbing the envelope
            // generator's phase/level/counter (SPU_ON_ENV_OFF). If the
            // voice was never keyed on, treat it as an immediate full-level
            // Sustain so audio is actually produced.
            if (!v.everKeyedOn)
                KeyOnVoice(v, i);
            if (v.envPhase == EnvPhase::Off || v.envPhase == EnvPhase::Attack)
            {
                v.envPhase = EnvPhase::Sustain;
                v.envLevel = 0x7FFF;
            }
            break;
        default:
            break;
        }
    }
}

int SPUCore::GetKeyStatus(uint32_t voiceBit) const
{
    for (int i = 0; i < kNumVoices; ++i)
    {
        if (voiceBit != static_cast<uint32_t>(SPU_VOICECH(i)))
            continue;

        const SPUVoiceState& v = m_voices[i];
        switch (v.envPhase)
        {
        case EnvPhase::Attack:
        case EnvPhase::Decay:
            return KeyStatus_On;
        case EnvPhase::Sustain:
            return v.envLevel > 0 ? KeyStatus_On : KeyStatus_OnEnvOff;
        case EnvPhase::Release:
            return v.envLevel > 0 ? KeyStatus_OffEnvOn : KeyStatus_Off;
        case EnvPhase::Off:
        default:
            return KeyStatus_Off;
        }
    }
    return KeyStatus_Off;
}

void SPUCore::GetAllKeysStatus(char* statusOut, int count) const
{
    int n = (count < kNumVoices) ? count : kNumVoices;
    for (int i = 0; i < n; ++i)
        statusOut[i] = static_cast<char>(GetKeyStatus(SPU_VOICECH(i)));
}

uint32_t SPUCore::GetEndxFlags() const
{
    uint32_t mask = 0;
    for (int i = 0; i < kNumVoices; ++i)
    {
        if (m_voices[i].reachedLoopEnd)
            mask |= static_cast<uint32_t>(SPU_VOICECH(i));
    }
    return mask;
}

// ---------------------------------------------------------------------------
// Reverb-send / noise / pitch-mod controls
// ---------------------------------------------------------------------------

void SPUCore::SetVoiceReverbSend(uint32_t voiceBitmask, bool enable)
{
    for (int i = 0; i < kNumVoices; ++i)
        if (voiceBitmask & SPU_VOICECH(i))
            m_voices[i].reverbSend = enable;
}

uint32_t SPUCore::GetVoiceReverbSendMask() const
{
    uint32_t mask = 0;
    for (int i = 0; i < kNumVoices; ++i)
        if (m_voices[i].reverbSend)
            mask |= static_cast<uint32_t>(SPU_VOICECH(i));
    return mask;
}

void SPUCore::SetReverbMasterEnable(bool enable)
{
    m_reverbMasterEnable = enable;
    m_reverb.SetMasterEnable(enable);
    m_idealReverb.SetMasterEnable(enable);
    PsyX_ReferenceReverb::PsyQParameters reference{};
    reference.mode = m_reverbAttr.mode;
    reference.depthLeft = m_reverbAttr.depth.left;
    reference.depthRight = m_reverbAttr.depth.right;
    reference.delay = m_reverbAttr.delay;
    reference.feedback = m_reverbAttr.feedback;
    reference.enabled = enable;
    m_referenceReverb.SetPsyQParameters(reference);
}
bool SPUCore::GetReverbMasterEnable() const { return m_reverbMasterEnable; }

void SPUCore::SetReverbModeParam(const SpuReverbAttr& attr)
{
    if (attr.mask & SPU_REV_MODE)
    {
        int mode = attr.mode & 0xFF;
        if (mode >= 0 && mode < static_cast<int>(PsyX_SPUReverb::PresetId::Count))
        {
            PsyX_SPUReverb::PresetId presetId = static_cast<PsyX_SPUReverb::PresetId>(mode);
            const PsyX_SPUReverb::Preset& preset = PsyX_SPUReverb::GetPreset(presetId);
            m_reverb.LoadPreset(presetId);
            m_reverb.SetBaseAddress(static_cast<uint16_t>((kSpuRamSize - preset.workAreaBytes) / 8u));
            // The reverb writes [kSpuRamSize - workAreaBytes, kSpuRamSize) every
            // tick, so the allocator must stop below it or a sample allocated up
            // there is overwritten with reverb state. PsyQ does this via
            // SpuReserveReverbWorkArea; there is no such entry point here.
            m_malloc.heapEnd = kSpuRamSize - preset.workAreaBytes;
        }
        m_reverbAttr.mode = attr.mode;
    }
    if (attr.mask & SPU_REV_DEPTHL)
        m_reverbAttr.depth.left = attr.depth.left;
    if (attr.mask & SPU_REV_DEPTHR)
        m_reverbAttr.depth.right = attr.depth.right;
    if (attr.mask & (SPU_REV_DEPTHL | SPU_REV_DEPTHR))
        m_reverb.SetOutputVolume(m_reverbAttr.depth.left, m_reverbAttr.depth.right);
    if (attr.mask & SPU_REV_DELAYTIME)
        m_reverbAttr.delay = attr.delay;
    if (attr.mask & SPU_REV_FEEDBACK)
        m_reverbAttr.feedback = attr.feedback;
    m_reverbAttr.mask |= attr.mask;

    PsyX_IdealReverb::PsyQParameters ideal{};
    ideal.mode = m_reverbAttr.mode;
    ideal.depthLeft = m_reverbAttr.depth.left;
    ideal.depthRight = m_reverbAttr.depth.right;
    ideal.enabled = m_reverbMasterEnable;
    m_idealReverb.SetPsyQParameters(ideal);

    PsyX_ReferenceReverb::PsyQParameters reference{};
    reference.mode = m_reverbAttr.mode;
    reference.depthLeft = m_reverbAttr.depth.left;
    reference.depthRight = m_reverbAttr.depth.right;
    reference.delay = m_reverbAttr.delay;
    reference.feedback = m_reverbAttr.feedback;
    reference.enabled = m_reverbMasterEnable;
    m_referenceReverb.SetPsyQParameters(reference);
}
void SPUCore::GetReverbModeParam(SpuReverbAttr& attr) const { attr = m_reverbAttr; }

void SPUCore::ClearReverbWorkArea()
{
    int mode = m_reverbAttr.mode & 0xFF;
    if (mode < 0 || mode >= static_cast<int>(PsyX_SPUReverb::PresetId::Count))
        mode = static_cast<int>(PsyX_SPUReverb::PresetId::Off);
    const PsyX_SPUReverb::Preset& preset =
        PsyX_SPUReverb::GetPreset(static_cast<PsyX_SPUReverb::PresetId>(mode));
    memset(m_ram + kSpuRamSize - preset.workAreaBytes, 0, preset.workAreaBytes);
    m_reverb.ClearDelayState();
    m_idealReverb.ClearDelayState();
    m_referenceReverb.Reset();

    PsyX_ReferenceReverb::PsyQParameters reference{};
    reference.mode = m_reverbAttr.mode;
    reference.depthLeft = m_reverbAttr.depth.left;
    reference.depthRight = m_reverbAttr.depth.right;
    reference.delay = m_reverbAttr.delay;
    reference.feedback = m_reverbAttr.feedback;
    reference.enabled = m_reverbMasterEnable;
    m_referenceReverb.SetPsyQParameters(reference);
}

void SPUCore::SetVoiceNoiseMode(uint32_t voiceBitmask, bool enable)
{
    for (int i = 0; i < kNumVoices; ++i)
        if (voiceBitmask & SPU_VOICECH(i))
            m_voices[i].noiseMode = enable;
}

uint32_t SPUCore::GetVoiceNoiseModeMask() const
{
    uint32_t mask = 0;
    for (int i = 0; i < kNumVoices; ++i)
        if (m_voices[i].noiseMode)
            mask |= static_cast<uint32_t>(SPU_VOICECH(i));
    return mask;
}

void SPUCore::SetNoiseClock(int nClock) { m_noiseClock = nClock; }
int SPUCore::GetNoiseClock() const { return m_noiseClock; }
int16_t SPUCore::GetCurrentNoiseLevel() const { return static_cast<int16_t>(m_noiseLevel); }

void SPUCore::SetVoicePitchModEnable(uint32_t voiceBitmask, bool enable)
{
    for (int i = 0; i < kNumVoices; ++i)
        if (voiceBitmask & SPU_VOICECH(i))
            m_voices[i].pitchModEnable = enable;
}

uint32_t SPUCore::GetVoicePitchModEnableMask() const
{
    uint32_t mask = 0;
    for (int i = 0; i < kNumVoices; ++i)
        if (m_voices[i].pitchModEnable)
            mask |= static_cast<uint32_t>(SPU_VOICECH(i));
    return mask;
}

// ---------------------------------------------------------------------------
// Master / CD controls
// ---------------------------------------------------------------------------

void SPUCore::SetMasterVolume(int16_t left, int16_t right)
{
    m_masterVolL = left;
    m_masterVolR = right;
    // Explicit direct setter: force Direct mode so the value takes effect
    // immediately, same convention as SetVoiceVolume().
    m_masterVolModeL = m_masterVolModeR = SPU_VOICE_DIRECT16;
}
void SPUCore::GetMasterVolume(int16_t* left, int16_t* right) const
{
    if (left) *left = m_masterVolL;
    if (right) *right = m_masterVolR;
}

void SPUCore::GetCurrentMasterVolume(int16_t* left, int16_t* right) const
{
    if (left) *left = SaturateS16(m_curMasterVolL);
    if (right) *right = SaturateS16(m_curMasterVolR);
}

void SPUCore::SetCommonAttr(const SpuCommonAttr& attr)
{
    if (attr.mask & SPU_COMMON_MVOLL) m_masterVolL = attr.mvol.left;
    if (attr.mask & SPU_COMMON_MVOLR) m_masterVolR = attr.mvol.right;
    if (attr.mask & SPU_COMMON_MVOLMODEL) m_masterVolModeL = attr.mvolmode.left;
    if (attr.mask & SPU_COMMON_MVOLMODER) m_masterVolModeR = attr.mvolmode.right;
    if (attr.mask & SPU_COMMON_CDVOLL) m_cdVolL = attr.cd.volume.left;
    if (attr.mask & SPU_COMMON_CDVOLR) m_cdVolR = attr.cd.volume.right;
    if (attr.mask & SPU_COMMON_CDREV) m_cdReverb = attr.cd.reverb != 0;
    if (attr.mask & SPU_COMMON_CDMIX) m_cdEnable = attr.cd.mix != 0;
}

void SPUCore::GetCommonAttr(SpuCommonAttr& attr) const
{
    memset(&attr, 0, sizeof(attr));
    attr.mvol.left = m_masterVolL;
    attr.mvol.right = m_masterVolR;
    attr.mvolmode.left = static_cast<short>(m_masterVolModeL);
    attr.mvolmode.right = static_cast<short>(m_masterVolModeR);
    attr.mvolx.left = SaturateS16(m_curMasterVolL);
    attr.mvolx.right = SaturateS16(m_curMasterVolR);
    attr.cd.volume.left = m_cdVolL;
    attr.cd.volume.right = m_cdVolR;
    attr.cd.reverb = m_cdReverb ? 1 : 0;
    attr.cd.mix = m_cdEnable ? 1 : 0;
}

int SPUCore::SetMute(int onOff)
{
    int prev = m_muted;
    m_muted = onOff;
    return prev;
}

int SPUCore::GetMute() const { return m_muted; }

void SPUCore::SetCdAudioEnable(bool enable) { m_cdEnable = enable; }
bool SPUCore::GetCdAudioEnable() const { return m_cdEnable; }
void SPUCore::SetCdAudioReverb(bool enable) { m_cdReverb = enable; }
bool SPUCore::GetCdAudioReverb() const { return m_cdReverb; }
void SPUCore::SetCdVolume(int16_t left, int16_t right) { m_cdVolL = left; m_cdVolR = right; }
void SPUCore::GetCdVolume(int16_t* left, int16_t* right) const
{
    if (left) *left = m_cdVolL;
    if (right) *right = m_cdVolR;
}

void SPUCore::SetXaMasterGain(double gain)
{
    if (!std::isfinite(gain))
        gain = 1.0;
    gain = std::max(0.0, std::min(1.0, gain));
    m_xaMasterGain = gain;
    m_xaMasterGainQ16 = static_cast<uint32_t>(gain * 65536.0 + 0.5);
}

void SPUCore::PushCdStereoFrame(int16_t left, int16_t right)
{
    CdFrame f; f.l = left; f.r = right;
    m_cdQueue.push_back(f);
}

void SPUCore::PushCdStereoFrameDouble(double left, double right)
{
    CdFrameDouble f{left, right};
    m_cdQueueDouble.push_back(f);
}

void SPUCore::ClearCdQueue()
{
    m_cdQueue.clear();
    m_cdQueueDouble.clear();
}

// ---------------------------------------------------------------------------
// SPU-ADPCM decode (block-on-demand)
// ---------------------------------------------------------------------------

void SPUCore::DecodeAdpcmBlock(SPUVoiceState& v, int voiceIndex)
{
    if (v.ovrPcm != nullptr)
    {
        // Replaced sample: fill the block from PC-owned PCM rather than decoding
        // the ADPCM at curAddr. Nothing downstream changes.
        bool ended = false;

        for (int i = 0; i < kAdpcmBlockSamples; ++i)
        {
            const uint32_t idx = static_cast<uint32_t>(v.ovrPos >> 16);

            if (idx >= v.ovrCount)
            {
                v.blockSamples[i] = 0;
                ended = true;
                continue;
            }
            v.blockSamples[i] = v.ovrPcm[idx];
            v.ovrPos += v.ovrStep;
        }

        v.blockSamplePos = 0;
        v.blockValid = true;

        // Running out ends the voice the way a final block carrying End+Mute
        // does, so it releases instead of falling back into the original ADPCM
        // at the repeat address. Clearing the pointer means the block decoded
        // after the release is the real (silent, enveloped-off) data again.
        v.blockLoopEnd = ended;
        v.forceReleaseOnBlockEnd = ended;
        if (ended)
        {
            v.reachedLoopEnd = true;
            v.ovrPcm = nullptr;
        }
        return;
    }

    if (v.curAddr + kAdpcmBlockBytes > kSpuRamSize)
    {
        // Out-of-range: treat as silence rather than reading (and corrupting
        // history from) memory outside the modeled 512KiB SPU RAM.
        memset(v.blockSamples, 0, sizeof(v.blockSamples));
        v.blockSamplePos = 0;
        v.blockValid = true;
        return;
    }

    const uint8_t* block = m_ram + v.curAddr;
    uint8_t shiftFilter = block[0];
    uint8_t flags = block[1];

    int shift = shiftFilter & 0xF;
    int filter = (shiftFilter >> 4) & 0xF;
    if (shift >= 13) shift = 9;      // documented hardware quirk: shift 13..15 behave as 9
    if (filter > 4) filter = 4;      // defensive clamp; filter>4 is undocumented/unused by real data

    int32_t f0 = s_adpcmFilterK0[filter];
    int32_t f1 = s_adpcmFilterK1[filter];

    bool loopEnd = (flags & 0x1) != 0;
    bool loopRepeat = (flags & 0x2) != 0;
    bool loopStart = (flags & 0x4) != 0;

    if (loopStart)
        v.repeatAddr = v.curAddr; // hardware auto-latches current block addr as the repeat address

    int32_t hist1 = v.adpcmHist1;
    int32_t hist2 = v.adpcmHist2;

    for (int i = 0; i < kAdpcmBlockSamples; ++i)
    {
        uint8_t byte = block[2 + (i >> 1)];
        int nibble = (i & 1) ? (byte >> 4) : (byte & 0xF);
        int32_t t = static_cast<int16_t>(nibble << 12); // sign-extend the 4bit nibble into bit15
        int32_t sample = t >> shift;
        sample += ((hist1 * f0 + hist2 * f1) >> 6);
        sample = SaturateS16(sample);

        v.blockSamples[i] = static_cast<int16_t>(sample);
        hist2 = hist1;
        hist1 = sample;
    }

    v.adpcmHist1 = hist1;
    v.adpcmHist2 = hist2;
    v.blockSamplePos = 0;
    v.blockValid = true;

    // Loop end handling happens AFTER the block finishes playing (i.e. once
    // ShiftInNextRawSample has consumed all 28 samples of THIS block); we
    // stash the pending action here and apply it when the block is exhausted.
    v.blockLoopEnd = loopEnd;
    if (loopEnd)
    {
        v.reachedLoopEnd = true;
        if (!loopRepeat)
            v.forceReleaseOnBlockEnd = true; // Code 1: End+Mute
        // Code 3 (End+Repeat): reachedLoopEnd set, no forced release.
        // The actual address jump to repeatAddr is applied in
        // ShiftInNextRawSample() once this block is fully consumed.
    }

    v.curAddr += kAdpcmBlockBytes;
    if (m_renderer == RendererMode::Reference && voiceIndex >= 0 && voiceIndex < kNumVoices)
        m_referenceVoices[voiceIndex].resampler.PushSamples(v.blockSamples, kAdpcmBlockSamples);
}

void SPUCore::ShiftInNextRawSample(SPUVoiceState& v, int voiceIndex)
{
    if (!v.blockValid || v.blockSamplePos >= kAdpcmBlockSamples)
    {
        // Was the block we just finished flagged Loop End? Apply the
        // pending jump/mute now, before decoding the next block.
        if (v.blockValid && v.blockLoopEnd)
        {
            bool wasForceRelease = v.forceReleaseOnBlockEnd;
            v.blockLoopEnd = false;
            v.forceReleaseOnBlockEnd = false;
            v.curAddr = v.repeatAddr;
            if (wasForceRelease)
            {
                v.envPhase = EnvPhase::Release;
                v.envLevel = 0;
                v.envCounter = 0;
            }
        }
        DecodeAdpcmBlock(v, voiceIndex);
    }

    int16_t newSample = v.blockSamples[v.blockSamplePos++];
    v.interpTaps[3] = v.interpTaps[2];
    v.interpTaps[2] = v.interpTaps[1];
    v.interpTaps[1] = v.interpTaps[0];
    v.interpTaps[0] = newSample;
}

// ---------------------------------------------------------------------------
// 4-point Gaussian interpolation (exact documented formula)
// ---------------------------------------------------------------------------

static inline int32_t GaussInterpolate(const int16_t taps[4], int i)
{
    // taps: [0]=new, [1]=old, [2]=older, [3]=oldest ; i in [0,255]
    const int16_t* g = s_gaussTable;
    int32_t out = (static_cast<int32_t>(g[0x0FF - i]) * taps[3]) >> 15;
    out += (static_cast<int32_t>(g[0x1FF - i]) * taps[2]) >> 15;
    out += (static_cast<int32_t>(g[0x100 + i]) * taps[1]) >> 15;
    out += (static_cast<int32_t>(g[0x000 + i]) * taps[0]) >> 15;
    return out;
}

// ---------------------------------------------------------------------------
// Pitch counter + per-tick voice stepping
// ---------------------------------------------------------------------------

int32_t SPUCore::StepVoiceOneSample(SPUVoiceState& v, int voiceIndex, int32_t prevVoiceOutX)
{
    uint32_t step = ResolveVoiceStep(v, voiceIndex, prevVoiceOutX);

    v.pitchCounter += step;
    while (v.pitchCounter >= kPitchUnity)
    {
        ShiftInNextRawSample(v, voiceIndex);
        v.pitchCounter -= kPitchUnity;
    }

    int interpIndex = static_cast<int>((v.pitchCounter >> 4) & 0xFF);
    return GaussInterpolate(v.interpTaps, interpIndex);
}

uint32_t SPUCore::ResolveVoiceStep(const SPUVoiceState& v, int voiceIndex, int32_t prevVoiceOutX) const
{
    uint32_t step = v.attr.pitch;
    if (v.pitchModEnable && voiceIndex > 0)
    {
        const int32_t factor = prevVoiceOutX + 0x8000;
        const int32_t signedStep = static_cast<int16_t>(step);
        const int64_t product = static_cast<int64_t>(signedStep) * static_cast<int64_t>(factor);
        step = static_cast<uint32_t>(static_cast<int32_t>(product >> 15)) & 0xFFFFu;
    }
    return step > kPitchStepClip ? kPitchClippedStep : step;
}

double SPUCore::GaussInterpolateDouble(const int16_t taps[4], uint32_t pitchCounter) const
{
    return EvaluateIdealGaussian(taps, pitchCounter);
}

// ---------------------------------------------------------------------------
// Shared envelope stepper: the exact documented Shift/Step/Mode/Direction
// pseudocode from psx-spx's "Envelope Operation" section. This single
// implementation backs BOTH the ADSR generator (StepAdsr(), always called
// with phaseNegative=false - plain ADSR has no such concept) and the
// Left/Right/Master volume Sweep envelopes (StepVolumeSweep()), since the
// hardware documents them as sharing one formula. Returns true if the
// level/counter actually stepped this tick (false => no-op tick, matching
// "no step this cycle" in the pseudocode).
// ---------------------------------------------------------------------------
static bool StepGenericEnvelope(int32_t& level, uint32_t& counter,
                                 int shift, int stepSel, bool hasStepField,
                                 bool exponential, bool decreasing, bool phaseNegative)
{
    int32_t adsrStep = hasStepField ? (7 - stepSel) : 7; // Decay/Release/no-step-field: fixed "-8" encoding
    if (decreasing != phaseNegative) // Decreasing XOR PhaseNegative
        adsrStep = ~adsrStep;        // +7..+4 => -8..-5

    int shiftForStep = (11 - shift);
    if (shiftForStep < 0) shiftForStep = 0;
    adsrStep <<= shiftForStep;

    int shiftForCounter = (shift - 11);
    if (shiftForCounter < 0) shiftForCounter = 0;
    uint32_t counterIncrement = 0x8000u >> shiftForCounter;

    if (exponential && !decreasing && level > 0x6000)
    {
        if (shift < 10)
            adsrStep /= 4;
        else if (shift >= 11)
            counterIncrement /= 4;
        else
        {
            adsrStep /= 2;
            counterIncrement /= 2;
        }
    }
    else if (exponential && decreasing)
    {
        const int32_t scaled =
            static_cast<int32_t>((static_cast<int64_t>(adsrStep) * level) / 0x8000);

        /* Integer division truncates toward zero, so this scaling reaches ZERO
         * while the level is still audible: with the fixed -8 step Decay and
         * Release use, any level below 0x8000/8 = 4096 gives -0.99.. -> 0, and
         * the envelope then never moves again. A key-off in that band is a
         * voice that plays FOREVER, which on a looping sample means forever
         * out loud -- the radio still going on the main menu long after the
         * demo ended, logged at exactly env=4095, one below the threshold.
         *
         * Keep the step's sign whenever it has any distance left to travel, so
         * an exponential decrease always converges. It costs at most 4095 extra
         * steps (~0.7 s of tail at the release rate that reported this). */
        adsrStep = (scaled == 0 && adsrStep != 0 && level > 0)
                 ? ((adsrStep < 0) ? -1 : 1)
                 : scaled;
    }

    // "Using a step value of all-ones causes the volume to never step, and
    // additionally never saturate" - all-ones means every bit of the
    // (step,shift) register field for this phase is 1.
    uint32_t allOnesMask = hasStepField ? 0x7Fu : 0x1Fu;
    uint32_t fieldBits = hasStepField ? (static_cast<uint32_t>(stepSel) | (static_cast<uint32_t>(shift) << 2))
                                       : static_cast<uint32_t>(shift);
    if (fieldBits != allOnesMask)
    {
        if (counterIncrement < 1) counterIncrement = 1;
    }

    counter += counterIncrement;
    if ((counter & 0x8000u) == 0)
        return false; // no step this 44.1kHz tick

    counter &= 0x7FFFu;
    level += adsrStep;

    // Saturate depending on mode (exact documented rules).
    if (!decreasing)
    {
        if (level > 0x7FFF) level = 0x7FFF;
        if (level < -0x8000) level = -0x8000;
    }
    else if (phaseNegative)
    {
        if (level < -0x8000) level = -0x8000;
        if (level > 0) level = 0;
    }
    else
    {
        if (level < 0) level = 0;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ADSR envelope generator (exact documented shift/step/counter rules)
// ---------------------------------------------------------------------------

void SPUCore::StepAdsr(SPUVoiceState& v)
{
    if (v.envPhase == EnvPhase::Off)
        return;

    int shift = 0;
    int stepSel = 0;      // 0..3, only meaningful for Attack/Sustain
    bool hasStepField = false;
    bool exponential = false;
    bool decreasing = false;

    switch (v.envPhase)
    {
    case EnvPhase::Attack:
        shift = (v.attr.ar >> 2) & 0x1F;
        stepSel = v.attr.ar & 0x3;
        hasStepField = true;
        exponential = (v.attr.a_mode != 0);
        decreasing = false;
        break;
    case EnvPhase::Decay:
        shift = v.attr.dr & 0xF;
        stepSel = 0;
        hasStepField = false;
        exponential = true; // Decay Mode is fixed always-exponential
        decreasing = true;
        break;
    case EnvPhase::Sustain:
        shift = (v.attr.sr >> 2) & 0x1F;
        stepSel = v.attr.sr & 0x3;
        hasStepField = true;
        exponential = (v.attr.s_mode & 1) != 0;
        decreasing = (v.attr.s_mode & 2) != 0; // see s_mode packing note above UnpackAdsr
        break;
    case EnvPhase::Release:
        shift = v.attr.rr & 0x1F;
        stepSel = 0;
        hasStepField = false;
        exponential = (v.attr.r_mode != 0);
        decreasing = true;
        break;
    default:
        return;
    }

    // Plain ADSR never runs in PhaseNegative mode (that concept only exists
    // for the Sweep volume envelope, see StepVolumeSweep()).
    if (!StepGenericEnvelope(v.envLevel, v.envCounter, shift, stepSel, hasStepField,
                              exponential, decreasing, /*phaseNegative=*/false))
        return; // no step this tick

    // Automatic phase transitions.
    switch (v.envPhase)
    {
    case EnvPhase::Attack:
        if (v.envLevel >= 0x7FFF)
        {
            v.envLevel = 0x7FFF;
            v.envPhase = EnvPhase::Decay;
            v.envCounter = 0;
        }
        break;
    case EnvPhase::Decay:
    {
        int32_t sustainLevel = (static_cast<int32_t>(v.attr.sl) + 1) * 0x800;
        if (v.envLevel <= sustainLevel)
        {
            v.envLevel = sustainLevel;
            v.envPhase = EnvPhase::Sustain;
            v.envCounter = 0;
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Left/Right/Master volume Sweep envelope (exact documented shift/step rules,
// shared with ADSR via StepGenericEnvelope() - see psx-spx "Sweep Volume
// Mode"). Direct (fixed) mode is NOT handled here: it is live-mirrored
// inline by the caller every tick (see RenderFrames()), since Direct mode
// has no envelope state of its own to step.
// ---------------------------------------------------------------------------

void SPUCore::StepVolumeSweep(int32_t& curVol, uint32_t& counter, int volMode, int rawRate)
{
    if (volMode == SPU_VOICE_DIRECT16)
        return;

    // SDK-level Sweep mode enum -> hardware Mode/Direction/Phase bits. See
    // libspu.h's SPU_VOICE_LINEARIncN..EXPDec block: N=Normal(Phase=0
    // Positive), R=Reverse(Phase=1 Negative); EXPDec has no N/R distinction
    // because "Phase bit has no effect in Exponential Decrease mode" (psx-spx).
    bool exponential   = (volMode == SPU_VOICE_EXPIncN || volMode == SPU_VOICE_EXPIncR || volMode == SPU_VOICE_EXPDec);
    bool decreasing    = (volMode == SPU_VOICE_LINEARDecN || volMode == SPU_VOICE_LINEARDecR || volMode == SPU_VOICE_EXPDec);
    bool phaseNegative = (volMode == SPU_VOICE_LINEARIncR || volMode == SPU_VOICE_LINEARDecR || volMode == SPU_VOICE_EXPIncR);

    // rawRate packs the documented 7bit (Shift<<2|Step) Sweep rate field,
    // the same convention this core already uses for ar/sr (see PackAdsr's
    // header note) - a documented assumption since SpuVoiceAttr has no
    // dedicated Sweep-rate field of its own.
    int shift = (rawRate >> 2) & 0x1F;
    int stepSel = rawRate & 0x3;

    StepGenericEnvelope(curVol, counter, shift, stepSel, /*hasStepField=*/true,
                         exponential, decreasing, phaseNegative);
}

// ---------------------------------------------------------------------------
// Shared noise generator (one for the whole SPU), as documented in psx-spx
// "SPU Noise Generator": a timer counts down by Step+4 every 44.1 kHz tick;
// each time it goes negative the 16-bit level shifts left, taking in the
// inverted parity of bits 15, 12, 11 and 10, and the timer is reloaded with
// 20000h >> Shift (at most twice).
// ---------------------------------------------------------------------------

void SPUCore::StepSharedNoise()
{
    const uint32_t clock = static_cast<uint32_t>(m_noiseClock) & 0x3Fu;
    const int32_t shift = static_cast<int32_t>(clock >> 2);
    const int32_t step = static_cast<int32_t>(clock & 3u) + 4;

    m_noiseTimer -= step;
    if (m_noiseTimer >= 0)
        return;
    const uint32_t level = m_noiseLevel;
    const uint32_t parity = ((level >> 15) ^ (level >> 12) ^ (level >> 11) ^ (level >> 10) ^ 1u) & 1u;
    m_noiseLevel = ((level << 1) | parity) & 0xFFFFu;
    m_noiseTimer += 0x20000 >> shift;
    if (m_noiseTimer < 0)
        m_noiseTimer += 0x20000 >> shift;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void SPUCore::RenderFrames(int16_t* outInterleavedLR, int frameCount, SplitOutput* split)
{
    for (int f = 0; f < frameCount; ++f)
    {
        // One shared noise-generator tick per output frame, regardless of
        // how many (if any) voices currently have NON set - see
        // StepSharedNoise()/IsNoiseGeneratorImplemented().
        StepSharedNoise();

        int64_t mixL = 0;
        int64_t mixR = 0;
        int64_t reverbInL = 0;
        int64_t reverbInR = 0;
        int32_t prevVoiceOutX = 0; // VxOUTX(x-1) for PMON; voice 0 has no predecessor

        for (int vi = 0; vi < kNumVoices; ++vi)
        {
            SPUVoiceState& v = m_voices[vi];

            // Hardware always keeps decoding/advancing every voice
            // regardless of key state (ENDX/IRQ can still fire on a keyed-
            // off voice) - see psx-spx: "even if the ADSR pattern has
            // finished the Release period". We mirror that: the pitch
            // counter and ADPCM decoder always run - even for Noise-mode
            // voices, whose ADPCM/loop/ENDX bookkeeping is identical to a
            // normal voice - only the ADSR envelope generator is gated by
            // envPhase, which naturally zeroes the voice's audible
            // contribution once Off/level==0.
            int32_t interpolated = StepVoiceOneSample(v, vi, prevVoiceOutX);
            StepAdsr(v);

            // Noise mode (NON): substitute the shared noise generator's
            // current level for this tick's ADPCM/Gaussian sample. The
            // ADPCM decode above still ran unconditionally (see comment).
            int32_t rawSample = v.noiseMode ? static_cast<int16_t>(m_noiseLevel) : interpolated;

            int32_t envScaled = (rawSample * v.envLevel) >> 15;

            // VxOUTX: saturated post-ADSR, pre-L/R-volume sample, consumed
            // by the NEXT voice's PMON pitch-modulation factor (see
            // StepVoiceOneSample()) and exposed via GetVoiceLastOutputX().
            int32_t outX = SaturateS16(envScaled);
            v.lastOutX = outX;
            prevVoiceOutX = outX;

            // Left/Right volume: Direct mode is a live mirror of the fixed
            // register (matches hardware always reading the current
            // register value); Sweep mode evolves the envelope by one tick
            // via StepVolumeSweep() (see SPUVoiceState::curVolL/R doc).
            // Fixed (direct) volume register stores volume/2 in
            // -0x4000..0x3FFF, actual multiplier is regVal*2 in
            // -0x8000..0x7FFE (see psx-spx "Voice Volume Left/Right").
            if (v.attr.volmode.left == SPU_VOICE_DIRECT16)
                v.curVolL = static_cast<int32_t>(v.attr.volume.left) * 2;
            else
                StepVolumeSweep(v.curVolL, v.sweepCounterL, v.attr.volmode.left, v.attr.volume.left);

            if (v.attr.volmode.right == SPU_VOICE_DIRECT16)
                v.curVolR = static_cast<int32_t>(v.attr.volume.right) * 2;
            else
                StepVolumeSweep(v.curVolR, v.sweepCounterR, v.attr.volmode.right, v.attr.volume.right);

            v.attr.volumex.left  = SaturateS16(v.curVolL);
            v.attr.volumex.right = SaturateS16(v.curVolR);
            v.attr.envx = static_cast<int16_t>(v.envLevel);

            /* Pre-pan mono tap for the spatialiser: the exact sample the mix
             * below is about to pan, so the two can never disagree. */
            if (split && split->voiceMono[vi])
                split->voiceMono[vi][f] = SaturateS16(envScaled);

            int32_t voiceL = static_cast<int32_t>((static_cast<int64_t>(envScaled) * v.curVolL) >> 15);
            int32_t voiceR = static_cast<int32_t>((static_cast<int64_t>(envScaled) * v.curVolR) >> 15);
            mixL += voiceL;
            mixR += voiceR;
            if (v.reverbSend)
            {
                reverbInL += voiceL;
                reverbInR += voiceR;
            }
        }

        if (m_muted)
        {
            mixL = 0;
            mixR = 0;
            reverbInL = 0;
            reverbInR = 0;
        }

        // CD Audio input (post CD volume), matching the mixer path described
        // for 1F801DB0h CD Audio Input Volume. Missing input is silence.
        if (m_cdEnable)
        {
            CdFrame cd = { 0, 0 };
            if (!m_cdQueue.empty())
            {
                cd = m_cdQueue.front();
                m_cdQueue.pop_front();
                m_lastCdFrame = cd;
            }
            const int32_t xaL = static_cast<int32_t>(
                (static_cast<int64_t>(cd.l) * m_xaMasterGainQ16) >> 16);
            const int32_t xaR = static_cast<int32_t>(
                (static_cast<int64_t>(cd.r) * m_xaMasterGainQ16) >> 16);
            int32_t cdL = static_cast<int32_t>((static_cast<int64_t>(xaL) * m_cdVolL) >> 15);
            int32_t cdR = static_cast<int32_t>((static_cast<int64_t>(xaR) * m_cdVolR) >> 15);
            if (split && split->cdLR)
            {
                split->cdLR[f * 2 + 0] = SaturateS16(cdL);
                split->cdLR[f * 2 + 1] = SaturateS16(cdR);
            }
            mixL += cdL;
            mixR += cdR;
            if (m_cdReverb)
            {
                reverbInL += cdL;
                reverbInR += cdR;
            }
        }

        else if (split && split->cdLR)
        {
            split->cdLR[f * 2 + 0] = 0;
            split->cdLR[f * 2 + 1] = 0;
        }

        int32_t reverbOutL = 0;
        int32_t reverbOutR = 0;
        m_reverb.Process(m_ram, kSpuRamSize,
                         SaturateS16(static_cast<int32_t>(reverbInL)),
                         SaturateS16(static_cast<int32_t>(reverbInR)),
                         &reverbOutL, &reverbOutR);
        if (split && split->wetLR)
        {
            split->wetLR[f * 2 + 0] = SaturateS16(reverbOutL);
            split->wetLR[f * 2 + 1] = SaturateS16(reverbOutR);
        }
        mixL += reverbOutL;
        mixR += reverbOutR;

        // Hardware clamps the accumulated mix before applying main volume.
        int32_t clampedL = SaturateS16(static_cast<int32_t>(mixL));
        int32_t clampedR = SaturateS16(static_cast<int32_t>(mixR));

        // Master volume: same Direct-mirror-or-Sweep handling as per-voice
        // volume above (see SetMasterVolume/SetCommonAttr doc comments).
        if (m_masterVolModeL == SPU_VOICE_DIRECT16)
            m_curMasterVolL = static_cast<int32_t>(m_masterVolL) * 2;
        else
            StepVolumeSweep(m_curMasterVolL, m_masterSweepCounterL, m_masterVolModeL, m_masterVolL);

        if (m_masterVolModeR == SPU_VOICE_DIRECT16)
            m_curMasterVolR = static_cast<int32_t>(m_masterVolR) * 2;
        else
            StepVolumeSweep(m_curMasterVolR, m_masterSweepCounterR, m_masterVolModeR, m_masterVolR);

        mixL = (static_cast<int64_t>(clampedL) * m_curMasterVolL) >> 15;
        mixR = (static_cast<int64_t>(clampedR) * m_curMasterVolR) >> 15;

        if (outInterleavedLR)
        {
            outInterleavedLR[f * 2 + 0] = SaturateS16(static_cast<int32_t>(mixL));
            outInterleavedLR[f * 2 + 1] = SaturateS16(static_cast<int32_t>(mixR));
        }
    }

    if (split)
    {
        /* End-of-block pan, for the caller's azimuth. Sampling once per block
         * rather than per frame is deliberate: OpenAL positions a source once
         * per queued buffer anyway. */
        for (int vi = 0; vi < kNumVoices; ++vi)
        {
            split->panL[vi] = m_voices[vi].curVolL;
            split->panR[vi] = m_voices[vi].curVolR;
        }
        split->masterL = m_curMasterVolL;
        split->masterR = m_curMasterVolR;
    }
}

bool SPUCore::SetRenderer(RendererMode renderer, ClipMode clipMode)
{
    if (renderer < RendererMode::Exact || renderer > RendererMode::Reference ||
        clipMode < ClipMode::None || clipMode > ClipMode::Soft)
        return false;

    m_renderer = renderer;
    m_clipMode = clipMode;
    m_nativePhase = 0;
    m_idealReverb.SetInternalSampleRate(PsyX_IdealReverb::kRate176400);
    if (renderer == RendererMode::Reference)
        ReferenceResampler::WarmUpAllBanks();
    for (int i = 0; i < kNumVoices; ++i)
    {
        m_referenceVoices[i].resampler.Reset();
        m_referenceVoices[i].processCount = 0;
        m_referenceVoices[i].lastBandlimitBucket = 0;
    }
    m_referenceSincProcessCount = 0;
    return true;
}

uint32_t SPUCore::GetNativeSampleRate() const
{
    switch (m_renderer)
    {
    case RendererMode::Ideal:
        return 176400u;
    case RendererMode::Reference:
        return 352800u;
    case RendererMode::Exact:
    default:
        return 44100u;
    }
}

int SPUCore::GetReferenceLastBandlimitBucket(int voiceIndex) const
{
    return voiceIndex >= 0 && voiceIndex < kNumVoices
        ? m_referenceVoices[voiceIndex].lastBandlimitBucket : -1;
}

double SPUCore::GetEnhancedReverbEnergy() const
{
    return m_renderer == RendererMode::Reference
        ? m_referenceReverb.InternalEnergy() : m_idealReverb.InternalEnergy();
}

double SPUCore::ApplyClip(double value, ClipMode mode)
{
    if (mode == ClipMode::Psx)
        return std::clamp(value, -32768.0, 32767.0);
    if (mode == ClipMode::Soft)
        return 32767.0 * std::tanh(value / 32767.0);
    return value;
}

void SPUCore::BeginHighRateLogicalFrame()
{
    StepSharedNoise();
    int32_t prevVoiceOutX = 0;

    for (int vi = 0; vi < kNumVoices; ++vi)
    {
        SPUVoiceState& v = m_voices[vi];
        const uint32_t step = ResolveVoiceStep(v, vi, prevVoiceOutX);
        m_highRateVoiceSteps[vi] = static_cast<int32_t>(step);

        int32_t boundaryRaw = 0;
        if (m_renderer == RendererMode::Reference)
        {
            boundaryRaw = StepVoiceOneSample(v, vi, prevVoiceOutX);
        }
        else
        {
            SPUVoiceState preview = v;
            boundaryRaw = StepVoiceOneSample(preview, vi, prevVoiceOutX);
        }

        StepAdsr(v);
        const int32_t stateRaw = v.noiseMode ? static_cast<int16_t>(m_noiseLevel) : boundaryRaw;
        const int32_t stateEnvScaled = (stateRaw * v.envLevel) >> 15;
        v.lastOutX = SaturateS16(stateEnvScaled);
        prevVoiceOutX = v.lastOutX;

        if (v.attr.volmode.left == SPU_VOICE_DIRECT16)
            v.curVolL = static_cast<int32_t>(v.attr.volume.left) * 2;
        else
            StepVolumeSweep(v.curVolL, v.sweepCounterL, v.attr.volmode.left, v.attr.volume.left);
        if (v.attr.volmode.right == SPU_VOICE_DIRECT16)
            v.curVolR = static_cast<int32_t>(v.attr.volume.right) * 2;
        else
            StepVolumeSweep(v.curVolR, v.sweepCounterR, v.attr.volmode.right, v.attr.volume.right);
        v.attr.volumex.left = SaturateS16(v.curVolL);
        v.attr.volumex.right = SaturateS16(v.curVolR);
        v.attr.envx = static_cast<int16_t>(v.envLevel);
    }

    if (m_masterVolModeL == SPU_VOICE_DIRECT16)
        m_curMasterVolL = static_cast<int32_t>(m_masterVolL) * 2;
    else
        StepVolumeSweep(m_curMasterVolL, m_masterSweepCounterL, m_masterVolModeL, m_masterVolL);
    if (m_masterVolModeR == SPU_VOICE_DIRECT16)
        m_curMasterVolR = static_cast<int32_t>(m_masterVolR) * 2;
    else
        StepVolumeSweep(m_curMasterVolR, m_masterSweepCounterR, m_masterVolModeR, m_masterVolR);

    m_highRateCdL = m_highRateCdR = 0.0;
    if (m_renderer == RendererMode::Ideal && m_cdEnable && !m_cdQueue.empty())
    {
        const CdFrame cd = m_cdQueue.front();
        m_cdQueue.pop_front();
        m_lastCdFrame = cd;
        m_highRateCdL = cd.l;
        m_highRateCdR = cd.r;
    }
}

void SPUCore::RenderHighRateSubframe(double* left, double* right)
{
    const uint32_t oversample = m_renderer == RendererMode::Reference ? 8u : 4u;
    double mixL = 0.0;
    double mixR = 0.0;
    double reverbInL = 0.0;
    double reverbInR = 0.0;

    for (int vi = 0; vi < kNumVoices; ++vi)
    {
        SPUVoiceState& v = m_voices[vi];
        double raw = 0.0;
        if (m_renderer == RendererMode::Reference)
        {
            ReferenceVoiceState& reference = m_referenceVoices[vi];
            const uint64_t step = ReferenceResampler::PitchToStep(
                static_cast<uint32_t>(m_highRateVoiceSteps[vi]));
            reference.lastBandlimitBucket = ReferenceResampler::BucketForStep(step);
            if (v.envLevel != 0)
            {
                raw = reference.resampler.ProcessDouble(step);
                ++reference.processCount;
                ++m_referenceSincProcessCount;
            }
            else
            {
                reference.resampler.Advance(step);
            }
        }
        else
        {
            const uint32_t step = static_cast<uint32_t>(m_highRateVoiceSteps[vi]);
            const uint32_t previous = (step * m_nativePhase) / oversample;
            const uint32_t current = (step * (m_nativePhase + 1u)) / oversample;
            v.pitchCounter += current - previous;
            while (v.pitchCounter >= kPitchUnity)
            {
                ShiftInNextRawSample(v, vi);
                v.pitchCounter -= kPitchUnity;
            }
            raw = GaussInterpolateDouble(v.interpTaps, v.pitchCounter);
        }

        if (v.noiseMode)
            raw = static_cast<int16_t>(m_noiseLevel);

        const double envScaled = raw * (static_cast<double>(v.envLevel) / 32768.0);
        const double voiceL = envScaled * (static_cast<double>(v.curVolL) / 32768.0);
        const double voiceR = envScaled * (static_cast<double>(v.curVolR) / 32768.0);
        mixL += voiceL;
        mixR += voiceR;
        if (v.reverbSend)
        {
            reverbInL += voiceL;
            reverbInR += voiceR;
        }
    }

    if (m_muted)
    {
        mixL = mixR = 0.0;
        reverbInL = reverbInR = 0.0;
    }

    if (m_cdEnable)
    {
        double cdL = m_highRateCdL;
        double cdR = m_highRateCdR;
        if (m_renderer == RendererMode::Reference)
        {
            if (!m_cdQueueDouble.empty())
            {
                const CdFrameDouble cd = m_cdQueueDouble.front();
                m_cdQueueDouble.pop_front();
                cdL = cd.l;
                cdR = cd.r;
            }
            else
            {
                cdL = cdR = 0.0;
            }
        }
        cdL *= m_xaMasterGain * static_cast<double>(m_cdVolL) / 32768.0;
        cdR *= m_xaMasterGain * static_cast<double>(m_cdVolR) / 32768.0;
        mixL += cdL;
        mixR += cdR;
        if (m_cdReverb)
        {
            reverbInL += cdL;
            reverbInR += cdR;
        }
    }

    double wetL = 0.0;
    double wetR = 0.0;
    if (m_renderer == RendererMode::Reference)
        m_referenceReverb.Process(reverbInL, reverbInR, &wetL, &wetR);
    else
        m_idealReverb.Process(reverbInL, reverbInR, &wetL, &wetR);
    mixL += wetL;
    mixR += wetR;

    if (m_clipMode == ClipMode::Psx)
    {
        mixL = ApplyClip(mixL, m_clipMode);
        mixR = ApplyClip(mixR, m_clipMode);
    }
    mixL *= static_cast<double>(m_curMasterVolL) / 32768.0;
    mixR *= static_cast<double>(m_curMasterVolR) / 32768.0;
    *left = ApplyClip(mixL, m_clipMode);
    *right = ApplyClip(mixR, m_clipMode);
}

void SPUCore::RenderFramesDouble(double* outInterleavedLR, int frameCount)
{
    if (!outInterleavedLR || frameCount <= 0)
        return;
    if (m_renderer == RendererMode::Exact)
    {
        std::vector<int16_t> exact(static_cast<size_t>(frameCount) * 2u);
        RenderFrames(exact.data(), frameCount);
        for (int i = 0; i < frameCount * 2; ++i)
            outInterleavedLR[i] = exact[static_cast<size_t>(i)];
        return;
    }

    const uint32_t oversample = m_renderer == RendererMode::Reference ? 8u : 4u;
    for (int f = 0; f < frameCount; ++f)
    {
        if (m_nativePhase == 0)
            BeginHighRateLogicalFrame();
        RenderHighRateSubframe(&outInterleavedLR[f * 2], &outInterleavedLR[f * 2 + 1]);
        m_nativePhase = (m_nativePhase + 1u) % oversample;
    }
}

// ---- Save states (rood-boi) --------------------------------------------------
namespace
{
template <typename T> void put(std::vector<uint8_t>& out, const T& value)
{
    static_assert(std::is_trivially_copyable<T>::value, "save states copy plain data");
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}
struct Reader
{
    const uint8_t* p;
    size_t left;
    template <typename T> bool get(T& value)
    {
        static_assert(std::is_trivially_copyable<T>::value, "save states copy plain data");
        if (left < sizeof(T))
            return false;
        memcpy(&value, p, sizeof(T));
        p += sizeof(T);
        left -= sizeof(T);
        return true;
    }
};
const uint32_t kStateMagic = 0x53505531u; // "SPU1"
} // namespace

#define SPU_STATE_FIELDS(X)                                                                        \
    X(m_ram) X(m_voices) X(m_malloc.initialized) X(m_malloc.heapBase) X(m_malloc.heapEnd)          \
    X(m_malloc.maxRecords) X(m_transferStartAddr) X(m_transferCurAddr) X(m_transferMode)           \
    X(m_masterVolL) X(m_masterVolR) X(m_masterVolModeL) X(m_masterVolModeR) X(m_curMasterVolL)    \
    X(m_curMasterVolR) X(m_masterSweepCounterL) X(m_masterSweepCounterR) X(m_muted)              \
    X(m_reverbMasterEnable) X(m_reverbAttr) X(m_reverb) X(m_noiseClock) X(m_noiseTimer)           \
    X(m_noiseLevel) X(m_cdEnable) X(m_cdReverb) X(m_cdVolL) X(m_cdVolR) X(m_xaMasterGainQ16)      \
    X(m_xaMasterGain) X(m_lastCdFrame) X(m_renderer) X(m_clipMode) X(m_nativePhase)              \
    X(m_highRateVoiceSamples) X(m_highRateVoiceSteps) X(m_highRateCdL) X(m_highRateCdR)

void SPUCore::SaveState(std::vector<uint8_t>& out) const
{
    out.clear();
    put(out, kStateMagic);
    put(out, static_cast<uint32_t>(sizeof(*this)));
#define PUT_FIELD(f) put(out, f);
    SPU_STATE_FIELDS(PUT_FIELD)
#undef PUT_FIELD
    put(out, static_cast<uint32_t>(m_malloc.blocks.size()));
    for (const SPUMallocState::Block& b : m_malloc.blocks)
        put(out, b);
    put(out, static_cast<uint32_t>(m_cdQueue.size()));
    for (const CdFrame& f : m_cdQueue)
        put(out, f);
    put(out, static_cast<uint32_t>(m_cdQueueDouble.size()));
    for (const CdFrameDouble& f : m_cdQueueDouble)
        put(out, f);
}

bool SPUCore::LoadState(const uint8_t* data, size_t size)
{
    Reader in = { data, size };
    uint32_t magic = 0, layout = 0, count = 0;
    if (!in.get(magic) || magic != kStateMagic || !in.get(layout) || layout != sizeof(*this))
        return false;
    Reset(); // the alternative renderers' histories restart
#define GET_FIELD(f) if (!in.get(f)) return false;
    SPU_STATE_FIELDS(GET_FIELD)
#undef GET_FIELD
    if (!in.get(count))
        return false;
    m_malloc.blocks.resize(count);
    for (SPUMallocState::Block& b : m_malloc.blocks)
        if (!in.get(b))
            return false;
    if (!in.get(count))
        return false;
    m_cdQueue.resize(count);
    for (CdFrame& f : m_cdQueue)
        if (!in.get(f))
            return false;
    if (!in.get(count))
        return false;
    m_cdQueueDouble.resize(count);
    for (CdFrameDouble& f : m_cdQueueDouble)
        if (!in.get(f))
            return false;
    return in.left == 0;
}

} // namespace PsyX
