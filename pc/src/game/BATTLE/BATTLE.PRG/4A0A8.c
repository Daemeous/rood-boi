#include "common.h"
#include "146C.h"
#include "2842C.h"
#include "30DB0.h"
#include "3A1A0.h"
#include "40564.h"
#include "4A0A8.h"
#include "573B8.h"
#include "5BF94.h"
#include "../../SLUS_010.40/main.h"
#include "../../SLUS_010.40/sfx.h"
#include "../../SLUS_010.40/32154.h"
#include "../../GIM/SCREFF2.PRG/0.h"
#include <stdio.h>
#include <memory.h>
#include <stddef.h>
#include <abs.h>
#include "pc/src/game/BATTLE/BATTLE.PRG/mod/hooks.h"

typedef struct {
    u_char mode;
    u_char ease1;
    u_char ease2;
    u_char ease3;
    u_char time;
    u_char duration;
    u_char follow;
    u_char following;
    int value;
    int delta;
    short angle;
    short angleDelta;
    VECTOR start;
    VECTOR position;
    VECTOR velocity;
    VECTOR aux;
    VECTOR followDelta;
    VECTOR followPosition;
    VECTOR previousCamera;
    VECTOR computed;
    VECTOR current;
    VECTOR target;
} vs_event_CameraTrack;

typedef struct {
    char hasChanged;
    char easingMode;
    short angle;
    short startAngle;
    short deltaAngle;
    short elapsedFrames;
    short durationFrames;
} vs_event_AngleTween;

typedef struct {
    union {
        u_char u8[2];
        short s16;
    } unk0;
    short displacement;
    short unk4;
    short unk6;
    char unk8;
    char unk9;
    char axisMask;
    char unkB;
} vs_event_ShakeOscillator;

typedef struct {
    char hasChanged;
    char easingMode;
    short value;
    short startValue;
    short deltaValue;
    char elapsedFrames;
    char durationFrames;
    char unkA;
    char unkB;
} vs_event_LensTween;

typedef struct {
    char unk0;
    char easingMode;
    short currentValue0;
    short currentValue1;
    short currentValue2;
    short unk8;
    short startValue0;
    short startValue1;
    short startValue2;
    short unk10;
    short deltaValue0;
    short deltaValue1;
    short deltaValue2;
    short unk18;
    short unk1A;
    short elapsedFrames;
    short durationFrames;
} vs_event_TripleTween;

typedef struct {
    vs_event_CameraTrack lookAt;
    vs_event_CameraTrack eye;
    vs_event_AngleTween unk168;
    vs_event_AngleTween unk174;
    vs_event_LensTween projectionDistance;
    vs_event_LensTween nearClip;
    vs_event_LensTween farClip;
    vs_event_ShakeOscillator unk1A4;
    vs_event_ShakeOscillator unk1B0;
    short unk1BC;
    short shakeEyeOffsetX;
    short unk1C0;
    short unk1C2;
    short unk1C4;
    short shakeLookAtOffsetX;
    short unk1C8;
    short unk1CA;
    int unk1CC[7];
    u_short originActorId;
    u_short unk1EA;
    VECTOR originOffset;
    vs_event_TripleTween screenScale;
    vs_event_TripleTween screenTint;
    vs_event_TripleTween screenTranslation;
    vs_event_TripleTween unk25C;
    VECTOR lastTerrainPosition;
} vs_event_CameraState;

typedef struct {
    u_int unk0_0 : 1;
    u_int unk0_1 : 1;
    u_int unk0_2 : 1;
    u_int unk0_3 : 3;
    u_int unk0_6 : 18;
    u_int unk0_24 : 8;
    int unk4;
    short unk8;
    short unkA;
    short unkC;
    char rest[sizeof(gim_t) - 0x10]; /* the rest of the 5BF94.h gim_t this views (three of them in a row) */
} D_800EB9BC_t;
_Static_assert(sizeof(D_800EB9BC_t) == sizeof(gim_t), "D_800EB9BC_t != gim_t");

typedef struct {
    char unk0;
    char easingMode;
    char elapsedFrames;
    char durationFrames;
    short startX;
    short startY;
    short deltaX;
    short deltaY;
    short startScaleFixed12;
    short deltaScaleFixed12;
    short startBrightness;
    short deltaBrightness;
} vs_event_SplashTween;

typedef struct {
    char unk0;
    char unk1;
    char elapsedFrames;
    char unk3;
    short windDirection;
    short unk6;
    short unk8;
    short unkA;
    short unkC;
    short unkE;
    short intensity;
    short unk12;
    short unk14;
} vs_event_WeatherTween;

typedef struct {
    int unk0;
} vs_event_EasedFractionScratch;

typedef struct {
    char unk0;
    char unk1;
    char unk2[22];
} vs_event_ActorSelection;

typedef struct {
    int x : 12;
    int y : 12;
    u_int alignment : 2;
    u_int flags : 4;
} textOffset;

typedef struct {
    textOffset offset;
    short x;
    short y;
    int distance;
} textPlacement;

typedef struct {
    int value;
    int distance;
    char unk8[8];
    VECTOR v1;
    VECTOR v2;
    VECTOR v3;
    char unk40[16];
    SVECTOR rotation;
    SVECTOR rotation2;
    char unk60[16];
    MATRIX matrix;
} cameraScratch;

typedef struct {
    int distance;
    int steps;
    char unk8[8];
    VECTOR delta;
    VECTOR candidate;
    VECTOR direction;
    char unk40[0x30];
    MATRIX matrix;
} cameraMotionScratch;

typedef struct {
    char unk0[0x50];
    SVECTOR a;
    SVECTOR b;
    SVECTOR c;
} cameraShakeScratch;

typedef struct {
    u_char low;
    u_char high;
} scriptBytePair;

void vs_render_readFramebufferEffectScale(int*);
void vs_render_readFramebufferEffectTint(P_CODE*);
void vs_render_readFramebufferEffectOffset(vs_render_FramebufferEffectTranslation*);
short vs_field_floorHeightAt(int, int);
int vs_field_setRoomWeather(int);
void vs_actors_anchorPoint(int, int, SVECTOR*, int);
void func_800AACDC(void);
int vs_event_calcDialogAnchorPlacement(
    SVECTOR* origin, u_char* script, textPlacement* result, u_char direction);
int func_800B9C58(u_char*, short);
void vs_event_buildScriptActorSelection(u_short, vs_event_ActorSelection*);
u_short vs_event_nextScriptSelectedActor(vs_event_ActorSelection* arg0);
int vs_event_actorDialogAnchor(int arg0, SVECTOR* arg1);
void func_800BBE10(u_short);
void func_800BBE04(int arg0);
void func_800BBE94(void);
void func_800BCA8C(vs_event_CameraTrack*, vs_event_CameraTrack*);
void func_800BCFB4(vs_event_CameraTrack*);
void func_800BC1CC(short, int);
int func_800BD2B8(MATRIX*);
void func_800BD57C(vs_event_AngleTween* arg0);
int func_800BD610(void);
int func_800BDBB4(vs_event_TripleTween* arg0);
void func_800BDF6C(vs_event_LensTween* arg0);
void func_800BE180(void);
void func_800BE36C(int, int);
void func_800BE3A0(void);
void func_800BE3D0(char arg0);
void func_800BE5A4(short arg0);
VECTOR* _vectorAdd(VECTOR* arg0, VECTOR* arg1, VECTOR* arg2);
VECTOR* _vectorSubtract(VECTOR* arg0, VECTOR* arg1, VECTOR* arg2);
VECTOR* _setVectorImm(VECTOR*, u_char*);
VECTOR* _vectorFixedPointMult(VECTOR*, VECTOR*, int);
void _vecToRotMatrix(VECTOR*, MATRIX*);
int _atan2FixedPoint(int arg0, int arg1);
static int _fixedPointMult(int a, int b);
void vs_event_releaseScriptActorControl();
short vs_battle_getShort(u_char*);
int vs_event_evaluateInterpolationCurve(int, int, int);
int vs_battle_setTextBox(int, char*);
void func_800CB79C(void);
int vs_battle_dismissTextBox(int);
void vs_effects_requeueTrack(int);
void func_800F9BC0(int, int);
int vs_main_getcurrentMusicSlot(void);
static int _vectorMagnitude(VECTOR*);
static VECTOR* _copyVector(VECTOR* arg0, VECTOR* arg1);
static VECTOR* _sVectorToFixedPointVector(VECTOR* arg0, SVECTOR* arg1);
static void _toNormalIntegerVector(VECTOR*, VECTOR*);

