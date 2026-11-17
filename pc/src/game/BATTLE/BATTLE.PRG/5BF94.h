#pragma once
#include "../../SLUS_010.40/main.h"
#include "146C.h"
#include "6E644.h"
#include "pc/src/psx_types.h"

enum vs_battle_warlockSpellIds_e {
    vs_battle_spell_solidShock = 0x61,
    vs_battle_spell_lightningBolt,
    vs_battle_spell_fireball,
    vs_battle_spell_vulcanLance,
    vs_battle_spell_aquaBlast,
    vs_battle_spell_spiritSurge,
    vs_battle_spell_darkChant,
    vs_battle_spell_exorcism,
    vs_battle_spell_banish,
    vs_battle_spell_explosion,
    vs_battle_spell_thunderburst = 0x6E,
    vs_battle_spell_flameSphere = 0x72,
    vs_battle_spell_gaeaStrike = 0x76,
    vs_battle_spell_avalanche = 0x7A,
    vs_battle_spell_radialSurge = 0x7E,
    vs_battle_spell_meteor = 0x82,
    vs_battle_spell_drainHeart = 0x5E,
    vs_battle_spell_drainMind = 0x5F
};

enum vs_battle_menuItemScrollable {
    menuItem_scrollNone,
    menuItem_scrollUp,
    menuItem_scrollDown
};

enum vs_battle_menuItemState {
    menuItemStateInactive,
    menuItemStateIdle,
    menuItemStateSlideX,
    menuItemStateSlideY,
    menuItemStateSlideXY,
    menuItemStateDismiss = 5
};

typedef struct {
    u_char state;
    u_char w;
    u_char backgroundWidth;
    u_char animationStep;
    u_char gradientState;
    u_char isScrollable;
    u_char selected;
    u_char unselectable;
    u_char buttonId;
    u_char rowIcon;
    u_char fontColor;
    u_char invertGradient;
    u_char material;
    u_char outsetIcon;
    u_short itemPage;
    u_short count;
    u_short max;
    short x;
    short y;
    short targetPosition0;
    short targetPosition1;
    char text[32];
    char* subText;
} vs_battle_menuItem_t;

enum vs_battle_menuStates {
    menuStateMainMenu,
    menuStateNoneClosing = 0x1F,
    menuStateNoneClosed = 0x3F,
    menuStateOpenedFromMainMenu = 0x40
};

typedef struct {
    char currentMenu;
    char returnState;
    char executeAbilityType;
    char loading;
    vs_main_CdQueueSlot* cdQueue;
} vs_battle_menuState_t;

/* The GIM image loader's record: three in a row, shared with 4A0A8.c's view of them. */
typedef struct {
    u_int unk0_0 : 1;
    u_int unk0_1 : 1;
    u_int unk0_2 : 1;
    u_int unk0_3 : 3;
    u_int unk0_6 : 3;
    u_int unk0_9 : 3;
    u_int unk0_12 : 1;
    u_int unk0_13 : 3;
    char unk2;
    char unk3;
    short unk4;
    short id;
    short unk8;
    short unkA;
    short unkC;
    short unkE;
    u_short* data;
    vs_main_CdQueueSlot* cdQueueSlot;
    short unk18[240];
    u_short imageTable[254];
    int unk3F4;
} gim_t;

typedef struct {
    u_int unk0_0 : 4;
    u_int unk0_4 : 2;
    u_int unk0_6 : 2;
    u_int unk0_8 : 4;
    u_int unk0_12 : 4;
    u_int arrowAnimFrame : 8;
    u_int unk0_24 : 6;
    u_int done : 1;
    u_int unk0_31 : 1;
} vs_battle_textBox_fields;

