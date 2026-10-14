#ifndef PSYX_SPUCORE_H
#define PSYX_SPUCORE_H

// PsyX_SPUCore - backend-neutral software model of the PlayStation 1 SPU.
//
// This is a from-scratch, standalone reimplementation of the documented SPU
// hardware behavior (512KiB SPU RAM, 24 voices, SPU-ADPCM decode, 4-point
// Gaussian interpolation, ADSR envelope generator, key on/off, transfer
// port + a convenience heap allocator, and master/CD mixing). It does not
// depend on OpenAL/SDL/WASAPI or any other audio backend: it only produces
// PCM frames into a caller-provided buffer via RenderFrames(). The caller
// (an actual audio backend/adapter) is responsible for threading, locking,
// and feeding the produced PCM to a device.
//
// Thread-agnostic: none of the methods below take any lock. If this object
// is shared across threads (e.g. game/update thread issuing register writes
// while an audio-callback thread calls RenderFrames), the CALLER must
// serialize access with its own mutex/critical section.
//
// Hardware facts and formulas implemented here are taken from the publicly
// documented SPU register/timing behavior (Sony's SPU is exhaustively
// reverse-engineered public knowledge - see the "Sound Processing Unit"
// chapter of the well known nocash/psx-spx hardware specification), notably:
//   - SPU-ADPCM block format, filter table and loop/end flag semantics.
//   - The 512-entry, 8bit-indexed 4-point Gaussian interpolation table.
//   - The 16bit pitch counter (12bit fraction, i.e. 0x1000 == 1.0x) with the
//     0x3FFF->0x4000 step clipping rule.
//   - The ADSR envelope shift/step/counter stepping rules.
// PMON pitch modulation, the shared noise generator (psx-spx "SPU Noise
// Generator"), per-voice/master volume sweeps and the reverb unit
// (PsyX_SPUReverb, from psx-spx "SPU Reverb Formula") are implemented too.

#include "psx/types.h"
#include "psx/libspu.h"
#include "PsyX_IdealReverb.h"
#include "PsyX_ReferenceResampler.h"
#include "PsyX_ReferenceReverb.h"
#include "PsyX_SPUReverb.h"

#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <vector>

namespace PsyX
{

// ---------------------------------------------------------------------------
// Hardware constants
// ---------------------------------------------------------------------------

const uint32_t kSpuRamSize        = 512u * 1024u; // 512KiB SPU RAM
const int      kNumVoices         = 24;           // 24 hardware voices
const uint32_t kAdpcmBlockBytes   = 16u;           // one SPU-ADPCM block
const int      kAdpcmBlockSamples = 28;            // 28 decoded PCM samples/block
const uint32_t kOutputSampleRate  = 44100u;        // fixed SPU mixing rate
const uint32_t kPitchUnity        = 0x1000u;       // VxPitch value == native rate (1.0 in 4.12)
const uint32_t kPitchStepClip     = 0x3FFFu;       // step > this is clipped to kPitchClippedStep
const uint32_t kPitchClippedStep  = 0x4000u;       // clipped step value (== 4 raw samples/tick)
const uint32_t kGaussTableSize    = 512u;          // documented Gauss table entry count

// Sound RAM Data Transfer Mode, mirrors SPUCNT bits 4-5 (0=Stop,1=Manual
// Write,2=DMA write,3=DMA read). This core does not model DMA vs manual-IO
// timing differences (both a caller-driven Write()/Read() call move bytes
// immediately); the mode is tracked purely as state for SpuGetTransferMode-
// style queries and for gating Write()/Read() the same way real SPUCNT does.
enum class TransferMode : int
{
    Stop        = 0,
    ManualWrite = 1,
    DmaWrite    = 2,
    DmaRead     = 3,
};

// The 4 values accepted by SpuSetKey()/written to the KON/KOFF-alike port,
// matching libspu.h's SPU_OFF(0)/SPU_ON(1)/SPU_OFF_ENV_ON(2)/SPU_ON_ENV_OFF(3).
enum KeyCommand
{
    KeyCmd_Off      = SPU_OFF,         // begin Release phase
    KeyCmd_On       = SPU_ON,          // reset envelope to 0 and begin Attack phase
    KeyCmd_OffEnvOn = SPU_OFF_ENV_ON,  // silence the voice but leave the envelope generator running
    KeyCmd_OnEnvOff = SPU_ON_ENV_OFF,  // (re)start audio without perturbing/reset of the envelope generator
};

// The 4 possible key-status values returned by GetKeyStatus()/GetAllKeysStatus(),
// same four values as KeyCommand and the PsyQ SPU status convention:
//   Off        - envelope finished releasing (level==0, phase==Off/Release)
//   On         - normal Attack/Decay or Sustain-with-nonzero-level
//   OffEnvOn   - Release phase, but the envelope has not yet reached zero
//   OnEnvOff   - Sustain phase, but the envelope has decayed to zero
enum KeyStatus
{
    KeyStatus_Off      = SPU_OFF,
    KeyStatus_On       = SPU_ON,
    KeyStatus_OffEnvOn = SPU_OFF_ENV_ON,
    KeyStatus_OnEnvOff = SPU_ON_ENV_OFF,
};

// Internal ADSR envelope generator phase (not a hardware register - derived
// state used to select which shift/step/mode fields apply and to compute the
// 4-state KeyStatus above).
enum class EnvPhase : int
{
    Off = 0,
    Attack,
    Decay,
    Sustain,
    Release,
};

enum class RendererMode : int
{
    Exact = 0,
    Ideal,
    Reference
};

enum class ClipMode : int
{
    None = 0,
    Psx,
    Soft
};

// ---------------------------------------------------------------------------
// Per-voice runtime state
// ---------------------------------------------------------------------------

struct SPUVoiceState
{
    // Register mirror, kept in sync so GetVoiceAttr()/GetVoiceAttrIndexed()
    // can hand back an accurate SpuVoiceAttr snapshot at any time.
    SpuVoiceAttr attr;