extern int D_800E9B34[];
extern int D_800E9B4C[];
extern char D_800E9B58[];
extern char vs_event_skipPaletteResetCommand[8]; /* the command the skip runs three times (byte 1 is set) */
extern char D_800E9BE8[4]; /* the marker command the skip walks to (memcmp of 4 bytes) */
extern u_char D_800E9BEC[4]; /* the four placement directions (a loop to 4) */
extern u_char vs_event_menuTransitionModes[4]; /* script[1] 10..12 select 0..2 */
extern short vs_event_sceneTimerModes[2]; /* by a script byte: index-checked (pc_event_index_ok); the disc uses no timer start (event_table_scan.py) */
extern short vs_event_sceneTimerResetScopes[4];
extern void pc_table_index_refused(const char*, long, long, const char*, unsigned long, unsigned long);
/* The event block being run, set by vs_event_initializeEventScriptData: where an offset in a log is counted from. */
extern u_char* pc_event_scriptBase;
/* An index a script supplies for a table declared with the extent the data needs: 1 when it is inside, else it is logged once (the event and the
 * offset of the opcode) and the caller refuses the access: no silent clamp to a wrong entry. */
int pc_event_index_ok(const char* table, int length, int index, const u_char* at);
void pc_event_setBase(u_char* data);
extern void vs_event_specialSubroutineScript;
extern short D_800E9C00[];
extern u_char vs_event_fieldCameraReturnCommands[];
extern short vs_event_orbitReturnHeightOffsets[];
extern u_char vs_event_orbitReturnFirstCommand;
extern u_char vs_event_orbitReturnSecondCommand;
extern u_char vs_event_opcodeSizes[256]; /* the size of each event opcode, by opcode byte */
extern u_char* vs_event_textBoxFrameStyles[16]; /* shapes 0..8 are static, 9..15 are set from an event script block */
extern D_800EB9BC_t* vs_render_gimLayers;
extern int D_800F19C8;
extern u_short vs_event_scanlineOverlayFrame;
extern u_short vs_event_customEasingCurves[];
extern vs_event_SplashTween vs_event_splashTweens[];
extern unsigned char vs_event_scriptControlledActors[17];
extern vs_event_WeatherTween vs_event_weatherTween;
extern char vs_event_weatherTweenDuration;
extern char vs_event_skipNextFieldCameraRestore;
extern vs_event_CameraState* vs_event_camera;
extern void* _mpdClearedSection;
extern u_short* vs_event_dialogStrings;
extern char vs_event_cachedFlagValue;
extern short vs_event_isFieldCameraRestoreScripted;
extern char vs_event_isStartConfirmed;
extern u_char vs_event_isEffectWaitPending;
extern short* vs_event_eventFileDiscRequest;
extern char* vs_event_vibrationPatterns[8];
extern u_short vs_event_eventSource;
extern char vs_event_isScanlineOverlayEnabled;
extern void* _mpdRoomDoorSection;
extern char vs_event_isMenuTransitionActive;
extern char vs_event_loopCounters[];
extern short vs_event_isMainThreadFinished;
extern void* vs_event_chainedEventBuffer;
extern char vs_event_playbackPhase;
extern u_char* vs_event_threadCursors[];
extern short vs_event_suspendedAmbientSoundId;
extern char vs_event_weaponTransitionPending;
extern short _mpdScriptSection;
extern short vs_event_isChainedEventLoaded;
extern char _textBoxSelectionPending;
extern u_char* vs_event_subroutineReturnCursor;
extern char vs_event_musicSlot;
extern char D_800F4C6A;

#ifndef ROOD_PC
__asm__(".set push;"
        ".set noreorder;"
        "glabel vs_actors_beginTileJump;"
        "la         $v0, vs_actors_actorRecords;"
        "sll        $v1, $a0, 2;"
        "addu       $v0, $v1;"
        "lw         $v0, 0x0($v0);"
        "nop;"
        "lb         $v1, 0xA($v0);"
        "lb         $t1, 0x5B0($v0);"
        "and        $t0, $v1, 0x40;"
        "bnez       $t0, .L800B6424;"
        "and        $t0, $v1, 0x20;"
        "bnez       $t0, .L800B6454;"
        "and        $t1, 0xFC;"
        "or         $t1, 0x1;"
        "sb         $t1, 0x5B0($v0);"
        ".L800B63E8:"
        "addu       $sp, -0x4;"
        "sw         $ra, 0x0($sp);"
        "jal        vs_actors_beginActorJumpToTile;"
        "addu       $a0, $v0, $zero;"
        "lw         $ra, 0x0($sp);"
        "addu       $sp, 0x4;"
        "jr         $ra;"
        "endlabel vs_actors_beginTileJump;"
        ".set pop");

__asm__(".set push;"
        ".set reorder;"
        "glabel vs_actors_beginAirborneTileJump;"
        "la         $v0, vs_actors_actorRecords;"
        "sll        $v1, $a0, 2;"
        "addu       $v0, $v1;"
        "lw         $v0, 0x0($v0);"
        "lb         $v1, 0xA($v0);"
        ".L800B6424:"
        "and        $v1, 0x9F;"
        "or         $v1, 0x40;"
        "sb         $v1, 0xA($v0);"
        ".set nomove;"
        "j          .L800B63E8;"
        "endlabel vs_actors_beginAirborneTileJump;"
        ".set pop");

void vs_actors_beginNavigationJump(int arg0, int* arg1, int arg2, int arg3)
{
    __asm__("la         $v0, vs_actors_actorRecords;"
            "sll        $v1, $a0, 2;"
            "addu       $v0, $v1;"
            "lw         $v0, 0x0($v0);"
            "nop;"
            "lb         $v1, 0xA($v0);"
            ".L800B6454:"
            "addu       $sp, -0x14;"
            "sw         $v0, 0x0($sp);"
            "sw         $a1, 0x4($sp);"
            "sw         $a2, 0x8($sp);"
            "sw         $a3, 0xC($sp);"
            "sw         $ra, 0x10($sp);"
            "or         $v1, 0x60;"
            "sb         $v1, 0xA($v0);"
            "or         $a1, $zero, 0x1F;"
            "addu       $a2, $zero, $zero;"
            "jal        vs_actors_playActorAnimation;"
            "or         $a3, $zero, 0x8;"
            "lw         $a0, 0x0($sp);"
            "lw         $a1, 0x4($sp);"
            "lw         $a2, 0x8($sp);"
            "jal        vs_actors_beginActorJumpToTile;"
            "lw         $a3, 0xC($sp);"
            "lw         $ra, 0x10($sp);"
            "addu       $sp, 0x14;");
}

__asm__(".set push;"
        ".set noreorder;"
        "glabel vs_actors_setDirectMovement;"
        "addu       $sp, -0x4;"
        "sw         $ra, 0x0($sp);"
        "jal        func_800B64CC;"
        "addu       $a3, $a1, $zero;"
        "or         $v1, $zero, 0x0;"
        "sb         $v1, 0x1848($v0);"
        "lw         $ra, 0x0($sp);"
        "addu       $sp, 0x4;"
        "j          $ra;"
        "endlabel vs_actors_setDirectMovement;"
        ".set pop");

void func_800B64CC(int arg0, int arg1, int arg2, int arg3)
{
    __asm__("la         $v0, vs_actors_actorRecords;"
            "sll        $v1, %0, 2;"
            "addu       $v0, $v1;"
            "lw         $v0, 0x0($v0);"
            "or         $v1, $zero, 0x1;"
            "sll        $t0, %3, 16;"
            "or         $t0, %1;"
            "sw         $t0, 0x184C($v0);"
            "sw         %2, 0x1850($v0);"
            ".L800B64F4:"
            "sw         $v1, 0x1848($v0);"
        :
        : "r"(arg0), "r"(arg1), "r"(arg2), "r"(arg3));
}

__asm__("glabel vs_actors_queueGroundOffset;"
        "la         $v0, vs_actors_actorRecords;"
        "sll        $v1, $a0, 2;"
        "addu       $v0, $v1;"
        "lw         $v0, 0x0($v0);"
        "lw         $t2, 0x0($a1);"
        "lw         $t3, 0x4($a1);"
        "sw         $t2, 0x1858($v0);"
        "sw         $t3, 0x185C($v0);"
        "lb         $t1, 0xA($v0);"
        "j          .L800B64F4;"
        "or         $v1, $zero, 0x2;"
        "endlabel vs_actors_queueGroundOffset;");

