#include "options_nav.h"

/* src/MENU/MAINMENU.PRG/58EC.c:1053-1109, with rowInfo = 10 - rows. The
 * original stores a window-relative cursor; this API stores cursor+page. */
static void move_one(options_nav_state* s, int down, int fresh)
{
    int row = s->cursor - s->top;
    int rows = s->rows > 0 ? s->rows : OPTIONS_NAV_ROWS;
    if (s->count <= rows) {
        /* 1055-1061 / 1083-1089: short lists wrap even on repeat. */
        s->cursor += down ? 1 : -1;
        if (s->cursor < 0)
            s->cursor = s->count - 1;
        if (s->cursor == s->count)
            s->cursor = 0;
    } else if (!down) {
        /* 1063-1078: fresh-only wrap; paging holds visible row 1.
         * The original's count+246+rowInfo is byte arithmetic for count-rows. */
        if (s->top == 0 && row == 0) {
            if (fresh) {
                s->cursor = s->count - 1;
                s->top = s->count - rows;
            }
        } else {
            --s->cursor;
            /* row 0 under a scrolled page never happens in the original
             * (paging holds row 1); a restored focus may start there: page too. */
            if (s->top && row <= 1)
                --s->top;
        }
    } else {
        /* 1091-1107: fresh-only wrap at bottom; otherwise page at
         * visible row (8-rowInfo) = rows-2 until the final page. */
        if (s->top == s->count - rows && row == rows - 1) {
            if (fresh)
                s->cursor = s->top = 0;
        } else {
            ++s->cursor;
            if (s->top < s->count - rows && row >= rows - 2) /* the last row: likewise */
                ++s->top;
        }
    }
}

static int move_selectable(options_nav_state* s, int down, int fresh)
{
    options_nav_state next = *s;
    /* Extension: skip headers/help, bounded even for all-disabled lists.
     * Roll back scrolling too when no selectable destination is reachable. */
    for (int i = 0; i < s->count; ++i) {
        int previous = next.cursor;
        move_one(&next, down, fresh);
        if (next.cursor == previous)
            break;
        if (!next.selectable || next.selectable[next.cursor]) {
            *s = next;
            return 1;
        }
    }
    return 0;
}

options_nav_result options_nav_step(options_nav_state state, unsigned input)
{
    options_nav_result result = { state, OPTIONS_NAV_NONE };
    if (!(input & (OPTIONS_NAV_UP | OPTIONS_NAV_DOWN)))
        return result;
    int any = 0;
    for (int i = 0; i < state.count; ++i) {
        if (!state.selectable || state.selectable[i]) {
            any = 1;
            break;
        }
    }
    if (!any) {
        result.sound = OPTIONS_NAV_INVALID;
        return result;
    }
    /* 1053 / 1081: two independent ifs, not an else-if. */
    if (input & OPTIONS_NAV_UP)
        move_selectable(&result.state, 0, input & OPTIONS_NAV_FRESH_UP);
    if (input & OPTIONS_NAV_DOWN)
        move_selectable(&result.state, 1, input & OPTIONS_NAV_FRESH_DOWN);
    /* 1111-1113: sound reflects the final absolute row, not page movement. */
    if (result.state.cursor != state.cursor)
        result.sound = OPTIONS_NAV_CHANGE;
    return result;
}
