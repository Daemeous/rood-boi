#include "common.h"
#include "146C.h"
#include "4A0A8.h"
#include "573B8.h"
#include "target_block.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include "build/src/include/lbas.h"
#include <libetc.h>

extern u_char vs_event_opcodeSizes[];
extern int (*_opcodeFunctionTable[])(u_char*, short);
extern unsigned char vs_event_scriptControlledActors[17];
extern vs_main_CdQueueSlot* vs_event_eventFileDiscRequest;
extern vs_main_CdFile vs_event_eventFileDiscLocation;

extern D_800EB9B8_t* vs_battle_targetSphere;
extern u_char D_800EB9AC;

extern void func_8007D260(int);
void vs_actors_playActorAnimation(int, int, int, int);
extern void func_800BBDDC(void);
int vs_event_opcodeSize(u_char*);
void vs_event_resetCutsceneActorsToIdle(void);

short vs_event_executeScriptThread(u_char** arg0, short arg1);

void vs_event_releaseScriptActorControl(void);

short vs_battle_getShort(u_char* arg0);

int vs_event_opcodeSize(u_char* arg0);

int vs_event_resolveScriptActorReference(u_short arg0);

extern signed char vs_event_customEasingCurves[][2];

int vs_event_evaluateInterpolationCurve(short mode, short frame, short duration);

void vs_event_queueEventScriptLoad(int arg0, void* arg1);

void vs_event_resetCutsceneActorsToIdle(void);

#ifndef ROOD_PC
__asm__("glabel vs_battle_copyAligned;"
        "and       $t0, $a2, 0x7;"
        "beqz      $t0, vs_battle_memcpy;"
        "lh        $t0, ($a1);"
        "addu      $a1, 2;"
        "sh        $t0, ($a0);"
        "addu      $a0, 2;"
        "j         vs_battle_copyAligned;"
        "add       $a2, -2;"
        "endlabel vs_battle_copyAligned;");
#else
/* PC: copy halfwords until the remaining size is a multiple of 8, then hand
 * the rest to vs_battle_memcpy. */
void vs_battle_copyAligned(void* dst, const void* src, int numBytes);

#endif

#ifndef ROOD_PC
__asm__("glabel vs_battle_memcpy;"
        "addu      $a2, $a1, $a2;"
        "0:;"
        "lh        $t0, ($a1);"
        "lh        $t1, 2($a1);"
        "lh        $t2, 4($a1);"
        "lh        $t3, 6($a1);"
        "addu      $a1, 8;"
        "sh        $t0, ($a0);"
        "sh        $t1, 2($a0);"
        "sh        $t2, 4($a0);"
        "sh        $t3, 6($a0);"
        "bne       $a1, $a2, 0b;"
        "addu      $a0, 8;"
        "jr        $ra;"
        "endlabel vs_battle_memcpy;");
#else
/* PC: copy 8 bytes per step as halfwords until the source reaches its end.
 * Like the original do-while, a zero size would not terminate. */
void vs_battle_memcpy(void* dst, const void* src, int numBytes);

#endif

