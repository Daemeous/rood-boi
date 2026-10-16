/* Modern menus, the native Data screen's planner (data_plan.h): pure. */
#include "data_plan.h"

int pc_ui_data_msg_waits(int msg)
{
    switch (msg) {
    case PC_UI_DATA_MSG_INSERT_ERROR:
    case PC_UI_DATA_MSG_EMPTY_CARD:
    case PC_UI_DATA_MSG_SAVE_FAILED:
    case PC_UI_DATA_MSG_LOAD_FAILED:
    case PC_UI_DATA_MSG_FILE_CORRUPT:
    case PC_UI_DATA_MSG_SAVED:
    case PC_UI_DATA_MSG_LOADED:
    case PC_UI_DATA_MSG_NO_DATA:
    case PC_UI_DATA_MSG_CARD_FULL:
    case PC_UI_DATA_MSG_FORMAT_FAILED:
    case PC_UI_DATA_MSG_FORMAT_CANCELLED:
    case PC_UI_DATA_MSG_REMOVED:
        return 1;
    default:
        return 0;
    }
}

int pc_ui_data_msg_prompt(int msg)
{
    switch (msg) {
    case PC_UI_DATA_MSG_OVERWRITE_PROMPT: return PC_UI_DATA_PROMPT_OVERWRITE;
    case PC_UI_DATA_MSG_FORMAT_PROMPT: return PC_UI_DATA_PROMPT_FORMAT;
    case PC_UI_DATA_MSG_CONTAINER_WARN: return PC_UI_DATA_PROMPT_CONTAINER;
    case PC_UI_DATA_MSG_CONTAINER_LOSS: return PC_UI_DATA_PROMPT_CONTAINER_LOSS;
    default: return PC_UI_DATA_PROMPT_NONE;
    }
}

/* MENU7's states (260.c vs_menu7_dataMenu): 6 the chooser, 8 the save flow
 * (_showSaveMenu), 10 the load flow (_loadFileMenu); the rest load, start a
 * flow or leave. */
int pc_ui_data_phase(int state, int elements_active, int progress_counter, int fade, int cursor_xy, int msg)
{
    int x = cursor_xy & 0xFFFF;
    if (state == 6)
        return PC_UI_DATA_PH_OPS;
    if (state != 8 && state != 10)
        return PC_UI_DATA_PH_BUSY;
    if (!elements_active || progress_counter != 0 || fade > 0)
        return PC_UI_DATA_PH_BUSY;
    if (cursor_xy != 0) {
        if (x == PC_UI_DATA_CURSOR_CARD_X)
            return PC_UI_DATA_PH_CARD;
        if (x == PC_UI_DATA_CURSOR_FILE_X)
            return PC_UI_DATA_PH_FILES;
        if (x == PC_UI_DATA_CURSOR_PROMPT_X)
            return PC_UI_DATA_PH_PROMPT;
        return PC_UI_DATA_PH_BUSY;
    }
    return pc_ui_data_msg_waits(msg) ? PC_UI_DATA_PH_WAIT : PC_UI_DATA_PH_BUSY;
}

void pc_ui_data_decode(int cursor_xy, int list_y0, int* card, int* file, int* cancelled)
{
    int x = cursor_xy & 0xFFFF, y = (cursor_xy >> 16) & 0xFFFF;
    *card = *file = *cancelled = -1;
    if (cursor_xy == 0)
        return;
    if (x == PC_UI_DATA_CURSOR_CARD_X) {
        /* _showSaveMenu / _loadFileMenu: (selectedFile + 1) * 16 + 10 */
        if (y >= 26 && (y - 26) % 16 == 0 && (y - 26) / 16 >= 1 && (y - 26) / 16 <= 2)
            *card = (y - 26) / 16;
    } else if (x == PC_UI_DATA_CURSOR_FILE_X) {
        /* _showSaveFilesMenu / _showLoadFilesMenu: slot * 40 + 62, element 5 at (0 - page) * 40 + 72 */
        int slot = (y - 62) / 40, page = (72 - list_y0) / 40;
        if (y >= 62 && (y - 62) % 40 == 0 && slot <= 2 && list_y0 <= 72 && (72 - list_y0) % 40 == 0 && page <= 2
            && slot + page < PC_UI_DATA_FILES)
            *file = slot + page;
    } else if (x == PC_UI_DATA_CURSOR_PROMPT_X) {
        /* _promptConfirm: cancelled * 16 + 10 */
        if (y == 10 || y == 26)
            *cancelled = (y - 10) / 16;
    }
}

int pc_ui_data_accepts(int phase, int kind)
{
    switch (phase) {
    case PC_UI_DATA_PH_OPS:
        return kind == PC_UI_DATA_GOAL_OP || kind == PC_UI_DATA_GOAL_BACK || kind == PC_UI_DATA_GOAL_CLOSE
            || kind == PC_UI_DATA_GOAL_TAB;
    case PC_UI_DATA_PH_CARD:
        return kind == PC_UI_DATA_GOAL_CARD || kind == PC_UI_DATA_GOAL_BACK || kind == PC_UI_DATA_GOAL_CLOSE
            || kind == PC_UI_DATA_GOAL_TAB;
    case PC_UI_DATA_PH_FILES:
        return kind == PC_UI_DATA_GOAL_FILE || kind == PC_UI_DATA_GOAL_MOVE || kind == PC_UI_DATA_GOAL_BACK || kind == PC_UI_DATA_GOAL_CLOSE
            || kind == PC_UI_DATA_GOAL_TAB;
    case PC_UI_DATA_PH_PROMPT:
        return kind == PC_UI_DATA_GOAL_ANSWER || kind == PC_UI_DATA_GOAL_BACK;
    case PC_UI_DATA_PH_WAIT:
        return kind == PC_UI_DATA_GOAL_DISMISS || kind == PC_UI_DATA_GOAL_BACK;
    default:
        return 0;
    }
}

