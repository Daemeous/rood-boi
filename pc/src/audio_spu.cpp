/* Host sound chip for the native build: the vendored PsyCross software SPU
 * (pc/third_party/psycross-audio), driven two ways, as on the PS1:
 *  - the game's sound driver writes voice/key/mode registers directly
 *    (src/include/hw.h); those land in a plain memory page at 0x1F801000 and
 *    are synchronised into the core by pc_spu_sync_registers();
 *  - the SDK library calls (SpuWrite, reverb, ...) are replaced on PC by
 *    pc/src/audio_libspu.c, which calls the pc_spu_* helpers below.
 * Rendering happens on the game thread (pc_spu_render), so the core needs no
 * locking; only the output ring (audio_output.c) is shared with a thread. */
#include "PsyX_SPUCore.h"
#include "PsyX/PsyX_public.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>
#include <cstdlib>

using PsyX::SPUCore;

extern "C" {
#include "audio.h"
}

static SPUCore* core;
static bool last_valid[24];
static uint16_t last_regs[24][8];
static uint16_t last_mode[6];
/* Main volume and SPUCNT as last applied; 0xffff forces the next sync. */
static uint16_t last_main[2] = { 0xffff, 0xffff }, last_control = 0xffff;

/* 0x1F801C00: 24 voices x 16 bytes, then the common registers. */
static volatile uint16_t* const regs = reinterpret_cast<volatile uint16_t*>(0x1F801C00);

extern "C" {
PsyX_SfxOverrideFn g_PsyX_SfxOverride = nullptr;
}

static SPUCore& spu()
{
    if (!core) {
        core = new SPUCore();
        core->Reset();
        core->SetRenderer(PsyX::RendererMode::Exact);
    }
    return *core;
}

/* Raw voice volume register -> SDK attribute: direct volumes are signed
 * 15-bit values (volume / 2); bit 15 selects a sweep with mode bits 14-12
 * (exponential, decrease, negative phase) and a 7-bit rate. */
static void volume_attr(uint16_t raw, short* volume, short* mode)
{
    if (!(raw & 0x8000)) {
        *volume = static_cast<short>(static_cast<int16_t>(raw << 1) >> 1);
        *mode = SPU_VOICE_DIRECT16;
        return;
    }
    bool exponential = raw & 0x4000, decrease = raw & 0x2000, negative = raw & 0x1000;
    if (exponential && decrease)
        *mode = SPU_VOICE_EXPDec;
    else if (exponential)
        *mode = negative ? SPU_VOICE_EXPIncR : SPU_VOICE_EXPIncN;
    else if (decrease)
        *mode = negative ? SPU_VOICE_LINEARDecR : SPU_VOICE_LINEARDecN;
    else
        *mode = negative ? SPU_VOICE_LINEARIncR : SPU_VOICE_LINEARIncN;
    *volume = raw & 0x7f;
}