#ifndef ROOD_PC
__asm__("glabel vs_battle_setSpriteDefault;"
        "lui      $v1, 0x1F80;"
        "addu     $a2, $a0, $zero;"
        "li       $a0, 0x80;"
        "lw       $a3, 0x8($v1);"
        "alabel vs_battle_setSpriteDefaultTexPage;"
        "lui      $t1, 0xE100;"
        "j        .L800C0234;"
        "or       $t1, 0xC;"
        "endlabel vs_battle_setSpriteDefault;"

        "glabel vs_battle_setSprite;"
        "addu       $t1, $zero, $zero;"
        ".L800C0234:;"
        "lw         $t0, ($a3);"
        "lui        $v1, 0x1F80;"
        "lui        $t2, 0x500;"
        "lw         $v0, ($v1);"
        "sll        $t0, 8;"
        "srl        $t0, 8;"
        "or         $t0, $t2;"
        "and        $t3, $a0, 0xFF;"
        "sll        $t4, $t3, 8;"
        "or         $t2, $t3, $t4;"
        "sll        $t3, 16;"
        "or         $t2, $t3;"
        "srl        $t3, $a0, 7;"
        "and        $t3, 0x2;"
        "addu       $t3, 100;"
        "sll        $t3, 24;"
        "or         $t2, $t3;"
        "sll        $t3, $v0, 8;"
        "srl        $t3, 8;"
        "addiu      $t4, $v0, 0x18;"
        "sw         $t0, ($v0);"
        "sw         $t1, 0x4($v0);"
        "sw         $t2, 0x8($v0);"
        "sw         $a1, 0xC($v0);"
        "sw         $zero, 0x10($v0);"
        "sw         $a2, 0x14($v0);"
        "sw         $t3, ($a3);"
        "j          $ra;"
        "sw         $t4, ($v1);"
        "endlabel vs_battle_setSprite;");
#endif

#ifndef ROOD_PC
__asm__("glabel vs_render_loadFrameViewMatrix;"
        "addu     $sp, -0x8;"
        "sw       $ra, ($sp);"
        "lui      $a0, 0x1F80;"
        "jal      SetRotMatrix;"
        "addu     $a0, 0x14;"
        "lui      $a0, 0x1F80;"
        "jal      SetTransMatrix;"
        "addu     $a0, 0x14;"
        "lw       $ra, ($sp);"
        ".nop;"
        "j        $ra;"
        "addu     $sp, 0x8;"
        "endlabel vs_render_loadFrameViewMatrix;");
#else
void vs_render_loadFrameViewMatrix(void);

#endif

#ifndef ROOD_PC
__asm__("glabel vs_battle_playSfx10;"
        "j         .L800C02FC;"
        "li        $a1, 10;"
        "endlabel  vs_battle_playSfx10;"

        "glabel vs_battle_playInvalidSfx;"
        "j         .L800C02FC;"
        "li        $a1, 7;"
        "endlabel  vs_battle_playInvalidSfx;"

        "glabel vs_battle_playMenuLeaveSfx;"
        "j         .L800C02FC;"
        "li        $a1, 6;"
        "endlabel  vs_battle_playMenuLeaveSfx;"

        "glabel vs_battle_playMenuSelectSfx;"
        "j         .L800C02FC;"
        "li        $a1, 5;"
        "endlabel  vs_battle_playMenuSelectSfx;"

        "glabel vs_battle_playMenuChangeSfx;"
        "li       $a1, 0x4;"
        ".L800C02FC:;"
        "addu     $sp, -0x8;"
        "sw       $ra, ($sp);"
        "jal      vs_main_playSfxDefault;"
        "li       $a0, 0x7E;"
        "lw       $ra, ($sp);"
        ".nop;"
        "j        $ra;"
        "addu     $sp, 0x8;"
        "endlabel vs_battle_playMenuChangeSfx;");
#else
void vs_battle_playSfx10(void);

void vs_battle_playInvalidSfx(void);

void vs_battle_playMenuLeaveSfx(void);

void vs_battle_playMenuSelectSfx(void);

void vs_battle_playMenuChangeSfx(void);

#endif

void vs_battle_rMemzero(void*, int);

void func_800C031C(void);

void func_800C05B4(void);

void func_800C05EC(func_800C1564_t* arg0, func_800C1564_t* arg1, u_char arg2, void* arg3);

void func_800C06E0(void);

void func_800C0700(func_800C1564_t* arg0);

void func_800C0738(void);


int func_800C0758(int phase, int segments, int index);

MATRIX* func_800C085C(u_char* scale, int angle);

extern u_char vs_core_mathArcTangentTable[];

void func_800C0990(SVECTOR* start, SVECTOR* end, u_int color, int intensity);

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
extern void pc_unimplemented(const char*);
extern void pc_require_native_callback(unsigned, const char*, unsigned);
