#include "common.h"
#include "146C.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include <abs.h>
#include <memory.h>
#include <rand.h>
#include <stdio.h>
#include <string.h>
#include "pc/src/psx_types.h"
#ifdef ROOD_PC
#include "pc/src/widescreen.h"
#include "pc/src/hud/hud_hook.h"
#endif

/* One entry of a script block's table (vs_field_createRoomEffectObjects builds them on the heap): the effect file's record and
 * the script's cursor. vs_field_executeRoomEffectCommands steps the cursor; the handlers read and write the rest. */
typedef struct {
    int* unk0;
    void* unk4;
    short unk8;
    short unkA;
    int unkC;
    int unk10;
    int unk14;
    int unk18;
    SVECTOR unk1C;
    short unk24;
    short unk26;
    short unk28;
    short unk2A;
    short unk2C;
    short unk2E;
    short unk30;
    short unk32;
} func_80092F74_t;

/* A script command (file data): its length and opcode, then its parameters. */
typedef struct {
    int unk0;
    union {
        u_short u16[2];
        short s16[2];
        int s32;
    } operands[4];
    int unk14;
    int unk18;
} vs_field_RoomEffectCommand;

typedef struct {
    int geometryOffset;
    char needsReset;
    char startsInactive;
    char kind;
    char axis;
    int angularVelocity;
    int acceleration;
    int dampingShift;
    int angle;
    int restAngle;
    int startAngle;
    int initialVelocity;
} vs_field_SwingEffectRecord;

typedef struct {
    char unk0;
    u_char unk1;
    char unk2;
    char unk3;
    vs_field_SwingEffectRecord* unk4;
    vs_field_SwingEffectRecord* unk8;
} vs_field_RoomEffectInstance;

typedef struct {
    short unk0;
    short unk2;
} vs_field_RoomEffectCommandHeader;

typedef struct {
    u_short unk0;
    u_short unk2;
    u_short unk4;
    u_short unk6;
    char unk8;
    char unk9;
    char unkA;
    char unkB;
    short unkC;
    char unkE;
    char unkF;
} func_800962E4_t;

typedef struct {
    u_char unk0[0xC];
    u_char red1;
    u_char green1;
    u_char unkE;
    u_char unkF;
    u_char red2;
    u_char green2;
    u_char unk12;
    u_char unk13;
    u_char red3;
    u_char green3;
    u_char unk16[0xA];
} vs_field_MpdTriangleColorView;

typedef struct {
    u_char unk0[0xC];
    u_char red1;
    u_char green1;
    u_char unkE;
    u_char unkF;
    u_char red2;
    u_char green2;
    u_char unk12;
    u_char unk13;
    u_char red3;
    u_char green3;
    u_char unk16[0xE];
    u_char red4;
    u_char green4;
    u_char unk26;
    u_char unk27;
} vs_field_MpdQuadColorView;

typedef struct {
    int geometryOffset;
    char unk4[3];
    u_char delay;
    u_char baseU;
    u_char baseV;
    u_char frameCount;
    u_char columns;
    u_char frameWidth;
    u_char frameHeight;
    u_char tick;
    u_char frame;
    u_char u[4];
    u_char v[4];
} vs_field_TextureFrameEffect;

typedef struct {
    int geometryOffset;
    char unk4[3];
    u_char delay;
    u_char baseU;
    u_char baseV;
    u_char width;
    u_char height;
    u_char currentU;
    u_char currentV;
    u_char windowWidth;
    u_char windowHeight;
    signed char stepU;
    signed char stepV;
    u_char tick;
    char unk13;
    u_char u[4];
    u_char v[4];
} vs_field_TextureScrollEffect;

typedef struct {
    u_char tick;
    u_char frame;
    u_char frameCount;
    u_char columns;
    char unk4[3];
    u_char delay;
    int scrollStep;
    RECT rect;
} func_80091FE8_t;

typedef struct {
    u_char count;
    u_char unk1;
    u_char active;
    u_char unk3;
} func_80092548_t;

