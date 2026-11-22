#include "common.h"
#include "5BF94.h"
#include "pc/src/gte_value_core.h"
#include "146C.h"
#include "2842C.h"
#include "30DB0.h"
#include "38C1C.h"
#include "3A1A0.h"
#include "40564.h"
#include "573B8.h"
#include "58578.h"
#include "target_block.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include "pc/src/game/SLUS_010.40/overlay.h"
#include "pc/src/game/SLUS_010.40/31724.h"
#include "pc/src/game/SLUS_010.40/32154.h"
#include "pc/src/game/MENU/MAINMENU.PRG/C48.h"
#include "pc/src/game/MENU/MAINMENU.PRG/58EC.h"
#include "pc/src/game/GIM/SCREFF2.PRG/0.h"
#include "build/src/include/lbas.h"
#include "build/assets/BATTLE/BATTLE.PRG/menuStrings.h"
#include "build/assets/BATTLE/BATTLE.PRG/statusStrings.h"
#include "vs_string.h"
#include "gpu.h"
#include <memory.h>
#include <libetc.h>
#include <rand.h>
#include <abs.h>
#include <inline_c.h>
#include "pc/src/psx_types.h"
#ifdef ROOD_PC
#include "pc/src/classic/classic_hook.h"
#include "pc/src/widescreen.h"
#endif

typedef struct {
    u_int unk0_0 : 8;
    u_int unk0_8 : 8;
    u_int unk0_16 : 4;
    u_int unk0_20 : 6;
    u_int unk0_26 : 6;
} func_800D57FC_t;

typedef struct {
    unsigned unk0_0 : 4;
    unsigned unk0_4 : 5;
} func_800D57FC_t2;

typedef struct {
    func_800D5780_t2 header;
    u_short dataOffset;
} vs_content_PFileTimelineTrackEntry;

typedef struct {
    int count;
    vs_content_PFileTimelineTrackEntry tracks[0];
} vs_content_PFileTimelineTable;

typedef struct {
    u_short unk0;
    u_char unk2;
    u_char unk3;
} func_800D6894_t;

typedef struct {
    D_800F53B8_t3* source;
    D_800F53B8_t3* emitFrom;
    D_800F53B8_t3* target;
    D_800F53B8_t3* emitTo;
    int priority;
    char* script;
    int scriptOffset;
    vs_effects_EffectInstance* parent;
    u_char token;
    u_char unk21;
    u_char unk22;
    u_char unk23;
    int unk24;
} vs_effects_EffectSpawnRequest;

typedef struct {
    int x;
    int y;
    int z;
    int unkC;
} vs_effects_ParticleGravity;

typedef struct {
    char unk0[4];
    short unk4;
    short unk6;
    short unk8;
    short unkA;
} func_800CF988_t2;

typedef struct {
    char unk0[8];
    func_800CF988_t2 unk8[4];
} func_800CF988_t;

typedef struct {
    u_char order[8];
    u_char unk8;
    u_char unk9;
    u_char selectedIndex;
    u_char count;
    u_char unkC;
} _textBoxSelector_t;

typedef struct {
    int unk0;
    int unk4;
    SVECTOR unk8;
    VECTOR unk10;
    VECTOR unk20;
    int unk30[2];
    VECTOR target;
    MATRIX unk48;
    int unk5C;
    int unk60;
    int unk64;
} func_800D0B30_t2;

typedef struct {
    u_char unk0[0x34];
    u_char unk34;
    u_char unk35;
    char unk36[0x42];
    func_800CFE98_t unk78;
    char unk9C[0x22];
    short unkBE;
} func_800D0C60_t;

typedef struct {
    short unk0;
    short unk2;
    unsigned char unk4;
    unsigned char unk5;
    unsigned char unk6;
    unsigned char unk7;
} func_800D6310_t;

typedef struct {
    char unk0[0x10];
    D_800F53B8_t3_2 unk10[16];
} func_800D6048_t;

typedef struct {
    u_int unk0_0 : 9;
    u_int unk0_9 : 9;
    u_int effectRenderer : 3;
    u_int unk0_21 : 6;
    u_int unk0_27 : 5;
    u_short unk4;
} func_800D5D74_t;

typedef struct {
    u_short unk0_0 : 9;
    u_short unk0_9 : 7;
    u_short unk2;
    u_int unk4_0 : 4;
    u_int unk4_4 : 5;
    u_int unk4_9 : 7;
    u_int unk4_16 : 1;
    u_int unk4_17 : 7;
    u_int unk4_24 : 1;
    u_int unk4_25 : 7;
} func_800D6A18_t;

typedef struct {
    u_char unk0;
    u_char unk1_0 : 5;
    u_char unk1_5 : 1;
    u_char unk1_6 : 1;
    u_char unk1_7 : 1;
    u_char unk2;
    u_char unk3;
    u_char unk4;
    u_char unk5;
} D_800F54B8_t;

typedef struct {
    int count;
    short offsets[0];
} pFileBlock11;

typedef struct {
    u_short unk0;
    u_short unk2;
    u_char unk4;
    u_char unk5;
    u_char unk6;
    u_char unk7;
    int unk8;
    int unkC;
    int unk10;
    short unk14[6];
    short unk20[6];
    short unk2C[6];
} func_800D0B30_t;


typedef struct {
    int unk0_0 : 12;
    int unk0_12 : 12;
    u_int unk0_24 : 2;
    u_int unk0_26 : 4;
} func_800CCE10_t;

