#include "common.h"
#include "../../SLUS_010.40/main.h"
#include "../../SLUS_010.40/sfx.h"
#include "../MAINMENU.PRG/C48.h"
#include "../MAINMENU.PRG/2D10.h"
#include "../MAINMENU.PRG/58EC.h"
#include "../../BATTLE/BATTLE.PRG/146C.h"
#include "../../BATTLE/BATTLE.PRG/5BF94.h"
#include "gpu.h"
#include "lbaMacros.h"
#include "build/src/include/lbas.h"
#include "vs_string.h"
#include "insertTPage.h"
#include "pc/src/game/menuItem.h"
#include <libetc.h>
#include <stdio.h>
#include <libgpu.h>

typedef struct {
    int unk0;
    int unk4;
    int unk8;
    int unkC;
} _menuBgTransparencies_t;

typedef struct {
    int unk0;
    int unk4;
    int unk8;
} _menuBgChunkWidths_t;

enum arrowType_e {
    arrowTypeUp,
    arrowTypeDown,
};

typedef struct {
    short enabled;
    u_short frameMask;
    short frameDuration;
    short repeat;
    short currentFrame;
    short timer;
    short isActive;
} AnimationState_t;

static char* _vsStrcpy(char* arg0, char* arg1);
static int _initMenuState(void);
static int _showMenu(void);
static void _drawSprite(short*);
static void _drawPaginationArrow(enum arrowType_e arrowType);
static void _drawContentLines(void);
static short _getAnimationState(AnimationState_t* arg0);
static int _topMenu(int arg0);
static void _copySprites(u_long const* arg0, int arg1);
static void _copyCluts(u_long* arg0, int arg1);
static int _getRoundedLineCount(void);
static void _copyNextPageText(void);
static void _drawContent(void);
static void _setPageBg(int arg0, int arg1, int arg2, int arg3, int arg4);
static void _fadeMenuUpper(void);
static void _fadeMenuLower(void);
static void _drawControlsBg(int x, int y, int w, int h);

static void func_80102C94(int arg0) __attribute__((unused));
void func_80102C94(int arg0)
{
    if (arg0 != 0) {
        vs_mainMenu_setMenuCommand(menuActionMenu);
        vs_mainMenu_hideMenu = 0;
        return;
    }
    vs_mainMenu_setMenuCommand(menuActionNone);
    vs_mainMenu_hideMenu = 1;
}

static void _setMenuRows(int rowCount, int highlightedRow, char** strings)
{
    int rowTypes[rowCount];
    int i;
    char cursor;

    for (i = 0; i < rowCount; ++i) {
        rowTypes[i] = 0;
    }

    rowTypes[rowCount - 1] |= 4;

    if ((rowCount < 9) || (highlightedRow < 8)) {
        vs_menu_frameworkMenuMemory.cursorMemories[4] = highlightedRow;
        vs_menu_frameworkMenuMemory.cursorMemories[5] = 0;
    } else if (highlightedRow >= (rowCount - 8)) {
        vs_menu_frameworkMenuMemory.cursorMemories[4] = highlightedRow - (rowCount - 9);
        vs_menu_frameworkMenuMemory.cursorMemories[5] = rowCount - 9;
    } else {
        vs_menu_frameworkMenuMemory.cursorMemories[4] = 4;
        vs_menu_frameworkMenuMemory.cursorMemories[5] = highlightedRow - 4;
    }
    cursor = vs_main_settings.cursorMemory;
    vs_main_settings.cursorMemory = 1;
    vs_mainmenu_setMenuRows(rowCount, 0x102, strings, rowTypes);
    vs_main_settings.cursorMemory = cursor;
}

static int _getSelectedRow(void)
{
    int row;

    row = vs_mainmenu_getSelectedRow();
    if (row < -1) {
        vs_mainMenu_clearMenuExcept(5);
        return row;
    }
    if (row >= 0) {
        vs_mainMenu_flyoutMenuRightAndHoistSelection(vs_menu_frameworkMenuMemory.cursorMemories[4], 1);
    }
    return row;
}