__asm__("glabel vs_actors_queueMovementVector;"
        "ori     $t4, $zero, 0x4;"
        "j       .L800B6540;"
        "addu    $a3, $a2, $zero;"
        "endlabel vs_actors_queueMovementVector;");

__asm__(".set push;"
        ".set noreorder;"
        "glabel vs_actors_queueFlightMovement;"
        "ori        $t4, $zero, 0x3;"
        ".L800B6540:"
        "la         $v0, vs_actors_actorRecords;"
        "sll        $v1, $a0, 2;"
        "addu       $v0, $v1;"
        "lw         $v0, 0x0($v0);"
        "lw         $t2, 0x0($a1);"
        "lw         $t3, 0x4($a1);"
        "sw         $a2, 0x1850($v0);"
        "lb         $t1, 0xA($v0);"
        "sh         $a3, 0x184E($v0);"
        "sw         $t2, 0x1858($v0);"
        "sw         $t3, 0x185C($v0);"
        "or         $t1, 0x20;"
        "sb         $t1, 0xA($v0);"
        "sw         $t4, 0x1848($v0);"
        "j          $ra;"
        "endlabel vs_actors_queueFlightMovement;"
        ".set pop;");

__asm__(".set push;"
        ".set noreorder;"
        "glabel vs_actors_beginTeleport;"
        "la         $v0, vs_actors_actorRecords;"
        "sll        $v1, $a0, 2;"
        "addu       $v0, $v1;"
        "lw         $v0, 0x0($v0);"
        "lw         $t2, 0x0($a1);"
        "lb         $t1, 0xB($v0);"
        "sll        $t0, $a2, 16;"
        "and        $t1, 0xF;"
        "bnez       $t1, .L800B65E4;"
        "or         $t1, 0x1;"
        "lbu        $a0, 0x17FD($v0);"
        "sh         $zero, 0x1A($v0);"
        "sw         $t2, 0x5EC($v0);"
        "sw         $t0, 0x5F0($v0);"
        "sb         $t1, 0xB($v0);"
        "slt        $t3, $a0, 0x2;"
        "bnez       $t3, .L800B65E4;"
        ".nop;"
        "addu       $sp, -0x4;"
        "sw         $ra, 0x0($sp);"
        "jal        vs_actors_beginTeleport;"
        ".nop;"
        "lw         $ra, 0x0($sp);"
        "addu       $sp, 0x4;"
        ".L800B65E4:"
        "jr         $ra;"
        "endlabel vs_actors_beginTeleport;"
        ".set pop;");

__asm__(".set push;"
        ".set noreorder;"
        "glabel func_800B65E8;"
        "ori     $t1,$zero,0x10;"
        "j       .L800B65F8;"
        ".nop;"
        "endlabel func_800B65E8;"
        ".set pop;");

__asm__(".set push;"
        ".set noreorder;"
        "glabel func_800B65F4;"
        "ori        $t1, $zero, 0x40;"
        ".L800B65F8:"
        "la         $v0, vs_actors_actorRecords;"
        "sll        $v1, $a0, 2;"
        "addu       $v0, $v0, $v1;"
        "lw         $v0, 0x0($v0);"
        "lw         $t2, 0x0($a1);"
        "lw         $t3, 0x4($a1);"
        "lb         $t4, 0xB($v0);"
        "sw         $t2, 0x5EC($v0);"
        "sw         $t3, 0x5F0($v0);"
        "sh         $a2, 0x5F2($v0);"
        "and        $t4, 0x8F;"
        "or         $t1, $t4, $t1;"
        "sb         $t1, 0xB($v0);"
        "j          $ra;"
        "endlabel func_800B65F4;"
        ".set pop;");

void vs_actors_turnNavigationActor(int arg0, int arg1)
{
    __asm__("la         $v0, vs_actors_actorRecords;"
            "sll        $v1, $a0, 2;"
            "addu       $v0, $v1;"
            "lw         $v0, 0x0($v0);"
            "addu       $sp, -0xC;"
            "lhu        $t0, 0x63C($v0);"
            "sw         $ra, 0x0($sp);"
            "or         $a2, $zero, 0x80;"
            "sltu       $t0, 0x80;"
            "beqz       $t0, .L800B6668;"
            "sw         $a0, 0x4($sp);"
            "or         $a2, $zero, 0x20;"
            ".L800B6668:"
            "jal        vs_actors_turnActorToAtRate;"
            "sw         $a1, 0x8($sp);"
            ".L800B6670:"
            "lw         $a0, 0x4($sp);"
            "jal        vs_actors_playActorTurnAnimation;"
            "lw         $a1, 0x8($sp);"
            "lw         $ra, 0x0($sp);"
            "addu       $sp, 0xC;");
}

__asm__(".set push;"
        ".set reorder;"
        "glabel vs_actors_turnActorAtSpeed;"
        "addu       $sp, -0xC;"
        "sw         $ra, 0x0($sp);"
        "sw         $a0, 0x4($sp);"
        "sw         $a1, 0x8($sp);"
        "jal        vs_actors_turnActorToAtAngularSpeed;"
        "j          .L800B6670;"
        "endlabel vs_actors_turnActorAtSpeed;"
        ".set pop;");

#endif

int vs_battle_invalidOpcode(u_char* arg0, short arg1);

int func_800B66E4(u_char* arg0, short arg1);

int vs_event_scriptWaitForEventFile(u_char* arg0, short arg1);

int func_800B6724(u_char* arg0, short arg1);

int vs_event_scriptBeginSkippableCutscene(u_char* arg0, short arg1);

void vs_event_finishSkippedCutscene(u_char* arg0);

int vs_event_scriptEndCutsceneAndJump(u_char* arg0, short arg1);

int vs_event_scriptEndCutscene(u_char* arg0, short arg1);

int vs_battle_script_initTextBox(u_char* script, short arg1 __attribute__((unused)));

int vs_battle_script_showTextBox(u_char* script, short arg1 __attribute__((unused)));

int vs_battle_script_dismissTextBox(u_char* arg0, short arg1);

textOffset func_800CCE10(int, int, int, int);

int vs_event_calcDialogAnchorPlacement( SVECTOR* origin, u_char* script, textPlacement* result, u_char direction);

int vs_event_scriptInitActorDialogBox(u_char* script, short arg1 __attribute__((unused)));

int vs_event_scriptWaitForDialogDismissal(u_char* arg0, short arg1);

int vs_battle_script_buttonPressed(u_char* arg0, short arg1);

int vs_battle_script_loadGim(u_char* arg0, short arg1);

int func_800B7014(u_char* arg0, short arg1);

int func_800B70E8(u_char* arg0, short arg1);

int func_800B71C4(u_char* arg0, short arg1);

int func_800B71F4(u_char* arg0, short arg1);

int vs_battle_script_setStringContextInt(u_char* arg0, short arg1);

int vs_event_scriptSetDialogTransitionDuration(u_char* arg0, short arg1);

int vs_battle_script_selectTextBox(u_char* script, short arg1 __attribute__((unused)));

int func_800B7428(u_char* arg0, short arg1);

int vs_event_scriptStoreActorTile(u_char* arg0, short arg1);

int vs_event_scriptSpawnActor(u_char* arg0, short arg1);

int vs_event_scriptNavigateActorToTile(u_char* arg0, short arg1);

int vs_event_scriptPlayActorAnimation(u_char* arg0);

int vs_event_scriptSetActorGroupFlags(u_char* arg0, short arg1);

int vs_event_scriptSetActorSoundMix(u_char* arg0, short arg1);

int vs_event_scriptWalkActorToTile(u_char* arg0, short arg1);

void vs_actors_moveActorToPositionAtRate(int, SVECTOR*, int, int);

int vs_event_scriptWalkActorToWorldPos(u_char* arg0, short arg1);

int vs_event_scriptSlideActorToTile(u_char* arg0, short arg1);

int vs_event_scriptSlideActorToWorldPos(u_char* arg0, short arg1);

int vs_event_scriptSetActorAttachment(u_char* arg0, short arg1);

int vs_event_scriptNavigateActorToWorldPos(u_char* arg0, short arg1);

int vs_event_scriptTurnActorToFacing(u_char* arg0, short arg1);

int vs_event_scriptFaceOtherActor(u_char* arg0, short arg1);

int vs_event_scriptFaceTile(u_char* arg0, short arg1);

int vs_event_scriptScaleActor(u_char* arg0, short arg1);