void _renderDigit(int, int, int, u_long*);
void vs_battle_updateTargetSelectionHighlight(int);
void func_800CA97C(void);
void vs_render_drawGimLayer(int arg0);
void vs_render_drawGimTiles(gim_t* arg0, int arg1, u_long* arg2);
int _breakArtsUnlocked(void);
extern int func_800CE174(func_800D4910_t*, u_int, int);
void vs_effects_resetEffectInstances(void);
vs_effects_EffectInstance* vs_effects_createPrimaryEffectInstance(vs_effects_EffectSpawnRequest*);
vs_effects_EffectInstance* vs_effects_createSecondaryEffectInstance(vs_effects_EffectSpawnRequest* arg0);
int vs_effects_updatePrimaryEffects(void);
char func_800CF218(func_800CF0E8_t* arg0, int arg1, int arg2);
void vs_effects_setPrimaryEffectPhase(int arg0);
void vs_effects_setSecondaryEffectPhase(int arg0, vs_effects_EffectInstance* arg1);
int vs_effects_allocateEffectToken();
void vs_effects_releaseEffectToken(int arg0);
void vs_effects_destroySecondaryEffectInstance(vs_effects_EffectInstance*);
func_800D4910_t* vs_effects_attachEffectRenderer(vs_effects_EffectInstance*, effectExec, int);
void vs_effects_detachEffectRenderer(vs_effects_EffectInstance*, func_800D4910_t*);
void vs_effects_initializeTransformStates(vs_effects_EffectInstance*);
void vs_core_mathTranslationTransform(SVECTOR* arg0, func_800CFE98_t* arg1);
void vs_effects_buildCameraAttachment(int, func_800D0C60_t*, int);
void vs_effects_updateEffectTransforms(vs_effects_EffectInstance*);
void vs_effects_sampleAllEffectCurves(int);
int vs_effects_segmentedEffectCurveDuration(int);
void func_800D169C(SVECTOR*, SVECTOR*, int, SVECTOR*);
void func_800D17A8(VECTOR*, VECTOR*, int, VECTOR*);
void vs_effects_resetEffectCameraMotion(void);
void vs_effects_captureEffectCamera(D_800F54D8_t*);
void vs_effects_applyEffectCamera(D_800F54D8_t*);
void vs_effects_updateCameraShakeChannels(void);
void vs_effects_interpolateEffectCameraLinear(void);
void vs_effects_interpolateEffectCameraCosine(void);
void vs_effects_updateEffectCameraMotion(void);
void vs_effects_resetParticlePoolReferences(void);
void vs_effects_acquireParticlePool(int);
void vs_effects_releaseParticlePool(void);
void vs_effects_releaseAllEffectParticles(vs_effects_EffectInstance*);
void vs_effects_branchScriptIf(int, vs_effects_EffectInstance*);
u_char vs_effects_readScriptByte(vs_effects_EffectInstance*);
u_short vs_effects_readScriptHalfword(vs_effects_EffectInstance*);
int vs_effects_runScript(vs_effects_EffectInstance* arg0);
void pc_effect_bad_opcode(int opcode, int cursor); /* boot_runtime.c: reports it, once per second of battle frames */
void vs_effects_loadEffectTimeline(vs_effects_EffectInstance*, int, int);
void vs_effects_particleKeyTimingOf(func_800D5780_t*);
void vs_effects_shakeKeyTimer(func_800D5780_t*);
int vs_effects_advanceTimelineKey(func_800D5780_t*);
int vs_effects_tickParticleEmissionTrack(vs_effects_EffectInstance*, func_800D5780_t*);
int vs_effects_tickSoundTrack(vs_effects_EffectInstance*, func_800D5780_t*);
typedef struct {
    u_char* data;
    char prefix[8];
    short delay;
    short index;
} timedSpawnState;

int vs_effects_tickChildEffectTrack(vs_effects_EffectInstance*, timedSpawnState*, int);
int vs_effects_tickRendererTrack(vs_effects_EffectInstance*, func_800D5780_t*);
int vs_effects_tickVibrationTrack(vs_effects_EffectInstance*, func_800D5780_t*);
int vs_effects_tickSourceActorColourTrack(vs_effects_EffectInstance*, func_800D5780_t*);
int vs_effects_tickTargetActorColourTrack(vs_effects_EffectInstance*, func_800D5780_t*, int);
int vs_effects_tickOtherActorColourTrack(vs_effects_EffectInstance*, func_800D5780_t*);
int vs_effects_tickSceneColourTrack(vs_effects_EffectInstance*, func_800D5780_t*);
void vs_effects_spawnTransformWeaponTrail(vs_effects_EffectInstance*, u_char, u_char);
void vs_effects_sourceWeaponBrightness(vs_effects_EffectInstance*, u_char);
void vs_effects_sourceReactionSound(vs_effects_EffectInstance*, u_char);
void vs_effects_focusCameraOnEffectTransform(vs_effects_EffectInstance*, u_char, u_char);
void vs_effects_restoreActionEffectCamera(vs_effects_EffectInstance*, u_char);
int vs_effects_tickCameraShakeTrack(vs_effects_EffectInstance*, func_800D5780_t*);
void vs_effects_startCameraShakeChannels(int, int, int, int, int, int, int);
void vs_effects_tickEffectTimeline(vs_effects_EffectInstance*, int);
void vs_effects_selectEffectResourceContext(int);
void vs_effects_saveEffectResourceContext(void);
void vs_effects_restoreEffectResourceContext(void);
int vs_effects_loadEffectPackage(void);
void vs_effects_releaseEffectPackageResources(void);
int vs_effects_updateEffectTextureLoading(void);
int vs_effects_updateEffectPluginLoading(void);
void vs_effects_bindEffectPackageBlocks(void*);
void vs_effpurge_exec(void);
void vs_audio_playActorModelSoundVariant(int, int);
void func_800C4650(func_800C5798_t* arg0, int arg1);
MATRIX* func_800C085C(func_800C1564_flags* arg0, int arg1);
void vs_render_loadFrameViewMatrix(void);
void func_800C28AC(SVECTOR* arg0, int arg1);
void func_800C2B0C(SVECTOR* arg0, int arg1);
void vs_battle_drawSphereTerrainIntersection(SVECTOR* arg0, MATRIX* arg1, int arg2);
void* func_800C282C(void);
int _getCollisionMapDimensions(int arg0);
int func_800C1D84(void);