static void func_80102E48(char* text) __attribute__((unused));
void func_80102E48(char* text)
{
    vs_battle_menuItem_t* menuItem;

    menuItem = vs_battle_setMenuItem(10, 320, 34, 0x8C, 9, text);
    menuItem->state = 2;
    menuItem->targetPosition0 = 180;
    menuItem->selected = 1;
    menuItem->fontColor = 1;
}

static void func_80102E9C(void) __attribute__((unused));
void func_80102E9C(void) { vs_mainMenu_clearMenuExcept(8); }

static void _menuReady(void) { vs_mainmenu_ready(); }

static u_short _menuText[] = {
#include "build/assets/MENU/MENUE.PRG/menuText.vsString"
};

static u_short _0 __attribute__((unused)) = 0x0380;

int vs_menuE_exec(u_char* state)
{
    enum state { none, init = 3, initMenu, waitReady, showMenu, shutDown, exit };

    static int menuResult = 0;
    static char isInit = 0;
    static char vs_menu_manualOpenDelay = 0;
    static short _2 __attribute__((unused)) = 0x54;

    switch (*state) {
    case init:
        vs_mainMenu_toggleBackgroundFade(1);
        vs_menu_manualOpenDelay = 8;
        isInit = 0;
        *state = initMenu;
        break;
    case initMenu:
        if (isInit == 0) {
            isInit = _initMenuState();
        }
        if (vs_menu_manualOpenDelay != 0) {
            vs_menu_manualOpenDelay -= 1;
        } else if (isInit != 0) {
            vs_field_setFieldRenderingEnabled(0);
            vs_battle_setHudHidden(1);
            vs_battle_hudSlideOffset = 0x200000;
            *state = waitReady;
        }
        break;
    case waitReady:
        *state += vs_mainmenu_ready();
        break;
    case showMenu:
        menuResult = _showMenu();
        if (menuResult == 0) {
            break;
        }
        vs_mainMenu_setBackgroundRenderPriority(-4, 128);
        vs_battle_setHudHidden(0);
        vs_battle_hudSlideOffset = 0;
        vs_field_setFieldRenderingEnabled(1);
        vs_mainMenu_setMenuCommand(menuActionNone);
        vs_mainMenu_clearMenuExcept(vs_mainMenu_menuItemIds_none);
        *state = shutDown;
        break;
    case shutDown:
        vs_mainMenu_toggleBackgroundFade(0);
        *state = exit;
        break;
    case exit:
        if (vs_mainMenu_backgroundFadeStep != 0) {
            break;
        }
        if (menuResult == 2) {
            vs_battle_menuState.currentMenu = 10;
        }
        vs_mainMenu_hideMenu = 0;
        *state = none;
        return 1;
    }
    return 0;
}

// Likely file split but...meh

static int _topMenu_state;
static int _selectedRow;
static vs_main_CdQueueSlot* _helpTextCdQueue;
static vs_main_CdQueueSlot* _helpAssetsCdQueue;
static int _showMenuState;
static int _scrollPosition;
static int _lineCount;
static int _pageArrowAnimState;
static int _helpPageLoadCounter;
static int* _helpText;
static u_long* _helpAssets;

static int _initMenuState(void)
{
    if (vs_main_settings.cursorMemory == 0) {
        vs_main_bzero(&vs_battle_manualDisplayState, sizeof vs_battle_manualDisplayState);
    }
    vs_render_reserveTexturePages(0x1D, 3, 5);
    _showMenuState = 0;
    _scrollPosition = 0;
    _lineCount = 0;
    _helpText = NULL;
    _helpAssets = NULL;
    return 1;
}

#include "../unused_vertices.h"

static vs_main_CdFile const _helpFileCdFiles[] = { mkHfoPair(HELP01), mkHfoPair(HELP02),
    mkHfoPair(HELP03), mkHfoPair(HELP04), mkHfoPair(HELP05), mkHfoPair(HELP06),
    mkHfoPair(HELP07), mkHfoPair(HELP08), mkHfoPair(HELP09), mkHfoPair(HELP10),
    mkHfoPair(HELP11), mkHfoPair(HELP12), mkHfoPair(HELP13), mkHfoPair(HELP14) };

