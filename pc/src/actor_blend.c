/* BATTLE/48C7C.s vs_actors_blendActorPose. Keep the GTE's signed 16-bit inputs,
 * fixed-point rounding and conditional stores, including the retained IR3
 * when a rotation's Z delta is zero in the original instruction sequence. */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
extern void vs_actors_blendActorPoseHalfway(void*, void*, int);
extern void* vs_main_memcpy(void*, const void*, unsigned);
extern void pc_unimplemented(const char*);
void vs_actors_blendActorPose(void* destination, void* source, int count, int step, int duration);

