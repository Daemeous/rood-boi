#ifndef PC_OPTIONS_NAV_H
#define PC_OPTIONS_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

enum { OPTIONS_NAV_ROWS = 9 };
enum {
    OPTIONS_NAV_UP = 1, OPTIONS_NAV_DOWN = 2,
    OPTIONS_NAV_FRESH_UP = 4, OPTIONS_NAV_FRESH_DOWN = 8
};
typedef enum {
    OPTIONS_NAV_NONE, OPTIONS_NAV_CHANGE, OPTIONS_NAV_INVALID
} options_nav_sound;

typedef struct {
    int count;
    int cursor; /* absolute row, not window-relative */
    int top;
    const unsigned char* selectable; /* count entries: nonzero selectable; NULL = all */
} options_nav_state;

typedef struct {
    options_nav_state state;
    options_nav_sound sound;
} options_nav_result;

/* Pure nine-row (vanilla rowInfo=1) step. UP/DOWN are repeat pulses, not
 * raw held buttons: the caller supplies the game's button-repeat timing.
 * FRESH bits permit edge wrapping but do not themselves cause movement.
 * Requires count >= 0, an in-range cursor and 0 <= top <= max(count-9,0)
 * for nonempty lists; a cursor on visible row 0 or 8 of a mid-list page (a
 * restored focus; the original's steps never leave it there) pages when it
 * moves past that edge, so it stays visible. Empty lists are unchanged. Non-selectable rows are
 * skipped using the same scrolling rules; if no target is reachable the
 * state is unchanged. A blocked repeat is silent, no selectable row is
 * INVALID, and a changed final cursor is CHANGE. Up runs before Down.
 */
options_nav_result options_nav_step(options_nav_state state, unsigned input);

#ifdef __cplusplus
}
#endif
#endif