    // -- ADPCM playback position (all addresses are BYTE offsets into SPU RAM) --
    uint32_t curAddr;          // address of the ADPCM block currently loaded/loading
    uint32_t repeatAddr;       // loop/repeat address (register LSAX mirror + hw auto-latch)
    bool     repeatAddrLatchedByBlock; // true once a Loop-Start header bit auto-latched it

    // ADPCM decoder history, persists across block boundaries (this is what
    // makes the filter "block-on-demand": we do not pre-decode a whole
    // sample, only ever the block currently needed, and the 2-sample filter
    // history/3-sample Gaussian tap history survive across block loads).
    int32_t  adpcmHist1;       // most recently decoded raw sample ("old")
    int32_t  adpcmHist2;       // sample before that ("older")

    int16_t  blockSamples[kAdpcmBlockSamples]; // decoded PCM for the currently loaded block
    int      blockSamplePos;   // index of the NEXT undelivered sample in blockSamples (0..28)
    bool     blockValid;       // false until the first block has been decoded

    // -- Loose per-sound replacement (pc_sfx_override.c, via g_PsyX_SfxOverride) --
    // Latched at Key On for a voice whose start address has a replacement
    // registered. While set, blocks are filled from this PC-owned PCM instead of
    // decoding the ADPCM at curAddr; everything downstream (pitch counter,
    // interpolation, ADSR, volumes, ENDX) is unaware of the substitution.
    const int16_t* ovrPcm;     // borrowed; owned by the game side. null = not replaced
    uint32_t ovrCount;         // samples available in ovrPcm
    uint64_t ovrPos;           // 16.16 cursor into ovrPcm
    uint32_t ovrStep;          // 16.16 source samples per emitted sample (rate latch)

    // 4-tap history window consumed by the Gaussian interpolator, in the
    // order the documented formula expects: [0]=new,[1]=old,[2]=older,[3]=oldest.
    int16_t  interpTaps[4];

    // 16bit pitch counter (12bit fraction => 0x1000 = 1.0x speed).
    uint32_t pitchCounter;

    bool     reachedLoopEnd;   // sticky ENDX bit for this voice (see GetEndxFlags())
    bool     blockLoopEnd;     // transient loop-end flag for the current ADPCM block
    bool     forceReleaseOnBlockEnd; // set by an ADPCM "End+Mute" header (code 1)

    // -- ADSR envelope generator --
    EnvPhase envPhase;
    int32_t  envLevel;         // current ADSR volume, nominally 0..0x7FFF
    uint32_t envCounter;       // accumulator; bit15 (0x8000) set => time to step

