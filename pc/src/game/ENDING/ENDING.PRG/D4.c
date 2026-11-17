#include "common.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include "pc/src/game/SLUS_010.40/31724.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "build/src/include/lbas.h"
#include "gpu.h"
#include "texture_t.h"
#include "scratch.h"
#define PC_LEAF_TABLE 0 /* (the frame is items alone: the leaves' items are placed in no table: menufLeaf.h) */
#include "pc/src/draw_items.h"
#include "pc/src/gte_value.h"
#include <libapi.h>
#include <libetc.h>
#include <memory.h>
#include <stdio.h>
#include <abs.h>

typedef struct _creditsElement {
    void (*renderer)(struct _creditsElement*);
    u_char* data;
    short state;
    u_short currentStep;
    u_short illustId;
    u_short width;
    int x;
    int y;
    int unk18;
    int unk1C;
    int unk20;
    int unk24;
    int unk28;
    int unk2C;
} _creditsElement;

typedef struct {
    int unk0;
    u_char vertexZeroRed;
    u_char vertexZeroGreen;
    u_char vertexZeroBlue;
    u_char unk7;
    short vertexZeroX;
    short vertexZeroY;
    short unkC;
    short unkE;
    u_char vertexOneRed;
    u_char vertexOneGreen;
    u_char vertexOneBlue;
    u_char unk13;
    short vertexOneX;
    short vertexOneY;
    short unk18;
    short unk1A;
    u_char vertexTwoRed;
    u_char vertexTwoGreen;
    u_char vertexTwoBlue;
    u_char unk1F;
    short vertexTwoX;
    short vertexTwoY;
    short unk24;
    short unk26;
    u_char vertexThreeRed;
    u_char vertexThreeGreen;
    u_char vertexThreeBlue;
    u_char unk2B;
    short vertexThreeX;
    short vertexThreeY;
} vs_ending_CreditsTexturedGradientQuadView;

typedef struct {
    int size;
    RECT rect;
    char data[0];
} vs_ending_CreditsTimImageBlockHeader;

typedef struct {
    u_char unk0;
    u_char unk1;
    u_char unk2;
    u_char unk3;
    int mode;
    vs_ending_CreditsTimImageBlockHeader firstBlock;
} vs_ending_CreditsTimHeader;

enum easeMode {
    easeModeLinear,
    easeModeEaseOut,
    easeModeEaseInOut,
    easeModeEaseIn,
};

void _renderPhantomPain(_creditsElement* arg0);
void _renderVagrantStory(_creditsElement* arg0);
void _renderText(_creditsElement* arg0);
static void vs_ending_creditsStartMusic(void);
static void _setDrawEnv(DRAWENV*);
static void vs_ending_creditsRunFrames(void);
static void vs_ending_creditsUploadTexture(vs_ending_CreditsTimHeader*);
static void _resetElements(void);
static int _renderElements(void);
static _creditsElement* _addRenderer(void (*arg0)(_creditsElement*));
static void _parseCreditsScript(void);
static void vs_ending_creditsQueueIllustrationDecode(vs_ending_CreditsTimHeader* arg0, void* arg1);
int vs_ending_creditsDecodeIllustrationStep(void);
static int _drawScreen(void);
static void vs_ending_creditsAllocateRenderBuffers(void);
static int _ease(short mode, short currentStep, short totalSteps);
static void _updateClearGameStats(void);
static void _beginFrame(void);
static void _checkStreamXaEnd(void);
static void vs_ending_showClearResults(void);
static int vs_ending_loadClearResultsStep(void);
static int _renderCongratulationsScreen(void);
void vs_ending_drawRankBackground(int);
static void _renderCongratulations(int arg0, int arg1, int arg2);
static void _renderScore(int, int, int, int);
static void _renderIncrementalScore(int, int, int, int);
static void _renderMapCompletion(int arg0, int arg1, int arg2, int arg3);
static void _renderIncrementalMapCompletion(int, int, int, int);
static void _renderRiskbreakerRankHeader(int arg0, int arg1, int arg2);
void _renderRiskbreakerRank(int, int, int);
static void _parseTim(u_int* arg0, TIM_IMAGE* arg1);
static void _determineRank(void);
static void _updateScore(void);

extern int vs_ending_creditsRipplePhaseAngle;
extern int vs_ending_creditsRippleScaleFrame;
extern u_char vs_ending_creditsRippleDistanceGrid[4][16];
#ifdef ROOD_PC
/* The logo mesh reads column 16 and row 4, past the table into the next row
 * and the data after it, as the PS1 does; flat reads keep GCC from assuming
 * the loops stop early. */
#define _phantomPainRsqrt(i, j) (((u_char*)vs_ending_creditsRippleDistanceGrid)[(i) * 16 + (j)])
#else
#define _phantomPainRsqrt(i, j) vs_ending_creditsRippleDistanceGrid[i][j]
#endif
extern u_char vs_ending_creditsScriptBytes[];
extern u_char _glyphWidths[];
extern vs_ending_CreditsTimHeader D_8007005C;
extern vs_ending_CreditsTimHeader D_8007709C;
extern vs_ending_CreditsTimHeader D_80088ABC;
extern vs_ending_CreditsTimHeader D_8009A4DC;
extern vs_ending_CreditsTimHeader D_800ABEFC;
extern vs_ending_CreditsTimHeader D_800BD91C;
extern vs_ending_CreditsTimHeader D_800CF33C;
extern vs_ending_CreditsTimHeader D_800D7F7C;
extern vs_ending_CreditsTimHeader D_800D837C;
extern vs_ending_CreditsTimHeader D_800D975C;
extern vs_ending_CreditsTimHeader D_800DB09C;
extern vs_ending_CreditsTimHeader D_800DB26C;
extern int _countableRooms[];
extern u_char vs_ending_creditsIllustrationDecodePhase;
extern vs_ending_CreditsTimHeader* vs_ending_creditsIllustrationTimWorkspace;
extern u_short _illustrationTimers[];
extern int _illustLbas[];
extern int _illustSizes[];
extern _texture_t _disMap[];
extern u_char vs_ending_rankHeadingWidth;
#ifdef ROOD_PC
#include "pc/src/classic/classic_hook.h"
/* The classic look drawn in Lua (classic_hook.h PC_CLASSIC): the MISSION COMPLETE
 * screen's leaves (pc/src/game/renderTextures.h, MENUF's own: 3B8.c) record their calls as
 * MENUF's do, one element a frame (pc_classic_menuf, its screen PC_CLASSIC_MENUF_ENDING),
 * the items they add the site's range (_renderCongratulationsScreen: not the picture under them,
 * vs_ending_drawRankBackground). */