short vs_field_floorHeightAt(int, int);
void func_8008F29C(int, int);
void func_8008F30C(int, int);
void func_8008F440(void);
void func_8008F9A4(int, int);
void func_8008FAC8(void);
void func_8008FDC4(void);
void func_80090434(void);
void func_80090B28(void);
int vs_battle_clamp(short, int, int);
void vs_field_applyWeatherIntensity(void);
void vs_field_createRoomEffectObjects(vs_field_RoomEffectInstance*);
int vs_field_executeRoomEffectCommands(func_80092F74_t*, int, vs_field_RoomEffectInstance*);
int vs_field_startRoomEffect(int);
void vs_field_writeRoomEffectTransform(func_80092F74_t* arg0);
int vs_field_stepRoomEffectGravity(func_80092F74_t* arg0);
int vs_field_stepRoomEffectDampedMotion(func_80092F74_t* arg0);
int func_8009406C(int, int, int, int);
int vs_battle_drawValueLabelCaption(int, int, D_800F1BAC_t*);
void func_80095C18(int, int);
int vs_actors_targetPoint(int, int, SVECTOR*, int);
int func_800A152C(int, int, int);
int vs_actors_anchorPoint(int, int, SVECTOR*, int);
int func_800A1AF8(int, int, u_short*, int);
void* vs_battle_reserveFloatingLabel();
void vs_battle_removeFloatingLabel(int);

extern int (*vs_field_textureEffectHandlers[])(vs_field_SwingEffectRecord*);
extern int (*vs_field_roomEffectOpcodeHandlers[])(func_80092F74_t*, vs_field_RoomEffectCommandHeader*);
extern char vs_field_weatherVolumeByIntensity[];
extern int D_80068F04[][4];
extern u_short D_80068F64[][4];
extern int D_80068F94[];
extern int D_800F2274;
int vs_core_mathAddPackedScreenCoordinates(int, int);
int vs_field_gimmickSubtype(int);
void func_8009651C(SVECTOR*, int);
extern char D_800691E4[];
extern RECT vs_battle_labelCaptionRects[];
typedef struct {
    u_char count;
    u_char labels[2];
    u_char color;
} CombatLabel;
extern CombatLabel vs_battle_statusLabelLayouts[];
extern const CVECTOR vs_battle_statusLabelColors[];
extern const RECT D_80069144[];
extern const RECT D_8006917C[];
extern int D_800E861C[];
extern short vs_battle_lockedLabelHalfHeights[];
extern RECT vs_battle_lockedLabelPieces[];
extern u_short vs_field_roomAnimationTicks;
extern short vs_field_isRoomEffectSoundRestored;
extern short D_800F1D98[];
extern short D_800F1DA4;
extern short D_800F1DA8;
extern short vs_field_backgroundOffsetX;
extern short vs_field_backgroundOffsetY;
extern short vs_field_weatherAmbientSoundId;
extern short vs_field_weatherParticleCount;
extern short vs_field_weatherParticleCapacity;
extern short vs_field_weatherIntensity;
extern char vs_field_roomWeatherKind;
extern char vs_field_isWeatherVariationEnabled;
extern void* vs_field_weatherParticles;
vs_field_RoomEffectInstance vs_field_roomEffectInstances[96]; /* 0x480 bytes of 12-byte entries on the PS1 */

/* room_effects.c: whether slot is running and its record (the PS1 reads were by byte offset). */
int pc_effect_slot(int slot, const unsigned char** record)
{
    const vs_field_RoomEffectInstance* e = &vs_field_roomEffectInstances[slot];
    *record = (const unsigned char*)e->unk4;
    return e->unk0 && e->unk2;
}
extern short vs_field_roomEffectSoundId;
extern char vs_field_textureEffectCount;
extern char vs_field_roomEffectScriptCount;
extern char D_800F2260[];
extern short vs_battle_actorMarkersHidden;
extern short vs_battle_abilityTimingCueFrames;
extern short D_800F2278;
extern short D_800F227A;
extern short vs_battle_placedLabelCount;
extern short vs_battle_floatingLabelCount;
void vs_battle_drawAbilityTimingCue(void);
void func_80094844(short*, D_800F1BAC_t*);
void vs_battle_advanceFloatingLabelQueue(D_800F1BAC_t*);
void func_80094B0C(int, int, D_800F1BAC_t*);
void func_8009506C(int, int, D_800F1BAC_t*);
void func_80095258(int, int, D_800F1BAC_t*);
void vs_battle_drawAbilityTimingResult(int, int, D_800F1BAC_t*);
void vs_battle_drawChainCount(int, int, D_800F1BAC_t*);
void func_8009695C(int, int, D_800F1BAC_t*);
void vs_battle_compactFloatingLabels(int);
extern SVECTOR vs_battle_abilityTimingCuePosition;

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", vs_field_weatherVolumeByIntensity);