static P_CODE const _colorStops0[] = { { 0, 0x20, 0x50, 0 }, { 0x19, 0x82, 0x6C, 0 },
    { 0x40, 0x30, 0x66, 0 }, { 0x40, 0x38, 0x20, 0 } };
static P_CODE const _colorStops1[] = { { 0, 0x5, 0x33, 0 }, { 0x1, 0x28, 0x26, 0 },
    { 0x8, 0x8, 0x20, 0 }, { 0x10, 0x10, 0x8, 0 } };

static int _showMenu(void)
{
    enum state {
        init,
        initMenu,
        getSelection,
        loadSubMenu,
        initSubMenu,
        showSubMenu,
        exitToMenu,
        exitToBattle,
    };

    char charBuf[60];
    int lastPage;
    int selectedRow;
    int currentPage;
    int cdState;

    switch (_showMenuState) {
    case init:
        vs_mainMenu_setBackgroundRenderPriority(2046, 128);
        ++_showMenuState;
        break;
    case initMenu:
        vs_mainMenu_initInformationBox();
        _topMenu(1);
        ++_showMenuState;
        break;
    case getSelection:
        selectedRow = _topMenu(0);
        if (selectedRow == -1) {
            break;
        }
        if (selectedRow < 0) {
            if (selectedRow == -3) {
                _showMenuState = exitToBattle;
            } else {
                _showMenuState = exitToMenu;
            }
        } else {
            _showMenuState = loadSubMenu;
            if (_helpText != NULL) {
                vs_main_freeHeapR(_helpText);
            }
            if (_helpAssets != NULL) {
                vs_main_freeHeapR(_helpAssets);
            }

            _helpText = vs_main_allocHeapR(_helpFileCdFiles[selectedRow * 2 + 1].size);
            _helpAssets = vs_main_allocHeapR(_helpFileCdFiles[selectedRow * 2].size);
            _helpAssetsCdQueue =
                vs_main_allocateCdQueueSlot(&_helpFileCdFiles[selectedRow * 2]);
            vs_main_cdEnqueue(_helpAssetsCdQueue, _helpAssets);
            _helpTextCdQueue =
                vs_main_allocateCdQueueSlot(&_helpFileCdFiles[selectedRow * 2 + 1]);
            vs_main_cdEnqueue(_helpTextCdQueue, _helpText);
        }
        vs_battle_manualDisplayState.currentManual =
            vs_menu_frameworkMenuMemory.cursorMemories[4] + vs_menu_frameworkMenuMemory.cursorMemories[5];
        vs_mainMenu_dismissInformationBox();
        break;
    case loadSubMenu:
        cdState = _helpAssetsCdQueue->state;
        if (cdState != vs_main_CdQueueStateLoaded) {
            break;
        }
        vs_main_freeCdQueueSlot(_helpAssetsCdQueue);
        _copySprites(_helpAssets + 2, _helpAssets[0]);
        _copyCluts(_helpAssets + ((_helpAssets[0] << 5) + 2), _helpAssets[1]);
        if (_helpAssets != NULL) {
            vs_main_freeHeapR(_helpAssets);
        }
        _helpAssets = NULL;
        _showMenuState = initSubMenu;
        break;
    case initSubMenu:
        if (_helpTextCdQueue->state != vs_main_CdQueueStateLoaded) {
            break;
        }
        vs_main_freeCdQueueSlot(_helpTextCdQueue);
        vs_battle_initTextBox(0, 0, 8, 0x34, 0x19, 0xA, 8, 0x34);
        _scrollPosition =
            vs_battle_manualDisplayState
                .scrollPositions[vs_battle_manualDisplayState.currentManual];
        _lineCount = _getRoundedLineCount();
        _showMenuState = showSubMenu;
        _helpPageLoadCounter = 0;
        break;
    case showSubMenu:
        currentPage = _scrollPosition / 10;
        lastPage = _lineCount / 10;
        if (vs_main_buttonsPressed.all & (PADRright | PADRdown)) {
            _showMenuState = initMenu;
        }
        if (vs_main_buttonsPressed.all & PADRup) {
            _showMenuState = 7;
        }
        if (vs_main_buttonsPressed.all & PADstart) {
            currentPage = 0;
            _scrollPosition = 0;
        } else if (vs_main_buttonsPreviousState & PADRleft) {
            if ((vs_main_buttonsPressed.all & PADLup) && (currentPage == 0)
                && (lastPage > 0)) {
                currentPage = lastPage;
                _scrollPosition = lastPage * 10;
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUCHANGE);
            } else if ((vs_main_buttonsPressed.all & PADLdown)
                       && (currentPage == lastPage) && (lastPage > 0)) {
                currentPage = 0;
                _scrollPosition = 0;
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUCHANGE);
            } else if ((vs_main_buttonRepeat & PADLup) && (currentPage > 0)) {
                --currentPage;
                _scrollPosition = currentPage * 10;
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUCHANGE);
            } else if ((vs_main_buttonRepeat & PADLdown) && (currentPage < lastPage)) {
                ++currentPage;
                _scrollPosition = currentPage * 10;
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUCHANGE);
            }
        } else {
            if ((vs_main_buttonsPressed.all & PADLup) && (_scrollPosition == 0)
                && (_lineCount > 0)) {
                _scrollPosition = _lineCount;
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUCHANGE);
            } else if (vs_main_buttonsPressed.all & PADLdown
                       && (_scrollPosition == _lineCount) && (_scrollPosition > 0)) {
                _scrollPosition = 0;
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUCHANGE);
            } else if ((vs_main_buttonRepeat & PADLup) && (_scrollPosition > 0)) {
                --_scrollPosition;
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUCHANGE);
            } else if ((vs_main_buttonRepeat & PADLdown)
                       && (_scrollPosition < _lineCount)) {
                ++_scrollPosition;
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUCHANGE);
            }
        }
        vs_mainmenu_renderButton(buttonIdSquare, 8, 16, (u_long*)vs_scratch.unk4 + 6);
        vs_battle_renderTextRawColor(
            "TURN    PAGE", 0x12001C, 0x808080, (u_long*)vs_scratch.unk4 + 6);
        _drawControlsBg(0x10, 0x12, 0x60, 0xC);
        vs_mainmenu_renderButton(buttonIdCircle, 8, 34, (u_long*)vs_scratch.unk4 + 6);
        vs_battle_renderTextRawColor(
            "MENU", 0x24001C, 0x808080, (u_long*)vs_scratch.unk4 + 6);
        _drawControlsBg(0x10, 0x24, 0x40, 0xC);
        vs_battle_renderTextRawColor(
            "PAGE", 0xC400D8, 0x808080, (u_long*)vs_scratch.unk4 + 7);
        sprintf(charBuf, "#%d/", currentPage + 1);
        vs_battle_renderTextRawColor(
            charBuf, 0xC40118, 0x808080, (u_long*)vs_scratch.unk4 + 7);
        sprintf(charBuf, "#%d", lastPage + 1);
        vs_battle_renderTextRawColor(
            charBuf, 0xC40130, 0x808080, (u_long*)vs_scratch.unk4 + 7);
        vs_battle_manualDisplayState
            .scrollPositions[vs_battle_manualDisplayState.currentManual] =
            _scrollPosition;
        if (_showMenuState != showSubMenu) {
            if (vs_main_buttonsPressed.all & PADRright) {
                vs_main_playSfxDefault(0x7E, VS_SFX_MENUSELECT);
            } else {
                vs_main_playSfxDefault(0x7E, VS_SFX_MENULEAVE);
            }
            vs_battle_getTextBox(0)->state = 0;
            if (_helpText != NULL) {
                vs_main_freeHeapR(_helpText);
            }
            _helpText = NULL;
            break;
        }
        _setPageBg(8, 52, 310, 136, 0);
        _pageArrowAnimState = (_pageArrowAnimState + 1) & 0xF;
        if (_scrollPosition > 0) {
            _fadeMenuUpper();
        }
        if (_scrollPosition < _lineCount) {
            _fadeMenuLower();
        }
        if (_helpPageLoadCounter < 8) {
            ++_helpPageLoadCounter;
        } else {
            _drawContent();
        }
        _copyNextPageText();
        break;
    case exitToMenu:
        if (_helpText != NULL) {
            vs_main_freeHeapR(_helpText);
        }
        if (_helpAssets != NULL) {
            vs_main_freeHeapR(_helpAssets);
        }
        vs_render_retireTexturePages(0x1D, 3, 5);
        return 1;
    case exitToBattle:
        if (_helpText != NULL) {
            vs_main_freeHeapR(_helpText);
        }
        if (_helpAssets != NULL) {
            vs_main_freeHeapR(_helpAssets);
        }
        vs_render_retireTexturePages(0x1D, 3, 5);
        return 2;
    }
    return 0;
}

