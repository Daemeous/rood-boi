#pragma once
#include <stddef.h>
#include <libgte.h>
#include "pc/src/psx_types.h"

typedef struct {
    /* 0x00 */ VECTOR position;
    /* 0x10 */ VECTOR lookAt;
    /* 0x20 */ VECTOR angles;
    /* 0x30 */ int farClip;
} vs_camera_t;

typedef struct {
    /* 0x00 */ psx_ptr(void) unk0;
    /* 0x04 */ psx_ptr(void) unk4;
    /* 0x08 */ psx_ptr(void) unk8;
    /* 0x0C */ psx_ptr(void) unkC;
    /* 0x10 */ u_int firstPersonViewFacing;
    /* 0x14 */ MATRIX viewMatrix;
    /* 0x34 */ vs_camera_t camera;
} vs_scratch_t;
PSX_ASSERT_SIZE(vs_scratch_t, 0x68);

extern vs_scratch_t vs_scratch;