extern u_int _gimLbas[];
extern int _menuLbas[];
extern u_char vs_battle_targetSelectionStep;
extern char vs_battle_targetInfoTemplate[];
extern char D_800EA9B6[];
extern char vs_battle_emptyStatusString[];
extern char D_800EB9AC;
extern signed char _loadedSubMenu;
extern D_800EB9B8_t* vs_battle_targetSphere;
extern gim_t* vs_render_gimLayers;
extern char vs_battle_hudBattleMode;
extern char vs_battle_battleModeIndicatorStep;
extern char vs_menu_textBoxBufferIndex;
extern union {
    int s32;
    u_char u8[4];
} D_800EB9D0;
extern u_int* _menuBgUnpackedBuf;
extern u_char D_800EB708[172]; /* two bits per glyph pair: index < 26 * 26, read at index >> 2 */
extern u_char vs_render_compactGlyphWidths[32]; /* two 4-bit widths per byte, by glyph >> 1 */
extern u_char D_800EB7D4[60];
extern int* vs_menu_sceneNotificationBuffer;
extern u_int D_800EBBB8[];
extern u_int D_800EBBC0[];
extern int vs_battle_hudGaugePositions[];
extern int vs_render_statGaugeColors[];
extern u_short D_800EBC68[];
extern char vs_battle_shortcutHudRequested;
extern int D_800EBC88[];
extern u_char D_800EBC98[];
extern u_char D_800EBCA0[];
extern int D_800EBCA8[];
extern u_short D_800EBCB8[];
extern u_char D_800EBCC0[];
extern u_char D_800EBCC8[];
extern int D_800EBCD0[];
extern u_short D_800EBCE0[];
extern u_char vs_battle_shortcutHudAnimationStep;
extern int D_800EBCEC[];
extern int D_800EBD14[];
extern u_int D_800EBD3C[];
extern u_short vs_battle_battleModeIndicatorWobble;
extern u_char _spellClassCounts[4];
extern u_char* _spellIds[4];
extern char D_800EBD68[8];
/* The door sigils' text block, 0x800EBDDC..0x800EC258 (1148 bytes): 380 bytes of text (word offsets, then the encoded strings), then 64 rows of 12 bytes:
 * the names of what a door needs (D_800EBF58, +380), the rows the scene message picks by (action - 0x1CA) * 2. One object: the rows are an offset into it. */
extern u_short vs_battle_doorStrings[574];
extern u_int _keystreamState;
extern char* vs_battle_puzzleTimerTextPointer;
extern void (*vs_render_gimLayerDrawRoutines[])(gim_t*, int, u_long*);
extern func_800CCE10_t D_800EC284[][2];
extern int D_800EC2CC[];
extern int D_800EC2D8[];
extern u_char D_800EC2E4;
extern effectExec D_800EC324[];
extern int vs_effects_emissionCounter;
extern char D_800EC32C[];
extern u_char D_800EC330[][2][4];
extern u_char D_800EC368[][5][4];
extern int (*vs_effects_scriptOpcodeHandlers[])(void*);
extern u_char vs_effects_lastReactionSoundVariant;
extern u_char D_800F522C;
extern u_char vs_battle_targetColumnEntries[2][16];
extern u_char vs_battle_secondColumnEntryCount;
extern u_char vs_battle_firstColumnEntryCount;
extern char D_800F4CB8;
extern char vs_battle_fontBank;
extern int _fontBrightness;
extern u_long vs_menu_textBoxOrderingTables[3 * 34]; /* the text boxes' three pages of 34 slots (func_800CB83C): slots only, no packet is linked in them */
extern char vs_battle_shortcutInvoked;
extern char vs_menu_sceneNotificationTimer;
extern char D_800F4E69;
extern char vs_battle_lowerScreenUiState;
extern _textBoxSelector_t textBoxSelector;
extern int vs_menu_frameworkUnusedInterfaceFlag;
extern short D_800F4ED8[8];
extern u_long* vs_menu_activeTextBoxOrderingTable;
extern char D_800F51C8;
extern vs_main_CdQueueSlot* D_800F5218;
extern u_int vs_effects_pendingTracks;
extern int vs_effects_trackCount;
extern int vs_effects_startedTrackCount;
extern func_800CF0E8_t vs_effects_actionRequest;
extern func_800CF0E8_t vs_effects_secondaryRequest;
extern u_char vs_battle_legacyUnreferencedByte;
extern char vs_effects_primaryToken;
extern u_int vs_effects_primaryPhase;
extern int vs_effects_tokenBitmaps[];
extern int vs_effects_curveSamples[];
extern int D_800F53B4;
extern char vs_effects_primaryLoaded;
extern char D_800F54A9;
extern vs_effects_EffectInstance* vs_effects_secondaryInstances;
extern D_800F54B8_t vs_effects_cameraShakeChannels[];
extern short D_800F54D0;
extern D_800F54D8_t vs_effects_effectCamera;
/* Defined here: these hold pointers among their words, so they cannot be imports (LP64). */
vs_battle_menuState_t vs_battle_menuState;
vs_battle_stringContext_t vs_battle_stringContext;
vs_battle_textBox vs_battle_textBoxes[8];
vs_effects_ParticleEmitterScratch pc_effect_scratch;
extern u_char D_800F4E80;
extern char vs_effects_enabledShakeChannels;
extern D_800F54D8_t vs_effects_cameraMotionEnds[2];
extern short D_800F55A0;
extern D_800F54D8_t vs_effects_savedActionCamera;
extern u_int D_800F55E8;
extern int vs_effects_particleCapacity;
extern func_800D2904_t* vs_effects_freeParticles;
extern int vs_effects_usedParticleCount;
extern int vs_effects_particlePoolReferences;
extern int D_800F5610;
extern int vs_effects_particleDrag;
extern vs_effects_ParticleGravity vs_effects_particleGravity;
extern func_800D2904_t* vs_effects_particlePool;

int vs_battle_updateSphereTargetPosition(SVECTOR* arg0);

void func_800C513C(int arg0, int arg1);

void func_800C5164(int arg0, int arg1, int arg2);

void vs_battle_setTargetActorColorOverride(int arg0, void* arg1);

void vs_battle_updateTargetSelectionHighlight(int mode);

extern u_char vs_battle_secondColumnGroups[8], vs_battle_firstColumnGroups[8], vs_battle_secondColumnGroupFirstTargets[8], vs_battle_firstColumnGroupFirstTargets[8];
extern u_char vs_battle_secondColumnGroupCount, vs_battle_firstColumnGroupCount;

void vs_battle_appendTargetGroupToColumn(int group);

extern u_char vs_battle_targetColumnTopOffset;

void vs_battle_buildTargetSelectionColumns(void);

void vs_battle_dismissTargetSelectionRows(void);

D_800EB9B8_unk990* vs_battle_initTargetSelectionRow(int arg0, int arg1, int arg2);

char* _getStatusString(uint arg0);

int vs_battle_updateTargetSelectionMenu(u_char* target);

void pc_frame_table_clear(unsigned* ot, int n, int reverse);

void vs_battle_renderTextRawColor(char const* text, int x, int color, void* nextPrim);