static int _topMenu(int init)
{
    enum state { initRows, selectRow };

    char* strings[28];
    int i;

    if (init != 0) {
        _topMenu_state = initRows;
        _selectedRow = 0;
    }

    switch (_topMenu_state) {
    case initRows:
        for (i = 0; i < 14; ++i) {
            strings[i * 2] = (char*)&_menuText[_menuText[i]];
            strings[i * 2 + 1] = (char*)&_menuText[_menuText[14 + i]];
        }
        _setMenuRows(14, vs_battle_manualDisplayState.currentManual, strings);
        ++_topMenu_state;
        break;
    case selectRow:
        _selectedRow = _getSelectedRow();
        if (_selectedRow != -1) {
            return _selectedRow;
        }
        break;
    }
    return -1;
}

static void _copySprites(u_long const* buffer, int count)
{
    RECT rect;
    int i;

    i = 0;
    rect.w = 4;
    rect.h = 16;

    for (i = 0; i < count; ++i) {
        rect.x = (i % 48) * 4 + 832;
        rect.y = (i / 48) * 16 + 256;
        LoadImage2(&rect, (u_long*)buffer);
        buffer += 4 * 16 / 2;
    }
}

static void _copyCluts(u_long* buffer, int count)
{
    RECT rect;
    int i;

    rect.w = 16;
    rect.h = 1;

    for (i = 0; i < count; ++i) {
        rect.x = (i % 16) * 16 + 768;
        rect.y = i / 16 + 496;
        LoadImage2(&rect, buffer);
        buffer += 8;
    }
}