int vs_event_scriptRemoveActorGroup(u_char* arg0, short arg1);

int vs_event_scriptWaitForActorFlags(u_char* arg0);

int func_800B81A0(u_char* arg0, short arg1);

int vs_event_scriptQueueActorAnimation(u_char* arg0);

int vs_event_scriptRotateActorAxes(u_char* arg0, short arg1);

int func_800B8360(u_char* arg0, short arg1);

int vs_event_scriptPlayActorAnimationAtFrame(u_char* arg0);

int vs_event_scriptQueueActorAnimationAtFrame(u_char* arg0);

int vs_event_scriptSetActorIdleAnimation(u_char* arg0, short arg1);

int func_800B8510(u_char* arg0, short arg1);

int func_800B8590(u_char* arg0, short arg1);

int vs_event_scriptLoadObjectModel(u_char* arg0, short arg1);

int vs_event_scriptWaitForObjectLoad(u_char* arg0, short arg1);

int vs_event_scriptReleaseObjectModel(u_char* arg0, short arg1);

int vs_event_scriptSelectObjectTextureBank(u_char* arg0, short arg1);

int func_800B8788(u_char* arg0, short arg1);

int vs_event_scriptSetActorWeaponDrawn(u_char* arg0, short arg1);

int vs_battle_script_jumpFwdIfFlag(u_char* arg0, short arg1);

int vs_battle_script_jumpFwdIfFlags(u_char* arg0, short arg1);

int vs_battle_script_jumpFwd(u_char* arg0, short arg1);

int vs_battle_script_jumpFwd2(u_char* arg0, short arg1);

int vs_battle_script_setFlagImmContext(u_char* arg0, short arg1);

int vs_battle_script_jumpFwdIfFlagContext(u_char* arg0, short arg1);

int vs_event_scriptReturnToSavedAddress(u_char* arg0, short arg1);

int vs_event_scriptSetLoopCounter(u_char* arg0, short arg1);

int vs_battle_script_jumpBackIf(u_char* arg0, short arg1);

int vs_battle_script_jumpFwd3(u_char* arg0, short arg1);

int vs_battle_script_jumpFwd4(u_char* arg0, short arg1);

int vs_battle_script_setFlagImm(u_char* arg0, short arg1);

int vs_battle_script_setFlag(u_char* arg0, short arg1);

int vs_battle_script_setFlagIfItemInInventory(u_char* arg0, short arg1);

int func_800B91C4(u_char* arg0, short arg1);

int func_800B91F0(u_char* arg0, short arg1);

int vs_event_scriptSetNpcUpdatesEnabled(u_char* arg0, short arg1);

int vs_battle_script_setFlagIfButton(u_char* arg0, short arg1);

int vs_event_scriptRestoreActorVitals(u_char* arg0, short arg1);

int func_800B92D0(u_char* arg0, short arg1);

int func_800B92F4(u_char* script, short arg1);

int vs_event_scriptConfigureTimer(u_char* arg0, short arg1);

int vs_event_scriptSetDialogSpeaker(u_char* arg0, short arg1);

int vs_event_scriptSetActorEquipmentModels(u_char* arg0, short arg1);

int vs_event_scriptSetActorYawOscillation(u_char* arg0, short arg1);

int vs_event_scriptWaitForButtonOrTimeout(u_char* arg0, short arg1);

int vs_event_scriptConfigureEasingCurve(u_char* arg0, short arg1);

int func_800B9770(u_char* arg0, short arg1);

int func_800B979C(u_char* arg0, short arg1);

int func_800B97E4(u_char* arg0, short arg1);

int vs_event_scriptClearAshleyStatusEffects(u_char* arg0, short arg1);

int func_800B9818(u_char* arg0, short arg1);

int func_800B983C(u_char* arg0, short arg1);

int func_800B9860(u_char* arg0, short arg1);

int func_800B988C(u_char* arg0, short arg1);

int func_800B98C0(u_char* arg0, short arg1);

int func_800B9A40(u_char* arg0, short arg1);

int func_800B9A94(u_char* arg0);

int func_800B9AC4(u_char* arg0, short arg1);

int func_800B9AE8(u_char* arg0, short arg1);

int func_800B9B18(u_char* arg0, short arg1);

int func_800B9BF8(void);

int func_800B9C18(u_char* arg0, short arg1);

int func_800B9C58(u_char* arg0, short arg1);

int func_800B9D74(u_char* arg0, short arg1);

int vs_event_scriptSetAshleyBattleMode(u_char* arg0, short arg1);

int func_800B9F28(u_char* arg0, short arg1);

int func_800B9F4C(u_char* arg0, short arg1);

int func_800B9F70(u_char* arg0, short arg1);

int func_800B9F90(u_char* arg0, short arg1);

int func_800B9FC0(u_char* arg0, short arg1);

int func_800BA0E4(u_char* arg0, short arg1);

int func_800BA108(u_char* arg0, short arg1);

int func_800BA194(u_char* arg0, short arg1);

int func_800BA218(u_char* arg0, short arg1);

int func_800BA264(u_char* arg0, short arg1);

int func_800BA288(u_char* arg0, short arg1);

int func_800BA2B4(u_char* arg0, short arg1);

int vs_event_scriptPlaySoundCommand(u_char* arg0);

int vs_event_commandPlayActorSfx(u_char* arg0, short arg1);

int vs_event_commandSlideSfxVolume(u_char* arg0, short arg1);

int vs_event_commandStopAllSfx(u_char* arg0, short arg1);

int vs_event_commandStopSfx(u_char* arg0, short arg1);

int vs_battle_script_loadSfxSlot(u_char* arg0, short arg1);

int vs_battle_script_freeSfxSlot(u_char* arg0, short arg1);

int vs_battle_script_freeSfx(u_char* arg0, short arg1);

int vs_battle_script_setCurrentSfx(u_char* arg0, short arg1);

int vs_battle_script_loadSoundFile(u_char* arg0, short arg1);

int vs_event_commandCrossfadeMusic(u_char* arg0);

int vs_battle_script_loadMusicSlot(u_char* arg0, short arg1);

int vs_battle_script_freeMusic(u_char* arg0, short arg1);

int vs_event_commandPlayMusic(u_char* arg0, short arg1);

int vs_event_commandSlideMusicVolume(u_char* arg0, short arg1);

int vs_event_commandSlideMusicTempo(u_char* arg0, short arg1);

int vs_event_commandSlideMusicPitch(u_char* arg0, short arg1);

int vs_event_commandStopMusic(u_char* arg0, short arg1);

int func_800BA6E0(u_char* arg0, short arg1);

int vs_event_commandPushMusicState(u_char* arg0, short arg1);

int vs_battle_script_clearMusicLoadSlot(u_char* arg0, short arg1);

int vs_battle_script_loadSoundFile2(u_char* arg0, short arg1);

int vs_battle_script_processSoundQueue(u_char* arg0, short arg1);

int vs_event_commandWaitMusicTimer(u_char* arg0, short arg1);

int func_800BA800(u_char* arg0, short arg1);

int func_800BA848(u_char* arg0, short arg1);

int func_800BA890(u_char* arg0, short arg1);

int vs_event_commandPlayActorEffect(u_char* arg0, short arg1);

int func_800BA90C(u_char* arg0, short arg1);

int vs_event_commandWaitEffectState(u_char* arg0, short arg1);

int vs_event_commandYieldEffectRelease(u_char* arg0, short arg1);

int vs_event_commandWaitEffectContinuation(u_char* arg0, short arg1);

int vs_event_commandWaitEffectRelease(u_char* arg0, short arg1);

int vs_event_commandMarkEffectStageReady(u_char* arg0, short arg1);

int vs_event_commandSetMusicJumpThreshold(u_char* arg0, short arg1);

int vs_event_commandPlayVibrationPattern(u_char* arg0);

int vs_event_commandSetActorTrail(u_char* arg0, short arg1);

int vs_event_commandSetEnteredDoor(u_char* arg0, short arg1);

int vs_event_commandStoreActorRangeBearing(u_char* arg0, short arg1);

int func_800BAD78(u_char* arg0, short arg1);

int vs_event_commandAwardMiscLoot(u_char* arg0);

int vs_event_commandConsumeMiscItem(u_char* arg0, short arg1);

int vs_event_commandStoreActorDistance(u_char* arg0, short arg1);

void func_800BAF6C(short arg0);

int vs_battle_script_specialOp(u_char* arg0, short arg1);