    // -- Reverb-send / noise / pitch-mod hardware bits. reverbSend (EON) is
    // state-only (the reverb DSP is NOT implemented, see
    // SPUCore::IsReverbDspImplemented()); noiseMode (NON) and pitchModEnable
    // (PMON) ARE implemented (see RenderFrames()'s shared noise generator
    // and StepVoiceOneSample()'s pitch-modulation formula).
    bool     reverbSend;       // EON bit
    bool     noiseMode;        // NON bit (ADPCM decode keeps running regardless - see RenderFrames)
    bool     pitchModEnable;   // PMON bit (this voice's pitch is modulated by voice (index-1)'s VxOUTX)

    bool     everKeyedOn;      // whether SetKey(On,...) has ever been issued

    // -- Per-voice Left/Right volume: Direct fixed value OR Sweep envelope --
    // curVolL/R mirror the real hardware "Current Volume Left/Right"
    // register (1F801E00h+voice*4). In SPU_VOICE_DIRECT16 mode they are a
    // live mirror of attr.volume.left/right*2, refreshed every tick (exactly
    // like the fixed-volume register). In any Sweep mode (attr.volmode !=
    // SPU_VOICE_DIRECT16) they instead evolve once per 44.1kHz tick via the
    // same shift/step/counter rules as the ADSR generator (see
    // StepVolumeSweep() in the .cpp), continuing from whatever value they
    // last held - matching the documented "Sweep starts at the current
    // volume" hardware behavior. Key On/Off does NOT touch these: on real
    // hardware the Volume Left/Right register block is entirely independent
    // of the ADSR/address block.
    int32_t  curVolL, curVolR;
    uint32_t sweepCounterL, sweepCounterR; // independent from envCounter

    // VxOUTX: this voice's saturated post-ADSR, pre-L/R-volume sample from
    // the most recently rendered tick. Real hardware calls this the voice's
    // "amplitude" and feeds voice (index-1)'s VxOUTX into voice (index)'s
    // pitch-modulation (PMON) factor - see StepVoiceOneSample(). Also
    // exposed for tests/tools via GetVoiceLastOutputX().
    int32_t  lastOutX;
};

// ---------------------------------------------------------------------------
// SPU RAM allocator state (SpuInitMalloc/SpuMalloc/SpuFree-equivalent). This
// is PsyQ SDK convenience bookkeeping, not a hardware feature - real SPU
// silicon has no notion of "allocation", so unlike the DSP-facing pieces
// above this does not need to match an undocumented Sony algorithm bit for
// bit; it only needs to hand out non-overlapping regions of SPU RAM the way
// SpuInitMalloc/SpuMalloc/SpuFree do from the caller's point of view.
// ---------------------------------------------------------------------------

struct SPUMallocState
{
    bool     initialized;
    uint32_t heapBase;   // first address available for allocation (bytes)
    uint32_t heapEnd;    // exclusive end of the allocatable region (bytes)
    int      maxRecords; // record-count hint passed to InitMalloc (advisory only)

    struct Block
    {
        uint32_t addr;
        uint32_t size;
        bool     free;
    };
    std::vector<Block> blocks;
};

// ---------------------------------------------------------------------------
// SPUCore
// ---------------------------------------------------------------------------

class SPUCore
{
public:
    SPUCore();

    void Reset();

    // ---- SPU RAM --------------------------------------------------------
    uint8_t*       RamData()       { return m_ram; }
    const uint8_t* RamData() const { return m_ram; }
    static uint32_t RamSize() { return kSpuRamSize; }

    // ---- Sound RAM Data Transfer port (1F801DA6h/1F801DA8h-equivalent) ---
    // PsyQ exposes byte addresses; the SDK hides the hardware register's /8 units.
    uint32_t     SetTransferStartAddr(uint32_t addr);
    uint32_t     GetTransferStartAddr() const;
    uint32_t     GetCurrentTransferAddr() const; // internal auto-incrementing byte address
    void         SetTransferMode(TransferMode mode);
    TransferMode GetTransferMode() const;

    // Manual-write/DMA-write-equivalent ingestion into SPU RAM at the
    // current transfer address (auto-incrementing). Returns bytes actually
    // written (clamped to remaining RAM). No DMA timing is modeled: bytes
    // land immediately, matching this core's synchronous, caller-driven design.
    u_int Write(const u_char* src, u_int sizeBytes);
    // Manual-read/DMA-read-equivalent read-back from the current transfer address.
    u_int Read(u_char* dst, u_int sizeBytes);