static int _getRoundedLineCount(void)
{
    int remainder;
    int lineCount;

    lineCount = _helpText[4] & 0xFFFF;
    remainder = (_helpText[4] & 0xFFFF) % 10;
    if (remainder > 0) {
        lineCount += 10 - remainder;
    }
    lineCount -= 10;
    if (lineCount < 0) {
        lineCount = 0;
    }
    return lineCount;
}

static void _copyNextPageText(void)
{
#ifdef ROOD_PC
    /* Text box 0 keeps a pointer to the page (vs_battle_setTextBox) and prints it every
     * frame: on the PS1 the dead stack frame happened to survive until then; here it is
     * kept (ui_text's whole-line lookup, pc/src/text_lines.c, walks it later in the
     * frame, after deeper calls have reused that stack). */
    static char buffer[1024];
#else
    char buffer[1024];
#endif
    int i;
    int nextPage;
    u_short* offsets;
    char* pBuffer;

    buffer[0] = vs_char_chunkSize;
    buffer[1] = vs_char_noChunking;
    buffer[2] = vs_char_fontTable;
    buffer[3] = vs_char_fontTable0;
    buffer[4] = vs_char_newline;

    offsets = (ushort*)(_helpText + 4);
    nextPage = _scrollPosition + 10;
    pBuffer = buffer + 4;

    if ((u_short)_helpText[4] < nextPage) {
        nextPage = (u_short)_helpText[4];
    }

    for (i = _scrollPosition; i < nextPage; ++i) {
        pBuffer = _vsStrcpy(pBuffer, (char*)(offsets + offsets[i + 1]));
    }

    pBuffer[-1] = vs_char_terminator;
    vs_battle_setTextBox(0, buffer);
}

