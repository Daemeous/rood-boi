/* Native audio: software SPU (audio_spu.cpp), SDK replacements and the sound
 * driver clock (audio_libspu.c), and host output (audio_output.c). */
#pragma once
#include "psx_types.h" /* psx_long */
void pc_spu_sync_registers(void);
void pc_spu_render(short* out, int frames);
void pc_spu_reset(void);
unsigned long pc_spu_set_transfer_addr(unsigned long addr);
void pc_spu_set_transfer_mode(int io);
unsigned long pc_spu_write(const unsigned char* data, unsigned long size);
unsigned long pc_spu_read(unsigned char* data, unsigned long size);
void pc_spu_init_malloc(int records, unsigned long base);
void pc_spu_set_reverb(int on);
void pc_spu_set_reverb_mode(int type, int depth_left, int depth_right, int mask);
int pc_spu_get_reverb_mode(void);
void pc_spu_set_noise_clock(int clock);
short pc_spu_get_envelope(int voice);
void pc_spu_set_master_volume(short left, short right);
/* CD-audio input (XA from the CD drive), 44.1 kHz stereo frames. */
void pc_spu_push_cd(const short* frames, int count);
int pc_spu_cd_pending(void);
void pc_spu_clear_cd(void);

void pc_audio_output_push(const short* frames, int count);
void pc_audio_output_start(void);
void pc_audio_set_tick(psx_long (*fn)(void));
void pc_audio_enable_tick(int enabled);
void pc_audio_frame(void);