    // ---- SPU RAM allocator (SpuInitMalloc/SpuMalloc/SpuFree-equivalent) --
    void     InitMalloc(int numRecords, uint32_t heapBaseAddr);
    uint32_t Malloc(uint32_t sizeBytes);          // returns 0 (SPU_NULL) on failure
    uint32_t MallocWithStartAddr(uint32_t addr, uint32_t sizeBytes);
    void     Free(uint32_t addr);
    const SPUMallocState& GetMallocState() const { return m_malloc; }

    // ---- Voice attribute setters/getters (SpuVoiceAttr-compatible) ------
    // SetVoiceAttr: attr.voice is a BITMASK selecting every voice the write
    // applies to (like real SpuSetVoiceAttr); attr.mask selects which fields
    // of SpuVoiceAttr are actually applied.
    void SetVoiceAttr(const SpuVoiceAttr& attr);
    // GetVoiceAttr: attr.voice (input) must be a single voice bit; the full
    // attr is filled in from that voice's current state (mask is ignored,
    // matching real SpuGetVoiceAttr).
    void GetVoiceAttr(SpuVoiceAttr& attr) const;

    // Index-addressed convenience mirrors of the SpuNSetVoiceAttr/
    // SpuNGetVoiceAttr/SpuSetVoice*() family.
    void SetVoiceAttrIndexed(int voiceIndex, const SpuVoiceAttr& attr);
    void GetVoiceAttrIndexed(int voiceIndex, SpuVoiceAttr& attr) const;

    void SetVoiceVolume(int voiceIndex, int16_t volL, int16_t volR);
    void GetVoiceVolume(int voiceIndex, int16_t* volL, int16_t* volR) const;
    // Current Volume Left/Right (hardware register 1F801E00h+voice*4): a
    // live mirror of the fixed volume when attr.volmode==SPU_VOICE_DIRECT16,
    // or the live Sweep-envelope value otherwise (see StepVolumeSweep()).
    void GetVoiceVolumeX(int voiceIndex, int16_t* volXL, int16_t* volXR) const;
    void SetVoicePitch(int voiceIndex, uint16_t pitch);
    void GetVoicePitch(int voiceIndex, uint16_t* pitch) const;
    void SetVoiceStartAddr(int voiceIndex, uint32_t startAddr);
    void SetVoiceLoopStartAddr(int voiceIndex, uint32_t lsa);
    void GetVoiceEnvelope(int voiceIndex, int16_t* envx) const;
    // VxOUTX: this voice's saturated post-ADSR, pre-L/R-volume sample from
    // the most recently rendered tick (see SPUVoiceState::lastOutX). Exposed
    // mainly so tests/tools can observe the exact value PMON on voice
    // (voiceIndex+1) would have consumed.
    int32_t GetVoiceLastOutputX(int voiceIndex) const;

    // ---- Key on/off + four-state key status -----------------------------
    void     SetKey(int onOffCmd, uint32_t voiceBitmask);
    int      GetKeyStatus(uint32_t voiceBit) const; // voiceBit: single SPU_VOICECH(n) bit
    void     GetAllKeysStatus(char* statusOut, int count = kNumVoices) const;
    uint32_t GetEndxFlags() const; // 24-bit ENDX mask; cleared per-voice on Key On

    // ---- Reverb-send flags/state -----------------------------------------
    void     SetVoiceReverbSend(uint32_t voiceBitmask, bool enable); // EON bits
    uint32_t GetVoiceReverbSendMask() const;
    void     SetReverbMasterEnable(bool enable); // SPUCNT bit7
    bool     GetReverbMasterEnable() const;
    void     SetReverbModeParam(const SpuReverbAttr& attr);
    void     GetReverbModeParam(SpuReverbAttr& attr) const;
    void     ClearReverbWorkArea();
    static bool IsReverbDspImplemented() { return true; }

