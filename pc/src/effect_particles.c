/* Native adaptation of the retained gte-macros/s5.c decompilation candidate
 * for BATTLE/6E644.s. Preserves original packet layout and GTE stages, but is
 * not a matching decompilation claim. Uses an explicit host argument instead
 * of the original wrapper's implicit MIPS argument-register forwarding. */
#include "common.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/6E644.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include <inline_c.h>
extern void pc_unimplemented(const char*);
typedef struct func_800D6E44_t {
    struct func_800D6E44_t* previous;
    struct func_800D6E44_t* next;
    short unk8;
    short unkA;
    int x;
    int y;
    int z;
    int dx;
    int dy;
    int dz;
    int unk24[3];
    int unk30[3];
    func_800D6CF_t sprite;
    int flags;
    union {
        u_int value;
        struct {
            u_int scale : 8;
            u_int depthMode : 4;
            u_int unk60_12 : 4;
            u_int unk62 : 16;
        } bits;
    } unk60;
    short unk64[2];
    short lifetime;
    u_char matrixIndex;
    u_char unk6B;
    u_char endEvent;
    u_char tickEvent;
    char unk6E[2];
    int unk70;
    func_800D2904_curves curves;
} func_800D6E44_t;

/* A host view of the game's particle node (func_800D2904_t, 5BF94.h): the two views must agree on every target. */
_Static_assert(__builtin_offsetof(func_800D6E44_t, sprite) == __builtin_offsetof(func_800D2904_t, sprite)
                   && __builtin_offsetof(func_800D6E44_t, curves)
                          == __builtin_offsetof(func_800D2904_t, unk74)
                   && sizeof(func_800D6E44_t) == sizeof(func_800D2904_t),
               "particle view != func_800D2904_t");

extern u_char vs_core_mathArcTangentTable[516];
/* Native particles (effect_native.c; absent in unit probes). */
extern void pc_particles_native_begin(void) __attribute__((weak));
extern int pc_particles_native_takes(int tiles, int bucket, int layered, int sz) __attribute__((weak));
extern void pc_particles_native_node(int tiles, const int* page_clut, const int* rgb, const int* xy, const int* uv,
    const int view[3], int sz, unsigned xy_centre, int bucket, const void* node, int age, unsigned source) __attribute__((weak));
extern void pc_particles_native_items(unsigned* start, unsigned* end) __attribute__((weak));
extern unsigned* pc_particles_native_end(unsigned* packet, unsigned* ot) __attribute__((weak));

void vs_effects_drawAnimatedSpriteParticles(void* head);

