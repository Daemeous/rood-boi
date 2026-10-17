/* PC replacements for the Sony libspu/libsnd calls the game's sound driver
 * makes, over the software SPU (audio_spu.cpp), plus the driver's clock.
 * On the PS1 the driver runs from a root-counter 2 interrupt at about 240 Hz
 * (reload 0x44E8); here it runs four times per 60 Hz frame on the game
 * thread, with the register sync and a quarter frame of audio after each
 * tick. Host transfers finish at once, so DMA completion callbacks run before
 * SpuWrite/SpuRead return (the driver busy-waits on the flag they clear). */
#include "psx_types.h" /* psx_long: the SDK's 32-bit long on every build */
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>

typedef void (*callback_t)(void);
static callback_t transfer_callback, irq_callback;
static psx_long irq_enabled;
static psx_ulong irq_address;
static psx_long (*driver_tick)(void);
static int tick_enabled, reverb_depth_left, reverb_depth_right;

/* SpuStart resets the SPU and leaves SPUCNT enabled and unmuted (0xC000), as
 * the SDK initialisation does; the driver then edits SPUCNT directly. */
void SpuStart(void)
{
    pc_spu_reset();
    pc_spu_set_transfer_mode(0);
    *(volatile unsigned short*)0x1F801DAA = 0xC000;
}
void SpuQuit(void) {}
psx_long SpuSetNoiseClock(psx_long clock)
{
    pc_spu_set_noise_clock((int)clock);
    return clock;
}
psx_long SpuSetReverb(psx_long on)
{
    pc_spu_set_reverb((int)on);
    return on;
}
void SsUtReverbOn(void) { pc_spu_set_reverb(1); }
void SsUtReverbOff(void) { pc_spu_set_reverb(0); }
psx_long SpuSetReverbModeType(psx_long mode)
{
    pc_spu_set_reverb_mode((int)mode, reverb_depth_left, reverb_depth_right, 1);
    return 0;
}
void SpuSetReverbModeDepth(short left, short right)
{
    reverb_depth_left = left;
    reverb_depth_right = right;
    pc_spu_set_reverb_mode(pc_spu_get_reverb_mode(), left, right, 2 | 4);
}
void SpuGetReverbModeType(psx_long* mode) { *mode = pc_spu_get_reverb_mode(); }
void SpuSetCommonMasterVolume(short left, short right) { pc_spu_set_master_volume(left, right); }
void SpuGetVoiceEnvelope(int voice, short* envx) { *envx = pc_spu_get_envelope(voice); }
psx_long SpuInitMalloc(psx_long num, char* top)
{
    (void)top;
    return num;
}
psx_long SpuSetTransferMode(psx_long mode)
{
    pc_spu_set_transfer_mode(mode == 1 /* SPU_TRANSFER_BY_IO */);
    return mode;
}
psx_ulong SpuSetTransferStartAddr(psx_ulong addr) { return pc_spu_set_transfer_addr(addr); }
psx_ulong SpuWrite(unsigned char* data, psx_ulong size)
{
    psx_ulong written = pc_spu_write(data, size);
    if (transfer_callback)
        transfer_callback();
    return written;
}
psx_ulong SpuRead(unsigned char* data, psx_ulong size)
{
    psx_ulong read = pc_spu_read(data, size);
    if (transfer_callback)
        transfer_callback();
    return read;
}
callback_t SpuSetTransferCallback(callback_t fn)
{
    callback_t previous = transfer_callback;
    transfer_callback = fn;
    return previous;
}
/* SPU address interrupt (SPUCNT bit 6): the sound driver streams effect and
 * cutscene sounds through a double buffer in SPU RAM and arms the interrupt
 * at the half a voice reaches next; the callback loads the following part,
 * and the last one keys the voices off (src 25AC.c Sound_Cutscene_*). The
 * core raises it when a voice reads the block holding the address
 * (audio_spu.cpp); pc_audio_frame runs the callback after the quarter frame
 * that raised it, on the game thread, as the PS1's interrupt handler would
 * between two driver ticks. As in libspu, disabling it acknowledges it. The
 * enable and address live here (saved with the game's statics) and are handed
 * to the core before each render. ROOD_SPU_IRQ=0 never runs the callback (a
 * diagnostic: streams then loop until the driver's stop command). */
