// From
// https://github.com/jdperos/chrono-cross-decomp/blob/ecef6eeac85fe573c08644041d7098beed1a9bf5/include/hw.h

#ifndef _HW_H
#define _HW_H

/* The sound chip's registers, 0x1F801C00..0x1F801DFF: 24 voices of 16 bytes, then the common registers. SPU_REG(offset) is the
 * halfword at that byte offset. The PS1 build writes the chip's own address; the PC build writes pc_spu_regs, a native array the software
 * SPU owns (pc/src/audio_spu.cpp) and reads when it synchronises, so the PC game has no I/O page. */
#ifdef ROOD_PC
extern u_short pc_spu_regs[0x100];
#define SPU_REG(offset) (pc_spu_regs + (offset) / 2)
#else
#define SPU_REG(offset) ((u_short*)(0x1F801C00 + (offset)))
#endif

#define VOICE_00_LEFT_RIGHT SPU_REG(0x00)
#define VOICE_00_ADPCM_SAMPLE_RATE SPU_REG(0x04)
#define VOICE_00_ADPCM_START_ADDR SPU_REG(0x06)
#define VOICE_00_ADPCM_ADSR_LOWER SPU_REG(0x08)
#define VOICE_00_ADPCM_ADSR_UPPER SPU_REG(0x0A)
#define VOICE_00_ADPCM_REPEAT_ADDR SPU_REG(0x0E)
#define SPU_MAIN_VOL_L SPU_REG(0x180)
#define SPU_MAIN_VOL_R SPU_REG(0x182)
#define SPU_VOICE_KEY_ON_LO SPU_REG(0x188)
#define SPU_VOICE_KEY_ON_HI SPU_REG(0x18A)
#define SPU_VOICE_KEY_OFF_LO SPU_REG(0x18C)
#define SPU_VOICE_KEY_OFF_HI SPU_REG(0x18E)
#define SPU_VOICE_CHN_FM_MODE_LO SPU_REG(0x190)
#define SPU_VOICE_CHN_FM_MODE_HI SPU_REG(0x192)
#define SPU_VOICE_CHN_NOISE_MODE_LO SPU_REG(0x194)
#define SPU_VOICE_CHN_NOISE_MODE_HI SPU_REG(0x196)
#define SPU_VOICE_CHN_REVERB_MODE_LO SPU_REG(0x198)
#define SPU_VOICE_CHN_REVERB_MODE_HI SPU_REG(0x19A)
#define SPU_CONTROL SPU_REG(0x1AA)

#define CD_VOL_L SPU_REG(0x1B0)
#define CD_VOL_R SPU_REG(0x1B2)

#endif