static pc_classic_menuf _pcMenuF;
static int _pcMenuFOn;
static void _pcMenuFCall(int op, int x, int y, int tex, int a, int b, P_CODE* colours)
{
    pc_classic_menuf_call* c;
    if (!_pcMenuFOn || _pcMenuF.count < 0)
        return;
    if (_pcMenuF.count >= PC_CLASSIC_MENUF_CALLS) {
        _pcMenuF.count = -1; /* (more than the record holds: the game draws the frame) */
        return;
    }
    c = &_pcMenuF.call[_pcMenuF.count++];
    memset(c, 0, sizeof *c);
    c->op = op;
    c->tex = tex < 0 ? 0xFF : tex;
    if (tex >= 0) {
        c->u = _disMap[tex].x;
        c->v = _disMap[tex].y;
        c->w = _disMap[tex].w;
        c->h = _disMap[tex].h;
        c->tpage = _disMap[tex].tpage;
        c->clut = _disMap[tex].clut;
    }
    c->x = x;
    c->y = y;
    c->a = a;
    c->b = b;
    if (colours != NULL) {
        memcpy(&c->colour[0], &colours[0], 4);
        memcpy(&c->colour[1], &colours[1], 4);
    }
}
#define PC_MENUF_CALL(op, x, y, tex, a, b, colours) \
    _pcMenuFCall(PC_CLASSIC_MENUF_##op, x, y, tex, a, b, colours)
static int _pcLeavesBegin(void)
{
    memset(&_pcMenuF, 0, sizeof _pcMenuF);
    _pcMenuF.screen = PC_CLASSIC_MENUF_ENDING;
    _pcMenuFOn = 1;
    return pc_frame_count();
}
static void _pcLeavesEnd(int first)
{
    _pcMenuFOn = 0;
    if (pc_classic_site) { /* (a site of items alone: no packets) */
        pc_frame_site(first, pc_frame_count());
        pc_classic_site(PC_CLASSIC_SITE_MENUF, &_pcMenuF, 0, 0, NULL, NULL);
    }
}
#endif
extern P_CODE vs_ending_rankHeadingTint[];
extern short titleThresholds[];
extern u_int rankScores[];
extern short vs_ending_creditsIllustrationBrightness;
extern short vs_ending_creditsIllustrationTexturePageX;
extern u_short vs_ending_creditsIllustrationPaletteY;
#ifdef ROOD_PC_LP64
/* Runtime state holding native pointers (renderer, data): defined natively in
 * the native layout; it restarts with the overlay's statics. */
_creditsElement _creditsElements[32];
#else
extern _creditsElement _creditsElements[];
#endif
extern u_char vs_ending_creditsHasFinished;
extern u_char vs_ending_creditsIsBackgroundFading;
extern u_char _skipInvoked;
extern int vs_ending_creditsElapsedFrames;
extern int _textStreamDelay;
extern u_char* _creditsBytestreamHead;
extern short _scrollSpeed;
extern short vs_ending_creditsDecodeBudgetStartTick;
extern u_short vs_ending_creditsBackgroundBrightness;
extern void* vs_ending_creditsQueuedCompressedImage;
extern vs_ending_CreditsTimHeader* vs_ending_creditsQueuedTimDestination;
extern void* vs_ending_creditsIllustrationResources[];
extern u_int _rank;
extern int _clearCount;
extern u_int _score;
extern int _mapCompletion;
extern u_int _onScreenMapCompletion;
extern int _onScreenScore;
extern int _submenuState;
extern int vs_ending_resultsAnimationFrame;
extern int vs_ending_isLeavingResults;
extern int _incrementingScore;
extern int _incrementingMapCompletion;
extern u_char _riskbreakerRanks[][4];
extern u_char* vs_ending_creditsDecodeBackreferenceCursor;
extern u_char* vs_ending_creditsDecodeInputCursor;
extern u_char* vs_ending_creditsDecodeOutputCursor;
extern u_char vs_ending_creditsDecodeFillByteZero;
extern u_char vs_ending_creditsDecodeFillByteOne;
extern u_short vs_ending_creditsDecodeRunLength;
extern u_short vs_ending_creditsDecodeBackreferenceOffset;

int func_800688D4(int angle)
{
    int value = psx_to_int(int, _trig_table);
    int quadrant, test, index;

    quadrant = angle & 0x400;
    test = angle & 0x3FF;
    if (quadrant) {
        if (!test) {
            value = 0x1000;
            goto negate;
        }
        angle = 0x800 - angle;
    }
    test = angle & 0x200;
    index = angle & 0x1FF;
    if (test) {
        quadrant = 0x200;
        index = quadrant - index;
        value += 2;
    }
    index <<= 2;
    value += index;
    value = *psx_to_ptr(u_short*, value);
negate:
    if (angle & 0x800) {
        value = -value;
    }
    return value;
}

void _renderPhantomPain(_creditsElement* element)
{
    SVECTOR rot;
    SVECTOR vertices[4];
    MATRIX matrix;
    u_short brightness;
    u_short scale;
    u_short i;
    u_short j;
    int sxy[4];
    vs_gte_view view;
    pc_item* poly;
    int k;
    int dx;
    int dy;
    int shift;
    int d;

    memset(&rot, 0, sizeof(rot));

    switch (element->state) {
    case 1:
        vs_ending_creditsRipplePhaseAngle = 0;
        element->currentStep = 0;
        element->state = 2;
        vs_ending_creditsRippleScaleFrame = 0;

        for (i = 0; i < 4; ++i) {
            shift = 2;
            d = 2 - i;
            dy = (ABS(d) << shift) * (ABS(d) << shift);
            for (j = 0; j < 16; ++j) {
                dx = 8 - j;
                dx = ABS(dx) * 18;
                vs_ending_creditsRippleDistanceGrid[i][j] = vs_gte_rsqrt(dx * dx + dy);
            }
        }
        /* fallthrough */

    case 2:
        brightness = (element->currentStep << 7) / 90;
        ++vs_ending_creditsRippleScaleFrame;
        if (++element->currentStep == 90) {
            element->currentStep = 0;
            element->state = 3;
        }
        break;

    case 3:
        brightness = 128;
        if (++element->currentStep == 120) {
            element->currentStep = 0;
            element->state = 4;
        }
        break;

    case 4:
        brightness = ((90 - element->currentStep) << 7) / 90;
        if (++element->currentStep == 90) {
            element->state = -1;
        }
        break;
    }

    vs_ending_creditsRipplePhaseAngle += 200;
    scale = (_ease(3, 90 - vs_ending_creditsRippleScaleFrame, 90) * 65) >> 10;

    RotMatrix_gte(&rot, &matrix);
    matrix.t[0] = matrix.t[1] = 0;
    matrix.t[2] = 512;
    view = vs_gte_view_of(&matrix, vs_gte_screen_current());

    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 16; ++j) {
            vertices[0].vx = vertices[2].vx = j * 9 - 72;
            vertices[1].vx = vertices[3].vx = (j + 1) * 9 - 72;
            vertices[0].vy = vertices[1].vy = i * 4 - 8;
            vertices[2].vy = vertices[3].vy = (i + 1) * 4 - 8;
            vertices[0].vz =
                (func_800688D4(vs_ending_creditsRipplePhaseAngle + _phantomPainRsqrt(i, j) * 400) * scale) >> 12;
            vertices[1].vz =
                (func_800688D4(vs_ending_creditsRipplePhaseAngle + _phantomPainRsqrt(i, j + 1) * 400) * scale)
                >> 12;
            vertices[2].vz =
                (func_800688D4(vs_ending_creditsRipplePhaseAngle + _phantomPainRsqrt(i + 1, j) * 400) * scale)
                >> 12;
            vertices[3].vz =
                (func_800688D4(vs_ending_creditsRipplePhaseAngle + _phantomPainRsqrt(i + 1, j + 1) * 400)
                    * scale)
                >> 12;

            for (k = 0; k < 4; ++k) {
                sxy[k] = vs_gte_project(&view, &vertices[k]).sxy;
            }
            poly = pc_frame_add(PC_ITEM_POLY, 3);
            if (poly == NULL) {
                continue;
            }
            poly->verts = 4;
            poly->flags = PC_ITEM_TEXTURED | PC_ITEM_SEMI;
            for (k = 0; k < 4; ++k) {
                poly->x[k] = sxy[k];
                poly->y[k] = sxy[k] >> 16;
            }
            poly->u[0] = poly->u[2] = j * 9;
            poly->u[1] = poly->u[3] = (j + 1) * 9;
            poly->v[0] = poly->v[1] = i * 4 + 180;
            poly->v[2] = poly->v[3] = (i + 1) * 4 + 180;
            poly->clut = getClut(896, 83);
            poly->mode = 46;
            poly->rgb[0][0] = poly->rgb[0][1] = poly->rgb[0][2] = brightness;
        }
    }
}

void _renderFin(_creditsElement* arg0)
{
    int temp_a0_2;
    int temp_a0;
    int temp_v0;
    u_char var_s0;

    switch (arg0->state) {
    case 1:
        arg0->currentStep = 0;
        arg0->state = 2;
        /* fallthrough */

    case 2:
        temp_v0 = arg0->currentStep;
        arg0->currentStep = temp_v0 + 1;
        var_s0 = (temp_v0 << 7) / 60;

        if (arg0->currentStep == 60) {
            arg0->currentStep = 0;
            arg0->state = 3;
        }
        break;

    case 3:
        var_s0 = 128;

        if (++arg0->currentStep == 240) {
            arg0->currentStep = 0;
            arg0->state = 4;
        }
        break;

    case 4:
        temp_a0 = arg0->currentStep;
        arg0->currentStep = temp_a0 + 1;
        var_s0 = ((0x3C - temp_a0) << 7) / 60;

        if (arg0->currentStep == 60) {
            arg0->state = -1;
        }

        break;
    }

    pc_frame_sprite(1, 248, 184, 36, 17, 0, 160, PC_GREY(var_s0), getClut(896, 82),
        PC_ITEM_MODE(getTPage(clut4Bit, semiTransparencyFull, 896, 0)), 0);
}

void _renderVagrantStory(_creditsElement* element)
{
    int var_fp;

    switch (element->state) {
    case 1:
        element->x = 61 << 8;
        element->y = 80 << 8;
        element->currentStep = 0;
        element->state = 2;
        /* fallthrough */

    case 2:
        var_fp = (vs_ending_creditsBackgroundBrightness * ++element->currentStep) / 60;

        if (element->currentStep == 60) {
            element->state = 3;
            element->currentStep = 0;
        }
        break;

    case 3:
        var_fp = vs_ending_creditsBackgroundBrightness;

        if (++element->currentStep == 60) {
            element->state = 4;
        }
        break;

    case 4:
        var_fp = vs_ending_creditsBackgroundBrightness;
        element->y += _scrollSpeed;

        if (element->y < -(60 << 8)) {
            element->state = -1;
            return;
        }
        break;
    }

    pc_frame_sprite(3, element->x >> 8, element->y >> 8, 208, 48, 0, 96, PC_GREY(var_fp),
        getClut(896, 80), PC_ITEM_MODE(0x2E), PC_ITEM_SEMI);
    pc_frame_sprite(3, element->x >> 8, element->y >> 8, 0xD0, 0x30, 0, 0x60, PC_GREY(var_fp),
        getClut(896, 81), PC_ITEM_MODE(0x4E), PC_ITEM_SEMI);
}

static int _inverseDistanceFromScreencenter(int arg0, int arg1)
{
    arg0 -= 160;
    arg1 -= 112;
    return vs_gte_rsqrt(arg0 * arg0 + arg1 * arg1);
}

static void vs_ending_creditsApplyRadialVertexFade(vs_ending_CreditsTexturedGradientQuadView* arg0, short arg1, short arg2)
    __attribute__((unused));