void vs_field_setRoomWeather(int arg0);

#ifdef ROOD_PC
/* The game's room weather, including event-script overrides. A room's
 * generated moon must not shine through the overcast during rain. Keep
 * this a render-only query: the original weather update remains unchanged. */
int pc_weather_raining(void)
{
    return vs_battle_roomData.sectionB != NULL && vs_field_roomWeatherKind == 1;
}
#endif

void vs_field_updateRoomWeather(void);

int vs_battle_clamp(short arg0, int arg1, int arg2);

void vs_field_applyWeatherIntensity(void);

void vs_field_setWeatherVariationEnabled(int arg0);

void vs_field_setWeatherIntensity(int arg0);

void vs_field_setWeatherWindAngles(int arg0, int arg1);

void vs_field_releaseWeatherParticles(void);

int vs_field_setWeatherAmbientSound(int arg0);

void vs_field_setRoomBackgroundOffset(int arg0, int arg1);

/* The sky panorama's tiles drawn (their top-left corner, 20.12 screen X): in
 * widescreen (port) as far past the sides as the 16:9 view reaches
 * (pc/src/widescreen.h). */
#ifdef ROOD_PC
#define SKY_LEFT (-0x40000 - (PC_WIDESCREEN_MARGIN << 12))
#define SKY_RIGHT (0x180000 + (PC_WIDESCREEN_MARGIN << 12))
#else
#define SKY_LEFT -0x40000
#define SKY_RIGHT 0x180000
#endif

void vs_field_drawRoomBackgroundTiles(vs_battle_backgroundLayout* arg0, int arg1, int arg2, int arg3);

void vs_field_duckWeatherAmbientSound(int arg0);

void* vs_field_findRoomGeometryGroup(int arg0);

int vs_field_showRoomGeometryGroup(int arg0);

int vs_field_hideRoomGeometryGroup(int arg0);

int vs_field_swapRoomGeometryGroupFlags(int arg0, int arg1);

int vs_field_addRoomGeometryDepthBias(int arg0, int arg1);

int func_80091AC0(int arg0);

void vs_field_updateRoomEffects(void);

int vs_field_advanceRoomTextureFrames(vs_field_TextureFrameEffect* arg0);

int vs_field_scrollRoomTextureCoordinates(vs_field_TextureScrollEffect* arg0);

int vs_field_animateRoomTextureImages(func_80091FE8_t* arg0);

int vs_field_updateRoomRotationEffect(vs_field_SwingEffectRecord* arg0);

int func_80092540(void);

void vs_field_initializeRoomEffects(void);

void vs_field_createRoomEffectObjects(vs_field_RoomEffectInstance* arg0);

int vs_field_startRoomEffect(int arg0);

int vs_field_releaseRoomEffects(void);

int vs_field_executeRoomEffectCommands(func_80092F74_t* arg0, int arg1, vs_field_RoomEffectInstance* arg2);

int func_80092C68(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1);

int vs_field_setRoomEffectVelocity(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1);

int vs_field_setRoomEffectTransform(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1);

void vs_field_writeRoomEffectTransform(func_80092F74_t* arg0);

int func_80092F6C(func_80092F74_t* arg0 __attribute__((unused)), vs_field_RoomEffectCommand* arg1 __attribute__((unused)));

int vs_field_hideRoomEffectObject(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1 __attribute__((unused)));

int vs_field_executeRoomEffectShowGroup(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1);

int vs_field_advanceRoomEffectMotion(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1);

int vs_field_stepRoomEffectGravity(func_80092F74_t* arg0);

int vs_field_stepRoomEffectDampedMotion(func_80092F74_t* arg0);

int vs_field_executeRoomEffectPlaySound(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1);

int vs_field_executeRoomEffectStopSound(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1);

int vs_field_executeRoomEffectStartEffect(func_80092F74_t* arg0 __attribute__((unused)), vs_field_RoomEffectCommand* arg1);

int vs_field_executeRoomEffectHideGroup(func_80092F74_t* unk0 __attribute__((unused)), vs_field_RoomEffectCommand* arg1);

int vs_field_executeRoomEffectStopScript(func_80092F74_t* arg0, vs_field_RoomEffectCommand* arg1);

int func_800936F0(func_80092F74_t* arg0 __attribute__((unused)), vs_field_RoomEffectCommand* arg1 __attribute__((unused)));

int vs_field_toggleRoomEffectUpdates(int arg0);

int vs_field_isRoomEffectActive(int arg0);

