#include "pc/src/game/SLUS_010.40/main.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/573B8.h"
#include "pc/src/game/MENU/MAINMENU.PRG/C48.h"
#include "pc/src/game/MENU/MAINMENU.PRG/2D10.h"
#include "pc/src/game/MENU/MAINMENU.PRG/58EC.h"
#include "gpu.h"
#include "scratch.h"
#include <libetc.h>
#ifdef ROOD_PC
#include "pc/src/classic/classic_hook.h"
#include "pc/src/game/spriteItem.h"
#endif

/**
 * Process L1/R1 input.
 *
 * @return The new page to display.
 */
static int _itemPaging(int items, int page)
{
    if ((vs_main_buttonsState & (PADL1 | PADR1)) != (PADL1 | PADR1)) {

        int previousPage = page;

        items = vs_mainMenu_getItemCount(items, NULL);

        if (vs_main_buttonRepeat & PADL1) {
            page += items - 1;
        }

        if (vs_main_buttonRepeat & PADR1) {
            ++page;
        }

        if (page >= items) {
            page -= items;
        }

        if ((vs_main_buttonsPressed.all & (PADL1 | PADR1)) && (page == previousPage)) {
            vs_battle_playInvalidSfx();
        }
    }

    return page;
}

/**
 * Draws the item categories, optionally highlighting the selection.
 */
static void _renderItemCategory(int animationStep, int highlightSelected)
{
    int i;
    int selectedCategory = (vs_menu_frameworkMenuMemory.unk3A.currentItemCategory - 1) & 7;

    if (animationStep == 4) {
        vs_mainMenu_renderTabNavigation(0);
    }

    animationStep = (animationStep * 8) - 16;
#ifdef ROOD_PC
    /* PC_CLASSIC (classic_hook.h): the eight icons, from the step, the highlight and the
     * selected category (on_row: the one the loop hands to row 31 instead, by its test). */
    {
        pc_classic_items_category pcCategory = { animationStep, highlightSelected,
            selectedCategory,
            highlightSelected == 1
                    && (((selectedCategory + 1) & 7) * 16) + 32
                           >= vs_battle_getMenuItem(31)->x - 12
                ? selectedCategory
                : -1 };
        PC_CLASSIC(PC_CLASSIC_SITE_ITEMS_CATEGORY, &pcCategory, 0, 0, {
#endif

    for (i = 0; i < 8; ++i) {
        u_long* texPage;
        int x = (((i + 1) & 7) * 16) + 32;

        if (i == selectedCategory && highlightSelected == 1) {

            vs_battle_menuItem_t* menuItem = vs_battle_getMenuItem(31);

            if (x >= (menuItem->x - 12)) {
                menuItem->rowIcon = i + 24;
                continue;
            }

            vs_battle_spriteItem(NULL, 128, x | (16 << 16), vs_getWH(16, 16), VS_SPRITE_PAGE,
                (120 + i * 16) | (128 << 8) | vs_getUV0Clut(0, 0, 976, 223));

        } else {
            vs_battle_spriteItem(vs_scratch.unk4, 128, x | (animationStep << 0x10), vs_getWH(16, 16),
                VS_SPRITE_PAGE,
                (120 + i * 16) | (128 << 8)
                    | (i == selectedCategory ? vs_getUV0Clut(0, 0, 976, 223) : vs_getUV0Clut(0, 0, 992, 223)));
        }
    }
#ifdef ROOD_PC
        });
    }
#endif
}

/**
 * Hoists the item category and selected item to the top of the menu
 * during the start of the menu transition animation.
 */
static void _startMenuTransition(int id)
{
    vs_battle_menuItem_t* menuItem;

    vs_mainMenu_clearMenuExcept(vs_mainMenu_menuItemIds_none);

    menuItem = vs_battle_getMenuItem(31);
    menuItem->state = menuItemStateSlideY;
    menuItem->targetPosition0 = 18;

    menuItem = vs_battle_getMenuItem(id);
    menuItem->state = menuItemStateSlideX;
    menuItem->targetPosition0 = 155;
    menuItem->selected = 1;
    menuItem->subText = NULL;
}

/**
 * Outdents the category and hoists the selected row to the top.
 */
static void _continueMenuTransition(int id)
{
    vs_battle_menuItem_t* menuItem = vs_battle_getMenuItem(31);
    menuItem->state = menuItemStateSlideX;
    menuItem->targetPosition0 = 16;
    menuItem->w = 164;

    menuItem = vs_battle_getMenuItem(id);
    menuItem->state = menuItemStateSlideY;
    menuItem->targetPosition0 = 18;
}

#ifdef _ITEMMENU
static u_short _menuText[] = {
#include "build/assets/MENU/MENU3.PRG/menuText.vsString"
};
#else
static u_short _menuText[] = {
#include "build/assets/MENU/MENUB.PRG/menuText.vsString"
};
#endif