static void vs_ending_creditsApplyRadialVertexFade(vs_ending_CreditsTexturedGradientQuadView* arg0, short arg1, short arg2)
{
    short temp_s0;
    short v0;

    if (arg2 == 0) {
        temp_s0 = (arg1 * 128) / 80;
        v0 = temp_s0
           + (((200 - _inverseDistanceFromScreencenter(arg0->vertexZeroX, arg0->vertexZeroY)) << 7)
               / 400);

        if (v0 < 0x81) {
            arg0->vertexZeroBlue = v0;
        } else {
            arg0->vertexZeroBlue = 128;
        }

        arg0->vertexZeroRed = arg0->vertexZeroGreen = arg0->vertexZeroBlue;

        v0 = temp_s0
           + (((200 - _inverseDistanceFromScreencenter(arg0->vertexOneX, arg0->vertexOneY)) << 7)
               / 400);

        if (v0 < 0x81) {
            arg0->vertexOneBlue = v0;
        } else {
            arg0->vertexOneBlue = 128;
        }

        arg0->vertexOneRed = arg0->vertexOneGreen = arg0->vertexOneBlue;

        v0 = temp_s0
           + (((200 - _inverseDistanceFromScreencenter(arg0->vertexTwoX, arg0->vertexTwoY)) << 7)
               / 400);

        if (v0 < 0x81) {
            arg0->vertexTwoBlue = v0;
        } else {
            arg0->vertexTwoBlue = 128;
        }

        arg0->vertexTwoRed = arg0->vertexTwoGreen = arg0->vertexTwoBlue;

        v0 = temp_s0
           + (((200 - _inverseDistanceFromScreencenter(arg0->vertexThreeX, arg0->vertexThreeY)) << 7)
               / 400);

        if (v0 < 0x81) {
            arg0->vertexThreeBlue = v0;
        } else {
            arg0->vertexThreeBlue = 128;
        }

    } else {

        temp_s0 = ((0x50 - arg1) << 7) / 80;
        v0 = temp_s0
           - (((200 - _inverseDistanceFromScreencenter(arg0->vertexZeroX, arg0->vertexZeroY)) << 7)
               / 400);

        if (v0 >= 0) {
            arg0->vertexZeroBlue = v0;
        } else {
            arg0->vertexZeroBlue = 0;
        }

        arg0->vertexZeroRed = arg0->vertexZeroGreen = arg0->vertexZeroBlue;

        v0 = temp_s0
           - (((200 - _inverseDistanceFromScreencenter(arg0->vertexOneX, arg0->vertexOneY)) << 7)
               / 400);

        if (v0 >= 0) {
            arg0->vertexOneBlue = v0;
        } else {
            arg0->vertexOneBlue = 0;
        }

        arg0->vertexOneRed = arg0->vertexOneGreen = arg0->vertexOneBlue;

        v0 = temp_s0
           - (((200 - _inverseDistanceFromScreencenter(arg0->vertexTwoX, arg0->vertexTwoY)) << 7)
               / 400);

        if (v0 >= 0) {
            arg0->vertexTwoBlue = v0;
        } else {
            arg0->vertexTwoBlue = 0;
        }

        arg0->vertexTwoRed = arg0->vertexTwoGreen = arg0->vertexTwoBlue;

        v0 = temp_s0
           - (((200 - _inverseDistanceFromScreencenter(arg0->vertexThreeX, arg0->vertexThreeY)) << 7)
               / 400);

        if (v0 >= 0) {
            arg0->vertexThreeBlue = v0;
        } else {
            arg0->vertexThreeBlue = 0;
        }
    }

    arg0->vertexThreeRed = arg0->vertexThreeGreen = arg0->vertexThreeBlue;
}

static void _renderllustration(_creditsElement* element)
{
    short temp_v0;

    temp_v0 = element->state - 1;

    switch (temp_v0) {
    case 0:
        if ((element->illustId >= 4) && (element->illustId != 15)) {
            vs_ending_creditsQueueIllustrationDecode(vs_ending_creditsIllustrationTimWorkspace, vs_ending_creditsIllustrationResources[element->illustId + 1]);
        }

        element->currentStep = 0;
        element->state = 2;
        vs_ending_creditsIllustrationTexturePageX = (element->illustId % 5) * 192;
        vs_ending_creditsIllustrationPaletteY = (element->illustId % 5) + 480;
        /* fallthrough */

    case 1:
        vs_ending_creditsIllustrationBrightness = (vs_ending_creditsBackgroundBrightness * element->currentStep) / 80;

        if (++element->currentStep == 80) {
            int var_a0 = 3;
            element->currentStep = 0;
            if (element->illustId == 15) {
                var_a0 = 5;
            }
            element->state = var_a0;
        }
        break;

    case 2:
        vs_ending_creditsIllustrationBrightness = 128;

        if (++element->currentStep == _illustrationTimers[element->illustId]) {
            element->currentStep = 0;
            element->state = 4;
        }

        break;

    case 3:
        vs_ending_creditsIllustrationBrightness = (vs_ending_creditsBackgroundBrightness * (80 - element->currentStep)) / 80;

        if (++element->currentStep == 80) {
            if (++element->illustId >= 5) {
                vs_ending_creditsUploadTexture(vs_ending_creditsIllustrationTimWorkspace);
            }
            element->state = 1;
        }
        break;

    case 4:
        vs_ending_creditsIllustrationBrightness = 0x80;
        break;
    }

    pc_frame_sprite(4, 0, 0, 0xA0, 0xE0, 0, 0, PC_GREY(vs_ending_creditsIllustrationBrightness),
        getClut(0, vs_ending_creditsIllustrationPaletteY), PC_ITEM_MODE(getTPage(1, 2, vs_ending_creditsIllustrationTexturePageX, 256)), PC_ITEM_SEMI);
    pc_frame_sprite(4, 0xA0, 0, 0xA0, 0xE0, 0x20, 0, PC_GREY(vs_ending_creditsIllustrationBrightness),
        getClut(0, vs_ending_creditsIllustrationPaletteY), PC_ITEM_MODE(getTPage(1, 2, vs_ending_creditsIllustrationTexturePageX + 64, 256)), PC_ITEM_SEMI);
}

static int _creditsStrWidth(u_char* str)
{
    short width;
    short i;
    unsigned short charTableOffset;
    short len;
    int c;
    int new_var;

    len = *str++;
    width = 0;
    charTableOffset = 0;
    i = 0;

    for (i = 0; i < len; ++i, ++str) {

        c = *str;

        if (c < 32u) {
            switch (c) {
            case 1:
                charTableOffset = 0;
                break;
            case 2:
                charTableOffset = 112;
                break;
            case 31:
                --width;
                break;
            }
        } else {
            width += _glyphWidths[(charTableOffset - (new_var = 32)) + c] - 1;
        }
    }

    return width;
}

// https://decomp.me/scratch/86Hv9
void _renderText(_creditsElement* element)
{
    u_char* str;
    short i;
    short len;
    u_char c;
    u_char glyph;
    u_char charTableOffset;
    short underline;
    short x;
    short y;
    u_short width;
    short glyphWidth;

    charTableOffset = 0;
    underline = 0;

    if (element->state == 1) {
        width = element->width = _creditsStrWidth(element->data);
        element->x = ((320 - width) >> 1) << 8;
        element->y = 224 << 8;
        element->state = 2;
    }

    element->y += _scrollSpeed;

    if (element->y < -(12 << 8)) {
        element->state = -1;
        return;
    }

    y = element->y >> 8;
    str = element->data;
    len = *str++;
    x = element->x >> 8;

    for (i = 0; i < len; ++i, ++str) {
        c = *str;
        if (c < 32u) {
            switch (c) {
            case 1:
                charTableOffset = 0;
                break;
            case 2:
                charTableOffset = 112;
                break;
            case 3:
                if (charTableOffset != 0) {
                    underline = 18;
                } else {
                    underline = 17;
                }
                break;
            case 31:
                --x;
                break;
            }
        } else {
            glyph = charTableOffset + c - 32;
            glyphWidth = _glyphWidths[glyph];
            pc_frame_sprite(3, x, y, glyphWidth, 16, (glyph & 0xF) * 16, (glyph >> 4) * 16,
                PC_GREY(vs_ending_creditsBackgroundBrightness), getClut(960, 480), PC_ITEM_MODE(31), 0);
            x += glyphWidth - 1;
        }
    }

    if (underline != 0) {
        pc_item* poly;

        x = 160 - ((element->width + 32) >> 1);

        y += underline;
        pc_frame_sprite(3, x, y, 16, 3, 0, 208, PC_GREY(vs_ending_creditsBackgroundBrightness),
            getClut(960, 480), PC_ITEM_MODE(31), 0);

        poly = pc_frame_add(PC_ITEM_POLY, 3);
        x += 16;
        if (poly != NULL) {
            poly->verts = 4;
            poly->flags = PC_ITEM_TEXTURED;
            poly->x[0] = poly->x[2] = x;
            poly->x[1] = poly->x[3] = x + element->width;
            poly->y[0] = poly->y[1] = y;
            poly->y[2] = poly->y[3] = y + 3;
            poly->u[0] = poly->u[2] = 8;
            poly->v[2] = poly->v[3] = 211;
            poly->u[1] = poly->u[3] = 16;
            poly->v[0] = poly->v[1] = 208;
            poly->mode = 31;
            poly->clut = getClut(960, 480);
            poly->rgb[0][0] = poly->rgb[0][1] = poly->rgb[0][2] = vs_ending_creditsBackgroundBrightness;
        }
        x += element->width;

        pc_frame_sprite(3, x, y, 16, 3, 168, 208, PC_GREY(vs_ending_creditsBackgroundBrightness),
            getClut(960, 480), PC_ITEM_MODE(31), 0);
    }
}

void _renderCopyright(_creditsElement* arg0)
{
    if (arg0->state == 1) {
        arg0->x = 100 << 8;
        arg0->y = 224 << 8;
        arg0->state = 2;
    }

    arg0->y += _scrollSpeed;

    if (arg0->y < -(12 << 8)) {
        arg0->state = -1;
        return;
    }

    pc_frame_sprite(3, arg0->x >> 8, arg0->y >> 8, 0x78, 0x10, 0, 0x30, PC_GREY(vs_ending_creditsBackgroundBrightness),
        getClut(960, 64), PC_ITEM_MODE(getTPage(clut4Bit, semiTransparencyFull, 960, 0)), 0);
}

