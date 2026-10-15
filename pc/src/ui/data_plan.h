/* Modern menus, the native Data screen's planner (data_frame.h): pure C, no
 * game headers. A goal (a click) becomes at most one synthetic pad press a
 * frame for MENU7, which decides everything itself: the planner only walks
 * MENU7's own cursor to the goal's target with Up / Down and confirms with
 * Circle once the cursor is there, in the same snapshot.
 *
 * Rules (data_plan_probe checks them exhaustively):
 *   - one press, then a frame without one (MENU7 reads Up / Down from the
 *     repeat word: an edge each time);
 *   - nothing while the real pad is busy; a real press clears the goal;
 *   - a publish from a call that carried a press (settling) is waited out
 *     as BUSY: MENU7 lays the file list out from the old page before it
 *     reads Up / Down, so a press that pages the list publishes the new
 *     cursor over the old page (the file one page behind) for that call;
 *   - MOVE (the wheel) walks the file cursor and confirms nothing;
 *   - Circle only when the phase and the cursor are the goal's target;
 *   - Cross only for BACK, ANSWER(no) and CLOSE / TAB backing out of the
 *     card rows and the file list (never at a prompt: prompts are never
 *     answered for the player; CLOSE / TAB stop at a prompt or a message);
 *   - a goal waits through BUSY (MENU7 animating, card I/O) for at most 600
 *     frames, expires after 60 frames in a phase that takes it, and is
 *     dropped in a phase that does not;
 *   - the chooser's goals (OPS) are the bridge's list's (PC_UI_DATA_PLAN_OPS).
 */
#ifndef PC_UI_DATA_PLAN_H
#define PC_UI_DATA_PLAN_H
#include "data_frame.h"
#ifdef __cplusplus
extern "C" {
#endif

/* The planner's presses (the bridge maps them to the PS1 pad bits). */
enum { PC_UI_DATA_PRESS_UP = 1, PC_UI_DATA_PRESS_DOWN = 2, PC_UI_DATA_PRESS_CIRCLE = 4, PC_UI_DATA_PRESS_CROSS = 8 };

/* pc_ui_data_plan's results. */
enum {
    PC_UI_DATA_PLAN_NONE,    /* no press this frame (no goal, waiting, the edge frame) */
    PC_UI_DATA_PLAN_PRESS,   /* *press_out goes to MENU7 this frame */
    PC_UI_DATA_PLAN_OPS,     /* the chooser's goal: the bridge's list takes it (goal kept) */
    PC_UI_DATA_PLAN_DROPPED, /* the goal is gone (phase, expiry, a real press) */
};

/* What the planner sees: one publish. */
typedef struct {
    int phase;   /* PC_UI_DATA_PH_* */
    int card;    /* the card row 1 / 2 (CARD) */
    int file;    /* the file 0..4 (FILES) */
    int cancelled; /* the prompt's cursor: 0 Yes, 1 No (PROMPT) */
    int real;    /* the real pad pressed or repeated this frame */
    int settling; /* the publish seen came from a call with a press (its file may be stale) */
} pc_ui_data_snap;

enum { PC_UI_DATA_GOAL_AGE = 60, PC_UI_DATA_GOAL_LIFE = 600 };

int pc_ui_data_plan(const pc_ui_data_snap* s, pc_ui_data_goal* g, unsigned* press_out);

/* Whether a phase takes a goal kind (posts elsewhere are refused). */
int pc_ui_data_accepts(int phase, int kind);

/* The phase of one publish. state: MENU7's *state; elements_active,
 * progress_counter, fade, cursor_xy, msg as published. */
int pc_ui_data_phase(int state, int elements_active, int progress_counter, int fade, int cursor_xy, int msg);
/* A message that waits for a press (WAIT when no cursor shows). */
int pc_ui_data_msg_waits(int msg);
/* The prompt kind of a message (PROMPT). */
int pc_ui_data_msg_prompt(int msg);

/* The cursor decoded: card 1 / 2, file 0..4 (from the page), cancelled 0 / 1;
 * -1 when the cursor is not on that screen. */
void pc_ui_data_decode(int cursor_xy, int list_y0, int* card, int* file, int* cancelled);

#ifdef __cplusplus
}
#endif
#endif