typedef struct {
    vs_battle_textBox_fields flags;
    short unk4;
    short unk6;
    char* string;
    u_short speed;
    u_short prevChunksPrinted;
    short xIndent;
    short yIndent;
    short x;
    short y;
    short centerX;
    short centerY;
    short state;
    u_char brightness;
    u_char unk1F;
    char charsPerLine;
    char lineCount;
    char unk22;
    char unk23;
    short unk24;
    short unk26;
    short unk28;
    short unk2A;
    short unk2C;
    short unk2E;
} vs_battle_textBox;

typedef struct {
    char cursorMemories[52];
    char unk34[6];
    struct {
        char currentItemCategory;
        char unk1;
        union {
            struct {
                u_char index;
                u_char page;
            } cursor;
            u_short value;
        } unk2[8];
    } unk3A;
    char unk4C[4];
    char unk50;
    char unk51[51];
    char selectedEquipment;
    char unk85[25];
    u_short unk9E;
    char unkA0[0x12];
    char unkB2[0x40];
} D_800F4EE8_t;

typedef struct {
    short unk0;
    short unk2;
    union {
        struct {
            u_int unk0_0 : 9;
            u_int unk0_9 : 5;
            u_int unk0_14 : 5;
            u_int unk0_19 : 13;
        } fields;
        u_short u16[2];
        int s32;
    } executeAbility;
} D_800F18EC_t;

typedef struct {
    int integers[10];
    char* strings[10];
} vs_battle_stringContext_t;

typedef struct {
    short unk0[6];
    short unkC[2];
    u_char unk10;
    u_char unk11;
    u_char unk12;
    u_char unk13;
    u_char unk14;
    u_char unk15;
    u_char unk16[4];
} pFileBlock9;

typedef struct {
    u_short count;
    u_short dataOffset;
    u_char curveSizes[0];
} pFileCurveBlock;

typedef struct {
    short unk0;
    short unk2;
} func_800CFE1C_t;

typedef struct {
    u_short unk0;
    u_short dataIndex;
} pFileBlock1Indices;

typedef struct {
    int unk0;
    short unk4;
    u_char unk6;
    u_char unk7;
    short unk8;
    short unkA;
    short unkC;
    u_short unkE;
    int unk10;
    short unk14;
    u_char unk16;
    u_char unk17;
    u_char* unk18;
    pFileBlock1Indices* unk1C;
} func_800D6CF_t;

typedef struct {
    u_char unk0;
    u_char unk1;
    u_char unk2;
    u_char unk3;
    u_int flags : 26;
    u_int transparencyCurve : 6;
    u_char unk8;
    u_char unk9;
    u_char unkA;
    u_char unkB;
    u_char unkC;
    u_char unkD;
    u_char unkE;
    u_char unkF;
    u_char unk10;
    u_char unk11;
    u_char unk12;
    u_char unk13;
    u_char unk14;
    u_char unk15;
    u_char unk16;
    u_char unk17;
    short unk18[6];
    short unk24[6];
    short unk30[2];
    short unk34[8][6];
    short pairRanges[5][4];
    short unkBC[2];
    short unkC0[2];
    u_char rCurve;
    u_char gCurve;
    u_char bCurve;
    u_char unkC7;
    u_char unkC8;
    u_char unkC9;
    u_char unkCA;
    u_char unkCB;
} func_800FA098_arg0;

typedef union {
    int packed;
    struct {
        u_char rCurve;
        u_char gCurve;
        u_char bCurve;
        u_char age;
    } fields;
} func_800D2904_curves;

typedef struct {
    VECTOR unk0;
    VECTOR unk10;
    int unk20;
    u_int flags;
    u_short unk28;
    u_short unk2A;
    SVECTOR unk2C;
    VECTOR unk34;
    VECTOR unk44;
    u_char unk54[0x10];
    MATRIX unk64;
    u_char unk84[0x14];
    VECTOR unk98;
    VECTOR unkA8;
    VECTOR unkB8;
    VECTOR unkC8;
    VECTOR unkD8;
    VECTOR unkE8;
    SVECTOR forceMin;
    SVECTOR unk100;
    SVECTOR accelerationMin;
    SVECTOR accelerationMax;
    int speedRange[2];
    int unk120;
    int unk124[2];
    int unk12C[2];
    int unk134[2];
    SVECTOR attractionOffset;
    SVECTOR attractionJitter;
    int attractionRange[2];
    func_800D2904_curves colorCurves;
    u_char unk158[0x10];
    int unk168;
    int unk16C;
    int unk170;
    int unk174;
    int unk178;
    int unk17C;
    u_char unk180;
    int ringSegments;
    int ringPositions;
} func_800FA098_arg1;