extern "C" void pc_spu_sync_registers(void)
{
    SPUCore& s = spu();
    for (int voice = 0; voice < 24; ++voice) {
        volatile uint16_t* r = regs + voice * 8;
        uint16_t now[8];
        for (int i = 0; i < 8; ++i)
            now[i] = r[i];
        uint16_t* was = last_regs[voice];
        bool first = !last_valid[voice];
        SpuVoiceAttr attr;
        memset(&attr, 0, sizeof(attr));
        if (first || now[0] != was[0] || now[1] != was[1]) {
            volume_attr(now[0], &attr.volume.left, &attr.volmode.left);
            volume_attr(now[1], &attr.volume.right, &attr.volmode.right);
            attr.mask |= SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_VOLMODEL | SPU_VOICE_VOLMODER;
        }
        if (first || now[2] != was[2]) {
            attr.pitch = now[2];
            attr.mask |= SPU_VOICE_PITCH;
        }
        if (first || now[3] != was[3]) {
            attr.addr = static_cast<unsigned long>(now[3]) * 8;
            attr.mask |= SPU_VOICE_WDSA;
        }
        if (first || now[4] != was[4] || now[5] != was[5]) {
            attr.adsr1 = now[4];
            attr.adsr2 = now[5];
            attr.mask |= SPU_VOICE_ADSR_ADSR1 | SPU_VOICE_ADSR_ADSR2;
        }
        if (first || now[7] != was[7]) {
            attr.loop_addr = static_cast<unsigned long>(now[7]) * 8;
            attr.mask |= SPU_VOICE_LSAX;
        }
        if (attr.mask)
            s.SetVoiceAttrIndexed(voice, attr);
        memcpy(was, now, sizeof(now));
        last_valid[voice] = true;
    }
    /* 0x1D88..: key on/off are write-triggered; consume and clear them. */
    uint32_t key_off = regs[0x18c / 2] | (static_cast<uint32_t>(regs[0x18e / 2]) << 16);
    uint32_t key_on = regs[0x188 / 2] | (static_cast<uint32_t>(regs[0x18a / 2]) << 16);
    regs[0x18c / 2] = regs[0x18e / 2] = regs[0x188 / 2] = regs[0x18a / 2] = 0;
    if (key_off)
        s.SetKey(SPU_OFF, key_off & 0xffffff);
    if (key_on)
        s.SetKey(SPU_ON, key_on & 0xffffff);
    if (getenv("ROOD_AUDIO_DEBUG") && (key_on || key_off)) {
        static int shown;
        if (shown++ < 40) {
            int16_t ml, mr;
            s.GetCurrentMasterVolume(&ml, &mr);
            fprintf(stderr, "[spu] kon=%06x koff=%06x v0 vol=%04x/%04x pitch=%04x addr=%04x adsr=%04x/%04x main=%d/%d cnt=%04x\n",
                key_on, key_off, regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], ml, mr, regs[0x1aa / 2]);
        }
    }
    /* 0x1D90..0x1D9A: pitch modulation, noise and reverb enable masks. */
    uint16_t mode[6];
    for (int i = 0; i < 6; ++i)
        mode[i] = regs[0x190 / 2 + i];
    if (memcmp(mode, last_mode, sizeof(mode))) {
        uint32_t pmon = mode[0] | (static_cast<uint32_t>(mode[1]) << 16);
        uint32_t non = mode[2] | (static_cast<uint32_t>(mode[3]) << 16);
        uint32_t eon = mode[4] | (static_cast<uint32_t>(mode[5]) << 16);
        s.SetVoicePitchModEnable(pmon & 0xffffff, true);
        s.SetVoicePitchModEnable(~pmon & 0xffffff, false);
        s.SetVoiceNoiseMode(non & 0xffffff, true);
        s.SetVoiceNoiseMode(~non & 0xffffff, false);
        s.SetVoiceReverbSend(eon & 0xffffff, true);
        s.SetVoiceReverbSend(~eon & 0xffffff, false);
        memcpy(last_mode, mode, sizeof(mode));
    }
    /* 0x1D80: main volume; 0x1DAA: SPUCNT (bit 14 unmute, bit 7 reverb,
     * bit 0 CD audio). Applied on change so library-level settings made
     * through audio_libspu.c are not overridden by an idle register. */
    if (regs[0x180 / 2] != last_main[0] || regs[0x182 / 2] != last_main[1]) {
        SpuCommonAttr common;
        memset(&common, 0, sizeof(common));
        last_main[0] = regs[0x180 / 2];
        last_main[1] = regs[0x182 / 2];
        volume_attr(last_main[0], &common.mvol.left, &common.mvolmode.left);
        volume_attr(last_main[1], &common.mvol.right, &common.mvolmode.right);
        common.mask = SPU_COMMON_MVOLL | SPU_COMMON_MVOLR | SPU_COMMON_MVOLMODEL
                    | SPU_COMMON_MVOLMODER;
        s.SetCommonAttr(common);
    }
    if (regs[0x1aa / 2] != last_control) {
        last_control = regs[0x1aa / 2];
        s.SetMute((last_control & 0x4000) ? SPU_OFF : SPU_ON);
        s.SetReverbMasterEnable((last_control & 0x80) != 0);
        s.SetCdAudioEnable((last_control & 1) != 0);
    }
    /* 0x1DB0: CD volume. */
    s.SetCdVolume(static_cast<int16_t>(regs[0x1b0 / 2]), static_cast<int16_t>(regs[0x1b2 / 2]));
}

extern "C" void pc_spu_render(short* out, int frames)
{
    spu().RenderFrames(out, frames);
    static int calls;
    if (getenv("ROOD_AUDIO_DEBUG") && ++calls % 960 == 0) {
        int nonzero = 0, peak = 0;
        for (uint32_t i = 0; i < SPUCore::RamSize(); ++i)
            nonzero += spu().RamData()[i] != 0;
        for (int i = 0; i < frames * 2; ++i)
            peak = std::max(peak, std::abs(static_cast<int>(out[i])));
        char keys[24];
        spu().GetAllKeysStatus(keys);
        int16_t env0 = 0, ml = 0, mr = 0;
        spu().GetVoiceEnvelope(0, &env0);
        spu().GetCurrentMasterVolume(&ml, &mr);
        fprintf(stderr, "[spu] ram nonzero=%d peak=%d env0=%d key0=%d mute=%d master=%d/%d\n", nonzero, peak,
            env0, keys[0], spu().GetMute(), ml, mr);
    }
}
/* A reset (SpuStart, also on every return to the title) clears the core's
 * voices, masks and main volume, so forget the register values last applied:
 * the game often rewrites identical values, which must still take effect. */