static char* _vsStrcpy(char* arg0, char* arg1)
{
    char c;

    for (c = *arg1; c != vs_char_terminator; c = *arg1) {
        if (c >= vs_char_control) {
            *arg0 = c;
            ++arg1;
            ++arg0;
        }
        *arg0++ = *arg1++;
    }
    *arg0 = vs_char_newline;
    return arg0 + 1;
}

static void _drawContent(void)
{
    RECT rect;
    int i;
    int spriteOffset;
    u_short* spriteData;
    RECT* clip = &vs_main_drawEnv[(vs_main_frameBuf + 1) & 1].clip;

    vs_menu_area(0, clip->x, clip->y, clip->w, clip->h);

    _drawContentLines();

    spriteOffset = _helpText[0];
    if (spriteOffset < 0) {
        spriteOffset += 3;
    }
    spriteData = (u_short*)_helpText + 8 + (spriteOffset >> 2) * 2;

    for (i = 0; i < spriteData[0]; ++i) {
        _drawSprite(&spriteData[spriteData[i + 1]]);
    }
    if (vs_main_drawEnv[(vs_main_frameBuf + 1) & 1].clip.x >= 320) {
        rect.x = 333;
    } else {
        rect.x = 13;
    }
    rect.w = 300;
    rect.y = 55;
    rect.h = 130;

    vs_menu_area(0, rect.x, rect.y, rect.w, rect.h);
}

static inline int _min(int arg0, int arg1)
{
    if (arg0 >= arg1) {
        return arg1;
    }
    return arg0;
}

static inline int _add_int(int arg0, int arg1) { return (arg0 + arg1); }

static void _drawSprite(short* data)
{
    short posX;
    short posY;
    short width;
    short height;
    short colors;
    short clutPacked;
    int spriteW;
    int spriteH;
    int tileStride;
    int spriteX;
    int spriteY;
    int j;
    int i;
    short tPage;

    if ((*data != 0) && ((_getAnimationState((AnimationState_t*)data) << 0x10) != 0)) {
        return;
    }

    data += 8;
    posX = *data++;
    posY = *data++;
    width = *data++;
    height = *data++;
    ++data; // num_sprites
    colors = *data++;
    clutPacked = *data++;

    clutPacked =
        ((((clutPacked % 16) << 4) + 0x300) >> 4) | (((clutPacked / 16) + 0x1F0) << 6);

    tileStride = 8;

    if (colors == 16) {
        tileStride = 16;
    }

    for (i = 0; i < height; i += 16) {
        spriteH = _min(height - i, 16);

        for (j = 0; j < width; ++data, j += tileStride) {
            int spriteU0;
            int spriteV0;
            int spriteU;

            spriteW = _min(width - j, tileStride);
            spriteX = 0x0D;
            spriteX = posX + (j + spriteX);
            spriteY = _add_int(posY, i + 0x37);
            spriteY -= _scrollPosition * 0xD;

            if ((spriteY < 0x27) || (0xB9 <= spriteY)) {
                continue;
            }

            spriteU0 = (*data / 16);
            tPage = spriteU0;
            tPage = tPage % 3 + 0x1D;

            if (colors == 16) {
                spriteU = ((*data - (spriteU0 << 4)) << 16) >> 12;
            } else {
                tPage |= 0x80;
                spriteU = ((*data - (spriteU0 << 4)) << 16) >> 13;
            }

            spriteV0 = (*data / 48);

            /* (the draw mode inserted after the sprite in its slot draws before it) */
            vs_menu_sprite(0, PC_ITEM_MODE(tPage), 0, 1, 0, spriteX, spriteY, spriteW, spriteH, spriteU, spriteV0 << 4,
                clutPacked);
        }
    }
}

