/* The native Score screen's list cursors (pc/src/ui/score_nav.c) against a
 * transcription of MENU9's own (src/MENU/MENU9.PRG/238.c _menuInput states 5,
 * 8, 11 and 14, the vs_battle_menu9CursorMemory fields as locals): every page x
 * row x {up, down} x {press, repeat only} of the four lists, the moves' sound,
 * and the canonical pairs (score_nav_canon) as Down presses from (0, 0). */
#include "score_nav.h"
#include <stdio.h>

static int failures;
#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            ++failures;                                                                                        \
            if (failures < 40) {                                                                               \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                    \
                printf(__VA_ARGS__);                                                                           \
                printf("\n");                                                                                  \
            }                                                                                                  \
        }                                                                                                      \
    } while (0)

/* 238.c state 5 (titles, max 0x18), 8 (records, 0xF), 14 (encyclopaedia, 0x46): the sound count returned. */
static int reference(int max, unsigned char* page, unsigned char* row, int pressed_up, int pressed_down,
    int repeat_up, int repeat_down)
{
    int sfx = 0;
    if ((*row == 0) && (*page == 0) && pressed_up) {
        ++sfx;
        *page = (unsigned char)max;
        *row = 7;
    } else if ((*row == 7) && (*page == max) && pressed_down) {
        ++sfx;
        *page = 0;
        *row = 0;
    } else {
        if (repeat_up) {
            if (*row >= 2) {
                ++sfx;
                --*row;
            } else {
                if (*page != 0) {
                    --*page;
                    ++sfx;
                } else if (*row != 0) {
                    --*row;
                    ++sfx;
                }
            }
        } else if (repeat_down) {
            if (*row >= 6U) {
                if (*page < (unsigned)max) {
                    ++*page;
                    ++sfx;
                } else {
                    if (*row < 7) {
                        ++*row;
                        ++sfx;
                    }
                }
            } else {
                ++*row;
                ++sfx;
            }
        }
    }
    return sfx;
}

/* 238.c state 11 (the time attack's boss) */
static int reference_trials(unsigned char* page, int repeat_up, int repeat_down)
{
    if (repeat_up) {
        *page = (unsigned char)((*page - 1) & 7);
        return 1;
    }
    if (repeat_down) {
        *page = (unsigned char)((*page + 1) & 7);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const int maxes[] = {0x18, 0x0F, 7, 0x46};
    static const int rows[] = {32, 23, 8, 78};
    int list, cases = 0;
    for (list = 0; list < PC_SCORE_LISTS; ++list) {
        CHECK(pc_score_nav_max_page(list) == maxes[list], "list %d max", list);
        CHECK(pc_score_nav_rows(list) == rows[list], "list %d rows", list);
        /* every pair the original can hold (the trials: its page 0..7), and some it cannot (8-bit wraps) */
        int pmax = list == PC_SCORE_LIST_TRIALS ? 7 : maxes[list] + 1;
        for (int p = 0; p <= pmax; ++p)
            for (int r = 0; r <= 8; ++r)
                for (int input = 0; input < 16; ++input) {
                    /* bits: up repeat, up press, down repeat, down press (a press is a repeat too) */
                    int ur = input & 1, up = (input >> 1) & 1, dr = (input >> 2) & 1, dp = (input >> 3) & 1;
                    if ((up && !ur) || (dp && !dr))
                        continue;
                    unsigned char pa = (unsigned char)p, ra = (unsigned char)r, pb = pa, rb = ra;
                    int moved = pc_score_nav_step(list, &pa, &ra, ur, up, dr, dp);
                    int sfx = list == PC_SCORE_LIST_TRIALS ? reference_trials(&pb, ur, dr)
                                                           : reference(maxes[list], &pb, &rb, up, dp, ur, dr);
                    if (list == PC_SCORE_LIST_TRIALS)
                        rb = ra; /* row unused */
                    CHECK(pa == pb && ra == rb && moved == (sfx > 0),
                        "list %d (%d,%d) input %x: (%d,%d) moved %d, original (%d,%d) sfx %d", list, p, r, input, pa,
                        ra, moved, pb, rb, sfx);
                    ++cases;
                }
        /* the canonical pairs: Down presses (each a press and a repeat) from (0, 0) */
        unsigned char p = 0, r = 0;
        for (int i = 0; i < rows[list]; ++i) {
            unsigned char cp, cr;
            pc_score_nav_canon(list, i, &cp, &cr);
            CHECK(cp == p && cr == r && pc_score_nav_index(list, cp, cr) == i,
                "list %d canon %d: (%d,%d), Down x%d reaches (%d,%d)", list, i, cp, cr, i, p, r);
            pc_score_nav_step(list, &p, &r, 0, 0, 1, 1);
        }
        /* the last entry's Down press wraps to the first */
        CHECK(pc_score_nav_index(list, p, r) == 0, "list %d: Down from the last wraps (%d,%d)", list, p, r);
        /* every reachable pair focuses an entry in range */
        for (int pp = 0; pp <= maxes[list]; ++pp)
            for (int rr = 0; rr < 8; ++rr)
                if (list != PC_SCORE_LIST_TRIALS)
                    CHECK(pc_score_nav_index(list, pp, rr) < rows[list], "list %d (%d,%d) in range", list, pp, rr);
    }
    printf("score_nav_probe: %d cases, %d failure(s)\n", cases, failures);
    return failures ? 1 : 0;
}