    // ---- Noise generator (IMPLEMENTED: shared LFSR-style generator) -----
    // One shared generator (NOT per-voice) is stepped once per output tick
    // in RenderFrames(); voices with NON (noiseMode) set substitute its
    // current signed 16bit level for their own ADPCM/Gaussian sample that
    // tick, but keep decoding ADPCM/advancing their pitch counter/ENDX flag
    // exactly as if NON were off (matches the documented "all voices are
    // permanently reading data from SPU RAM - even in Noise mode" behavior).
    void     SetVoiceNoiseMode(uint32_t voiceBitmask, bool enable); // NON bits
    uint32_t GetVoiceNoiseModeMask() const;
    // nClock packs the documented SPUCNT "Noise Frequency Shift"(4bit) and
    // "Noise Frequency Step"(2bit) fields the same way this core already
    // packs ADSR shift/step fields elsewhere: (shift<<2)|step. This bit
    // packing convention is not stated by any public PsyQ SDK header (the
    // SDK just calls it a single opaque "n_clock" int), so it is a
    // documented best-effort assumption, consistent with the rest of this
    // core's field-packing choices.
    void     SetNoiseClock(int nClock);
    int      GetNoiseClock() const;
    int16_t  GetCurrentNoiseLevel() const; // current shared NoiseLevel, signed
    static bool IsNoiseGeneratorImplemented() { return true; }

    // ---- Pitch modulation (IMPLEMENTED: exact documented PMON formula) ---
    // Voice (index)'s pitch step is modulated by voice (index-1)'s VxOUTX
    // (post-ADSR, pre-L/R-volume sample) exactly per psx-spx's "Pitch
    // Counter" pseudocode, including the documented sign/glitch sequence
    // (VxPitch is sign-extended for the multiply, then the 16bit product
    // has its sign silently discarded) - see StepVoiceOneSample(). Voice 0
    // has no predecessor, so PMON has no effect there (matches hardware:
    // PMON bit0 is unused).
    void     SetVoicePitchModEnable(uint32_t voiceBitmask, bool enable); // PMON bits
    uint32_t GetVoicePitchModEnableMask() const;
    static bool IsPitchModulationImplemented() { return true; }

    // ---- Master / CD controls --------------------------------------------
    // SetMasterVolume forces Direct (fixed) mode for both channels - use
    // SetCommonAttr with SPU_COMMON_MVOLMODEL/R to engage a Sweep envelope
    // on the master volume instead (same Direct/Sweep semantics as voice
    // volume, see SPUVoiceState::curVolL/R doc comment; this core treats
    // 1F801D80h/1F801D82h as sharing the exact same register format as the
    // per-voice volume registers, per psx-spx's shared "Fixed/Sweep Volume
    // Mode" description - a documented assumption since PsyQ's headers only
    // expose SPU_COMMON_MVOLMODEL/R bits without spelling out the format).
    void SetMasterVolume(int16_t left, int16_t right);
    void GetMasterVolume(int16_t* left, int16_t* right) const; // raw Direct/rate register, NOT live value
    // Current (post-sweep) master volume - hardware's 1F801DB8h register.
    void GetCurrentMasterVolume(int16_t* left, int16_t* right) const;
    void SetCommonAttr(const SpuCommonAttr& attr);
    void GetCommonAttr(SpuCommonAttr& attr) const;
    int  SetMute(int onOff); // returns previous state, matching SpuSetMute
    int  GetMute() const;


    void SetCdAudioEnable(bool enable);
    bool GetCdAudioEnable() const;
    void SetCdAudioReverb(bool enable);
    bool GetCdAudioReverb() const;
    void SetCdVolume(int16_t left, int16_t right);
    void GetCdVolume(int16_t* left, int16_t* right) const;
    void SetXaMasterGain(double gain);
    double GetXaMasterGain() const { return m_xaMasterGain; }

    // ---- XA/CD stereo-frame ingress (no external resampling performed) ---
    // The SPU mixer's "CD Audio" input is nothing more than a stereo sample
    // pair sampled once per 44.1kHz tick. Real hardware receives that stream
    // already at 44100Hz straight from the CDROM controller's XA-ADPCM/CD-DA
    // decoder. This core does not decode XA or resample CD audio itself: the
    // caller (CDROM/XA subsystem) must decode/resample to 44100Hz and push
    // frames in with PushCdStereoFrame(); RenderFrames() then consumes
    // exactly one pushed frame per output frame. If the queue underflows
    // (caller pushed too few frames), the CD Audio input is silence (0,0)
    // for that tick - it does NOT hold/repeat the last frame and does NOT
    // invent interpolation, matching the fact that real hardware has no
    // buffering of its own to fall back on if the CDROM controller stalls.
    void PushCdStereoFrame(int16_t left, int16_t right);
    void PushCdStereoFrameDouble(double left, double right);
    size_t GetPendingCdFrameCount() const { return m_cdQueue.size(); }
    void ClearCdQueue();