static void _setPageBg(int x, int y, int w, int h, int color)
{
    unsigned c0 = vs_getRGB888(_colorStops0[color].r0, _colorStops0[color].g0, _colorStops0[color].b0);
    unsigned c1 = vs_getRGB888(_colorStops1[color].r0, _colorStops1[color].g0, _colorStops1[color].b0);

    vs_menu_g4(7, PC_ITEM_MODE(getTPage(0, 3, 0, 0)), 0, c0, vs_getXY(x, y), c1, vs_getXY((x + w) - 1, y), c0,
        vs_getXY(x, (y + h) - 1), c1, vs_getXY((x + w) - 1, (y + h) - 1));
}

static _menuBgChunkWidths_t const _menuBgChunkWidths = { 128, 128, 64 };

static _menuBgTransparencies_t const _menuBgTransparencies[] = { { 128, 115, 121, 88 },
    { 76, 64, 53, 42 }, { 33, 24, 17, 11 }, { 6, 3, 1, 0 } };

static void _fadeMenuUpper(void)
{
    int widths[4];
    int transparency[16];
    _menuBgTransparencies_t const* var_v0;
    _menuBgTransparencies_t* var_v1;
    int i;
    int j;
    /* (a draw mode inserted at the slot's head draws before all of them: the tiles' mode, the polygons' dither bits) */
    int mode = PC_ITEM_MODE(getTPage(0, 2, 0, 0));

    *(_menuBgChunkWidths_t*)widths = _menuBgChunkWidths;

    var_v1 = (_menuBgTransparencies_t*)transparency;
    var_v0 = _menuBgTransparencies;

    do {
        *var_v1++ = *var_v0++;
    } while (var_v0 != (_menuBgTransparencies + 4));

    _drawPaginationArrow(arrowTypeUp);

    for (i = 0; i < 16; ++i) {
        for (j = 0; j < 3; ++j) {
            vs_menu_ft4(-7, mode, (j + 32) | 128 | (640 >> 6) | (256 >> 4), getClut(768, 227), 1, 0,
                vs_getRGB888(transparency[i], transparency[i], transparency[i]), vs_getXY(j << 7, i + 52),
                vs_getXY(widths[j] + (j << 7), i + 52), vs_getXY(j << 7, i + 53), vs_getXY(widths[j] + (j << 7), i + 53),
                (i + 52) << 8, widths[j] | ((i + 52) << 8), (i + 53) << 8, widths[j] | ((i + 53) << 8));
        }
    }

    for (i = 0; i < 16; ++i) {
        vs_menu_tile(-7, mode, 1, vs_getRGB888(transparency[i], transparency[i], transparency[i]), vs_getXY(0, i + 52),
            vs_getWH(320, 1));
    }
}

static void _fadeMenuLower(void)
{
    int widths[4];
    int transparency[16];
    _menuBgTransparencies_t const* var_v0;
    _menuBgTransparencies_t* var_v1;
    int i;
    int j;
    int mode = PC_ITEM_MODE(getTPage(0, 2, 0, 0));

    *(_menuBgChunkWidths_t*)widths = _menuBgChunkWidths;

    var_v1 = (_menuBgTransparencies_t*)transparency;
    var_v0 = _menuBgTransparencies;

    do {
        *var_v1++ = *var_v0++;
    } while (var_v0 != (_menuBgTransparencies + 4));

    _drawPaginationArrow(arrowTypeDown);

    for (i = 0; i < 16; ++i) {
        for (j = 0; j < 3; ++j) {
            vs_menu_ft4(-7, mode, (j + 32) | 128 | (640 >> 6) | (256 >> 4), getClut(768, 227), 1, 0,
                vs_getRGB888(transparency[i], transparency[i], transparency[i]), vs_getXY(j << 7, 187 - i),
                vs_getXY(widths[j] + (j << 7), 187 - i), vs_getXY(j << 7, 188 - i), vs_getXY(widths[j] + (j << 7), 188 - i),
                ((-69 - i) & 0xFF) << 8, widths[j] | (((-69 - i) & 0xFF) << 8), ((-68 - i) & 0xFF) << 8,
                widths[j] | (((-68 - i) & 0xFF) << 8));
        }
    }

    for (i = 0; i < 16; ++i) {
        vs_menu_tile(-7, mode, 1, vs_getRGB888(transparency[i], transparency[i], transparency[i]), vs_getXY(0, 0xBB - i),
            vs_getWH(320, 1));
    }
}