static void _renderBackground(_creditsElement* arg0 __attribute__((unused)))
{
    int subtract = PC_ITEM_MODE(getTPage(clut8Bit, semiTransparencySubtract, 0, 0));
    pc_item* poly;

    pc_frame_sprite(0x7FF, 0, 10, 256, 204, 0, 10, PC_GREY(vs_ending_creditsBackgroundBrightness),
        getClut(780, 255), PC_ITEM_MODE(getTPage(clut4Bit, semiTransparencyFull, 768, 0)), 0);
    pc_frame_sprite(0x7FF, 256, 10, 64, 204, 0, 10, PC_GREY(vs_ending_creditsBackgroundBrightness),
        getClut(780, 255), PC_ITEM_MODE(getTPage(clut4Bit, semiTransparencyFull, 832, 0)), 0);

    poly = pc_frame_add(PC_ITEM_POLY, 2);
    if (poly != NULL) {
        poly->verts = 4;
        poly->flags = PC_ITEM_SHADED | PC_ITEM_SEMI;
        poly->mode = subtract;
        poly->x[0] = poly->x[2] = 0;
        poly->x[1] = poly->x[3] = 320;
        poly->y[0] = poly->y[1] = 10;
        poly->y[2] = poly->y[3] = 30;
        memset(poly->rgb[0], 255, 6);
    }

    poly = pc_frame_add(PC_ITEM_POLY, 2);
    if (poly != NULL) {
        poly->verts = 4;
        poly->flags = PC_ITEM_SHADED | PC_ITEM_SEMI;
        poly->mode = subtract;
        poly->x[0] = poly->x[2] = 0;
        poly->x[1] = poly->x[3] = 320;
        poly->y[0] = poly->y[1] = 194;
        poly->y[2] = poly->y[3] = 214;
        memset(poly->rgb[2], 255, 6);
    }

    pc_frame_tile(2, 0, 0, 320, 10, 0, subtract, 0);
    pc_frame_tile(2, 0, 0, 320, 10, 0, subtract, 0);
    pc_frame_tile(2, 0, 214, 320, 10, 0, subtract, 0);
}

static void _backgroundFadeIn(_creditsElement* element)
{
    if (element->state == 1) {
        element->currentStep = 0;
        element->state = 2;
    }

    if (++element->currentStep == 128) {
        vs_ending_creditsIsBackgroundFading = 0;
        element->state = -1;
    }

    vs_ending_creditsBackgroundBrightness = (u_int)_ease(easeModeLinear, element->currentStep, 128) / 32;
}

static void _backgroundFadeOut(_creditsElement* arg0)
{
    if (arg0->state == 1) {
        arg0->state = 2;
        arg0->currentStep = 0;
        vs_ending_creditsIsBackgroundFading = 1;
    }

    if (++arg0->currentStep == 128) {
        if (arg0->width != 0) {
            vs_ending_creditsHasFinished = 1;
        }
        arg0->state = -1;
    }

    vs_ending_creditsBackgroundBrightness = 128 - ((_ease(easeModeLinear, arg0->currentStep, 128) << 7) >> 0xC);
}

static void vs_ending_creditsFadeOutForSkip(_creditsElement* element)
{
    int level;

    if (element->state == 1) {
        element->currentStep = 0;
        element->state = 2;
    }

    if (++element->currentStep == 128) {
        vs_ending_creditsHasFinished = 1;
    }

    level = (_ease(easeModeEaseOut, element->currentStep, 128) * 255) >> 0xC;
    pc_frame_tile(0, 0, 0, 320, 240, PC_GREY(level),
        PC_ITEM_MODE(getTPage(clut4Bit, semiTransparencySubtract, 0, 0)), PC_ITEM_SEMI);
}

void vs_ending_exec(void)
{
    vs_main_CdFile file;
    short i;
    vs_main_CdQueueSlot* slot;

    vs_ending_creditsAllocateRenderBuffers();
    SetGeomOffset(0xA0, 0x70);
    SetGeomScreen(0x200);
    _setDrawEnv(vs_main_drawEnv);
    _setDrawEnv(&vs_main_drawEnv[1]);

    vs_main_dispEnv[1].isinter = 0;
    vs_main_dispEnv[0].isinter = 0;

    vs_ending_creditsUploadTexture(&D_8007005C);
    vs_ending_creditsUploadTexture(&D_800CF33C);
    vs_ending_creditsUploadTexture(&D_800D7F7C);
    vs_ending_creditsUploadTexture(&D_800D975C);
    vs_ending_creditsUploadTexture(&D_800D837C);
    vs_ending_creditsUploadTexture(&D_800DB09C);
    vs_ending_creditsUploadTexture(&D_800DB26C);
    vs_ending_creditsUploadTexture(&D_8007709C);
    vs_ending_creditsUploadTexture(&D_80088ABC);
    vs_ending_creditsUploadTexture(&D_8009A4DC);
    vs_ending_creditsUploadTexture(&D_800ABEFC);
    vs_ending_creditsUploadTexture(&D_800BD91C);

    for (i = 5; i < 16; ++i) {
        file.lba = _illustLbas[i - 5];
        file.size = _illustSizes[i - 5];
        vs_ending_creditsIllustrationResources[i] = vs_main_allocHeapR(file.size);
        slot = vs_main_allocateCdQueueSlot(&file);
        vs_main_cdEnqueuePriority(slot, vs_ending_creditsIllustrationResources[i]);

        do {
            vs_main_gametimeUpdate(0);
        } while (slot->state != 4);

        vs_main_freeCdQueueSlot(slot);
    }

    vs_ending_creditsBackgroundBrightness = 0;
    _skipInvoked = 0;

    _resetElements();
    _addRenderer(_renderBackground);
    DrawSync(0);
    SetDispMask(1);

    _creditsBytestreamHead = vs_ending_creditsScriptBytes;
    _scrollSpeed = -128;
    vs_ending_creditsHasFinished = 0;
    _textStreamDelay = 0;
    vs_ending_creditsElapsedFrames = 0;
    vs_ending_creditsIsBackgroundFading = 1;

    vs_ending_creditsStartMusic();
    vs_ending_creditsRunFrames();
    vs_main_checkStreamXaEnd();
    SetDispMask(0);

    for (i = 5; i < 16; ++i) {
        vs_main_freeHeapR(vs_ending_creditsIllustrationResources[i]);
    }

    _checkStreamXaEnd();
    _updateClearGameStats();
    vs_ending_showClearResults();
}

static void _setDrawEnv(DRAWENV* drawenv)
{
    drawenv->isbg = 1;
    drawenv->dtd = 0;
    drawenv->dfe = 0;
    drawenv->b0 = 0;
    drawenv->g0 = 0;
    drawenv->r0 = 0;
    drawenv->tpage = 0x20;
}

static void vs_ending_creditsRunFrames(void)
{
    while (vs_ending_creditsHasFinished == 0) {

        vs_ending_creditsDecodeBudgetStartTick = VSync(1);

        _beginFrame();
        _parseCreditsScript();
        _renderElements();
        vs_ending_creditsDecodeIllustrationStep();

        if ((vs_main_buttonsPressed.all & PADstart) && (_skipInvoked == 0)) {
            _addRenderer(vs_ending_creditsFadeOutForSkip);
            _skipInvoked = 1;
        }

        _drawScreen();

        ++vs_ending_creditsElapsedFrames;
    }
}

static void vs_ending_creditsUploadTexture(vs_ending_CreditsTimHeader* arg0)
{
    RECT rect;
    vs_ending_CreditsTimImageBlockHeader* p = &arg0->firstBlock;

    if (arg0->mode & 8) {
        rect.x = p->rect.x;
        rect.y = p->rect.y;
        rect.w = p->rect.w;
        rect.h = p->rect.h;
        LoadImage(&rect, (void*)p->data);
        p = (vs_ending_CreditsTimImageBlockHeader*)((void*)arg0 + (p->size + 8));
    }

    rect.x = p->rect.x;
    rect.y = p->rect.y;
    rect.w = p->rect.w;
    rect.h = p->rect.h;

    LoadImage(&rect, (void*)p->data);
}

static void _resetElements(void)
{
    short i;
    _creditsElement* element = _creditsElements;
    for (i = 0; i < 32; ++i, ++element) {
        element->state = 0;
    }
}

static int _renderElements(void)
{
    short i;
    short var_s3 = 0;
    _creditsElement* element = _creditsElements;

    for (i = 0; i < 32; ++i, ++element) {
        if (element->state < 0) {
            element->state = 0;
        } else if (element->state > 0) {
            ++var_s3;
            element->renderer(element);
        }
    }

    return var_s3;
}

_creditsElement* _addRenderer(void (*renderer)(_creditsElement*))
{
    short i;
    _creditsElement* element = &_creditsElements[1];

    for (i = 1; i < 32; ++i, ++element) {
        if (element->state == 0) {
            element->renderer = renderer;
            element->state = 1;
            element->currentStep = 0;
            return element;
        }
    }

    SystemError('M', 100);
}

_creditsElement* _replaceMainRenderer(void (*renderer)(_creditsElement*))
{
    if (_creditsElements[0].state == 0) {
        _creditsElements[0].renderer = renderer;
        _creditsElements[0].state = 1;
        _creditsElements[0].currentStep = 0;
        return &_creditsElements[0];
    }

    return NULL;
}