void vs_battle_renderTextRaw(char const* text, int xy, void* nextPrim);

#pragma vsstring(start)

char* vs_battle_printf(char* dest, char* src);

#pragma vsstring(end)

int vs_battle_setTextBox(int boxId, char* str);

int vs_battle_selectTextBox(u_char boxMask);

#ifdef ROOD_PC
/* ui_text_layout line (pc/src/ui_text.c): each character this printer sets. */
extern void pc_text_glyph(void* const* commands, int count, int glyph, int x, int y,
    int next_x, int advance) __attribute__((weak));
/* PC_CLASSIC (classic_hook.h): the glyphs _printVariableWidthFont sets, recorded as it sets them; a classic
 * draw's copies into the parity gate's shadow are not ui_text's. */
extern void pc_classic_text_char(int glyph, int x, int y) __attribute__((weak));
extern int pc_classic_text_muted(void) __attribute__((weak));
static void pc_text_char(
    void* const* commands, int count, int glyph, int x, int y, int advance)
{
    if (pc_classic_text_muted && pc_classic_text_muted())
        return;
    if (pc_text_glyph)
        pc_text_glyph(
            commands, count, glyph, x << 16, y << 16, (x + advance) << 16, advance);
}
#endif

#pragma vsstring(start)

int vs_battle_getTextLineLength(char* str);

int vs_battle_printVariableWidthFontChar(u_int glyphId, int x, int y, u_long* ot);

void vs_battle_setFontStyle(int arg0);

void _printVariableWidthFont(vs_battle_textBox* arg0);

#pragma vsstring(end)

// (Native: pc/src/fixed_font.c.)
int _printFixedWidthFontChar(int charId, int x, int y, int width, int height, int scale);

#pragma vsstring(start)
void _printFixedWidthFont(vs_battle_textBox* ctx, int scale);

#pragma vsstring(end)

void vs_render_blendGlyphIntoPixels(u_short*, u_int, int, u_int);
extern u_short D_800EB98C[16];

void vs_render_blendGlyphIntoPixels(u_short* destination, u_int glyph, int stride, u_int alpha);

#ifdef ROOD_PC
/* _renderTextImmediate with the hardware renderer: the frame buffer read back
 * there is the PS1-resolution mirror, so compositing the text into it and
 * uploading it again would put a pixelated rectangle of an older picture over
 * the scene (the original font faintly inside) while a box closes. The text is
 * drawn as glyph sprites of the box instead, in its colours, at the fading
 * opacity (pc/src/gpu_hw.c pc_hw_opacity_packets; ui_text restyles them as the
 * text was drawn while the box was open). The PS1 build has none of this
 * (blocks kept short there: a long skipped one makes a line marker the
 * vsstring pass mangles). */
extern int pc_hw_opacity_on(void) __attribute__((weak));
extern void pc_hw_opacity_packets(const void*, const void*, int) __attribute__((weak));
static u_long* _pcFadeOt __attribute__((used));
static void* _pcFadeStart __attribute__((used));
static int _pcFadeColor __attribute__((used)), _pcFadeBrightness __attribute__((used));

/* One character (its code in the current font table; spaces too: the glyphs
 * of the string stay one line for ui_text) with its top-left at x, y. */
#endif

#pragma vsstring(start)
void _renderTextImmediate(vs_battle_textBox* arg0, int arg1);

#pragma vsstring(end)

void vs_battle_loadGim(int id, int arg1);

void vs_render_activateGimLayer(u_int arg0, void* arg1, u_char* arg2);

void vs_render_releaseGimImageSet(void);

void vs_render_updateGimImageSet(void);

int vs_battle_loadMenuPrg(int arg0);

void vs_battle_initInformationTextBox(enum vs_battle_informationTextBoxHeader header);

vs_battle_menuItem_t* vs_battle_setMenuItem( int row, int x, int y, int w, int backgroundWidth, char* text);

/**
 * Lerps between two colors.
 *
 * @param brightness Color multiplier
 * @param blendFactor Proportion of color0 to color1
 */
u_int _lerpColor(u_int color0, u_int color1, int brightness, int blendFactor);

int vs_battle_uiGradientStop(u_int gradient, u_int colorIndex, int brightness);

void vs_battle_renderMenuItem(vs_battle_menuItem_t* menuItem);

extern int D_800F4CC0;
extern char D_800EBA78[];

#ifdef ROOD_PC
/* Modern menus (pc/src/ui/menu_bridge.c): the shell replaces the top level
 * when the ui_menus setting is 1. */
extern int pc_ui_menu_open(int mode);
extern int pc_ui_menu_top(void);
extern int pc_ui_menu_active(void);
extern int pc_ui_menu_hide_rows(void);
/* Fast travel (pc/src/fast_travel.c): the Magic command on a Circle of Magic
 * once the port offers Teleportation (shown, never stored in menuFlags), and
 * MP 0 not greying it while travel is free. */
extern unsigned pc_fast_travel_menu_bits(void);
extern unsigned pc_fast_travel_mask(unsigned bits);
#define MAGIC_COMMAND_MASK pc_fast_travel_mask(0xB7)
#else
#define MAGIC_COMMAND_MASK 0xB7
#endif

int vs_menu_frameworkUpdateMainMenuSelection(int mode);

void vs_menu_frameworkUpdateMenuOverlay(void);

void _renderDigit(int font, int xy, int digit, u_long* before);

int vs_battle_renderValue(int font, int xy, int value, u_long* before);

void vs_battle_renderStatBar(int colorIndex, int w, u_long* before, int xy);

#ifdef ROOD_PC
extern int pc_hud_label_opaque; /* project_settings.cpp: the label opaque (1) or the original's (0) */
#endif
void _renderStatBar(int colorIndex, int current, int max);

void vs_battle_drawSlidingStatusIcon(int arg0, int arg1, int arg2);

int vs_battle_getStatusFlags(vs_battle_actor2* arg0);

int vs_battle_getLimbStatus(vs_battle_uiEquipment_limb* limb);

void vs_battle_showShortcutHud(void);

void func_800C9F98(int arg0, int arg1);

#ifdef ROOD_PC
/* QoL weapon slots (pc/src/weapon_slots.c): in their mode the labels show the
 * slots' weapons; otherwise vs_battle_shortcutIconBrightness's answer. */