int vs_event_commandWaitScreenEffectLoad(u_char* arg0, short arg1);

int func_800BB2B8(u_char* arg0, short arg1);

int vs_event_commandLoadScreenEffects(u_char* arg0, short arg1);

int func_800BB304(u_char* arg0, short arg1);

int vs_event_commandFreeScreenEffects(u_char* arg0, short arg1);

int func_800BB33C(u_char* arg0, short arg1);

int func_800BB35C(u_char* arg0, short arg1);

int func_800BB38C(u_char* arg0, short arg1);

int func_800BB3BC(u_char* arg0, short arg1);

int func_800BB41C(u_char* arg0, short arg1);

int func_800BB450(u_char* arg0, short arg1);

int vs_event_commandWaitFrames(u_char* arg0, short arg1);

int vs_event_commandSetTickSpeed(u_char* arg0, short arg1);

int vs_event_commandSetScanlineOverlay(u_char* arg0, short arg1);

int vs_event_commandWaitScriptContext(u_char* arg0, short arg1);

int vs_event_commandStartScriptContext(u_char* arg0, short arg1);

int vs_event_commandStopScriptContext(u_char* arg0, short arg1);

int vs_event_commandLoadNextEvent(u_char* arg0, short arg1);

int vs_event_commandFinishEvent(u_char* arg0, short arg1);

int vs_battle_script_playMenuSelectSfx(u_char* arg0, short arg1);

void vs_event_buildScriptActorSelection(u_short arg0, vs_event_ActorSelection* arg1);

u_short vs_event_nextScriptSelectedActor(vs_event_ActorSelection* arg0);

int vs_event_actorDialogAnchor(int arg0, SVECTOR* arg1);

void vs_event_drawEventScanlineOverlay(void);

void func_800BB9B0(void);

void func_800BBAFC(void);

extern P_CODE vs_event_defaultScreenTint;
extern vs_render_FramebufferEffectTranslation D_800E9C0C;

void func_800BBBE8(void);

void func_800BBDDC(void);

void func_800BBE04(int arg0);

void func_800BBE10(u_short arg0);

void func_800BBE94(void);

void vs_battle_applyCameraState(void);

void func_800BC1CC(short arg0, int arg1);

int func_800BC2E8(u_char* script, short unused);

void func_800BC9E0(void);

void func_800BCA8C(vs_event_CameraTrack* c, vs_event_CameraTrack* other);

void func_800BCFB4(vs_event_CameraTrack* c);

int func_800BD2B8(MATRIX* arg0);

int func_800BD444(u_char* arg0, short arg1);

void func_800BD57C(vs_event_AngleTween* arg0);

int func_800BD610(void);

int func_800BD6C4(u_char* script, short arg1);

void func_800BDAB4(void);

int func_800BDBB4(vs_event_TripleTween* arg0);

int func_800BDC9C(u_char* script, short arg1);

void func_800BDF6C(vs_event_LensTween* arg0);

int func_800BE01C(vs_event_ShakeOscillator* arg0);

void func_800BE180(void);

void func_800BE36C(int arg0, int arg1);

void func_800BE3A0(void);

void func_800BE3D0(char arg0);

void func_800BE53C(vs_event_CameraTrack* arg0);

void func_800BE5A4(short arg0);

VECTOR* _vectorAdd(VECTOR* arg0, VECTOR* arg1, VECTOR* arg2);

VECTOR* _vectorSubtract(VECTOR* arg0, VECTOR* arg1, VECTOR* arg2);

VECTOR* _vectorFixedPointMult(VECTOR* arg0, VECTOR* arg1, int arg2);

static VECTOR* _copyVector(VECTOR* arg0, VECTOR* arg1)
{
    copyVector(arg0, arg1);
    return arg0;
}

static int _vectorMagnitude(VECTOR* arg0)
{
    VECTOR sp10;
    int temp_lo;
    int temp_lo_2;
    int temp_v0;

    _copyVector(&sp10, arg0);
    temp_lo = (sp10.vx >> 0xC) * (sp10.vx >> 0xC);
    temp_lo_2 = (sp10.vy >> 0xC) * (sp10.vy >> 0xC);
    temp_v0 = (sp10.vz >> 0xC) * (sp10.vz >> 0xC);
    sp10.vx = sp10.vx >> 0xC;
    sp10.vy = sp10.vy >> 0xC;
    sp10.vz = sp10.vz >> 0xC;
    return vs_gte_rsqrt(temp_lo + temp_lo_2 + temp_v0) * ONE;
}

VECTOR* _setVectorImm(VECTOR* arg0, u_char* arg1);

static VECTOR* _sVectorToFixedPointVector(VECTOR* arg0, SVECTOR* arg1)
{
    arg0->vx = arg1->vx * ONE;
    arg0->vy = arg1->vy * ONE;
    arg0->vz = arg1->vz * ONE;
    return arg0;
}

static int _fixedPointMult(int a, int b)
{
#ifdef ROOD_PC
    return (int)(((long long)a * b) >> 12);
#else
    int result;
    __asm__("mult       %1, %2;"
            "mflo       $t0;"
            "mfhi       $t1;"
            "srl        $t0, 12;"
            "sll        $t1, 20;"
            "or         %0, $t0, $t1;"
        : "=r"(result)
        : "r"(a), "r"(b));
    return result;
#endif
}

int _atan2FixedPoint(int arg0, int arg1);

void _vecToRotMatrix(VECTOR* arg0, MATRIX* arg1);

static void _toNormalIntegerVector(VECTOR* arg0, VECTOR* arg1)
{
    VECTOR sp10;

    setVector(&sp10, arg0->vx >> 0xC, arg0->vy >> 0xC, arg0->vz >> 0xC);
    VectorNormal(&sp10, arg1);
}

int _loadMpdClearedScriptDoorSections(void* clearedSection, u_short clearedSectionLen, void* scriptSection, u_short scriptSectionLen, void* doorSection, u_short doorSectionLen, int startState);

int vs_event_eventPlaybackState(void);

short vs_event_pendingEventSelector(void);

void vs_event_freeRoomEventSections(void);

void vs_state_scriptFlagsResetZone(int arg0);

void func_800BEBEC(void);

char vs_battle_getStateFlag(short arg0);

void vs_battle_setStateFlag(short id, char value);

int vs_event_requestEventStart(void);

extern char D_800F4BA8;
int vs_actors_actorHpPercentage(int);
int vs_field_gimmickPlacement(int, void*);
int vs_field_gimmickSupportHeight(int, int);
int vs_actors_isActorRetainedAfterDefeat(int);

short vs_event_evaluateRoomEventTriggers(int arg0, int arg1, __INTPTR_TYPE__ arg2, int arg3);

void vs_event_initializeEventScriptData(u_char* data);

short vs_event_executeScriptThread(u_char** arg0, short arg1);

int vs_event_updateEventPlayback(void);

/* The event opcode table: native function pointers in the original's order (guest address 0x800E9D30).
 * pc_event_* take the handlers that the original calls with fewer arguments. */