typedef struct {
    int count;
    func_800FA098_arg0 definitions[0];
} pFileBlock5;

typedef struct {
    int count;
    u_short offsets[0];
} pFileBlock3;

typedef struct {
    short clut;
    short tpage;
    u_char u0;
    u_char v0;
    u_char u1;
    u_char v1;
    u_char unk8[0x10];
} pFileBlock1Data;

typedef struct {
    int unk0;
    int unk4;
    pFileBlock5* block5Data;
    u_char* curves[32];
    pFileCurveBlock* curveBlock;
    char* block8Data;
    int block1TableCount;
    pFileBlock1Indices* block1Tables[4];
    pFileBlock1Data* block1Data;
    int block2Count;
    u_short* block2Data;
    pFileBlock9* block9Data;
    void* block10Data;
    pFileBlock3* block3Data;
    u_char* block11Data;
    void* block12Data;
    void* block13Data;
    int unkCC;
    void* unkD0;
} D_800F569C_t;

typedef struct {
    VECTOR position;
    VECTOR lookAt;
    int unk20;
    int unk24;
    int unk28;
    int unk2C;
    int roll;
    int nearClip;
    int projectionDistance;
    int farClip;
} D_800F54D8_t;

typedef struct func_800D2904_t {
    struct func_800D2904_t* previous;
    struct func_800D2904_t* next;
    short unk8;
    short unkA;
    int unkC;
    int unk10;
    int unk14;
    int unk18[3];
    int unk24[3];
    int unk30[3];
    func_800D6CF_t sprite; /* the sprite state (the PS1's int unk3C[8]) */
    int unk5C;
    u_int unk60_0 : 8;
    u_int unk60_8 : 4;
    u_int unk60_12 : 4;
    short unk62[3];
    short lifetime;
    u_char unk6A;
    u_char unk6B;
    u_char endEvent;
    u_char tickEvent;
    char unk6E[2];
    int unk70;
    func_800D2904_curves unk74;
} func_800D2904_t;

typedef struct {
    int unk0;
    int unk4;
    int unk8;
    int unkC;
    int unk10;
    int unk14;
    int unk18;
    int unk1C;
    int unk20;
    int unk24;
    int unk28;
    int unk2C;
    int unk30;
    u_char unk34;
    u_char unk35;
    u_char unk36;
    u_char unk37;
    MATRIX unk38;
    MATRIX unk58;
    u_char unk78[0x46];
    short unkBE;
    u_char unkC0[0x10];
} func_800CE714_t2_2;

typedef struct {
    u_short unk0;
    u_short unk2;
    func_8006CE70_t unk4;
} D_800F53B8_t3_2;

typedef struct {
    u_short unk0_0 : 8;
    u_short unk0_8 : 8;
} func_800D5780_t2;

typedef struct {
    /* 0x0 */ u_long* keys;
    /* 0x4 */ func_800D5780_t2 header;
    /* 0x6 */ short keyIndex;
    /* 0x8 */ short unk8;
    /* 0xA */ short unkA;
    /* 0xC */ int unkC;
} func_800D5780_t;

typedef struct {
    char trackCount;
    char unk1;
    short frame;
    int activeTracks;
    func_800D5780_t tracks[0];
} D_800F53B8_t4;