psx_long SpuSetIRQ(psx_long on)
{
    if (on == 0 || on == 3)
        irq_enabled = 0;
    pc_spu_set_irq(irq_enabled, (unsigned int)irq_address);
    if (on == 1 || on == 3)
        irq_enabled = 1;
    pc_spu_set_irq(irq_enabled, (unsigned int)irq_address);
    return on;
}
psx_ulong SpuSetIRQAddr(psx_ulong addr)
{
    if (addr > 0x7FFF8)
        return 0;
    irq_address = addr;
    pc_spu_set_irq(irq_enabled, (unsigned int)irq_address);
    return addr;
}
void pc_spu_irq_get(int* enabled, unsigned int* addr)
{
    *enabled = irq_enabled != 0;
    *addr = (unsigned int)irq_address;
}
callback_t SpuSetIRQCallback(callback_t fn)
{
    callback_t previous = irq_callback;
    irq_callback = fn;
    return previous;
}
/* Root counter 2 is only used to clock the sound driver. */
psx_long SetRCnt(psx_ulong spec, unsigned short target, psx_long mode)
{
    (void)spec;
    (void)target;
    (void)mode;
    return 1;
}
psx_long StartRCnt(psx_ulong spec)
{
    (void)spec;
    return 1;
}

void pc_audio_set_tick(psx_long (*fn)(void)) { driver_tick = fn; }
void pc_audio_enable_tick(int enabled) { tick_enabled = enabled; }

void pc_audio_frame(void)
{
    /* 44100 / 59.94 = 735.735 frames per field: carry the fraction so the
     * output rate matches what the sound card consumes. */
    static unsigned remainder;
    short buffer[200 * 2];
    remainder += 44100 * 1001;
    int total = remainder / 60000;
    remainder %= 60000;
    int chunk[4] = { total / 4, total / 4, total / 4, total - 3 * (total / 4) };
    static int frames;
    if (getenv("ROOD_AUDIO_DEBUG") && ++frames % 120 == 0)
        fprintf(stderr, "[audio] frame %d tick=%p enabled=%d\n", frames, (void*)driver_tick, tick_enabled);
    /* ROOD_SPU_IRQ=0: never serve the interrupt (read each frame: a static
     * would be restored by save states) */
    const char* irq_env = getenv("ROOD_SPU_IRQ");
    int raise = !irq_env || atoi(irq_env) != 0;
    for (int i = 0; i < 4; ++i) {
        if (driver_tick && tick_enabled)
            driver_tick();
        pc_spu_sync_registers();
        pc_spu_set_irq((int)irq_enabled, (unsigned int)irq_address);
        pc_spu_render(buffer, chunk[i]);
        pc_audio_output_push(buffer, chunk[i]);
        if (pc_spu_take_irq() && irq_callback && raise)
            irq_callback();
    }
}

/* libcd CdMix sets the CD-audio attenuation, the same four bytes as libds
 * DsMix; the CD drive (cd_drive.c) applies them to XA audio. */
extern int DsMix(void* attenuation);
int CdMix(void* attenuation) { return DsMix(attenuation); }
/* The driver times its own tick with root counter 2 (GetRCnt before/after)
 * into a load average; on the host the tick is effectively free, so the
 * counter advances by one per read (a one-count tick, never a wrap). */
psx_long GetRCnt(psx_ulong spec)
{
    static psx_long count;
    (void)spec;
    return ++count & 0xffff;
}
psx_long StopRCnt(psx_ulong spec)
{
    (void)spec;
    return 1;
}