extern "C" void pc_spu_reset(void)
{
    spu().Reset();
    memset(last_valid, 0, sizeof(last_valid));
    memset(last_mode, 0xff, sizeof(last_mode));
    last_main[0] = last_main[1] = last_control = 0xffff;
}
extern "C" unsigned long pc_spu_set_transfer_addr(unsigned long addr)
{
    return spu().SetTransferStartAddr(static_cast<uint32_t>(addr));
}
extern "C" void pc_spu_set_transfer_mode(int io)
{
    spu().SetTransferMode(io ? PsyX::TransferMode::ManualWrite : PsyX::TransferMode::DmaWrite);
}
extern "C" unsigned long pc_spu_write(const unsigned char* data, unsigned long size)
{
    return spu().Write(data, static_cast<u_int>(size));
}
extern "C" unsigned long pc_spu_read(unsigned char* data, unsigned long size)
{
    return spu().Read(data, static_cast<u_int>(size));
}
extern "C" void pc_spu_init_malloc(int records, unsigned long base)
{
    spu().InitMalloc(records, static_cast<uint32_t>(base));
}
extern "C" void pc_spu_set_reverb(int on) { spu().SetReverbMasterEnable(on != 0); }
extern "C" void pc_spu_set_reverb_mode(int type, int depth_left, int depth_right, int mask)
{
    SpuReverbAttr attr;
    spu().GetReverbModeParam(attr);
    attr.mask = mask;
    attr.mode = type;
    attr.depth.left = static_cast<short>(depth_left);
    attr.depth.right = static_cast<short>(depth_right);
    spu().SetReverbModeParam(attr);
}
extern "C" int pc_spu_get_reverb_mode(void)
{
    SpuReverbAttr attr;
    spu().GetReverbModeParam(attr);
    return attr.mode;
}
extern "C" void pc_spu_set_noise_clock(int clock) { spu().SetNoiseClock(clock); }
extern "C" short pc_spu_get_envelope(int voice)
{
    int16_t envx = 0;
    spu().GetVoiceEnvelope(voice, &envx);
    return envx;
}
extern "C" void pc_spu_set_master_volume(short left, short right)
{
    spu().SetMasterVolume(left, right);
}

extern "C" void pc_spu_push_cd(const short* frames, int count)
{
    SPUCore& s = spu();
    for (int i = 0; i < count; ++i)
        s.PushCdStereoFrame(frames[i * 2], frames[i * 2 + 1]);
}
extern "C" int pc_spu_cd_pending(void) { return static_cast<int>(spu().GetPendingCdFrameCount()); }
extern "C" void pc_spu_clear_cd(void) { spu().ClearCdQueue(); }

/* Save states (snapshot.c): the core's state plus this adapter's register
 * mirror (what the next pc_spu_sync_registers sends depends on it). */
struct adapter_state {
    bool last_valid[24];
    uint16_t last_regs[24][8], last_mode[6], last_main[2], last_control;
};
static std::vector<uint8_t> saved;
extern "C" int pc_spu_state_size(void)
{
    spu().SaveState(saved);
    return static_cast<int>(saved.size() + sizeof(adapter_state));
}
extern "C" void pc_spu_state_save(void* out)
{
    adapter_state a;
    memcpy(a.last_valid, last_valid, sizeof last_valid);
    memcpy(a.last_regs, last_regs, sizeof last_regs);
    memcpy(a.last_mode, last_mode, sizeof last_mode);
    memcpy(a.last_main, last_main, sizeof last_main);
    a.last_control = last_control;
    memcpy(out, &a, sizeof a);
    memcpy(static_cast<uint8_t*>(out) + sizeof a, saved.data(), saved.size()); /* from pc_spu_state_size */
    saved.clear();
    saved.shrink_to_fit();
}
extern "C" int pc_spu_state_load(const void* in, int size)
{
    if (size < static_cast<int>(sizeof(adapter_state)))
        return 0;
    const uint8_t* bytes = static_cast<const uint8_t*>(in);
    if (!spu().LoadState(bytes + sizeof(adapter_state), static_cast<size_t>(size) - sizeof(adapter_state)))
        return 0;
    adapter_state a;
    memcpy(&a, bytes, sizeof a);
    memcpy(last_valid, a.last_valid, sizeof last_valid);
    memcpy(last_regs, a.last_regs, sizeof last_regs);
    memcpy(last_mode, a.last_mode, sizeof last_mode);
    memcpy(last_main, a.last_main, sizeof last_main);
    last_control = a.last_control;
    return 1;
}