static void _parseCreditsScript(void)
{
    enum opcodes { wait = 1 };

    u_char* bytestream;
    int op;
    int delay;

    if (_textStreamDelay != 0) {
        --_textStreamDelay;
        return;
    }

    while (1) {
        bytestream = _creditsBytestreamHead;

        switch (bytestream[0]) {

        // 0x1 Wait: u8 duration
        case wait:
            _creditsBytestreamHead = bytestream + 1;
            delay = bytestream[1] * 2;
            _creditsBytestreamHead = bytestream + 2;
            _textStreamDelay = delay;
            return;

        // 0x2 Render text: u8 len
        case 2:
            _creditsBytestreamHead = bytestream + 1;
            _addRenderer(_renderText)->data = _creditsBytestreamHead;
            _creditsBytestreamHead += _creditsBytestreamHead[0] + 1;
            break;

        // 0x3 Enable background illustrations
        case 3:
            _creditsBytestreamHead = bytestream + 1;
            _addRenderer(_renderllustration)->illustId = 0;
            break;

        // 0x4 Render special: u8 secondary op
        case 4:
            _creditsBytestreamHead = bytestream + 1;
            op = bytestream[1];
            _creditsBytestreamHead = bytestream + 2;

            switch (op) {
            case 0:
                _addRenderer(_renderCopyright);
                break;

            case 1:
                _addRenderer(_renderVagrantStory);
                break;

            case 2:
                _addRenderer(_renderFin);
                break;

            case 3:
                _addRenderer(_renderPhantomPain);
                break;
            }
            break;

        // 0x5 Fade in
        case 5:
            _creditsBytestreamHead = bytestream + 1;
            _replaceMainRenderer(_backgroundFadeIn);
            break;

        // 0x6 Fade out
        case 6:
            _creditsBytestreamHead = bytestream + 1;
            _replaceMainRenderer(_backgroundFadeOut)->width = 0;
            break;

        // 0x7 Freeze scrolling
        case 7:
            _creditsBytestreamHead = bytestream + 1;
            _scrollSpeed = 0;
            break;

        // 0x8 Set illustration timer: u8 illustration id, u16 duration
        case 8:
            *(_illustrationTimers + bytestream[1]) =
                (bytestream[2] + (bytestream[3] << 8)) - 160;
            _creditsBytestreamHead = bytestream + 4;
            break;

        // 0xFF End of stream
        case 255:
            vs_ending_creditsHasFinished = 1;
            return;

        default:
            return;
        }
    }
}

static int _ease(short mode, short currentStep, short totalSteps)
{
    switch (mode) {
    case 0:
        return (currentStep * ONE) / totalSteps;
    case 1:
        return rsin((currentStep * (ONE / 4)) / totalSteps);
    case 2:
        return (rcos(((currentStep * (ONE / 2)) / totalSteps) + (ONE / 2)) + ONE) >> 1;
    case 3:
        return rsin(((currentStep * (ONE / 4)) / totalSteps) - (ONE / 4)) + ONE;
    }
}

static void vs_ending_creditsQueueIllustrationDecode(vs_ending_CreditsTimHeader* arg0, void* arg1)
{
    vs_ending_creditsIllustrationDecodePhase = 1;
    vs_ending_creditsQueuedCompressedImage = arg1;
    vs_ending_creditsQueuedTimDestination = arg0;
}

/* Incrementally decompresses the stream queued by vs_ending_creditsQueueIllustrationDecode, yielding
   once the frame budget (200 VSync ticks since vs_ending_creditsDecodeBudgetStartTick) is used up.
   Returns 1 when the end-of-stream marker (0x7F) is reached, 0 otherwise. */
int vs_ending_creditsDecodeIllustrationStep(void)
{
    short i;
    u_char op;

    if (vs_ending_creditsIllustrationDecodePhase == 0) {
        return 0;
    }

    if (vs_ending_creditsIllustrationDecodePhase == 1) {
        vs_ending_creditsIllustrationDecodePhase = 2;
        vs_ending_creditsDecodeInputCursor = vs_ending_creditsQueuedCompressedImage;
        vs_ending_creditsDecodeOutputCursor = (u_char*)vs_ending_creditsQueuedTimDestination;
    }

    while (1) {
        if ((VSync(1) - vs_ending_creditsDecodeBudgetStartTick) > 200) {
            return 0;
        }

        op = *vs_ending_creditsDecodeInputCursor;

        if (!(op & 0x80)) {
            if (op == 0x7F) {
                vs_ending_creditsIllustrationDecodePhase = 0;
                return 1;
            }
            if (op == 0x7D) {
                // 0x7D: u8 count, u8 value: fill count + 1 bytes
                ++vs_ending_creditsDecodeInputCursor;
                vs_ending_creditsDecodeRunLength = *vs_ending_creditsDecodeInputCursor++;
                vs_ending_creditsDecodeFillByteZero = *vs_ending_creditsDecodeInputCursor++;
                for (i = 0; i <= vs_ending_creditsDecodeRunLength; ++i) {
                    *vs_ending_creditsDecodeOutputCursor++ = vs_ending_creditsDecodeFillByteZero;
                }
            } else if (op == 0x7E) {
                // 0x7E: u8 count, u8 value0, u8 value1: fill count + 1 byte pairs
                ++vs_ending_creditsDecodeInputCursor;
                vs_ending_creditsDecodeRunLength = *vs_ending_creditsDecodeInputCursor++;
                vs_ending_creditsDecodeFillByteZero = *vs_ending_creditsDecodeInputCursor++;
                vs_ending_creditsDecodeFillByteOne = *vs_ending_creditsDecodeInputCursor++;
                for (i = 0; i <= vs_ending_creditsDecodeRunLength; ++i) {
                    *vs_ending_creditsDecodeOutputCursor++ = vs_ending_creditsDecodeFillByteZero;
                    *vs_ending_creditsDecodeOutputCursor++ = vs_ending_creditsDecodeFillByteOne;
                }
            } else {
                // 0x00-0x7C: copy op + 1 literal bytes
                vs_ending_creditsDecodeRunLength = *vs_ending_creditsDecodeInputCursor++;
                for (i = 0; i <= vs_ending_creditsDecodeRunLength; ++i) {
                    *vs_ending_creditsDecodeOutputCursor++ = *vs_ending_creditsDecodeInputCursor++;
                }
            }
        } else {
            // 1LLLOOOO OOOOOOOO: copy L + 3 bytes from offset O + 1 back,
            // repeated while followed by 0x7C
            vs_ending_creditsDecodeRunLength = ((op >> 4) & 7) + 3;
            vs_ending_creditsDecodeBackreferenceOffset = ((vs_ending_creditsDecodeInputCursor[0] & 0xF) << 8) + vs_ending_creditsDecodeInputCursor[1];
            vs_ending_creditsDecodeInputCursor += 2;
            vs_ending_creditsDecodeBackreferenceCursor = vs_ending_creditsDecodeOutputCursor - vs_ending_creditsDecodeBackreferenceOffset - 1;
            for (i = 0; i < vs_ending_creditsDecodeRunLength; ++i) {
                *vs_ending_creditsDecodeOutputCursor++ = *vs_ending_creditsDecodeBackreferenceCursor++;
            }
            while (*vs_ending_creditsDecodeInputCursor == 0x7C) {
                vs_ending_creditsDecodeBackreferenceCursor = vs_ending_creditsDecodeOutputCursor - vs_ending_creditsDecodeBackreferenceOffset - 1;
                ++vs_ending_creditsDecodeInputCursor;
                for (i = 0; i < vs_ending_creditsDecodeRunLength; ++i) {
                    *vs_ending_creditsDecodeOutputCursor++ = *vs_ending_creditsDecodeBackreferenceCursor++;
                }
            }
        }
    }
}

static void vs_ending_creditsInitializeDisplay(int arg0, int arg1, int arg2, int arg3, int arg4, int arg5)
{
    RECT rect;
    int temp_s1;

    rect = (RECT) { 0, 0, 1024, 512 };

    SetDispMask(0);
    SetGeomOffset(arg0 / 2, (arg1 + (int)((u_int)arg1 >> 0x1F)) >> 1);
    SetGeomScreen(arg2);

    temp_s1 = arg1 - 0x10;

    SetDefDrawEnv(vs_main_drawEnv, 0, 0, arg0, temp_s1);
    SetDefDispEnv(vs_main_dispEnv, arg0, 0, arg0, temp_s1);
    SetDefDrawEnv(&vs_main_drawEnv[1], arg0, 0, arg0, temp_s1);
    SetDefDispEnv(&vs_main_dispEnv[1], 0, 0, arg0, temp_s1);

    setRECT(&vs_main_dispEnv[0].screen, 0, 8, 256, 224);
    setRECT(&vs_main_dispEnv[1].screen, 0, 8, 256, 224);

    D_8005DFD4 = 0;
    D_8005DFD8 = 0;
    vs_render_viewportWidth = arg0;
    vs_render_viewportHeight = temp_s1;
    vs_main_drawEnv[1].dtd = 0;
    vs_main_drawEnv[0].dtd = 0;
    vs_main_drawEnv[1].isbg = 1;
    vs_main_drawEnv[0].isbg = 1;
    setRGB0(&vs_main_drawEnv[0], arg3, arg4, arg5);
    setRGB0(&vs_main_drawEnv[1], arg3, arg4, arg5);

    ClearImage(&rect, 0, 0, 0);
    DrawSync(0);
    FntLoad(0x3C0, 0);
    SetDumpFnt(FntOpen(0x28, 0x10, 0x200, 0x100, 0, 0x200));
    PutDispEnv(&vs_main_dispEnv[vs_main_frameBuf]);
    PutDrawEnv(&vs_main_drawEnv[vs_main_frameBuf]);

    vs_main_frameDuration = 0;
}