int pc_weapon_slots_label(int id);
#endif

void vs_battle_drawShortcutHud(void);

void vs_battle_drawBattleHud(void);

void func_800CA97C(void);

#ifdef ROOD_PC
/* Modern menus (pc/src/ui/menu_bridge.c): the cancelled target's action was
 * handed to the game by the shell, not by an original screen. */
extern int pc_ui_menu_cast_cancelled(void) __attribute__((weak));
#endif

void vs_menu_frameworkResumeAfterActionSelection(void);

int vs_menu_frameworkActionRestrictionFlags(int arg0);

int vs_menu_frameworkOpenGameMenu(int menuState, int arg1);

int vs_battle_isSpellClassUnlocked(int spellClass);

int _breakArtsUnlocked(void);

int vs_battle_chainAbilitiesUnlocked(int defense);

int vs_battle_validateShortcutSelection(int shortcut);

void vs_menu_frameworkReleaseMenuRows(void);

void vs_menu_frameworkOpenLootListMenu(vs_battle_lootListNode* arg0);

void vs_menu_frameworkOpenLootEntryMenu(vs_battle_loot* arg0);

void vs_menu_frameworkOpenChestStorageMenu(int arg0);

void _setArtAndAbilityToUnlock(int art, int battleAbility);

void vs_menu_frameworkOpenChallengeMenu(void);

void vs_battle_displaySceneMessage(int arg0, int arg1, int arg2);

int func_800CB45C(void);

void func_800CB50C(void);

void vs_battle_showPuzzleTimer(void);

void vs_battle_hidePuzzleTimer(void);

char const vs_battle_puzzleTimerText[] = "00:00:00";

void _renderTimer(int* value);

void vs_battle_setHudHidden(int arg0);

void vs_battle_setHudBattleMode(int arg0);

D_800F18EC_t* vs_menu_frameworkActionSelectionState(void);

void _loadScreff2(int arg0);

void func_800CB708(void);

void func_800CB79C(void);

void func_800CB7DC(void);

void func_800CD158(int);
void vs_menu_drawTextBoxFrame(int);
extern int vs_battle_puzzleTimer;

void func_800CB83C(void);

void vs_render_drawGimTiles(gim_t* image, int clut, u_long* ot);

void vs_render_drawGimFramebufferFade(gim_t* arg0, int arg1, u_long* arg2);

/* Draws a tiled image wiped in horizontally: per scanline the visible span is
   [left[y], right[y]) from the per-line widths at unk18 scaled by unkC (below ONE
   the image wipes in, above ONE it wipes out; unk3 picks the side). Each 64x15
   tile cell is drawn as one sprite per scanline, odd start columns first as a
   1-pixel sprite. The original reuses x/end for the scale/side and column for the
   scaled width. */
void vs_render_drawGimScanlineWipe(gim_t* image, int clut, u_long* ot);

void vs_render_setGimWorkspaceDrawArea(u_long* arg0, int arg1);

void vs_render_setGimFramebufferDrawArea(u_long* arg0, int arg1);

void vs_render_drawGimCrossfade(gim_t* image, int clut, u_long* ot);

void vs_render_drawGimLayer(int arg0);

int vs_battle_decreaseMiscCount(int miscId);

void vs_battle_rMemzero(void* arg0, int arg1);

int vs_battle_toBCD(int value);

u_int vs_battle_keystreamBits(int value);


int vs_battle_drawCursor(int animStep, int position);

void vs_battle_renderImage(int xy, void* buffer, int wh);

vs_battle_textBox* vs_battle_getTextBox(int id);

func_800CCE10_t func_800CCE10(int arg0, int arg1, int arg2, int arg3);

void vs_battle_initTextBox( int id, int flags, int x, int y, int charsPerLine, int h, int centerX, int centerY);

int vs_battle_dismissTextBox(int id);

void func_800CD0FC(int arg0, u_int arg1);

void func_800CD158(int id);

int func_800CD3A0(int arg0, int arg1);

extern u_char* vs_event_textBoxFrameStyles[16];

#define POINT_X(p) (((short*)&points[p])[0])
#define POINT_Y(p) (((short*)&points[p])[1])

/* Draws the frame of text box `index` from shape vs_event_textBoxFrameStyles[unk0_8]: header word
   (b0 vertex count, b1 part count, b2 outline vertex count) followed by one word per
   part (b0/b1 outline join indices, b2 outline length, b3 polygon count), the packed
   x/y vertices (flipped by func_800CD3A0) and 4-byte polygons (0xFF = triangle).
   The original reuses its locals heavily: w/h/flip become polygon vertex 2/3 and the
   colour, i/n/k/v change roles between the passes. */
void vs_menu_drawTextBoxFrame(int index);

#undef POINT_X
#undef POINT_Y

typedef struct {
    union {
        int value;
        u_char bytes[4];
        struct {
            u_int actor : 16, color : 4, texture : 4, life : 8;
        } bits;
    } config;
    int index, age;
    SVECTOR first[8], second[8], previousFirst, currentFirst, previousSecond,
        currentSecond;
    int projectionDepth, projectionFlags;
} effectTrailState;
extern CVECTOR D_800EC308[], D_800EC2E8[];
void func_800CF988(func_800CF988_t*, int, int, int);
void func_800CDCBC(effectTrailState* t, int head, int tail);

int func_800CE174(func_800D4910_t* node, u_int mode, int config);

int func_800CE644(int arg0 __attribute__((unused)));

void vs_effects_resetEffectInstances(void);

void vs_effects_initEffectInstance(vs_effects_EffectSpawnRequest* arg0, vs_effects_EffectInstance* arg1);

vs_effects_EffectInstance* vs_effects_createPrimaryEffectInstance(vs_effects_EffectSpawnRequest* arg0);

void vs_effects_destroyPrimaryEffectInstance(vs_effects_EffectInstance* arg0);

int vs_effects_updatePrimaryEffects(void);

int vs_effects_updateSecondaryEffects(void);

int func_800CEEBC(void);

void vs_effects_requeueTrack(int arg0);

int vs_effects_startEffectTrack(int arg0);

int vs_effects_updateEffectRuntime(void);

int vs_effects_beginActionEffect(func_800CF0E8_t* arg0, int arg1, int arg2);

void vs_effects_clearPrimaryEffects(void);

