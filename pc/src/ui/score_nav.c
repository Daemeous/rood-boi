/* Modern menus, the native Score screen's list cursors (score_nav.h). */
#include "score_nav.h"

int pc_score_nav_rows(int list)
{
    static const int rows[PC_SCORE_LISTS] = {32, 23, 8, 78};
    return list >= 0 && list < PC_SCORE_LISTS ? rows[list] : 0;
}

int pc_score_nav_max_page(int list)
{
    static const int max[PC_SCORE_LISTS] = {0x18, 0x0F, 7, 0x46};
    return list >= 0 && list < PC_SCORE_LISTS ? max[list] : 0;
}

int pc_score_nav_step(int list, unsigned char* page, unsigned char* row, int up_repeat, int up_press,
    int down_repeat, int down_press)
{
    int max = pc_score_nav_max_page(list);
    if (list == PC_SCORE_LIST_TRIALS) { /* state 11 */
        if (up_repeat) {
            *page = (unsigned char)((*page - 1) & 7);
            return 1;
        }
        if (down_repeat) {
            *page = (unsigned char)((*page + 1) & 7);
            return 1;
        }
        return 0;
    }
    if (list < 0 || list >= PC_SCORE_LISTS)
        return 0;
    /* states 5, 8, 14 */
    if (*row == 0 && *page == 0 && up_press) {
        *page = (unsigned char)max;
        *row = 7;
        return 1;
    }
    if (*row == 7 && *page == max && down_press) {
        *page = 0;
        *row = 0;
        return 1;
    }
    if (up_repeat) {
        if (*row >= 2) {
            --*row;
            return 1;
        }
        if (*page != 0) {
            --*page;
            return 1;
        }
        if (*row != 0) {
            --*row;
            return 1;
        }
        return 0;
    }
    if (down_repeat) {
        if (*row >= 6) {
            if (*page < max) {
                ++*page;
                return 1;
            }
            if (*row < 7) {
                ++*row;
                return 1;
            }
            return 0;
        }
        ++*row;
        return 1;
    }
    return 0;
}

int pc_score_nav_index(int list, int page, int row)
{
    return list == PC_SCORE_LIST_TRIALS ? (page & 7) : page + row;
}

void pc_score_nav_canon(int list, int index, unsigned char* page, unsigned char* row)
{
    int rows = pc_score_nav_rows(list), max = pc_score_nav_max_page(list);
    if (index < 0)
        index = 0;
    if (index >= rows)
        index = rows - 1;
    if (list == PC_SCORE_LIST_TRIALS) {
        *page = (unsigned char)index;
        *row = 0;
        return;
    }
    if (index <= 6) {
        *page = 0;
        *row = (unsigned char)index;
    } else if (index <= max + 6) {
        *page = (unsigned char)(index - 6);
        *row = 6;
    } else {
        *page = (unsigned char)max;
        *row = 7;
    }
}
