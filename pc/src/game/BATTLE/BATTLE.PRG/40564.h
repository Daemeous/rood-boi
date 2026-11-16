#pragma once
#include "func_8006EBF8_t.h"
#include <stddef.h>
#include <libgte.h>
#include <libgpu.h>

void vs_actors_moveActorToTileAtRate(int, func_8006EBF8_t_fields*, int, int);
void vs_actors_translateActorToTileAtRate(int, func_8006EBF8_t_fields*, int);
void vs_actors_turnActorTowardTileAtRate(int, func_8006EBF8_t_fields*, int);
void vs_actors_translateActorToAtRate(int, SVECTOR*, int);
void vs_actors_turnActorByAtRate(int, short, int);
void vs_actors_turnActorToAtRate(int, short, int);
void vs_actors_rotateActorByAtRate(int, SVECTOR*, int);
void vs_actors_rotateActorToAtRate(int, SVECTOR*, int);
void vs_actors_turnActorToOverTime(int, u_short, int);
void vs_actors_scaleActorToOverTime(int, SVECTOR*, int);
int vs_actors_setTextureAnimation(int, int, int, int);
void vs_actors_stepTextureScrolling(void*);
void vs_actors_startActorAppearanceTransition(int, int);
void vs_actors_initializeActorAnimationPose(void*, void*);
int vs_actors_findActorAnimationHeader(void*, int, u_short**);