char func_800CF218(func_800CF0E8_t* arg0, int arg1, int arg2);

int vs_effects_beginSecondaryEffects(func_800CF0E8_t* arg0, int arg1);

void vs_effects_setPrimaryEffectPhase(int arg0);

void vs_effects_setSecondaryEffectPhase(int arg0, vs_effects_EffectInstance* arg1);

int func_800CF48C(void);

int vs_effects_allocateEffectToken(void);

void vs_effects_releaseEffectToken(int arg0);

vs_effects_EffectInstance* vs_effects_createSecondaryEffectInstance(vs_effects_EffectSpawnRequest* arg0);

void vs_effects_destroySecondaryEffectInstance(vs_effects_EffectInstance* arg0);

func_800D4910_t* vs_effects_attachEffectRenderer(vs_effects_EffectInstance* arg0, effectExec renderer, int arg2);

void vs_effects_detachEffectRenderer(vs_effects_EffectInstance* arg0, func_800D4910_t* arg1);

void func_800CF7A8(int arg0, int arg1, int arg2, int arg3);

void func_800CF830(int arg0, int arg1);

void func_800CF8BC(void);

void func_800CF920(void);

void func_800CF92C(int arg0, int arg1, int arg2, short* arg3);

void func_800CF988(func_800CF988_t* arg0, int arg1, int arg2, int arg3);

int vs_battle_randUniformInt(int arg0, int arg1);

int _absMax3(int arg0, int arg1, int arg2) __attribute__((unused));
int _absMax3(int arg0, int arg1, int arg2);

int _absMax2(int arg0, int arg1) __attribute__((unused));
int _absMax2(int arg0, int arg1);

void vs_battle_lerpVector(short* src, int t, VECTOR* vec);

void vs_battle_lerpSvector(short* src, int t, SVECTOR* vec);

void vs_battle_lerp2DVector(short* src, int t, int* vec);

int func_800CFE1C(short* arg0, int arg1);

void _addSVectorToVector(SVECTOR* svec, VECTOR* vec, VECTOR* out) __attribute__((unused));
void _addSVectorToVector(SVECTOR* svec, VECTOR* vec, VECTOR* out);

void vs_core_mathIdentityRotation(func_800CFE98_t* arg0);

void vs_core_mathTranslationTransform(SVECTOR* arg0, func_800CFE98_t* arg1);

typedef struct {
    short rotation[3];
    u_short flags;
    u_char pad8[6];
    short frame;
    u_char pad10[12];
    VECTOR position;
    u_char pad2C[8];
    u_char actor, bone, targetActor, targetBone;
    MATRIX world, view, attachment, facing;
    SVECTOR saved, control0, control1;
} effectTransformState;

void vs_effects_initializeTransformStates(vs_effects_EffectInstance* arg0);

typedef struct {
    u_short flags;
    u_char pad2[3];
    u_char curve;
    u_char pad6[4];
    u_short mode;
    u_char padC[2];
    u_char limit;
    u_char padF;
    u_short unk10;
    u_short shape12;
} func_800D0548_arg0;

typedef struct {
    u_char pad0[6];
    u_short flags;
    u_char pad8[6];
    short frame;
    u_char pad10[12];
    VECTOR position;
    u_char pad2C[32];
    VECTOR endpoint;
    u_char pad5C[92];
    SVECTOR saved;
    SVECTOR control0;
    SVECTOR control1;
} func_800D0548_arg1;

void func_800D1718(SVECTOR*, SVECTOR*, int, int, SVECTOR*);
void func_800D1778(SVECTOR*, SVECTOR*, int, int, SVECTOR*);

void vs_effects_interpolateTransformPosition(func_800D0548_arg0* arg0, func_800D0548_arg1* arg1);

void vs_effects_interpolateTransformSpline(func_800D0548_arg0* arg0, func_800D0548_arg1* arg1);

void vs_effects_advanceTransformMotion(func_800D0548_arg0* arg0, func_800D0548_arg1* arg1);

void vs_effects_buildTransformSplineControls(func_800D0548_arg0* arg0, func_800D0548_arg1* arg1);

void vs_effects_buildCameraAttachment(int arg0, func_800D0C60_t* arg1, int arg2);

void vs_effects_buildEffectOriginAttachment(func_800CFE98_t* arg0);

void vs_effects_buildAnimatedLocalTransform(func_800D0B30_t* arg0, SVECTOR* arg1, func_800D0B30_t2* arg2);

void vs_effects_resolveTransformAttachment(int arg0, func_800D0C60_t* arg1);

void vs_effects_updateEffectTransforms(vs_effects_EffectInstance* arg0);

void vs_effects_sampleAllEffectCurves(int arg0);

int vs_battle_sampleCurve(int curveId, int distance);

int vs_effects_sampleSegmentedEffectCurve(int arg0, int arg1);

int vs_effects_segmentedEffectCurveDuration(int arg0);

void vs_battle_splineInterpolate( SVECTOR* p0, SVECTOR* p1, SVECTOR* p2, SVECTOR* p3, int t, SVECTOR* out);

int vs_battle_lerp(int arg0, int arg1, int arg2);

void func_800D169C(SVECTOR* arg0, SVECTOR* arg1, int arg2, SVECTOR* arg3);

void func_800D1718(SVECTOR* arg0, SVECTOR* arg1, int arg2, int arg3, SVECTOR* arg4);

void func_800D1778(SVECTOR* arg0, SVECTOR* arg1, int arg2, int arg3, SVECTOR* arg4);

void func_800D17A8(VECTOR* arg0, VECTOR* arg1, int arg2, VECTOR* arg3);

void func_800D1824(VECTOR* arg0, VECTOR* arg1, int arg2, int arg3, VECTOR* arg4);

void func_800D1884(VECTOR* arg0, VECTOR* arg1, int arg2, int arg3, VECTOR* arg4);

int func_800D18B4(int arg0, int arg1, int arg2, int arg3);

int vs_core_mathInterpolateTimedScalar(int arg0, int arg1, int arg2, int arg3);

void vs_effects_resetEffectCameraMotion(void);

void vs_effects_captureEffectCamera(D_800F54D8_t* arg0);

void vs_effects_applyEffectCamera(D_800F54D8_t* camera);

void vs_effects_enableCameraShakeChannels(int arg0);

void vs_effects_disableCameraShakeChannels(int arg0);