typedef struct {
    D_800F53B8_t3_2 unk0;
    D_800F53B8_t3_2 unkC;
    D_800F53B8_t3_2 unk18;
    D_800F53B8_t3_2 unk24;
    D_800F53B8_t4* unk30;
    func_800D4910_t* unk34;
    short effectId;
    short unk3A;
    int unk3C;
} D_800F53B8_t3;

typedef struct D_800F53B8_t {
    struct D_800F53B8_t* next;
    struct D_800F53B8_t* previous;
    char unk8;
    char unk9;
    u_short cursor; /* the script cursor: offset of the next command byte */
    char* unkC;
    char unk10[4];
    u_int unk14_0 : 8;
    u_int unk14_8 : 3;
    u_int unk14_11 : 5;
    u_int unk14_16 : 16;
    func_800D2904_t* unk18;
    func_800CE714_t2_2 unk1C[16];
    D_800F53B8_t3 context;
} D_800F53B8_t;

typedef struct {
    u_char unk0[0xC];
    VECTOR unkC;
    u_char unk1C[0x44];
    u_int unk60 : 8;
    u_int unk60_8 : 4;
    u_int unk60_12 : 20;
    u_char unk64[20];
} func_800FB4C0_t;

enum vs_battle_limbStatus {
    limbStatusCritical,
    limbStatusDamaged,
    limbStatusWounded,
    limbStatusGood,
    limbStatusExcellent
};

int vs_battle_updateSphereTargetPosition(SVECTOR*);
int vs_battle_updateTargetSelectionMenu(u_char*);
int vs_battle_setTextBox(int, char*);
int vs_battle_selectTextBox(u_char);
char* vs_battle_printf(char*, char*);

/**
 * Renders ASCII text and chains it to the provided OT.
 */
void vs_battle_renderTextRawColor(char const* text, int xy, int color, void* nextPrim);

/**
 * Same as vs_battle_renderTextRawColor, except color is predefined as gray midpoint
 * (0x808080).
 */
void vs_battle_renderTextRaw(char const* text, int xy, void* nextPrim);

/**
 * Renders a glyph from the font table at the specified location.
 * @return Glyph width
 */
int vs_battle_printVariableWidthFontChar(u_int glyphId, int x, int y, u_long* ot);
void vs_battle_setFontStyle(int arg0);
void vs_battle_loadGim(int, int);
void vs_render_activateGimLayer(u_int, void*, u_char*);
void vs_render_releaseGimImageSet(void);
int vs_battle_loadMenuPrg(int arg0);

enum vs_battle_informationTextBoxHeader {
    informationTextBoxHeaderNone,
    informationTextBoxHeaderCaution,
    informationTextBoxHeaderInformation,
    informationTextBoxHeaderCheck
};

/**
 * Initializes textbox ID 7
 */
void vs_battle_initInformationTextBox(enum vs_battle_informationTextBoxHeader flags);

/**
 * Lerps between two predefined colors, used for menu item backgrounds.
 *
 * @param gradient Lerp weight from 0-8
 * @param colorIndex Valid indices are 0-8, 16, and 24
 */
int vs_battle_uiGradientStop(u_int gradient, u_int colorIndex, int brightness);

/**
 * Gets menu row element.
 */
vs_battle_menuItem_t* vs_battle_getMenuItem(int row);

/**
 * Initializes a menu row element.
 *
 * @return The populated row.
 */
vs_battle_menuItem_t* vs_battle_setMenuItem(
    int row, int x, int y, int w, int backgroundWidth, char* text);
void vs_battle_renderMenuItem(vs_battle_menuItem_t*);
int vs_menu_frameworkUpdateMainMenuSelection(int);
void vs_menu_frameworkUpdateMenuOverlay(void);

/**
 * Renders a digit or symbol from the battle digits.
 *
 * @param font 0 = small, 1 = large, 2 = symbols
 * @param before Can be NULL, in which case the pointer at (scratchpad + 4)[1] is used.
 * @return The position immediately to the left of the rendered value.
 */
int vs_battle_renderValue(int font, int xy, int value, u_long* before);

/**
 * Renders HP / MP / Risk bars
 */