/* Every frame here is the frame's items alone: no ordering table, no packets. */
static int _drawScreen(void)
{
    int _[2];
    int temp_s1;

    vs_main_frameBuf = vs_main_frameBuf == 0;

    DrawSync(0);

    temp_s1 = vs_main_gametimeUpdate(0);

    PutDispEnv(&vs_main_dispEnv[vs_main_frameBuf]);
    PutDrawEnv(&vs_main_drawEnv[vs_main_frameBuf]);
    pc_frame_draw();

    ++vs_main_frameDuration;

    return temp_s1;
}

static void _updateTitles(void);

static void _do_updateClearGameStats(void)
{
    int i;

    if (vs_main_scoredata.completionTimeMinutes == 0) {
        vs_main_scoredata.clearCount = vs_main_stateFlags.clearCount;
        vs_main_scoredata.completionTimeMinutes =
            (vs_main_gametime.t.h * 60) + vs_main_gametime.t.m;
    } else {
        int minutes = (vs_main_gametime.t.h * 60) + vs_main_gametime.t.m;
        if (minutes < vs_main_scoredata.completionTimeMinutes) {
            vs_main_scoredata.completionTimeMinutes = minutes;
            vs_main_scoredata.clearCount = vs_main_stateFlags.clearCount;
        }
    }

    _updateTitles();
    vs_main_bzero(&vs_main_mapStatus, sizeof vs_main_mapStatus);

    for (i = 0x10; i < 0x440; ++i) {
        ((u_char*)&vs_main_stateFlags)[i] = 0;
    }

    if (vs_main_stateFlags.difficulty != 0) {
        vs_main_stateFlags.normalModeCleared = 1;
    } else {
        vs_main_stateFlags.difficulty = 1;
    }

    if (vs_main_stateFlags.clearCount < 99) {
        ++vs_main_stateFlags.clearCount;
    }

    vs_state_savedEntryDoor.zndId = 1;
    vs_state_savedEntryDoor.mpdId = 0;
}

static void vs_ending_creditsAllocateRenderBuffers(void)
{
    vs_ending_creditsInitializeDisplay(0x140, 0xF0, 0x200, 0, 0, 0);
    vs_main_initHeap((vs_main_HeapHeader*)VS_MAIN_HEAP_ADDRESS, VS_MAIN_HEAP_SIZE);
}

static void _updateClearGameStats(void) { _do_updateClearGameStats(); }

static void _beginFrame(void)
{
    vs_main_processPadState();
    pc_frame_begin();
}

static void vs_ending_creditsStartMusic(void)
{
    vs_main_streamXa(VS_ENDING_XA_LBA, 0x7FFF); // Partial playback?
}

static void _checkStreamXaEnd(void) { vs_main_checkStreamXaEnd(); }

static void _updateTitles(void)
{
    int i;
    int j;
    u_int count;

    if (vs_main_scoredata.completionTimeMinutes < 600) {
        vs_main_scoredata.titles |= 2;
    }

    for (i = 184; i < 224; ++i) {
        if (!vs_main_actions[i].unlocked) {
            break;
        }
    }

    if (i == 224) {
        vs_main_scoredata.titles |= 1 << 14;
    }

    for (i = 22; i < 54; ++i) {
        if ((i != 33) && (i != 36) && (i != 38) && (i != 39)
            && !vs_main_actions[i].unlocked) {
            break;
        }
    }

    if (i == 54) {
        vs_main_scoredata.titles |= 1 << 15;
    }

    if (vs_main_settings.currentGameSaveCount == 0) {
        vs_main_scoredata.titles |= 1 << 16;
    }

    if (vs_main_stateFlags.magicUsed == 0) {
        vs_main_scoredata.titles |= 1 << 17;
    }

    if (vs_main_stateFlags.battleAbilitiesUsed == 0) {
        vs_main_scoredata.titles |= 1 << 18;
    }

    if (vs_main_stateFlags.breakArtsUsed == 0) {
        vs_main_scoredata.titles |= 1 << 19;
    }

    count = 0;

    for (i = 0; i < 16; ++i) {
        int t1 = 1;
        for (j = 0; j < 32; ++j) {
            if (vs_main_mapStatus.roomFlags[i] & _countableRooms[i] & (t1 << j)) {
                ++count;
            }
        }
    }

    if (vs_main_scoredata.mapCompletion < count) {
        vs_main_scoredata.mapCompletion = count;
    }

    count = 0;

    for (i = 0; i < 64; ++i) {
        if (vs_main_stateFlags.chestsOpened[i] != 0) {
            ++count;
        }
    }

    if (vs_main_scoredata.openedChestCount < count) {
        vs_main_scoredata.openedChestCount = count;
    }

    if (vs_main_stateFlags.damascusGolemDefeated != 0) {
        vs_main_scoredata.titles |= 1 << 4;
    }

    if (vs_main_stateFlags.damascusCrabDefeated != 0) {
        vs_main_scoredata.titles |= 1 << 5;
    }

    if (vs_main_stateFlags.ravanaDefeated != 0) {
        vs_main_scoredata.titles |= 1 << 6;
    }

    if (vs_main_stateFlags.dragonZombieDefeated != 0) {
        vs_main_scoredata.titles |= 1 << 7;
    }

    if (vs_main_stateFlags.deathAndOgreZombieDefeated != 0) {
        vs_main_scoredata.titles |= 1 << 8;
    }

    if (vs_main_stateFlags.asuraDefeated != 0) {
        vs_main_scoredata.titles |= 1 << 9;
    }

    if (vs_main_stateFlags.goldKeyObtained != 0) {
        vs_main_scoredata.titles |= 1 << 12;
    }

    if (vs_main_stateFlags.chestKeyObtained != 0) {
        vs_main_scoredata.titles |= 1 << 13;
    }
}

static void vs_ending_showClearResults(void)
{
    int temp_s0;

    _submenuState = 0;
    SetDispMask(1);

    do {
        _beginFrame();
        temp_s0 = vs_ending_loadClearResultsStep();
        _drawScreen();
    } while (temp_s0 == 0);

    do {
        _beginFrame();
        temp_s0 = _renderCongratulationsScreen();
        VSync(0);
        _drawScreen();
    } while (temp_s0 == 0);
}

#ifdef ROOD_PC
/* Also at file scope: build_boot.py --arch 64 reads import types from the
 * globals' DWARF, and these pointers must move natively there. */
extern vs_main_CdQueueSlot* cdSlot;
extern void* timData;
#endif

static int vs_ending_loadClearResultsStep(void)
{
    static const vs_main_CdFile endScrDisFiles[] = {
        { VS_END_DIS_LBA, VS_END_DIS_SIZE },
        { VS_ENDSCR00_DIS_LBA, VS_ENDSCR00_DIS_SIZE },
        { VS_ENDSCR01_DIS_LBA, VS_ENDSCR01_DIS_SIZE },
        { VS_ENDSCR02_DIS_LBA, VS_ENDSCR02_DIS_SIZE },
        { VS_ENDSCR03_DIS_LBA, VS_ENDSCR03_DIS_SIZE },
        { VS_ENDSCR04_DIS_LBA, VS_ENDSCR04_DIS_SIZE },
        { VS_ENDSCR05_DIS_LBA, VS_ENDSCR05_DIS_SIZE },
        { VS_ENDSCR06_DIS_LBA, VS_ENDSCR06_DIS_SIZE },
        { VS_ENDSCR07_DIS_LBA, VS_ENDSCR07_DIS_SIZE },
        { VS_ENDSCR08_DIS_LBA, VS_ENDSCR08_DIS_SIZE },
        { VS_ENDSCR09_DIS_LBA, VS_ENDSCR09_DIS_SIZE },
        { VS_ENDSCR10_DIS_LBA, VS_ENDSCR10_DIS_SIZE },
        { VS_ENDSCR11_DIS_LBA, VS_ENDSCR11_DIS_SIZE },
        { VS_ENDSCR12_DIS_LBA, VS_ENDSCR12_DIS_SIZE },
        { VS_ENDSCR13_DIS_LBA, VS_ENDSCR13_DIS_SIZE },
        { VS_ENDSCR14_DIS_LBA, VS_ENDSCR14_DIS_SIZE },
        { VS_ENDSCR15_DIS_LBA, VS_ENDSCR15_DIS_SIZE },
    };

    extern vs_main_CdQueueSlot* cdSlot;
    extern void* timData;

    TIM_IMAGE sp10;
    int i;
    int var_a1;
    int var_v1;

    if (_submenuState == 0) {

        timData = vs_main_allocHeapR(endScrDisFiles->size);
        cdSlot = vs_main_allocateCdQueueSlot(endScrDisFiles);

        vs_main_cdEnqueue(cdSlot, timData);

        ++_submenuState;

    } else if (_submenuState == 1) {

        if (cdSlot->state != vs_main_CdQueueStateLoaded) {
            return 0;
        }

        for (i = 0; i < 3; ++i) {

            _parseTim(timData + i * 0x8220, &sp10);

            if (sp10.paddr != NULL) {
                sp10.prect->x = 832 + i * 64;
                sp10.prect->y = 0x100;
                sp10.prect->h = 0xFF;
                LoadImage(sp10.prect, sp10.paddr);
            }

            if (i == 0) {
                if (sp10.caddr != NULL) {
                    sp10.crect->x = 0x300;
                    sp10.crect->y = 0x1FF;
                    sp10.crect->w = 0x80;
                    sp10.crect->h = 1;
                    *sp10.caddr = 0;
                    LoadImage(sp10.crect, sp10.caddr);
                }
            }
        }

        vs_main_freeCdQueueSlot(cdSlot);
        ++_submenuState;

    } else if (_submenuState == 2) {

        vs_main_freeHeapR(timData);

        var_a1 = 0;
        _onScreenMapCompletion = 0;
        _onScreenScore = 0;
        vs_ending_resultsAnimationFrame = 0;
        vs_ending_isLeavingResults = 0;
        _clearCount = vs_main_stateFlags.clearCount;

        for (i = 0; i < 16; ++i) {
            int flag;
            for (flag = 0; flag < 32; ++flag) {
                int v = 1;
                if (vs_main_mapStatus.roomFlags[i] & _countableRooms[i] & (v << flag)) {
                    ++var_a1;
                }
            }
        }

        var_v1 = 0;

        for (i = 0; i < 64; ++i) {
            if (vs_main_stateFlags.chestsOpened[i] != 0) {
                ++var_v1;
            }
        }

        if (vs_main_scoredata.mapCompletion < var_a1) {
            vs_main_scoredata.mapCompletion = var_a1;
        }

        if (vs_main_scoredata.openedChestCount < var_v1) {
            vs_main_scoredata.openedChestCount = var_v1;
        }

        _mapCompletion = (vs_main_scoredata.mapCompletion * 100) / 361;

        _updateScore();
        _determineRank();

        timData = vs_main_allocHeapR(endScrDisFiles[_rank + 1].size);
        cdSlot = vs_main_allocateCdQueueSlot(&endScrDisFiles[_rank + 1]);
        vs_main_cdEnqueue(cdSlot, timData);
        ++_submenuState;

    } else if (_submenuState == 3) {

        if (cdSlot->state != 4) {
            return 0;
        }

        _parseTim(timData, &sp10);

        if (sp10.paddr != NULL) {
            sp10.prect->x = 0;
            sp10.prect->y = 0x100;
            LoadImage(sp10.prect, sp10.paddr);
        }

        vs_main_freeCdQueueSlot(cdSlot);

        ++_submenuState;

    } else {
        _submenuState = 0;
        vs_main_freeHeapR(timData);

        return 1;
    }

    return 0;
}