void vs_effects_setEffectCameraTransitionMode(int arg0);

u_char vs_effects_cameraShakePhaseStep(D_800F54B8_t* arg0);

u_char vs_effects_randomCameraShakePhaseStep(D_800F54B8_t* arg0);

void vs_effects_startCameraShakeChannels(int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6);

void vs_effects_stopCameraShakeChannels(int arg0);

void vs_effects_updateCameraShakeChannels(void);

void vs_effects_interpolateEffectCameraLinear(void);

void vs_effects_interpolateEffectCameraCosine(void);

void vs_effects_holdEffectCameraStartPose(void);

void vs_effects_updateEffectCameraMotion(void);

void vs_effects_resetParticlePoolReferences(void);

void vs_effects_acquireParticlePool(int count);

void vs_effects_releaseParticlePool(void);

func_800D2904_t* vs_effects_allocateEffectParticle(vs_effects_EffectInstance* arg0);

void vs_effects_releaseEffectParticle(func_800D2904_t* arg0, vs_effects_EffectInstance* arg1);

void vs_effects_releaseAllEffectParticles(vs_effects_EffectInstance* arg0);

void vs_battle_addVecToSvec(VECTOR* arg0, SVECTOR* arg1, VECTOR* arg2);

void vs_battle_svecToVec(SVECTOR* arg0, VECTOR* arg1);

void vs_battle_vecToSvec(VECTOR* arg0, SVECTOR* arg1);

void vs_effects_placeParticleSpawn(vs_effects_ParticleEmitterScratch* arg0, func_800D2904_t* arg1, const vs_gte_view* rotation);

/* Uniform integer between a and b (inclusive) using 30 random bits. */
#define RAND_UNIFORM_WIDE(a, b)                                                    \
    ((a) == (b)  ? (a)                                                             \
     : (b) < (a) ? (rand() + (rand() << 16)) % ((a) - (b) + 1) + (b)               \
                 : (rand() + (rand() << 16)) % ((b) - (a) + 1) + (a))

void func_800D2ADC( vs_effects_EffectInstance* arg0, int event, int arg2, int arg3, func_800FB4C0_t* parent);

void vs_effects_branchScriptIf(int arg0, vs_effects_EffectInstance* arg1);

int vs_effects_yieldScriptAt(vs_effects_EffectInstance* arg0);

int vs_effects_jumpScript(vs_effects_EffectInstance* arg0);

int func_800D4778(vs_effects_EffectInstance* arg0);

int vs_effects_endScript(vs_effects_EffectInstance* arg0 __attribute__((unused)));

int func_800D47C8(vs_effects_EffectInstance* arg0);

int vs_effects_attachScriptRenderer(vs_effects_EffectInstance* arg0);

int vs_effects_branchIfRenderersFinished(vs_effects_EffectInstance* arg0);

int vs_effects_loadTimelineImmediate(vs_effects_EffectInstance* arg0);

int func_800D48E4(vs_effects_EffectInstance* arg0);

int vs_effects_tickScriptRenderers(vs_effects_EffectInstance* arg0);

int vs_effects_tickScriptTransforms(vs_effects_EffectInstance* arg0);

int vs_effects_setActionEffectVerticalOffset(vs_effects_EffectInstance* arg0);

int vs_effects_initializeScriptTransforms(vs_effects_EffectInstance* arg0);

int func_800D4AB4(vs_effects_EffectInstance* arg0);

int vs_effects_setScriptRegister(vs_effects_EffectInstance* arg0);

int vs_effects_branchIfScriptRegisterEqual(vs_effects_EffectInstance* arg0);

int vs_effects_branchIfParticleCountEqual(vs_effects_EffectInstance* arg0);

int vs_effects_emitRegisterParticles(vs_effects_EffectInstance* arg0);

void vs_effects_integrateParticleMotion(func_800D2904_t*);
void vs_effects_drawEffectParticleList(void);

int vs_effects_tickEffectParticles(vs_effects_EffectInstance* arg0);

int vs_effects_refreshParticleEnvironment(vs_effects_EffectInstance* arg0 __attribute__((unused)));

int func_800D4D64(vs_effects_EffectInstance* arg0);

int func_800D4D98(vs_effects_EffectInstance* arg0 __attribute__((unused)));

int vs_effects_continueScript(vs_effects_EffectInstance* arg0 __attribute__((unused)));

int vs_effects_loadRegisterTimeline(vs_effects_EffectInstance* arg0);

int vs_effects_tickTimelineFromScript(vs_effects_EffectInstance* arg0);

int vs_effects_branchIfTimelineFinished(vs_effects_EffectInstance* arg0);

int vs_effects_freeScriptTimeline(vs_effects_EffectInstance* arg0);

int vs_effects_setParticleColorCurves(vs_effects_EffectInstance* arg0);

int func_800D4FB4(vs_effects_EffectInstance* arg0);

int vs_effects_completeChildScript(vs_effects_EffectInstance* arg0);

int func_800D5088(vs_effects_EffectInstance* arg0);
int func_800D5088(vs_effects_EffectInstance* arg0);

int func_800D5150(vs_effects_EffectInstance* arg0 __attribute__((unused)));

u_char vs_effects_readScriptByte(vs_effects_EffectInstance* arg0);

u_short vs_effects_readScriptHalfword(vs_effects_EffectInstance* arg0);

int vs_effects_runScript(vs_effects_EffectInstance* arg0);

void vs_effects_setParticleGravity(vs_effects_ParticleGravity* arg0);

void vs_effects_setParticleDrag(int* arg0);

typedef struct {
    short mass;
    short gravityScale;
    int x, y, z;
    int vx, vy, vz;
    int fx, fy, fz;
    int ax, ay, az;
} effectMotionState;

void vs_effects_integrateParticleMotion(func_800D2904_t* node);

void* _getPFileBlock3section(pFileBlock3* arg0, int arg1);

void* vs_content_pFileTimelineTrackData(pFileBlock3* arg0, int arg1, int arg2);

void vs_effects_loadEffectTimeline(vs_effects_EffectInstance* arg0, int arg1, int arg2);

void vs_effects_particleKeyTimingOf(func_800D5780_t* arg0);

void vs_effects_shakeKeyTimer(func_800D5780_t* arg0);

int vs_effects_advanceTimelineKey(func_800D5780_t* arg0);