static void _drawPaginationArrow(enum arrowType_e arrowType)
{
    int y;

    if (arrowType != arrowTypeUp) {
        if (_pageArrowAnimState < 9) {
            y = (_pageArrowAnimState >> 1) + 184;
        } else {
            y = -(_pageArrowAnimState >> 2) + 190;
        }
    } else {
        if (_pageArrowAnimState < 9) {
            int v0 = 40;
            y = v0 - (_pageArrowAnimState >> 1);
        } else {
            y = (_pageArrowAnimState >> 2) + 34;
        }
    }

    vs_menu_ft4(-7, 0, getTPage(clut4Bit, semiTransparencyHalf, 768, 0),
        (vs_main_buttonsPreviousState & PADRleft) ? getClut(896, 223) : getClut(912, 223), 0, 1, 0, vs_getXY(152, y),
        vs_getXY(168, y), vs_getXY(152, y + 16), vs_getXY(168, y + 16), (arrowType * 16) | (48 << 8),
        (arrowType * 16 + 16) | (48 << 8), (arrowType * 16) | (64 << 8), (arrowType * 16 + 16) | (64 << 8));
}

static void _drawContentLines(void)
{
    short animationState;
    short i;
    short* linesData;
    int linesOffset;
    int spriteOffset;
    short x0;
    short y0;
    short x1;
    short y1;
    short linesCount;
    char* c;

    if (_helpText[2] <= 0) {
        return;
    }

    spriteOffset = _helpText[0];
    if (spriteOffset < 0) {
        spriteOffset += 3;
    }
    spriteOffset >>= 2;

    linesOffset = _helpText[1];
    if (linesOffset < 0) {
        linesOffset += 3;
    }

    linesOffset = spriteOffset + (linesOffset >> 2);
    linesData = (short*)&(_helpText)[linesOffset + 4];

    linesCount = *linesData++;

    for (i = 0; i < linesCount; ++i) {
        animationState = 0;
        if (*linesData != 0) {
            animationState = _getAnimationState((AnimationState_t*)linesData);
        }
        linesData += 8;
        x0 = *linesData++ + 13;
        y0 = *linesData++ + 55 - _scrollPosition * 13;
        x1 = *linesData++ + 13;
        y1 = *linesData++ + 55 - _scrollPosition * 13;
        c = (char*)linesData;
        linesData += 2;
        if ((y0 < 55) && ((y1 >= 55) == 0)) {
            continue;
        }
        if (((y0 < 186) || (y1 < 186)) && ((animationState << 16) == 0)) {
            vs_menu_line(0, 0, 0, vs_getRGB888(c[0], c[1], c[2]), vs_getXY(x0, y0), vs_getXY(x1, y1));
        }
    }
}

static short _getAnimationState(AnimationState_t* state)
{
    short nextState;

    if (state->currentFrame >= 16) {
        return state->isActive;
    }

    nextState = state->frameMask >> state->currentFrame;
    nextState = nextState & 1;

    if ((++state->timer) >= state->frameDuration) {
        state->timer = 0;
        if ((++state->currentFrame) >= 16) {
            if (state->repeat != 0) {
                state->currentFrame = 0;
            }
        }
    }
    return state->isActive = nextState ^ 1;
}

static void _drawControlsBg(int x, int y, int w, int h)
{
    int i;
    int j;

    for (i = y, j = w; i < (y + h) - 1; ++i, --j) {
        vs_menu_line_g(7, 0, 0, 0, vs_getRGB888(64, 56, 32), vs_getXY(x, i), vs_getRGB888(16, 16, 8), vs_getXY((x + j) - 1, i));
    }
    x += 2;
    y += 2;
    vs_menu_f4(7, 0, 0, 0, vs_getXY(x, y), vs_getXY((x + w) - 1, y), vs_getXY(x, (y + h) - 1), vs_getXY((x + w) - h, (y + h) - 1));
}
