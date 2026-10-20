#include "options_nav.h"
#include <stdio.h>
#include <stdlib.h>

#define U OPTIONS_NAV_UP
#define D OPTIONS_NAV_DOWN
#define FU OPTIONS_NAV_FRESH_UP
#define FD OPTIONS_NAV_FRESH_DOWN
#define N OPTIONS_NAV_NONE
#define C OPTIONS_NAV_CHANGE
#define I OPTIONS_NAV_INVALID
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

int main(void)
{
    static const unsigned char skip[] = { 1, 0, 0, 1, 0 };
    static const unsigned char none[] = { 0, 0, 0 };
    static const unsigned char long_skip[] = { 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0 };
    static const struct {
        int count, cursor, top;
        const unsigned char* flags;
        unsigned input;
        int cursor_after, top_after;
        options_nav_sound sound;
    } cases[] = {
        /* 58EC.c:1055-1061,1083-1089: <=9 wraps with repeat too. */
        { 3, 0, 0, NULL, U, 2, 0, C },
        { 3, 2, 0, NULL, D, 0, 0, C },
        { 9, 0, 0, NULL, U, 8, 0, C },
        { 9, 8, 0, NULL, D, 0, 0, C },
        { 1, 0, 0, NULL, U | FU, 0, 0, N },
        { 3, 1, 0, NULL, U, 0, 0, C },
        { 3, 1, 0, NULL, D, 2, 0, C },
        /* 1063-1078 / 1091-1099: scrolling lists need fresh edges. */
        { 10, 0, 0, NULL, U, 0, 0, N },
        { 10, 0, 0, NULL, U | FU, 9, 1, C },
        { 12, 11, 3, NULL, D, 11, 3, N },
        { 12, 11, 3, NULL, D | FD, 0, 0, C },
        { 12, 0, 0, NULL, U | FD, 0, 0, N },
        { 12, 11, 3, NULL, D | FU, 11, 3, N },
        /* 1074-1078 / 1104-1107: interior paging at row 1 / row 7. */
        { 12, 7, 0, NULL, D, 8, 1, C },
        { 12, 8, 1, NULL, D, 9, 2, C },
        { 12, 9, 2, NULL, D, 10, 3, C },
        { 12, 10, 3, NULL, D, 11, 3, C },
        { 12, 4, 3, NULL, U, 3, 2, C },
        { 12, 2, 1, NULL, U, 1, 0, C },
        { 12, 1, 0, NULL, U, 0, 0, C },
        { 12, 5, 3, NULL, U, 4, 3, C },
        /* A restored focus on visible row 0 / 8 mid-list (never reached by
         * these steps): it pages instead of leaving the window. */
        { 12, 2, 2, NULL, U, 1, 1, C },
        { 12, 9, 1, NULL, D, 10, 2, C },
        /* 1053,1081,1111-1113: Up then Down; final-row sound only.
         * At a paging threshold both directions can leave a different top. */
        { 12, 4, 3, NULL, U | D, 4, 2, N },
        { 12, 0, 0, NULL, U | D | FU | FD, 0, 0, N },
        { 3, 1, 0, NULL, 0, 1, 0, N },
        { 3, 1, 0, NULL, FU | FD, 1, 0, N },
        /* New policy: skip non-selectable rows, retaining vanilla paging. */
        { 5, 0, 0, skip, D, 3, 0, C },
        { 5, 3, 0, skip, U, 0, 0, C },
        { 5, 3, 0, skip, D, 0, 0, C },
        { 5, 0, 0, skip, U, 3, 0, C },
        { 12, 7, 0, long_skip, D, 10, 3, C },
        { 12, 10, 3, long_skip, D, 10, 3, N },
        { 12, 10, 3, long_skip, D | FD, 1, 0, C },
        { 12, 1, 0, long_skip, U, 1, 0, N },
        { 12, 1, 0, long_skip, U | FU, 10, 3, C },
        { 3, 0, 0, none, D, 0, 0, I },
        { 3, 0, 0, none, 0, 0, 0, N },
        { 0, 0, 0, NULL, D, 0, 0, I },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        options_nav_state s = { cases[i].count, cases[i].cursor, cases[i].top, cases[i].flags, 0 };
        options_nav_result r = options_nav_step(s, cases[i].input);
        if (r.state.cursor != cases[i].cursor_after || r.state.top != cases[i].top_after
            || r.sound != cases[i].sound) {
            fprintf(stderr, "case %u: got cursor=%d top=%d sound=%d\n",
                i, r.state.cursor, r.state.top, r.sound);
            return 1;
        }
        CHECK(r.state.count == s.count && r.state.selectable == s.selectable);
    }
    /* Traverse several full cycles: nine visible rows, no header focus. */
    options_nav_state s = { 12, 1, 0, long_skip, 0 };
    for (int i = 0; i < 200; ++i) {
        options_nav_result r = options_nav_step(s, i < 100 ? D | FD : U | FU);
        s = r.state;
        CHECK(s.cursor >= s.top && s.cursor < s.top + OPTIONS_NAV_ROWS);
        CHECK(s.top >= 0 && s.top <= s.count - OPTIONS_NAV_ROWS);
        CHECK(s.selectable[s.cursor]);
    }
    /* Eight visible rows (rowInfo 2: the classic Option pages): the same
     * rules one row shorter, paging at visible row 6, wrapping at row 7. */
    {
        options_nav_state e = { 12, 6, 0, NULL, 8 };
        options_nav_result r = options_nav_step(e, D);
        CHECK(r.state.cursor == 7 && r.state.top == 1);
        e = (options_nav_state) { 12, 11, 4, NULL, 8 };
        CHECK(options_nav_step(e, D).state.cursor == 11);         /* no fresh press: held */
        r = options_nav_step(e, D | FD);
        CHECK(r.state.cursor == 0 && r.state.top == 0);
        e = (options_nav_state) { 12, 0, 0, NULL, 8 };
        r = options_nav_step(e, U | FU);
        CHECK(r.state.cursor == 11 && r.state.top == 4);
        e = (options_nav_state) { 8, 7, 0, NULL, 8 };          /* a list that fits: wraps on repeat */
        CHECK(options_nav_step(e, D).state.cursor == 0);
        e = (options_nav_state) { 12, 1, 0, long_skip, 8 };
        for (int i = 0; i < 200; ++i) {
            e = options_nav_step(e, i < 100 ? D | FD : U | FU).state;
            CHECK(e.cursor >= e.top && e.cursor < e.top + 8);
            CHECK(e.top >= 0 && e.top <= e.count - 8);
            CHECK(e.selectable[e.cursor]);
        }
    }
    puts("options_nav_probe: OK");
    return 0;
}