static int drop(pc_ui_data_goal* g)
{
    g->kind = PC_UI_DATA_GOAL_NONE;
    g->a = g->automatic = g->age = g->total = g->cool = g->walking = 0;
    return PC_UI_DATA_PLAN_DROPPED;
}

/* A press out: the goal done (consumed) or still walking. */
static int press(pc_ui_data_goal* g, unsigned* out, unsigned bits, int done)
{
    *out = bits;
    if (done) {
        drop(g);
    } else {
        g->cool = 1;
    }
    return PC_UI_DATA_PLAN_PRESS;
}

int pc_ui_data_plan(const pc_ui_data_snap* s, pc_ui_data_goal* g, unsigned* press_out)
{
    int k = g->kind;
    *press_out = 0;
    if (k == PC_UI_DATA_GOAL_NONE)
        return PC_UI_DATA_PLAN_NONE;
    if (s->real)
        return drop(g); /* the player took over */
    if (g->cool) {      /* the edge after a press */
        g->cool = 0;
        return PC_UI_DATA_PLAN_NONE;
    }
    if (++g->total > PC_UI_DATA_GOAL_LIFE)
        return drop(g);
    if (s->phase == PC_UI_DATA_PH_BUSY || s->settling)
        return PC_UI_DATA_PLAN_NONE; /* MENU7 is not taking input, or its list is a page behind: wait */
    /* CLOSE / TAB back out of the flow: they stop at a prompt or a message
     * (never answered for the player), and an auto-advance stops there too */
    if (!pc_ui_data_accepts(s->phase, k) || (g->automatic && s->phase != PC_UI_DATA_PH_CARD))
        return drop(g);
    if (++g->age > PC_UI_DATA_GOAL_AGE)
        return drop(g);
    switch (s->phase) {
    case PC_UI_DATA_PH_OPS:
        return PC_UI_DATA_PLAN_OPS;
    case PC_UI_DATA_PH_CARD:
        if (k == PC_UI_DATA_GOAL_CARD) {
            if (g->a != 1 && g->a != 2)
                return drop(g);
            if (s->card == g->a)
                return press(g, press_out, PC_UI_DATA_PRESS_CIRCLE, 1);
            if (s->card < 1)
                return PC_UI_DATA_PLAN_NONE;
            return press(g, press_out, PC_UI_DATA_PRESS_UP, 0); /* Up and Down both swap the rows */
        }
        if (k == PC_UI_DATA_GOAL_BACK)
            return press(g, press_out, PC_UI_DATA_PRESS_CROSS, 1);
        g->walking = 1, g->age = 0; /* CLOSE / TAB: a step out */
        return press(g, press_out, PC_UI_DATA_PRESS_CROSS, 0);
    case PC_UI_DATA_PH_FILES:
        if (k == PC_UI_DATA_GOAL_FILE) {
            if (g->a < 0 || g->a >= PC_UI_DATA_FILES)
                return drop(g);
            if (s->file == g->a)
                return press(g, press_out, PC_UI_DATA_PRESS_CIRCLE, 1);
            if (s->file < 0)
                return PC_UI_DATA_PLAN_NONE;
            return press(g, press_out, s->file > g->a ? PC_UI_DATA_PRESS_UP : PC_UI_DATA_PRESS_DOWN, 0);
        }
        if (k == PC_UI_DATA_GOAL_MOVE) {
            if (g->a < 0 || g->a >= PC_UI_DATA_FILES)
                return drop(g);
            if (s->file == g->a) {
                drop(g); /* there: nothing confirmed */
                return PC_UI_DATA_PLAN_NONE;
            }
            if (s->file < 0)
                return PC_UI_DATA_PLAN_NONE;
            return press(g, press_out, s->file > g->a ? PC_UI_DATA_PRESS_UP : PC_UI_DATA_PRESS_DOWN, 0);
        }
        if (k == PC_UI_DATA_GOAL_BACK)
            return press(g, press_out, PC_UI_DATA_PRESS_CROSS, 1);
        g->walking = 1, g->age = 0;
        return press(g, press_out, PC_UI_DATA_PRESS_CROSS, 0);
    case PC_UI_DATA_PH_PROMPT:
        if (k == PC_UI_DATA_GOAL_ANSWER && g->a) {
            if (s->cancelled == 0)
                return press(g, press_out, PC_UI_DATA_PRESS_CIRCLE, 1);
            if (s->cancelled < 0)
                return PC_UI_DATA_PLAN_NONE;
            return press(g, press_out, PC_UI_DATA_PRESS_UP, 0);
        }
        return press(g, press_out, PC_UI_DATA_PRESS_CROSS, 1); /* No, or BACK */
    case PC_UI_DATA_PH_WAIT:
        if (k == PC_UI_DATA_GOAL_DISMISS)
            return press(g, press_out, PC_UI_DATA_PRESS_CIRCLE, 1);
        return press(g, press_out, PC_UI_DATA_PRESS_CROSS, 1);
    default:
        return drop(g);
    }
}

int pc_ui_data_plan_yesno(int ready, int yes_lit, int waiting, int real, pc_ui_data_goal* g, unsigned* press_out)
{
    pc_ui_data_snap s;
    s.phase = waiting ? PC_UI_DATA_PH_WAIT : ready ? PC_UI_DATA_PH_PROMPT : PC_UI_DATA_PH_BUSY;
    s.card = s.file = -1;
    s.cancelled = ready ? (yes_lit ? 0 : 1) : -1;
    s.real = real;
    s.settling = 0;
    return pc_ui_data_plan(&s, g, press_out);
}