static int _renderCongratulationsScreen(void)
{
    if (vs_ending_isLeavingResults == 0) {

        if (vs_ending_resultsAnimationFrame == 0xD0) {
            vs_main_playSfxDefault(0x7E, 0x77);
            vs_main_playSfxDefault(0x7E, 0x78);
        }

        vs_ending_drawRankBackground(vs_ending_resultsAnimationFrame);
#ifdef ROOD_PC
        int pcFirst = _pcLeavesBegin();
#endif
        _renderCongratulations(0xA0, 0x28, vs_ending_resultsAnimationFrame);
        _renderIncrementalScore(0xA0, 0x50, vs_ending_resultsAnimationFrame - 0x20, vs_ending_isLeavingResults);
        _renderIncrementalMapCompletion(0xA0, 0x66, vs_ending_resultsAnimationFrame - 0x70, vs_ending_isLeavingResults);
        _renderRiskbreakerRankHeader(0xA0, 0x8A, vs_ending_resultsAnimationFrame - 0xC0);
        _renderRiskbreakerRank(0xA0, 0x9A, vs_ending_resultsAnimationFrame - 0xD0);
#ifdef ROOD_PC
        _pcLeavesEnd(pcFirst);
#endif

        if (vs_ending_resultsAnimationFrame >= 0x1001) {
            vs_ending_resultsAnimationFrame = 0x1000;
        }

        if (vs_main_buttonsPressed.all != 0) {
            vs_ending_isLeavingResults = 1;
            vs_ending_resultsAnimationFrame = 0;
        }

    } else {

        vs_ending_drawRankBackground(8 - vs_ending_resultsAnimationFrame);
#ifdef ROOD_PC
        int pcFirst = _pcLeavesBegin();
#endif
        _renderCongratulations(0xA0, 0x28, 8 - vs_ending_resultsAnimationFrame);
        _renderScore(0xA0, 0x50, 8 - vs_ending_resultsAnimationFrame, 3);
        _renderMapCompletion(0xA0, 0x66, 8 - vs_ending_resultsAnimationFrame, 3);
        _renderRiskbreakerRankHeader(0xA0, 0x8A, 8 - vs_ending_resultsAnimationFrame);
        _renderRiskbreakerRank(0xA0, 0x9A, 8 - vs_ending_resultsAnimationFrame);
#ifdef ROOD_PC
        _pcLeavesEnd(pcFirst);
#endif

        if (vs_ending_resultsAnimationFrame >= 8) {
            return 1;
        }
    }

    ++vs_ending_resultsAnimationFrame;
    return 0;
}

void vs_ending_drawRankBackground(int arg0)
{
    int i;
    int temp_s4;
    pc_item* poly;

    if (arg0 < 0) {
        arg0 = 0;
    }

    if (arg0 > 64) {
        arg0 = 64;
    }

    temp_s4 = arg0 * 2;

    if (arg0 <= 0) {
        return;
    }

    for (i = 0; i < 5; ++i) {
        poly = pc_frame_add(PC_ITEM_POLY, 0x7FE);
        if (poly == NULL) {
            continue;
        }
        poly->verts = 4;
        poly->flags = PC_ITEM_TEXTURED;
        poly->x[0] = poly->x[2] = i * 64;
        poly->x[1] = poly->x[3] = 64 + i * 64;
        poly->y[0] = poly->y[1] = 8;
        poly->y[2] = poly->y[3] = 232;
        poly->u[1] = poly->u[3] = 64;
        poly->v[2] = poly->v[3] = 224;
        poly->rgb[0][0] = poly->rgb[0][1] = poly->rgb[0][2] = temp_s4;
        poly->mode = (i + 16) | getTPage(3, 0, 0, 0);
    }

}

static void _renderCongratulations(int arg0, int arg1, int arg2)
{
    extern P_CODE vs_ending_congratulationsTint[2];

    if (arg2 < 0) {
        arg2 = 0;
    }

    if (arg2 > 64) {
        arg2 = 64;
    }

    if (arg2 > 0) {
        vs_ending_congratulationsTint[0].code = arg2;
        vs_ending_congratulationsTint[1].code = arg2;
        arg0 = arg0 - ((_disMap[22].w + _disMap[23].w) >> 1);
        _renderTexturePopIn(arg0, arg1, 0x16, vs_ending_congratulationsTint);
        _renderTexturePopIn(arg0 + _disMap[22].w, arg1, 0x17, &vs_ending_congratulationsTint[1]);
    }
}

static void _renderScore(int arg0, int arg1, int arg2, int arg3 __attribute__((unused)))
{
    extern P_CODE vs_ending_scoreTint[];

    char sp10[10];
    int i;
    int v0;
    int v1;

    if (arg2 < 0) {
        arg2 = 0;
    }

    if (arg2 > 64) {
        arg2 = 64;
    }

    if (arg2 > 0) {

        vs_ending_scoreTint[0].code = arg2;

        sprintf(sp10, "%09d", _score);

        v1 = _disMap[18].w + _disMap[26].w + _disMap[20].w;
        arg0 -= (_disMap[10].w * 2 + v1 + 0x74) >> 1;

        _renderTextureFadeInTint(arg0, arg1, 0x12, vs_ending_scoreTint);

        arg0 += _disMap[18].w;

        _renderTextureFadeInTint(arg0, arg1 + 7, 0x1A, vs_ending_scoreTint);

        v0 = arg0 + 2;
        arg0 = v0 + _disMap[26].w;

        for (i = 0; i < 9; ++i) {
            _renderTextureFadeInTint(arg0, arg1 + 3, sp10[i] - 0x30, vs_ending_scoreTint);
            arg0 += 12;
            if ((i == 2) || (i == 5)) {
                int v0;
                _renderTextureFadeInTint(arg0, arg1 + 0xE, 0xA, vs_ending_scoreTint);
                v0 = arg0 + 3;
                arg0 = v0 + _disMap[10].w;
            }
        }

        _renderTextureFadeInTint(arg0, arg1 + 8, 0x14, vs_ending_scoreTint);
    }
}

static void _renderIncrementalScore(
    int arg0, int arg1, int arg2, int arg3 __attribute__((unused)))
{
    extern P_CODE vs_ending_countingScoreTint[];

    char sp18[16];
    int temp_s2;
    int var_s1;
    int i;
    int v0;
    int v1;

    var_s1 = arg2;

    if (var_s1 < 0) {
        var_s1 = 0;
    }

    if (var_s1 > 64) {
        var_s1 = 64;
    }

    if (var_s1 > 0) {
        temp_s2 = arg0 + (var_s1 * 8);

        if (_score != 0 && var_s1 >= 32) {

            if (_onScreenScore == 0) {
                _incrementingScore = 1;
                vs_main_playSfxDefault(0x7E, 0x72);
            }

            if ((_onScreenScore == _score) && (_incrementingScore != 0)) {
                _incrementingScore = 0;
                vs_audio_stopSfx(0x7E, 0x72);
                vs_main_playSfxDefault(0x7E, 0x73);
            }
        }

        if (var_s1 >= 32) {
            _onScreenScore = ((u_int)_score >> 5) * (var_s1 - 32);
        }

        if (var_s1 == 64) {
            _onScreenScore = _score;
        }

        sprintf(sp18, "%09d", _onScreenScore);

        v1 = _disMap[18].w + _disMap[26].w + _disMap[20].w;
        arg0 -= (_disMap[10].w * 2 + v1 + 0x74) >> 1;

        _renderTextureWipe(arg0, arg1, 0x12, vs_ending_countingScoreTint, temp_s2);

        arg0 += _disMap[18].w;

        _renderTextureWipe(arg0, arg1 + 7, 0x1A, vs_ending_countingScoreTint, temp_s2);

        v0 = arg0 + 2;
        arg0 = v0 + _disMap[26].w;

        for (i = 0; i < 9; ++i) {
            _renderTextureWipe(arg0, arg1 + 3, sp18[i] - '0', vs_ending_countingScoreTint, temp_s2);
            arg0 += 12;
            if ((i == 2) || (i == 5)) {
                _renderTextureWipe(arg0, arg1 + 0xE, 0xA, vs_ending_countingScoreTint, temp_s2);
                arg0 += 3 + _disMap[10].w;
            }
        }

        _renderTextureWipe(arg0, arg1 + 8, 0x14, vs_ending_countingScoreTint, temp_s2);
    }
}