void vs_battle_renderStatBar(int colorIndex, int w, u_long* before, int xy);

/**
 * Retrieves the status effects currently applied.
 *
 * @return Packed bits.
 */
int vs_battle_getStatusFlags(vs_battle_actor2*);
int vs_battle_getLimbStatus(vs_battle_uiEquipment_limb*);
void vs_menu_frameworkResumeAfterActionSelection(void);
int vs_menu_frameworkActionRestrictionFlags(int);

/**
 * Determines whether the selected spell class is available
 *
 * @param spellClass Class from 0-3
 * @return Bool
 */
int vs_battle_isSpellClassUnlocked(int spellClass);
int vs_battle_chainAbilitiesUnlocked(int);
void vs_menu_frameworkOpenChallengeMenu(void);
void vs_battle_setHudBattleMode(int arg0);
D_800F18EC_t* vs_menu_frameworkActionSelectionState(void);
void vs_menu_frameworkReleaseMenuRows(void);
void vs_menu_frameworkOpenLootListMenu(vs_battle_lootListNode*);
void vs_menu_frameworkOpenLootEntryMenu(vs_battle_loot*);
void vs_menu_frameworkOpenChestStorageMenu(int);
void _setArtAndAbilityToUnlock(int art, int battleAbility);
void vs_battle_displaySceneMessage(int, int, int);
int func_800CB45C(void);
void func_800CB50C(void);
void vs_battle_showPuzzleTimer(void);
void vs_battle_hidePuzzleTimer(void);
void vs_battle_setHudHidden(int);
void func_800CB7DC(void);
void vs_battle_rMemzero(void* arg0, int arg1);
void vs_battle_rMemcpy(void* dest, void const* src, int size);

/**
 * Converts the lowest decimal digit to BCD.
 *
 * @return Packed value
 * - Bits 0-3: Lowest decimal digit from 0-9
 * - Bits 4+: value / 10
 */
int vs_battle_toBCD(int value);
int vs_battle_drawCursor(int animStep, int position);
u_int vs_battle_keystreamBits(int value);
int vs_battle_decreaseMiscCount(int);
vs_battle_textBox* vs_battle_getTextBox(int arg0);
void vs_battle_initTextBox(
    int id, int flags, int x, int y, int w, int h, int centerX, int centerY);
int vs_battle_dismissTextBox(int);
void vs_battle_renderImage(int xy, void* buffer, int wh);
void vs_effects_initEffectRuntime(void);
int func_800CEEBC(void);
int vs_effects_startEffectTrack(int);
int vs_effects_beginActionEffect(func_800CF0E8_t*, int, int);
void vs_effects_clearPrimaryEffects(void);
int vs_effects_beginSecondaryEffects(func_800CF0E8_t*, int);
int func_800CF48C(void);
void func_800CF7A8(int, int, int, int);
void func_800CF830(int, int);
void func_800CF920(void);
int vs_battle_getTextLineLength(char* str);
int vs_battle_validateShortcutSelection(int shortcut);
void vs_render_updateGimImageSet(void);
void vs_battle_showShortcutHud(void);
void vs_battle_drawBattleHud(void);
int vs_menu_frameworkOpenGameMenu(int menuState, int arg1);
void func_800CB83C(void);
int vs_effects_updateEffectRuntime(void);
void vs_battle_addVecToSvec(VECTOR* arg0, SVECTOR* arg1, VECTOR* arg2);
void vs_battle_lerp2DVector(short* src, int t, int* vec);
void vs_battle_lerpSvector(short* src, int t, SVECTOR* vec);
void vs_battle_lerpVector(short* src, int t, VECTOR* vec);
int vs_battle_randUniformInt(int, int);
int func_800CFE1C(short* arg0, int arg1);
void vs_effects_advanceTextureAnimation(func_800D6CF_t* arg0);