    // ---- Rendering ---------------------------------------------------------
    // Renders exactly frameCount interleaved (L,R) int16 frames at 44100Hz
    // into out. Deterministic: for identical starting state + identical
    // register writes/CD ingress between calls, output is bit-exact
    // regardless of platform/compiler (integer-only decode/interpolation/
    // envelope math; only the final per-frame sum->int16 step saturates).
    // ---- Split (spatialised) rendering -------------------------------------
    // Optional per-voice taps for the OpenAL spatialiser. When a SplitOutput is
    // supplied, RenderFrames ALSO writes each voice's post-ADSR, PRE-PAN mono
    // sample, the reverb return on its own bus, and the CD/XA stereo bed, so the
    // caller can place each of them in a speaker field instead of taking this
    // core's stereo downmix.
    //
    // The mix itself is untouched: the same loop still pans, sums and feeds the
    // reverb exactly as before, so the accurate stereo output and the split taps
    // can never drift apart. Only the reverb OUTPUT is separated out -- its
    // INPUT is still the panned voice sum, which is what the hardware does and
    // what makes the reverb sound right.
    //
    // pan[] carries each voice's live L/R volume so the caller can derive an
    // azimuth; it is the value at the LAST frame of the block.
    struct SplitOutput
    {
        int16_t* voiceMono[kNumVoices]; // per voice, frameCount samples (may be null)
        int16_t* wetLR;                 // reverb return, interleaved (may be null)
        int16_t* cdLR;                  // CD/XA post-volume, interleaved (may be null)
        int32_t  panL[kNumVoices];      // out: live voice volume, end of block
        int32_t  panR[kNumVoices];      // out: live voice volume, end of block
        int32_t  masterL;               // out: live master volume, end of block
        int32_t  masterR;               // out: live master volume, end of block
    };
    void RenderFrames(int16_t* outInterleavedLR, int frameCount, SplitOutput* split = nullptr);
    bool SetRenderer(RendererMode renderer, ClipMode clipMode = ClipMode::None);
    RendererMode GetRenderer() const { return m_renderer; }
    ClipMode GetClipMode() const { return m_clipMode; }
    uint32_t GetNativeSampleRate() const;
    void RenderFramesDouble(double* outInterleavedLR, int frameCount);
    uint64_t GetReferenceSincProcessCount() const { return m_referenceSincProcessCount; }
    int GetReferenceLastBandlimitBucket(int voiceIndex) const;
    double GetEnhancedReverbEnergy() const;

    // ---- Direct access for tests/tools -------------------------------------
    const SPUVoiceState& GetVoiceState(int voiceIndex) const { return m_voices[voiceIndex]; }

    // ---- Save states (rood-boi, pc/src/snapshot.c) ---------------------------
    // The state the Exact renderer runs on (RAM, voices, allocator, transfer,
    // volumes, reverb, noise, CD input) as bytes. The Ideal/Reference
    // renderers' resampler and reverb histories are not included: LoadState
    // resets them, as Reset() does.
    void SaveState(std::vector<uint8_t>& out) const;
    bool LoadState(const uint8_t* data, size_t size);

    // 512-entry documented Gaussian interpolation table (exposed for tests).
    static const int16_t* GaussTable();
    static double EvaluateIdealGaussian(const int16_t taps[4], uint32_t pitchCounter);

private:
    uint8_t        m_ram[kSpuRamSize];
    SPUVoiceState  m_voices[kNumVoices];
    SPUMallocState m_malloc;

    uint32_t     m_transferStartAddr; // PsyQ-visible byte address (does not auto-increment)
    uint32_t     m_transferCurAddr;   // internal auto-incrementing byte address
    TransferMode m_transferMode;

    int16_t m_masterVolL, m_masterVolR;   // raw Direct-mode value / Sweep rate field (see class comment)
    int     m_masterVolModeL, m_masterVolModeR; // SPU_VOICE_DIRECT16 or a Sweep enum value
    int32_t m_curMasterVolL, m_curMasterVolR;   // live (post-sweep) master volume, *2 scale like voices
    uint32_t m_masterSweepCounterL, m_masterSweepCounterR;
    int     m_muted;