int pc_event_func_800B76A8(u_char* script, short slot);
int pc_event_func_800B810C(u_char* script, short slot);
int pc_event_func_800B822C(u_char* script, short slot);
int pc_event_func_800B83D4(u_char* script, short slot);
int pc_event_func_800B8438(u_char* script, short slot);
int pc_event_func_800B9A94(u_char* script, short slot);
int pc_event_func_800B9BF8(u_char* script, short slot);
int pc_event_func_800BA2E0(u_char* script, short slot);
int pc_event_func_800BA52C(u_char* script, short slot);
int pc_event_func_800BAB14(u_char* script, short slot);
int pc_event_func_800BADE0(u_char* script, short slot);
int vs_battle_invalidOpcode(u_char* script, short slot);
int vs_event_scriptWaitForEventFile(u_char* script, short slot);
int func_800B6724(u_char* script, short slot);
int vs_event_scriptBeginSkippableCutscene(u_char* script, short slot);
int vs_event_scriptEndCutsceneAndJump(u_char* script, short slot);
int vs_event_scriptEndCutscene(u_char* script, short slot);
int vs_battle_script_initTextBox(u_char* script, short slot);
int vs_battle_script_showTextBox(u_char* script, short slot);
int vs_battle_script_dismissTextBox(u_char* script, short slot);
int vs_event_scriptInitActorDialogBox(u_char* script, short slot);
int vs_event_scriptWaitForDialogDismissal(u_char* script, short slot);
int vs_battle_script_buttonPressed(u_char* script, short slot);
int vs_battle_script_loadGim(u_char* script, short slot);
int func_800B7014(u_char* script, short slot);
int func_800B70E8(u_char* script, short slot);
int func_800B71C4(u_char* script, short slot);
int func_800B71F4(u_char* script, short slot);
int vs_battle_script_setStringContextInt(u_char* script, short slot);
int vs_battle_script_selectTextBox(u_char* script, short slot);
int vs_event_scriptSetDialogTransitionDuration(u_char* script, short slot);
int func_800B7428(u_char* script, short slot);
int vs_event_scriptStoreActorTile(u_char* script, short slot);
int vs_event_scriptSpawnActor(u_char* script, short slot);
int vs_event_scriptNavigateActorToTile(u_char* script, short slot);
int vs_event_scriptSetActorGroupFlags(u_char* script, short slot);
int vs_event_scriptSetActorSoundMix(u_char* script, short slot);
int vs_event_scriptWalkActorToTile(u_char* script, short slot);
int vs_event_scriptWalkActorToWorldPos(u_char* script, short slot);
int vs_event_scriptSlideActorToTile(u_char* script, short slot);
int vs_event_scriptSlideActorToWorldPos(u_char* script, short slot);
int vs_event_scriptSetActorAttachment(u_char* script, short slot);
int vs_event_scriptNavigateActorToWorldPos(u_char* script, short slot);
int vs_event_scriptTurnActorToFacing(u_char* script, short slot);
int vs_event_scriptFaceOtherActor(u_char* script, short slot);
int vs_event_scriptFaceTile(u_char* script, short slot);
int vs_event_scriptScaleActor(u_char* script, short slot);
int vs_event_scriptRemoveActorGroup(u_char* script, short slot);
int func_800B81A0(u_char* script, short slot);
int vs_event_scriptRotateActorAxes(u_char* script, short slot);
int func_800B8360(u_char* script, short slot);
int vs_event_scriptSetActorIdleAnimation(u_char* script, short slot);
int func_800B8510(u_char* script, short slot);
int func_800B8590(u_char* script, short slot);
int vs_event_scriptLoadObjectModel(u_char* script, short slot);
int vs_event_scriptWaitForObjectLoad(u_char* script, short slot);
int vs_event_scriptReleaseObjectModel(u_char* script, short slot);
int vs_event_scriptSelectObjectTextureBank(u_char* script, short slot);
int func_800B8788(u_char* script, short slot);
int vs_event_scriptSetActorWeaponDrawn(u_char* script, short slot);
int vs_battle_script_jumpFwdIfFlag(u_char* script, short slot);
int vs_battle_script_jumpFwdIfFlags(u_char* script, short slot);
int vs_battle_script_jumpFwd(u_char* script, short slot);
int func_800B66E4(u_char* script, short slot);
int vs_battle_script_jumpFwd2(u_char* script, short slot);
int vs_battle_script_setFlagImmContext(u_char* script, short slot);
int vs_battle_script_jumpFwdIfFlagContext(u_char* script, short slot);
int vs_event_scriptReturnToSavedAddress(u_char* script, short slot);
int vs_event_scriptSetLoopCounter(u_char* script, short slot);
int vs_battle_script_jumpBackIf(u_char* script, short slot);
int vs_battle_script_jumpFwd4(u_char* script, short slot);
int vs_battle_script_jumpFwd3(u_char* script, short slot);
int vs_battle_script_setFlagImm(u_char* script, short slot);
int vs_battle_script_setFlag(u_char* script, short slot);
int vs_battle_script_setFlagIfItemInInventory(u_char* script, short slot);
int func_800B91C4(u_char* script, short slot);
int func_800B91F0(u_char* script, short slot);
int vs_event_scriptSetNpcUpdatesEnabled(u_char* script, short slot);
int vs_battle_script_setFlagIfButton(u_char* script, short slot);
int vs_event_scriptRestoreActorVitals(u_char* script, short slot);
int func_800B92D0(u_char* script, short slot);
int func_800B92F4(u_char* script, short slot);
int vs_event_scriptConfigureTimer(u_char* script, short slot);
int vs_event_scriptSetDialogSpeaker(u_char* script, short slot);
int vs_event_scriptSetActorEquipmentModels(u_char* script, short slot);
int vs_event_scriptSetActorYawOscillation(u_char* script, short slot);
int vs_event_scriptWaitForButtonOrTimeout(u_char* script, short slot);
int vs_event_scriptConfigureEasingCurve(u_char* script, short slot);
int func_800B9770(u_char* script, short slot);
int func_800B979C(u_char* script, short slot);
int func_800B97E4(u_char* script, short slot);
int vs_event_scriptClearAshleyStatusEffects(u_char* script, short slot);
int func_800B9818(u_char* script, short slot);
int func_800B983C(u_char* script, short slot);
int func_800B9860(u_char* script, short slot);
int func_800B988C(u_char* script, short slot);
int func_800B98C0(u_char* script, short slot);
int func_800B9A40(u_char* script, short slot);
int func_800B9AC4(u_char* script, short slot);
int func_800B9AE8(u_char* script, short slot);
int func_800B9B18(u_char* script, short slot);
int func_800B9C18(u_char* script, short slot);
int func_800B9C58(u_char* script, short slot);
int func_800B9D74(u_char* script, short slot);
int vs_event_scriptSetAshleyBattleMode(u_char* script, short slot);
int func_800B9F28(u_char* script, short slot);
int func_800B9F4C(u_char* script, short slot);
int func_800B9F70(u_char* script, short slot);
int func_800B9F90(u_char* script, short slot);
int func_800B9FC0(u_char* script, short slot);
int func_800BA0E4(u_char* script, short slot);
int func_800BA194(u_char* script, short slot);
int func_800BA108(u_char* script, short slot);
int func_800BA218(u_char* script, short slot);
int func_800BA264(u_char* script, short slot);
int func_800BA288(u_char* script, short slot);
int func_800BA2B4(u_char* script, short slot);
int vs_event_commandPlayActorSfx(u_char* script, short slot);
int vs_event_commandSlideSfxVolume(u_char* script, short slot);
int vs_event_commandStopAllSfx(u_char* script, short slot);
int vs_event_commandStopSfx(u_char* script, short slot);
int vs_battle_script_loadSfxSlot(u_char* script, short slot);
int vs_battle_script_freeSfxSlot(u_char* script, short slot);
int vs_battle_script_freeSfx(u_char* script, short slot);
int vs_battle_script_setCurrentSfx(u_char* script, short slot);
int vs_battle_script_loadSoundFile(u_char* script, short slot);
int vs_battle_script_loadMusicSlot(u_char* script, short slot);
int vs_battle_script_freeMusic(u_char* script, short slot);
int vs_event_commandPlayMusic(u_char* script, short slot);
int vs_event_commandSlideMusicVolume(u_char* script, short slot);
int vs_event_commandSlideMusicTempo(u_char* script, short slot);
int vs_event_commandSlideMusicPitch(u_char* script, short slot);
int vs_event_commandStopMusic(u_char* script, short slot);
int func_800BA6E0(u_char* script, short slot);
int vs_event_commandPushMusicState(u_char* script, short slot);
int vs_battle_script_clearMusicLoadSlot(u_char* script, short slot);
int vs_event_commandWaitMusicTimer(u_char* script, short slot);
int func_800BA800(u_char* script, short slot);
int func_800BA848(u_char* script, short slot);
int vs_battle_script_loadSoundFile2(u_char* script, short slot);
int vs_battle_script_processSoundQueue(u_char* script, short slot);
int func_800BA890(u_char* script, short slot);
int vs_event_commandPlayActorEffect(u_char* script, short slot);
int func_800BA90C(u_char* script, short slot);
int vs_event_commandWaitEffectState(u_char* script, short slot);
int vs_event_commandYieldEffectRelease(u_char* script, short slot);
int vs_event_commandWaitEffectContinuation(u_char* script, short slot);
int vs_event_commandWaitEffectRelease(u_char* script, short slot);
int vs_event_commandMarkEffectStageReady(u_char* script, short slot);
int vs_event_commandSetMusicJumpThreshold(u_char* script, short slot);
int vs_event_commandSetActorTrail(u_char* script, short slot);
int vs_event_commandSetEnteredDoor(u_char* script, short slot);
int vs_event_commandStoreActorRangeBearing(u_char* script, short slot);
int func_800BAD78(u_char* script, short slot);
int vs_event_commandConsumeMiscItem(u_char* script, short slot);
int vs_event_commandStoreActorDistance(u_char* script, short slot);
int vs_battle_script_specialOp(u_char* script, short slot);
int vs_event_commandWaitScreenEffectLoad(u_char* script, short slot);
int func_800BD6C4(u_char* script, short slot);
int func_800BB2B8(u_char* script, short slot);
int vs_event_commandLoadScreenEffects(u_char* script, short slot);
int func_800BB304(u_char* script, short slot);
int vs_event_commandFreeScreenEffects(u_char* script, short slot);
int func_800BC2E8(u_char* script, short slot);
int func_800BB33C(u_char* script, short slot);
int func_800BB35C(u_char* script, short slot);
int func_800BB41C(u_char* script, short slot);
int func_800BB450(u_char* script, short slot);
int func_800BD444(u_char* script, short slot);
int func_800BB38C(u_char* script, short slot);
int func_800BB3BC(u_char* script, short slot);
int func_800BDC9C(u_char* script, short slot);
int vs_event_commandWaitFrames(u_char* script, short slot);
int vs_event_commandSetTickSpeed(u_char* script, short slot);
int vs_battle_script_playMenuSelectSfx(u_char* script, short slot);
int vs_event_commandSetScanlineOverlay(u_char* script, short slot);
int vs_event_commandWaitScriptContext(u_char* script, short slot);
int vs_event_commandStartScriptContext(u_char* script, short slot);
int vs_event_commandStopScriptContext(u_char* script, short slot);
int vs_event_commandLoadNextEvent(u_char* script, short slot);
int vs_event_commandFinishEvent(u_char* script, short slot);
int (*_opcodeFunctionTable[256])(u_char*, short) = {
    vs_battle_invalidOpcode, /* 0x00 */
    vs_battle_invalidOpcode, /* 0x01 */
    vs_battle_invalidOpcode, /* 0x02 */
    vs_battle_invalidOpcode, /* 0x03 */
    vs_battle_invalidOpcode, /* 0x04 */
    vs_battle_invalidOpcode, /* 0x05 */
    vs_battle_invalidOpcode, /* 0x06 */
    vs_battle_invalidOpcode, /* 0x07 */
    vs_battle_invalidOpcode, /* 0x08 */
    vs_battle_invalidOpcode, /* 0x09 */
    vs_event_scriptWaitForEventFile, /* 0x0a */
    vs_battle_invalidOpcode, /* 0x0b */
    func_800B6724, /* 0x0c */
    vs_event_scriptBeginSkippableCutscene, /* 0x0d */
    vs_event_scriptEndCutsceneAndJump, /* 0x0e */
    vs_event_scriptEndCutscene, /* 0x0f */
    vs_battle_script_initTextBox, /* 0x10 */
    vs_battle_script_showTextBox, /* 0x11 */
    vs_battle_script_dismissTextBox, /* 0x12 */
    vs_event_scriptInitActorDialogBox, /* 0x13 */
    vs_event_scriptWaitForDialogDismissal, /* 0x14 */
    vs_battle_script_buttonPressed, /* 0x15 */
    vs_battle_script_loadGim, /* 0x16 */
    func_800B7014, /* 0x17 */
    func_800B70E8, /* 0x18 */
    func_800B71C4, /* 0x19 */
    func_800B71F4, /* 0x1a */
    vs_battle_script_setStringContextInt, /* 0x1b */
    vs_battle_script_selectTextBox, /* 0x1c */
    vs_event_scriptSetDialogTransitionDuration, /* 0x1d */
    func_800B7428, /* 0x1e */
    vs_event_scriptStoreActorTile, /* 0x1f */
    vs_event_scriptSpawnActor, /* 0x20 */
    vs_event_scriptNavigateActorToTile, /* 0x21 */
    pc_event_func_800B76A8, /* 0x22 */
    vs_event_scriptSetActorGroupFlags, /* 0x23 */
    vs_event_scriptSetActorSoundMix, /* 0x24 */
    vs_event_scriptWalkActorToTile, /* 0x25 */
    vs_event_scriptWalkActorToWorldPos, /* 0x26 */
    vs_event_scriptSlideActorToTile, /* 0x27 */
    vs_event_scriptSlideActorToWorldPos, /* 0x28 */
    vs_event_scriptSetActorAttachment, /* 0x29 */
    vs_event_scriptNavigateActorToWorldPos, /* 0x2a */
    vs_event_scriptTurnActorToFacing, /* 0x2b */
    vs_event_scriptFaceOtherActor, /* 0x2c */
    vs_event_scriptFaceTile, /* 0x2d */
    vs_event_scriptScaleActor, /* 0x2e */
    vs_event_scriptRemoveActorGroup, /* 0x2f */
    pc_event_func_800B810C, /* 0x30 */
    func_800B81A0, /* 0x31 */
    pc_event_func_800B822C, /* 0x32 */
    vs_event_scriptRotateActorAxes, /* 0x33 */
    func_800B8360, /* 0x34 */
    pc_event_func_800B83D4, /* 0x35 */
    pc_event_func_800B8438, /* 0x36 */
    vs_event_scriptSetActorIdleAnimation, /* 0x37 */
    func_800B8510, /* 0x38 */
    func_800B8590, /* 0x39 */
    vs_event_scriptLoadObjectModel, /* 0x3a */
    vs_event_scriptWaitForObjectLoad, /* 0x3b */
    vs_event_scriptReleaseObjectModel, /* 0x3c */
    vs_event_scriptSelectObjectTextureBank, /* 0x3d */
    func_800B8788, /* 0x3e */
    vs_event_scriptSetActorWeaponDrawn, /* 0x3f */
    vs_battle_script_jumpFwdIfFlag, /* 0x40 */
    vs_battle_script_jumpFwdIfFlags, /* 0x41 */
    vs_battle_script_jumpFwd, /* 0x42 */
    func_800B66E4, /* 0x43 */
    func_800B66E4, /* 0x44 */
    vs_battle_script_jumpFwd2, /* 0x45 */
    vs_battle_script_setFlagImmContext, /* 0x46 */
    vs_battle_script_jumpFwdIfFlagContext, /* 0x47 */
    vs_event_scriptReturnToSavedAddress, /* 0x48 */
    vs_event_scriptSetLoopCounter, /* 0x49 */
    vs_battle_script_jumpBackIf, /* 0x4a */
    vs_battle_script_jumpFwd4, /* 0x4b */
    vs_battle_script_jumpFwd3, /* 0x4c */
    vs_battle_invalidOpcode, /* 0x4d */
    vs_battle_invalidOpcode, /* 0x4e */
    vs_battle_invalidOpcode, /* 0x4f */
    vs_battle_script_setFlagImm, /* 0x50 */
    vs_battle_script_setFlag, /* 0x51 */
    vs_battle_script_setFlagIfItemInInventory, /* 0x52 */
    func_800B91C4, /* 0x53 */
    func_800B91F0, /* 0x54 */
    vs_event_scriptSetNpcUpdatesEnabled, /* 0x55 */
    vs_battle_script_setFlagIfButton, /* 0x56 */
    vs_event_scriptRestoreActorVitals, /* 0x57 */
    func_800B92D0, /* 0x58 */
    func_800B92F4, /* 0x59 */
    vs_event_scriptConfigureTimer, /* 0x5a */
    vs_event_scriptSetDialogSpeaker, /* 0x5b */
    vs_event_scriptSetActorEquipmentModels, /* 0x5c */
    vs_event_scriptSetActorYawOscillation, /* 0x5d */
    vs_event_scriptWaitForButtonOrTimeout, /* 0x5e */
    vs_event_scriptConfigureEasingCurve, /* 0x5f */
    func_800B9770, /* 0x60 */
    func_800B979C, /* 0x61 */
    func_800B97E4, /* 0x62 */
    vs_event_scriptClearAshleyStatusEffects, /* 0x63 */
    func_800B9818, /* 0x64 */
    func_800B983C, /* 0x65 */
    func_800B9860, /* 0x66 */
    func_800B988C, /* 0x67 */
    func_800B98C0, /* 0x68 */
    func_800B9A40, /* 0x69 */
    pc_event_func_800B9A94, /* 0x6a */
    func_800B9AC4, /* 0x6b */
    func_800B9AE8, /* 0x6c */
    func_800B9B18, /* 0x6d */
    pc_event_func_800B9BF8, /* 0x6e */
    func_800B9C18, /* 0x6f */
    func_800B9C58, /* 0x70 */
    func_800B9D74, /* 0x71 */
    vs_event_scriptSetAshleyBattleMode, /* 0x72 */
    func_800B9F28, /* 0x73 */
    func_800B9F4C, /* 0x74 */
    func_800B9F70, /* 0x75 */
    func_800B9F90, /* 0x76 */
    func_800B9FC0, /* 0x77 */
    func_800BA0E4, /* 0x78 */
    func_800BA194, /* 0x79 */
    func_800BA108, /* 0x7a */
    func_800BA218, /* 0x7b */
    func_800BA264, /* 0x7c */
    func_800BA288, /* 0x7d */
    func_800BA2B4, /* 0x7e */
    vs_battle_invalidOpcode, /* 0x7f */
    pc_event_func_800BA2E0, /* 0x80 */
    vs_event_commandPlayActorSfx, /* 0x81 */
    vs_event_commandSlideSfxVolume, /* 0x82 */
    vs_event_commandStopAllSfx, /* 0x83 */
    vs_event_commandStopSfx, /* 0x84 */
    vs_battle_script_loadSfxSlot, /* 0x85 */
    vs_battle_script_freeSfxSlot, /* 0x86 */
    vs_battle_script_freeSfx, /* 0x87 */
    vs_battle_script_setCurrentSfx, /* 0x88 */
    vs_battle_script_loadSoundFile, /* 0x89 */
    vs_battle_invalidOpcode, /* 0x8a */
    vs_battle_invalidOpcode, /* 0x8b */
    vs_battle_invalidOpcode, /* 0x8c */
    vs_battle_invalidOpcode, /* 0x8d */
    vs_battle_invalidOpcode, /* 0x8e */
    pc_event_func_800BA52C, /* 0x8f */
    vs_battle_script_loadMusicSlot, /* 0x90 */
    vs_battle_script_freeMusic, /* 0x91 */
    vs_event_commandPlayMusic, /* 0x92 */
    vs_event_commandSlideMusicVolume, /* 0x93 */
    vs_event_commandSlideMusicTempo, /* 0x94 */
    vs_event_commandSlideMusicPitch, /* 0x95 */
    vs_event_commandStopMusic, /* 0x96 */
    func_800BA6E0, /* 0x97 */
    vs_event_commandPushMusicState, /* 0x98 */
    vs_battle_script_clearMusicLoadSlot, /* 0x99 */
    vs_event_commandWaitMusicTimer, /* 0x9a */
    func_800BA800, /* 0x9b */
    func_800BA848, /* 0x9c */
    vs_battle_script_loadSoundFile2, /* 0x9d */
    vs_battle_script_processSoundQueue, /* 0x9e */
    func_800BA890, /* 0x9f */
    vs_event_commandPlayActorEffect, /* 0xa0 */
    func_800BA90C, /* 0xa1 */
    vs_event_commandWaitEffectState, /* 0xa2 */
    vs_event_commandYieldEffectRelease, /* 0xa3 */
    vs_event_commandWaitEffectContinuation, /* 0xa4 */
    vs_event_commandWaitEffectRelease, /* 0xa5 */
    vs_event_commandMarkEffectStageReady, /* 0xa6 */
    vs_battle_invalidOpcode, /* 0xa7 */
    vs_event_commandSetMusicJumpThreshold, /* 0xa8 */
    pc_event_func_800BAB14, /* 0xa9 */
    vs_event_commandSetActorTrail, /* 0xaa */
    vs_battle_invalidOpcode, /* 0xab */
    vs_battle_invalidOpcode, /* 0xac */
    vs_battle_invalidOpcode, /* 0xad */
    vs_battle_invalidOpcode, /* 0xae */
    vs_battle_invalidOpcode, /* 0xaf */
    vs_battle_invalidOpcode, /* 0xb0 */
    vs_event_commandSetEnteredDoor, /* 0xb1 */
    vs_event_commandStoreActorRangeBearing, /* 0xb2 */
    func_800BAD78, /* 0xb3 */
    pc_event_func_800BADE0, /* 0xb4 */
    vs_event_commandConsumeMiscItem, /* 0xb5 */
    vs_event_commandStoreActorDistance, /* 0xb6 */
    vs_battle_script_specialOp, /* 0xb7 */
    vs_event_commandWaitScreenEffectLoad, /* 0xb8 */
    func_800BD6C4, /* 0xb9 */
    func_800BD6C4, /* 0xba */
    func_800BB2B8, /* 0xbb */
    vs_event_commandLoadScreenEffects, /* 0xbc */
    func_800BB304, /* 0xbd */
    vs_event_commandFreeScreenEffects, /* 0xbe */
    func_800BD6C4, /* 0xbf */
    func_800BC2E8, /* 0xc0 */
    func_800BC2E8, /* 0xc1 */
    func_800BC2E8, /* 0xc2 */
    func_800BC2E8, /* 0xc3 */
    func_800BC2E8, /* 0xc4 */
    func_800BC2E8, /* 0xc5 */
    vs_battle_invalidOpcode, /* 0xc6 */
    func_800BC2E8, /* 0xc7 */
    func_800BC2E8, /* 0xc8 */
    func_800BC2E8, /* 0xc9 */
    func_800BC2E8, /* 0xca */
    func_800BC2E8, /* 0xcb */
    vs_battle_invalidOpcode, /* 0xcc */
    vs_battle_invalidOpcode, /* 0xcd */
    vs_battle_invalidOpcode, /* 0xce */
    vs_battle_invalidOpcode, /* 0xcf */
    func_800BC2E8, /* 0xd0 */
    func_800BC2E8, /* 0xd1 */
    func_800BC2E8, /* 0xd2 */
    func_800BC2E8, /* 0xd3 */
    func_800BC2E8, /* 0xd4 */
    func_800BC2E8, /* 0xd5 */
    vs_battle_invalidOpcode, /* 0xd6 */
    func_800BC2E8, /* 0xd7 */
    func_800BC2E8, /* 0xd8 */
    func_800BC2E8, /* 0xd9 */
    func_800BC2E8, /* 0xda */
    func_800BC2E8, /* 0xdb */
    vs_battle_invalidOpcode, /* 0xdc */
    vs_battle_invalidOpcode, /* 0xdd */
    func_800BB33C, /* 0xde */
    func_800BB35C, /* 0xdf */
    func_800BB41C, /* 0xe0 */
    func_800BB450, /* 0xe1 */
    func_800BD444, /* 0xe2 */
    func_800BD444, /* 0xe3 */
    func_800BD6C4, /* 0xe4 */
    func_800BD6C4, /* 0xe5 */
    func_800BD6C4, /* 0xe6 */
    func_800BD6C4, /* 0xe7 */
    func_800BB38C, /* 0xe8 */
    func_800BB3BC, /* 0xe9 */
    func_800BDC9C, /* 0xea */
    func_800BDC9C, /* 0xeb */
    func_800BDC9C, /* 0xec */
    func_800BD6C4, /* 0xed */
    func_800BDC9C, /* 0xee */
    func_800BDC9C, /* 0xef */
    vs_event_commandWaitFrames, /* 0xf0 */
    vs_event_commandSetTickSpeed, /* 0xf1 */
    vs_battle_script_playMenuSelectSfx, /* 0xf2 */
    vs_event_commandSetScanlineOverlay, /* 0xf3 */
    vs_event_commandWaitScriptContext, /* 0xf4 */
    vs_event_commandStartScriptContext, /* 0xf5 */
    vs_event_commandStopScriptContext, /* 0xf6 */
    vs_event_commandLoadNextEvent, /* 0xf7 */
    vs_battle_invalidOpcode, /* 0xf8 */
    vs_battle_invalidOpcode, /* 0xf9 */
    vs_battle_invalidOpcode, /* 0xfa */
    vs_battle_invalidOpcode, /* 0xfb */
    vs_battle_invalidOpcode, /* 0xfc */
    vs_battle_invalidOpcode, /* 0xfd */
    vs_event_commandFinishEvent, /* 0xfe */
    vs_event_commandFinishEvent, /* 0xff */
};

u_char* pc_event_scriptBase;

void pc_event_setBase(u_char* data) { pc_event_scriptBase = data; }

int pc_event_index_ok(const char* table, int length, int index, const u_char* at)
{
    if ((unsigned)index < (unsigned)length)
        return 1;
    pc_table_index_refused(table, index, length, "event, offset", ((unsigned long)vs_event_eventSource << 12) | (unsigned short)_evtFile,
        (unsigned long)(at - pc_event_scriptBase));
    return 0;
}