/**
 * Samples a curve from the currently loaded .P file.
 *
 * @param curveId Curve to sample (1-indexed).
 * @param distance Point to sample, will wrap around if too long.
 * @return Sampled value; 0 if `curveId` is not a valid index.
 */
int vs_battle_sampleCurve(int curveId, int distance);

/**
 * Evaluates a point on a spline segment between p1 and p2.
 *
 * The curve passes through p1 at t = 0 and p2 at t = ONE;
 *
 * @param p0  Control point before segment.
 * @param p1  Segment start point.
 * @param p2  Segment end point.
 * @param p3  Control point afater segment.
 * @param t   Interpolation value ([0, ONE] ?).
 * @param out Output vector.
 */
void vs_battle_splineInterpolate(
    SVECTOR* p0, SVECTOR* p1, SVECTOR* p2, SVECTOR* p3, int t, SVECTOR* out);

void vs_battle_svecToVec(SVECTOR* arg0, VECTOR* arg1);
void vs_battle_vecToSvec(VECTOR* arg0, SVECTOR* arg1);
void func_800D2ADC(D_800F53B8_t*, int, int, int, func_800FB4C0_t*);
void vs_effects_clearTextureAnimation(void* sprite); /* a func_800D6CF_t, cleared whole */
void vs_effects_restartTextureAnimation(func_800D6CF_t* arg0, int arg1, int arg2);

extern u_short vs_battle_menuStrings[];
extern char* vs_battle_limbNames[];
extern char vs_battle_characterWidths[];
extern u_short vs_battle_statusStrings[];
extern char D_800EB9AE;
extern char vs_battle_hudHidden;
extern int vs_battle_hudSlideOffset;
extern vs_battle_lootListNode* vs_menu_frameworkLootLists;
extern vs_battle_loot* vs_menu_frameworkLootEntry;
extern vs_battle_menuItem_t* vs_battle_menuItems;
extern char vs_battle_rowAnimationSteps[];
extern int vs_battle_hudGaugeLabelPositions[];
extern char vs_battle_hudGaugeLabelWidths[];
extern char D_800EBC00[];
extern u_short vs_battle_statusIconTexOffsets[];
extern u_int vs_battle_limbStatusColors[];
extern char vs_battle_animationIndices[];
extern char vs_battle_warlockSpellIds[];
extern char vs_battle_shamanSpellIds[];
extern char vs_battle_sorcererSpellIds[];
extern char vs_battle_enchanterSpellIds[];
extern char vs_battle_cursorBrightnessAnimation[];
extern char vs_battle_chainAbilityOffsets[];
extern char vs_battle_defenseAbilityOffsets[];
extern char vs_battle_shortcutInvoked;
extern char D_800F4E6B;
extern int* vs_battle_rowTypeBuf;
extern char vs_battle_unlockedBreakArt;
extern char* vs_battle_stringBuf;
extern char vs_battle_submenuStates[16];
extern char vs_battle_lowerScreenUiState;
extern D_800F18EC_t vs_menu_frameworkActionSelection;
extern int vs_menu_frameworkActionRestrictions;
extern D_800F4EE8_t vs_menu_frameworkMenuMemory;
extern char vs_battle_unlockedBattleAbility;
extern char D_800F4FDB;
extern D_800F569C_t* vs_effects_package;
extern D_800F569C_t D_800F56C8;
extern vs_battle_textBox vs_battle_textBoxes[];
extern D_800F53B8_t* vs_effects_primaryInstances; /* the effect scripts, the first of a list */
/* The particle emitter's working record (the PS1 kept it in the scratchpad at 0x1F8001D0). */
extern func_800FA098_arg1 pc_effect_scratch;
extern u_char vs_battle_miscItemInvoked;
extern vs_battle_stringContext_t vs_battle_stringContext;
extern vs_battle_menuState_t vs_battle_menuState;
extern SVECTOR vs_effects_effectOrigin[];
extern D_800F53B8_t* vs_effects_currentInstance;
extern effectExec vs_battle_effectRenderers[];