    bool    m_reverbMasterEnable;
    SpuReverbAttr m_reverbAttr;
    PsyX_SPUReverb::Reverb m_reverb;
    PsyX_IdealReverb::Reverb m_idealReverb;
    PsyX_ReferenceReverb::Reverb m_referenceReverb;

    // -- Shared noise generator (one generator for the whole SPU, not per
    // voice - see StepSharedNoise()/IsNoiseGeneratorImplemented()). --
    int      m_noiseClock;  // packed (shift<<2)|step, see SetNoiseClock() doc
    int32_t  m_noiseTimer;  // psx-spx noise timer (counts down at 44.1 kHz)
    uint32_t m_noiseLevel;  // low 16 bits are the signed noise sample

    bool    m_cdEnable;
    bool    m_cdReverb;
    int16_t m_cdVolL, m_cdVolR;
    uint32_t m_xaMasterGainQ16;
    double m_xaMasterGain;
    struct CdFrame { int16_t l, r; };
    std::deque<CdFrame> m_cdQueue;
    CdFrame m_lastCdFrame;
    struct CdFrameDouble { double l, r; };
    std::deque<CdFrameDouble> m_cdQueueDouble;

    struct ReferenceVoiceState
    {
        ReferenceResampler resampler;
        uint64_t processCount = 0;
        int lastBandlimitBucket = 0;
    };
    ReferenceVoiceState m_referenceVoices[kNumVoices];
    RendererMode m_renderer = RendererMode::Exact;
    ClipMode m_clipMode = ClipMode::None;
    uint32_t m_nativePhase = 0;
    uint64_t m_referenceSincProcessCount = 0;
    double m_highRateVoiceSamples[kNumVoices]{};
    int32_t m_highRateVoiceSteps[kNumVoices]{};
    double m_highRateCdL = 0.0;
    double m_highRateCdR = 0.0;

    void ResetVoice(SPUVoiceState& v);
    void ApplyVoiceAttrFields(SPUVoiceState& v, const SpuVoiceAttr& src, uint32_t mask);
    void KeyOnVoice(SPUVoiceState& v, int voiceIndex);
    void KeyOffVoice(SPUVoiceState& v);

    void DecodeAdpcmBlock(SPUVoiceState& v, int voiceIndex = -1);
    void ShiftInNextRawSample(SPUVoiceState& v, int voiceIndex = -1);
    // Advances pitch/decoder by 1 tick and returns the interpolated raw
    // sample. voiceIndex/prevVoiceOutX are needed only for the PMON pitch-
    // modulation formula (voiceIndex>0 && pitchModEnable): prevVoiceOutX
    // must be voice (voiceIndex-1)'s VxOUTX computed earlier in the SAME
    // RenderFrames tick (0 for voice 0, which has no PMON predecessor).
    int32_t StepVoiceOneSample(SPUVoiceState& v, int voiceIndex, int32_t prevVoiceOutX);
    uint32_t ResolveVoiceStep(const SPUVoiceState& v, int voiceIndex, int32_t prevVoiceOutX) const;
    double GaussInterpolateDouble(const int16_t taps[4], uint32_t pitchCounter) const;
    void BeginHighRateLogicalFrame();
    void RenderHighRateSubframe(double* left, double* right);
    static double ApplyClip(double value, ClipMode mode);
    void StepAdsr(SPUVoiceState& v);

    // Steps a per-voice or master Left/Right Sweep volume envelope by one
    // 44.1kHz tick, in place. No-op if volMode==SPU_VOICE_DIRECT16 (Direct
    // mode is instead live-mirrored inline by the caller every tick - see
    // RenderFrames()). rawRate is the packed (shift<<2|step) 7bit field
    // this core stores in attr.volume/attr.mvol while NOT in Direct mode.
    static void StepVolumeSweep(int32_t& curVol, uint32_t& counter, int volMode, int rawRate);

    // Advances the single shared noise generator by exactly one 44.1kHz
    // tick (see m_noiseTimer/m_noiseLevel doc comments above).
    void StepSharedNoise();
};

} // namespace PsyX

#endif // PSYX_SPUCORE_H