void vs_field_finishRoomDoorRotation(int arg0);

void vs_field_finishRoomTiltRotation(int arg0);

void vs_field_duckRoomEffectSound(int arg0);

void vs_field_swapRoomGeometryRedGreen(int arg0);

void vs_field_restoreRoomEffectSoundVolume(void);

void vs_field_restartRoomEffectSound(void);

void vs_battle_resetFloatingLabels(void);

int vs_battle_queueWorldLockedLabel(u_short* arg0);

int func_80093B68(int arg0, int arg1, int arg2, int arg3);

int vs_battle_renderBattleAbilityTimingResult(int arg0);

int func_80093F24(int arg0);

int func_80093FEC(int arg0, int arg1, int arg2, int arg3);

int func_8009406C(int arg0, int arg1, int arg2, int arg3);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", D_80068F04);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", D_80068F64);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", D_80068F94);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", vs_battle_labelCaptionRects);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", vs_battle_statusLabelLayouts);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", vs_battle_statusLabelColors);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", D_80069144);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", D_8006917C);

INCLUDE_RODATA("build/src/BATTLE/BATTLE.PRG/nonmatchings/2842C", D_800691E4);

#ifdef ROOD_PC
/* PC_CLASSIC: a word over an actor (MISS or a combat label) for the classic layer
 * (classic_hook.h): the draw the label loop chose and its inputs, copied before it runs.
 */
#define PC_ACTOR_WORD(miss, call)                                                                       \
    do {                                                                                                \
        pc_classic_actor_word pc_word = { label, miss, label->unkB, label->unkC, label->unkE & 1 };    \
        PC_CLASSIC(PC_CLASSIC_SITE_ACTOR_WORD, &pc_word, screen[0], screen[1], call);                  \
    } while (0)
#endif

void func_800941FC(void);

void func_80094844(short* pos, D_800F1BAC_t* label);

void vs_battle_advanceFloatingLabelQueue(D_800F1BAC_t* arg0);

void func_80094B0C(int x, int y, D_800F1BAC_t* p);

int vs_battle_drawValueLabelCaption(int x, int y, D_800F1BAC_t* p);

void func_8009506C(int arg0, int arg1, D_800F1BAC_t* arg2);

void func_80095258(int x, int y, D_800F1BAC_t* p);

void func_800957D0(int arg0, int arg1, int arg2);

#ifdef ROOD_PC
/* ui_text font (pc/src/ui_text.c): the status words' packets, set as words. */
extern void pc_text_status(const void* first, const void* end) __attribute__((weak));
#define PC_TEXT_STATUS(call)                                                     \
    do {                                                                         \
        void* pc_first = vs_scratch.unk0;                                        \
        call;                                                                    \
        if (pc_text_status)                                                      \
            pc_text_status(pc_first, vs_scratch.unk0);                           \
    } while (0)
#else
#define PC_TEXT_STATUS(call) call
#endif

void vs_battle_drawAbilityTimingResult(int x, int y, D_800F1BAC_t* p);

void vs_battle_drawChainCount(int x, int y, D_800F1BAC_t* p);

void vs_battle_setActorMarkersHidden(int arg0);

void vs_battle_setActorMarker(int arg0, int arg1);

#ifdef ROOD_PC
/* PC_CLASSIC: what func_80095C18 decided, as it decides it, for the classic layer
 * (classic_hook.h). Defined in hud_bridge.c: func_8009627C's Zig port hands it to the site too. */
extern pc_classic_emote pc_emote_record;
#endif

void func_80095C18(int actor, int kind);

int vs_core_mathAddPackedScreenCoordinates(int arg0, int arg1);

void func_8009627C(void);

void* vs_battle_reserveFloatingLabel(void);

void vs_battle_compactFloatingLabels(int arg0);

void vs_battle_removeFloatingLabel(int arg0);

void func_8009651C(SVECTOR* position, int direction);

int func_80096768(int arg0, int arg1, int arg2);

void func_8009695C(int x, int y, D_800F1BAC_t* p);

void func_80096FF0(int arg0);

void vs_battle_drawAbilityTimingCue(void);

void func_800971D4(void);

void vs_battle_suppressAshleyStatusLabels(void);

#ifdef ROOD_PC
#include "pc/src/room_callbacks.inc"

/* Defined here: the slots mix words with a pointer, so they cannot be an LP64 import. */
vs_battle_objectData vs_battle_objectDataSlots[16];
#endif
