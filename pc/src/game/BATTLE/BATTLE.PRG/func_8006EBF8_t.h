#pragma once
#include <libgte.h>

typedef struct {
    u_int unk0_0 : 8;
    u_int unk0_8 : 8;
    u_int unk0_16 : 8;
    u_int unk0_24 : 8;
} func_8006EBF8_t_fields;

typedef struct {
    union {
        func_8006EBF8_t_fields fields;
        int value;
    } packedTile;
    SVECTOR position;
} vs_actors_ActorLocation;

typedef struct {
    vs_actors_ActorLocation location;
    int actorIndex;
} func_8006EBF8_t;