static void _renderMapCompletion(
    int arg0, int arg1, int arg2, int arg3 __attribute__((unused)))
{
    extern P_CODE vs_ending_mapCompletionTint[];

#ifdef ROOD_PC
    /* sprintf writes four bytes; the PS1 overran into unused frame space. */
    char sp10[4];
#else
    char sp10[2];
#endif
    int i;
    int v0;

    if (arg2 < 0) {
        arg2 = 0;
    }

    if (arg2 > 64) {
        arg2 = 64;
    }

    if (arg2 > 0) {

        vs_ending_mapCompletionTint[0].code = arg2;

        sprintf(sp10, "%03d", _mapCompletion);

        arg0 -= ((_disMap[21].w + _disMap[26].w + _disMap[19].w + 0x26) >> 1);

        _renderTextureFadeInTint(arg0, arg1, 0x15, vs_ending_mapCompletionTint);

        arg0 += _disMap[21].w;

        _renderTextureFadeInTint(arg0, arg1 + 7, 0x1A, vs_ending_mapCompletionTint);

        v0 = arg0 + 2;
        arg0 = v0 + _disMap[26].w;

        for (i = 0; i < 3; ++i) {
            _renderTextureFadeInTint(arg0, arg1 + 3, sp10[i] - '0', vs_ending_mapCompletionTint);
            arg0 += 12;
        }

        _renderTextureFadeInTint(arg0, arg1 + 8, 0x13, vs_ending_mapCompletionTint);
    }
}

static void _renderIncrementalMapCompletion(
    int arg0, int arg1, int arg2, int arg3 __attribute__((unused)))
{
    extern P_CODE vs_ending_countingMapCompletionTint[];

#ifdef ROOD_PC
    /* sprintf writes four bytes; the PS1 overran into unused frame space. */
    char sp18[4];
#else
    char sp18[2];
#endif
    int temp_s4;
    int i;
    int v0;

    if (arg2 < 0) {
        arg2 = 0;
    }

    if (arg2 > 64) {
        arg2 = 64;
    }

    if (arg2 <= 0) {
        return;
    }

    temp_s4 = arg0 + arg2 * 8;

    if (_mapCompletion != 0 && arg2 >= 32) {

        if (_onScreenMapCompletion == 0) {
            _incrementingMapCompletion = 1;
            vs_main_playSfxDefault(0x7E, 0x72);
        }

        if ((_onScreenMapCompletion == _mapCompletion)
            && (_incrementingMapCompletion != 0)) {
            _incrementingMapCompletion = 0;
            vs_audio_stopSfx(0x7E, 0x72);
            vs_main_playSfxDefault(0x7E, 0x73);
        }
    }

    if (arg2 >= 32) {
        if (_mapCompletion >= 32u) {
            _onScreenMapCompletion = (u_int)(_mapCompletion * (arg2 - 32)) >> 5;
        } else if (_onScreenMapCompletion < _mapCompletion) {
            _onScreenMapCompletion += 1;
        }
    }

    sprintf(sp18, "%03d", _onScreenMapCompletion);

    arg0 -= ((int)(_disMap[21].w + _disMap[26].w + _disMap[19].w + 0x26) >> 1);

    _renderTextureWipe(arg0, arg1, 0x15, vs_ending_countingMapCompletionTint, temp_s4);

    arg0 += _disMap[21].w;

    _renderTextureWipe(arg0, arg1 + 7, 0x1A, vs_ending_countingMapCompletionTint, temp_s4);

    v0 = arg0 + 2;
    arg0 = v0 + _disMap[26].w;

    for (i = 0; i < 3; ++i) {
        _renderTextureWipe(arg0, arg1 + 3, sp18[i] - '0', vs_ending_countingMapCompletionTint, temp_s4);
        arg0 += 12;
    }

    _renderTextureWipe(arg0, arg1 + 8, 0x13, vs_ending_countingMapCompletionTint, temp_s4);
}

static void _renderRiskbreakerRankHeader(int x, int y, int timer)
{
    if (timer < 0) {
        timer = 0;
    }

    if (timer > 64) {
        timer = 64;
    }

    if (timer > 0) {
        vs_ending_rankHeadingTint[0].code = timer;
        _renderTextureFadeInTint(x - (vs_ending_rankHeadingWidth >> 1), y, 12, vs_ending_rankHeadingTint);
    }
}

void _renderRiskbreakerRank(int x, int y, int timer)
{
    extern P_CODE vs_ending_rankLabelTint[];

    int xInset;
    int i;

    if (timer < 0) {
        timer = 0;
    }

    if (timer > 64) {
        timer = 64;
    }

    xInset = 0;

    if (timer > 0) {

        vs_ending_rankLabelTint[0].code = timer;

        for (i = 0; i < _riskbreakerRanks[_rank][0]; ++i) {
            xInset += _disMap[_riskbreakerRanks[_rank][i + 1]].w;
        }

        xInset /= 2;
        x -= xInset;

        for (i = 0; i < _riskbreakerRanks[_rank][0]; ++i) {
            _renderTexturePopIn(x, y, _riskbreakerRanks[_rank][i + 1], vs_ending_rankLabelTint);

            x += _disMap[_riskbreakerRanks[_rank][i + 1]].w;
        }
    }
}

#include "pc/src/game/renderTextures.h"

static void _parseTim(u_int* data, TIM_IMAGE* tim)
{

    ++data;
    tim->mode = *data;
    ++data;

    if (tim->mode & 8) {
        tim->crect = (void*)(data + 1);
        tim->caddr = (void*)(data + 3);
        data += data[0] / 4;
    } else {
        tim->crect = 0;
        tim->caddr = 0;
    }

    tim->prect = (void*)(data + 1);
    tim->paddr = (void*)(data + 3);
}

static void _determineRank(void)
{
    int unlockedTitles;
    int flag;
    int i;

    for (i = 0, unlockedTitles = 0, flag = 1; i < 16; ++i) {
        if (vs_main_scoredata.titles & (flag << i)) {
            ++unlockedTitles;
        }
    }

    for (i = 0; i < 16; ++i) {
        if (unlockedTitles >= titleThresholds[i]) {
            if (_score >= rankScores[i]) {
                _rank = i;
                return;
            }
        }
    }
}

static void _updateScore(void)
{
    short enemyKillPoints[] = { 20, 20, 40, 80, 100, 60 };
    short weaponAttackPoints[] = { 20, 40, 20, 100, 60, 100, 60, 100, 80, 60 };
    int i;

    _score = 0;

    for (i = 0; i < 6; ++i) {
        _score += vs_main_scoredata.enemyKills[i] * enemyKillPoints[i];
    }

    for (i = 0; i < 10; ++i) {
        _score += vs_main_scoredata.weaponAttacks[i] * weaponAttackPoints[i];
    }

    for (i = 0; i < 8; ++i) {
        if (vs_main_scoredata.bossTimeTrialScores[i][0].time != 0x800000) {
            _score += 20000;
        }
    }

    if (vs_main_scoredata.maxChain < 6) {
        _score += vs_main_scoredata.maxChain * 10;
    } else if (vs_main_scoredata.maxChain < 11) {
        _score += vs_main_scoredata.maxChain * 20;
    } else if (vs_main_scoredata.maxChain < 16) {
        _score += vs_main_scoredata.maxChain * 40;
    } else if (vs_main_scoredata.maxChain < 21) {
        _score += vs_main_scoredata.maxChain * 80;
    } else if (vs_main_scoredata.maxChain < 26) {
        _score += vs_main_scoredata.maxChain * 160;
    } else if (vs_main_scoredata.maxChain < 31) {
        _score += vs_main_scoredata.maxChain * 320;
    } else if (vs_main_scoredata.maxChain < 51) {
        _score += vs_main_scoredata.maxChain * 640;
    } else {
        _score += vs_main_scoredata.maxChain * 1280;
    }

    _score += _clearCount * 100000;

    if ((_clearCount != 0) && (vs_main_scoredata.completionTimeMinutes < 600)) {
        if (vs_main_scoredata.completionTimeMinutes >= 540) {
            _score += _score / 4;
        } else if (vs_main_scoredata.completionTimeMinutes >= 300) {
            _score += _score / 2;
        } else {
            _score *= 2;
        }
    }

    _score += vs_main_scoredata.streakScore;
    _score += vs_main_scoredata.miscScore;

    if (_score > 999999999) {
        _score = 999999999;
    }
}