int vs_effects_tickParticleEmissionTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

void vs_audio_playCutsceneStreamSlotCentered(int);

int vs_effects_tickSoundTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

int vs_effects_tickChildEffectTrack(vs_effects_EffectInstance* arg0, timedSpawnState* arg1, int arg2);

int vs_effects_tickRendererTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

typedef struct {
    u_short unk0;
    u_char count, unk3;
    char prefix[12];
    D_800F53B8_t3_2 target[0];
} soundEventTargets;
int vs_effects_tickVibrationTrack(vs_effects_EffectInstance* actor, func_800D5780_t* event);

int vs_effects_tickSourceActorColourTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

int vs_effects_tickTargetActorColourTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1, int arg2);

int vs_effects_tickOtherActorColourTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

int vs_effects_tickSceneColourTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

int vs_effects_tickBackgroundColourTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

void func_800D6388(vs_effects_EffectInstance* arg0);

void func_800D63D0(vs_effects_EffectInstance* arg0);

void vs_effects_targetRoomTile(vs_effects_EffectInstance* arg0);

void vs_effects_spawnTransformWeaponTrail(vs_effects_EffectInstance* arg0, u_char arg1, u_char arg2);

void vs_effects_fadeActionWeaponTrails(void);

void vs_effects_resumeTransformMotion(vs_effects_EffectInstance* arg0, char arg1);

void vs_effects_holdTimelineFrame(vs_effects_EffectInstance* arg0);

void func_800D6554(void);

void func_800D6574(vs_effects_EffectInstance* arg0);

void vs_effects_invalidateTransformAttachments(vs_effects_EffectInstance* arg0, char arg1);

void vs_effects_sourceWeaponBrightness(vs_effects_EffectInstance* arg0, u_char arg1);

void vs_effects_sourceReactionSound(vs_effects_EffectInstance* arg0, u_char arg1);

void func_800D66CC(vs_effects_EffectInstance* arg0);

void vs_effects_focusCameraOnEffectTransform(vs_effects_EffectInstance* arg0, u_char arg1, u_char arg2);

void vs_effects_restoreActionEffectCamera(vs_effects_EffectInstance* arg0, u_char arg1);

void func_800D6860(vs_effects_EffectInstance* arg0);

int vs_effects_tickActionControlTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

int vs_effects_tickCameraShakeTrack(vs_effects_EffectInstance* arg0, func_800D5780_t* arg1);

void vs_effects_tickEffectTimeline(vs_effects_EffectInstance* arg0, int arg1);

void vs_effects_clearTextureAnimation(void* sprite);/* a func_800D6CF_t, cleared whole */

void vs_effects_restartTextureAnimation(func_800D6CF_t* arg0, int arg1, int arg2);

#ifndef ROOD_PC
void vs_effects_drawEffectParticleList(void) { vs_effects_drawAnimatedSpriteParticles(); }
#endif

/* The shared effect command table (guest address 0x800EC3F4) and the built-in trail renderer: native function
 * pointers in the original's order. */
int pc_effect_func_800D4720(void* node);
int pc_effect_func_800D474C(void* node);
int pc_effect_func_800D4778(void* node);
int pc_effect_func_800D47C0(void* node);
int pc_effect_func_800D47C8(void* node);
int pc_effect_func_800D47F4(void* node);
int pc_effect_func_800D487C(void* node);
int pc_effect_func_800D48A4(void* node);
int pc_effect_func_800D48E4(void* node);
int pc_effect_func_800D5150(void* node);
int pc_effect_func_800D4910(void* node);
int pc_effect_func_800D4984(void* node);
int pc_effect_func_800D49A4(void* node);
int pc_effect_func_800D4A94(void* node);
int pc_effect_func_800D4AB4(void* node);
int pc_effect_func_800D4AEC(void* node);
int pc_effect_func_800D4B34(void* node);
int pc_effect_func_800D4B90(void* node);
int pc_effect_func_800D4BD0(void* node);
int pc_effect_func_800D4C18(void* node);
int pc_effect_func_800D4D44(void* node);
int pc_effect_func_800D4D64(void* node);
int pc_effect_func_800D4D98(void* node);
int pc_effect_func_800D4DA8(void* node);
int pc_effect_func_800D4DB0(void* node);
int pc_effect_func_800D4E5C(void* node);
int pc_effect_func_800D4E90(void* node);
int pc_effect_func_800D4EC0(void* node);
int pc_effect_func_800D4F00(void* node);
int pc_effect_func_800D4FB4(void* node);
int pc_effect_func_800D5048(void* node);
int pc_effect_func_800D5088(void* node);
int (*vs_effects_scriptOpcodeHandlers[49])(void*) = {
    pc_effect_func_800D4720,
    pc_effect_func_800D474C,
    pc_effect_func_800D4778,
    pc_effect_func_800D47C0,
    pc_effect_func_800D47C8,
    pc_effect_func_800D47F4,
    pc_effect_func_800D487C,
    pc_effect_func_800D48A4,
    pc_effect_func_800D48E4,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D4910,
    pc_effect_func_800D4984,
    pc_effect_func_800D49A4,
    pc_effect_func_800D4A94,
    pc_effect_func_800D4AB4,
    pc_effect_func_800D4AEC,
    pc_effect_func_800D4B34,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D4B90,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D4BD0,
    pc_effect_func_800D4C18,
    pc_effect_func_800D4D44,
    pc_effect_func_800D4D64,
    pc_effect_func_800D4D98,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D5150,
    pc_effect_func_800D4DA8,
    pc_effect_func_800D4DB0,
    pc_effect_func_800D4E5C,
    pc_effect_func_800D4E90,
    pc_effect_func_800D4EC0,
    pc_effect_func_800D4F00,
    pc_effect_func_800D4FB4,
    pc_effect_func_800D5048,
    pc_effect_func_800D5088,
};
effectExec D_800EC324[1] = { func_800CE174 };
#ifdef ROOD_PC
void (*vs_render_gimLayerDrawRoutines[4])(gim_t*, int, u_long*) = { vs_render_drawGimTiles, vs_render_drawGimFramebufferFade,
    vs_render_drawGimScanlineWipe, vs_render_drawGimCrossfade };
#endif

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
extern void pc_unimplemented(const char*);
extern void pc_opening_image_ready(int);
extern void pc_charge_skip_name(int now);
