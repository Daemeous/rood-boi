#pragma once
#include <sys/types.h>

typedef struct {
    int unk0;
    int unk4;
    char unk8;
    char flags;
} vs_main_sfxContext;

int TeardownSound(void);
int InitSound(void);
int Sound_BindAkaoSfxBlob(void* arg0);
int vs_audio_playbackMusicVolume(u_int);
int vs_audio_playbackPackedMusicSampleBankIds(void);
int func_80011F90(void);
u_int vs_audio_playbackStartMusic(void*);
void vs_audio_playbackStopMusic(u_int);
void vs_audio_playbackSaveMusicState(void);
int vs_audio_playbackInvalidateSavedMusicId(void);
u_int vs_audio_playbackCrossfadeMusic(void*, u_int, int);
u_int vs_audio_playbackQueueMusicTransition(void*);
int vs_audio_playbackPlaySoundEffect(u_long, int, int, int);
__UINTPTR_TYPE__ vs_audio_playbackPlaySoundEffectBlob(void*, int, int, int);
void vs_audio_playbackStopSoundEffects(__UINTPTR_TYPE__, int);
__UINTPTR_TYPE__ vs_audio_playbackSoundEffectIdForTag(int);
int func_800123C8(vs_main_sfxContext*);
void vs_sound_setMonoSound(int);
void vs_audio_controlSetMusicBranchThreshold(void*);
void vs_audio_controlFadeSoundEffectVolume(__UINTPTR_TYPE__, int, int, int);
void vs_audio_controlFadeSoundEffectPan(__UINTPTR_TYPE__, int, int, int);
void vs_audio_controlFadeSoundEffectPitch(__UINTPTR_TYPE__, int, int, int);
void vs_audio_controlFadeMusicVolume(u_long, int, int);
void vs_audio_controlSetDiscVolume(int);
void vs_audio_controlFadeDiscVolume(int, int);
void vs_audio_controlFadeMusicTempoScale(int, int);
int vs_audio_controlFadeMusicPitchScale(int, int);
void vs_audio_controlStopAllMusic(void);
void vs_audio_controlStopSceneSoundEffects(void);
void vs_audio_samplesLoadBank(void*, int);
int vs_sound_spuTransferring(void);
int vs_audio_samplesClearMusicBankIds(void);
int vs_audio_samplesLoadMusicBank(void*, int);
int vs_audio_samplesLoadSoundEffectBank(void*, int, int);
int vs_sound_setCdVol(u_int);
void vs_audio_controlStartCutsceneStream(void*, int, int);
void vs_audio_controlStopCutsceneStream(void);
void vs_audio_controlFadeCutsceneVolume(int, int);
