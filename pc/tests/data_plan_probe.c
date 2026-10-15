/* The native Data screen's planner (pc/src/ui/data_plan.c), exhaustively:
 * every phase x cursor x goal, and short walks against a model of MENU7's
 * own cursor rules (260.c). */
#include "data_plan.h"
#include <stdio.h>
#include <string.h>

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

static pc_ui_data_goal goal(int kind, int a)
{
    pc_ui_data_goal g;
    memset(&g, 0, sizeof g);
    g.kind = kind, g.a = a;
    return g;
}

/* MENU7's cursor words as 260.c writes them. */
static int xy(int x, int y) { return (y << 16) | x; }

static void decode_formulas(void)
{
    int card, file, cancelled, sf, page, slot;
    /* _showSaveMenu / _loadFileMenu: vs_getXY(180, (selectedFile + 1) * 16 + 10) */
    for (sf = 1; sf <= 2; ++sf) {
        pc_ui_data_decode(xy(180, (sf + 1) * 16 + 10), 72, &card, &file, &cancelled);
        CHECK(card == sf && file < 0 && cancelled < 0, "card row %d decodes as %d", sf, card);
    }
    /* the file lists: (slot * 40 + 62) << 16 | 24, element 5 at ((0 - page) * 40) + 72 */
    for (page = 0; page <= 2; ++page)
        for (slot = 0; slot <= 2; ++slot) {
            pc_ui_data_decode(xy(24, slot * 40 + 62), (0 - page) * 40 + 72, &card, &file, &cancelled);
            CHECK(file == slot + page && card < 0, "file slot %d page %d decodes as %d", slot, page, file);
        }
    /* _promptConfirm: (cancelled * 16 + 10) << 16 */
    for (sf = 0; sf <= 1; ++sf) {
        pc_ui_data_decode(xy(0, sf * 16 + 10), 72, &card, &file, &cancelled);
        CHECK(cancelled == sf, "prompt cursor %d decodes as %d", sf, cancelled);
    }
    pc_ui_data_decode(0, 72, &card, &file, &cancelled);
    CHECK(card < 0 && file < 0 && cancelled < 0, "no cursor: nothing");
    pc_ui_data_decode(xy(180, 30), 72, &card, &file, &cancelled);
    CHECK(card < 0, "an off-grid card cursor is no row");
}

static void phases(void)
{
    int s;
    for (s = 0; s < 16; ++s) {
        int ph = pc_ui_data_phase(s, 1, 0, 0, xy(24, 62), PC_UI_DATA_MSG_SELECT_FILE);
        if (s == 6)
            CHECK(ph == PC_UI_DATA_PH_OPS, "state 6 is the chooser");
        else if (s == 8 || s == 10)
            CHECK(ph == PC_UI_DATA_PH_FILES, "state %d with the file cursor is FILES", s);
        else
            CHECK(ph == PC_UI_DATA_PH_BUSY, "state %d is busy (%d)", s, ph);
    }
    CHECK(pc_ui_data_phase(8, 0, 0, 0, xy(24, 62), 0) == PC_UI_DATA_PH_BUSY, "animating: busy");
    CHECK(pc_ui_data_phase(8, 1, 5, 0, xy(24, 62), 0) == PC_UI_DATA_PH_BUSY, "progress: busy");
    CHECK(pc_ui_data_phase(8, 1, -3, 0, 0, PC_UI_DATA_MSG_SAVED) == PC_UI_DATA_PH_BUSY, "the flash: busy");
    CHECK(pc_ui_data_phase(10, 1, 0, 4, 0, PC_UI_DATA_MSG_LOADED) == PC_UI_DATA_PH_BUSY, "the fade: busy");
    CHECK(pc_ui_data_phase(8, 1, 0, 0, xy(180, 42), 0) == PC_UI_DATA_PH_CARD, "card rows");
    CHECK(pc_ui_data_phase(8, 1, 0, 0, xy(0, 26), PC_UI_DATA_MSG_OVERWRITE_PROMPT) == PC_UI_DATA_PH_PROMPT,
        "prompt");
    for (s = 0; s < PC_UI_DATA_MSGS; ++s) {
        int ph = pc_ui_data_phase(8, 1, 0, 0, 0, s);
        CHECK(ph == (pc_ui_data_msg_waits(s) ? PC_UI_DATA_PH_WAIT : PC_UI_DATA_PH_BUSY), "message %d", s);
    }
    {
        static const int waits[] = { PC_UI_DATA_MSG_INSERT_ERROR, PC_UI_DATA_MSG_EMPTY_CARD,
            PC_UI_DATA_MSG_SAVE_FAILED, PC_UI_DATA_MSG_LOAD_FAILED, PC_UI_DATA_MSG_FILE_CORRUPT,
            PC_UI_DATA_MSG_SAVED, PC_UI_DATA_MSG_LOADED, PC_UI_DATA_MSG_NO_DATA, PC_UI_DATA_MSG_CARD_FULL,
            PC_UI_DATA_MSG_FORMAT_FAILED, PC_UI_DATA_MSG_FORMAT_CANCELLED, PC_UI_DATA_MSG_REMOVED };
        int n = 0;
        for (s = 0; s < PC_UI_DATA_MSGS; ++s)
            n += pc_ui_data_msg_waits(s);
        CHECK(n == (int)(sizeof waits / sizeof waits[0]), "%d waiting messages", n);
        for (s = 0; s < (int)(sizeof waits / sizeof waits[0]); ++s)
            CHECK(pc_ui_data_msg_waits(waits[s]), "message %d waits", waits[s]);
        CHECK(!pc_ui_data_msg_waits(PC_UI_DATA_MSG_CHECKING) && !pc_ui_data_msg_waits(PC_UI_DATA_MSG_SAVING)
                && !pc_ui_data_msg_waits(PC_UI_DATA_MSG_LOADING) && !pc_ui_data_msg_waits(PC_UI_DATA_MSG_FORMATTING)
                && !pc_ui_data_msg_waits(PC_UI_DATA_MSG_ACCESSING0),
            "progress messages do not wait");
    }
}

/* Every phase x cursor x goal, one frame: the invariants. */
static void exhaustive(void)
{
    int phase, card, file, cancelled, kind, a, real, cool, settling;
    for (phase = 0; phase < PC_UI_DATA_PHASES; ++phase)
        for (card = -1; card <= 2; ++card)
            for (file = -1; file < PC_UI_DATA_FILES; ++file)
                for (cancelled = -1; cancelled <= 1; ++cancelled)
                    for (kind = 0; kind < PC_UI_DATA_GOALS; ++kind)
                        for (a = -1; a <= 5; ++a)
                            for (real = 0; real <= 1; ++real)
                                for (cool = 0; cool <= 1; ++cool)
                                for (settling = 0; settling <= 1; ++settling) {
                                    pc_ui_data_snap s = { phase, card, file, cancelled, real, settling };
                                    pc_ui_data_goal g = goal(kind, a);
                                    unsigned p = 99;
                                    int r;
                                    g.cool = cool;
                                    r = pc_ui_data_plan(&s, &g, &p);
                                    CHECK(r != PC_UI_DATA_PLAN_PRESS || p != 0, "a press has bits");
                                    CHECK(r == PC_UI_DATA_PLAN_PRESS || p == 0, "no press: no bits");
                                    CHECK((p & (p - 1)) == 0, "one button at a time (%u)", p);
                                    if (kind == PC_UI_DATA_GOAL_NONE)
                                        CHECK(r == PC_UI_DATA_PLAN_NONE, "no goal: nothing");
                                    if (real && kind)
                                        CHECK(r == PC_UI_DATA_PLAN_DROPPED && g.kind == 0,
                                            "a real press clears the goal");
                                    if (cool && !real)
                                        CHECK(r != PC_UI_DATA_PLAN_PRESS, "the edge frame has no press");
                                    if (phase == PC_UI_DATA_PH_BUSY || phase == PC_UI_DATA_PH_OFF)
                                        CHECK(r != PC_UI_DATA_PLAN_PRESS, "nothing while busy");
                                    if (settling)
                                        CHECK(r != PC_UI_DATA_PLAN_PRESS && r != PC_UI_DATA_PLAN_OPS,
                                            "nothing on a settling publish (its file may be a page behind)");
                                    if (kind == PC_UI_DATA_GOAL_MOVE)
                                        CHECK(p != PC_UI_DATA_PRESS_CIRCLE && p != PC_UI_DATA_PRESS_CROSS,
                                            "MOVE only walks");
                                    if (p == PC_UI_DATA_PRESS_CIRCLE) {
                                        int target = (kind == PC_UI_DATA_GOAL_CARD && phase == PC_UI_DATA_PH_CARD
                                                         && card == a)
                                            || (kind == PC_UI_DATA_GOAL_FILE && phase == PC_UI_DATA_PH_FILES
                                                && file == a)
                                            || (kind == PC_UI_DATA_GOAL_ANSWER && a && phase == PC_UI_DATA_PH_PROMPT
                                                && cancelled == 0)
                                            || (kind == PC_UI_DATA_GOAL_DISMISS && phase == PC_UI_DATA_PH_WAIT);
                                        CHECK(target, "Circle only on the target (phase %d kind %d a %d card %d "
                                                      "file %d cancelled %d)",
                                            phase, kind, a, card, file, cancelled);
                                    }
                                    if (p == PC_UI_DATA_PRESS_CROSS) {
                                        CHECK(kind == PC_UI_DATA_GOAL_BACK
                                                || (kind == PC_UI_DATA_GOAL_ANSWER && !a
                                                    && phase == PC_UI_DATA_PH_PROMPT)
                                                || ((kind == PC_UI_DATA_GOAL_CLOSE || kind == PC_UI_DATA_GOAL_TAB)
                                                    && (phase == PC_UI_DATA_PH_CARD
                                                        || phase == PC_UI_DATA_PH_FILES)),
                                            "Cross only for BACK, No and backing out (phase %d kind %d)", phase,
                                            kind);
                                    }
                                    if (phase == PC_UI_DATA_PH_PROMPT && kind != PC_UI_DATA_GOAL_ANSWER
                                        && kind != PC_UI_DATA_GOAL_BACK)
                                        CHECK(r != PC_UI_DATA_PLAN_PRESS, "a prompt is never answered for "
                                                                           "another goal (kind %d)", kind);
                                    if (r == PC_UI_DATA_PLAN_OPS)
                                        CHECK(phase == PC_UI_DATA_PH_OPS && g.kind == kind, "the chooser's goal "
                                                                                             "kept for the list");
                                }
}

/* MENU7's cursor rules (260.c): the card rows swap on Up or Down; the file
 * list moves one file, clamped; the prompt swaps. */
typedef struct {
    int phase, card, file, cancelled;
    int confirmed, crossed;
} menu7;

static void apply(menu7* m, unsigned p)
{
    if (p == PC_UI_DATA_PRESS_UP || p == PC_UI_DATA_PRESS_DOWN) {
        if (m->phase == PC_UI_DATA_PH_CARD)
            m->card = 3 - m->card;
        if (m->phase == PC_UI_DATA_PH_FILES) {
            if (p == PC_UI_DATA_PRESS_UP && m->file > 0)
                --m->file;
            if (p == PC_UI_DATA_PRESS_DOWN && m->file < 4)
                ++m->file;
        }
        if (m->phase == PC_UI_DATA_PH_PROMPT)
            m->cancelled = 1 - m->cancelled;
    }
    if (p == PC_UI_DATA_PRESS_CIRCLE)
        ++m->confirmed;
    if (p == PC_UI_DATA_PRESS_CROSS)
        ++m->crossed;
}

static int walk(menu7* m, pc_ui_data_goal* g, int frames)
{
    int f, presses = 0, last = 0;
    for (f = 0; f < frames && g->kind; ++f) {
        pc_ui_data_snap s = { m->phase, m->card, m->file, m->cancelled, 0, 0 };
        unsigned p;
        int r = pc_ui_data_plan(&s, g, &p);
        if (r == PC_UI_DATA_PLAN_PRESS) {
            CHECK(!last, "two presses in a row");
            apply(m, p);
            ++presses;
        }
        last = r == PC_UI_DATA_PLAN_PRESS;
        if (r == PC_UI_DATA_PLAN_OPS)
            break;
    }
    return presses;
}

static void walks(void)
{
    int from, to;
    for (from = 0; from < 5; ++from)
        for (to = 0; to < 5; ++to) {
            menu7 m = { PC_UI_DATA_PH_FILES, -1, from, -1, 0, 0 };
            pc_ui_data_goal g = goal(PC_UI_DATA_GOAL_FILE, to);
            int n = walk(&m, &g, 40);
            int d = from > to ? from - to : to - from;
            CHECK(m.file == to && m.confirmed == 1 && n == d + 1 && g.kind == 0, "FILE %d from %d: %d presses",
                to, from, n);
        }
    for (from = 0; from < 5; ++from)
        for (to = 0; to < 5; ++to) {
            menu7 m = { PC_UI_DATA_PH_FILES, -1, from, -1, 0, 0 };
            pc_ui_data_goal g = goal(PC_UI_DATA_GOAL_MOVE, to);
            int n = walk(&m, &g, 40);
            int d = from > to ? from - to : to - from;
            CHECK(m.file == to && !m.confirmed && !m.crossed && n == d && g.kind == 0,
                "MOVE %d from %d: %d presses", to, from, n);
        }
    {
        /* a real Down paged the list (slot 2, page 0 -> 1): the publish reads file 2 while MENU7 is on 3;
         * a FILE goal for 2 posted then waits the settling publish out, then walks back up */
        menu7 m = { PC_UI_DATA_PH_FILES, -1, 3, -1, 0, 0 };
        pc_ui_data_goal g = goal(PC_UI_DATA_GOAL_FILE, 2);
        pc_ui_data_snap stale = { PC_UI_DATA_PH_FILES, -1, 2, -1, 0, 1 };
        unsigned p;
        CHECK(pc_ui_data_plan(&stale, &g, &p) == PC_UI_DATA_PLAN_NONE && p == 0 && g.kind == PC_UI_DATA_GOAL_FILE,
            "settling: no Circle on the stale file");
        walk(&m, &g, 10);
        CHECK(m.file == 2 && m.confirmed == 1, "then the file posted, not the one a page on (%d)", m.file);
    }
    for (from = 1; from <= 2; ++from)
        for (to = 1; to <= 2; ++to) {
            menu7 m = { PC_UI_DATA_PH_CARD, from, -1, -1, 0, 0 };
            pc_ui_data_goal g = goal(PC_UI_DATA_GOAL_CARD, to);
            walk(&m, &g, 20);
            CHECK(m.card == to && m.confirmed == 1, "CARD %d from %d", to, from);
        }
    for (from = 0; from <= 1; ++from) {
        menu7 m = { PC_UI_DATA_PH_PROMPT, -1, -1, from, 0, 0 };
        pc_ui_data_goal g = goal(PC_UI_DATA_GOAL_ANSWER, 1);
        walk(&m, &g, 20);
        CHECK(m.cancelled == 0 && m.confirmed == 1 && !m.crossed, "Yes from %d", from);
        m.cancelled = from, m.confirmed = 0;
        g = goal(PC_UI_DATA_GOAL_ANSWER, 0);
        walk(&m, &g, 20);
        CHECK(m.crossed == 1 && !m.confirmed, "No from %d is Cross", from);
    }
    {
        /* expiry: a FILE goal whose cursor never comes (MENU7 refusing) */
        menu7 m = { PC_UI_DATA_PH_FILES, -1, -1, -1, 0, 0 };
        pc_ui_data_goal g = goal(PC_UI_DATA_GOAL_FILE, 3);
        walk(&m, &g, PC_UI_DATA_GOAL_AGE + 5);
        CHECK(g.kind == 0 && !m.confirmed, "a goal expires after %d frames", PC_UI_DATA_GOAL_AGE);
    }
    {
        /* BUSY waits (up to the life), then the target phase */
        pc_ui_data_goal g = goal(PC_UI_DATA_GOAL_CARD, 2);
        pc_ui_data_snap busy = { PC_UI_DATA_PH_BUSY, -1, -1, -1, 0, 0 };
        unsigned p;
        int f;
        g.automatic = 1;
        for (f = 0; f < 200; ++f)
            CHECK(pc_ui_data_plan(&busy, &g, &p) == PC_UI_DATA_PLAN_NONE, "busy: wait");
        CHECK(g.kind == PC_UI_DATA_GOAL_CARD, "kept through busy");
        {
            menu7 m = { PC_UI_DATA_PH_CARD, 1, -1, -1, 0, 0 };
            walk(&m, &g, 10);
            CHECK(m.card == 2 && m.confirmed == 1, "then confirmed on its card");
        }
        g = goal(PC_UI_DATA_GOAL_CARD, 1);
        g.automatic = 1;
        {
            pc_ui_data_snap wait = { PC_UI_DATA_PH_WAIT, -1, -1, -1, 0, 0 };
            CHECK(pc_ui_data_plan(&wait, &g, &p) == PC_UI_DATA_PLAN_DROPPED, "auto-advance stops at a message");
        }
        g = goal(PC_UI_DATA_GOAL_CARD, 1);
        for (f = 0; f < PC_UI_DATA_GOAL_LIFE + 2 && g.kind; ++f)
            pc_ui_data_plan(&busy, &g, &p);
        CHECK(g.kind == 0, "busy forever: the goal's life ends");
    }
    {
        /* CLOSE walks out (Cross per step), stops at a prompt */
        pc_ui_data_goal g = goal(PC_UI_DATA_GOAL_CLOSE, 0);
        menu7 m = { PC_UI_DATA_PH_FILES, -1, 2, -1, 0, 0 };
        unsigned p;
        walk(&m, &g, 2);
        CHECK(m.crossed == 1 && g.kind == PC_UI_DATA_GOAL_CLOSE, "CLOSE: Cross out of the list, kept");
        {
            pc_ui_data_snap prompt = { PC_UI_DATA_PH_PROMPT, -1, -1, 1, 0, 0 };
            int r;
            g.cool = 0;
            r = pc_ui_data_plan(&prompt, &g, &p);
            CHECK(r == PC_UI_DATA_PLAN_DROPPED && p == 0, "CLOSE stops at a prompt (never answered)");
        }
        g = goal(PC_UI_DATA_GOAL_TAB, 3);
        {
            pc_ui_data_snap ops = { PC_UI_DATA_PH_OPS, -1, -1, -1, 0, 0 };
            CHECK(pc_ui_data_plan(&ops, &g, &p) == PC_UI_DATA_PLAN_OPS && g.kind == PC_UI_DATA_GOAL_TAB,
                "TAB in the chooser: the list's");
        }
    }
    {
        int ph, k, n = 0;
        for (ph = 0; ph < PC_UI_DATA_PHASES; ++ph)
            for (k = 1; k < PC_UI_DATA_GOALS; ++k)
                n += pc_ui_data_accepts(ph, k);
        CHECK(!pc_ui_data_accepts(PC_UI_DATA_PH_BUSY, PC_UI_DATA_GOAL_BACK), "busy takes nothing");
        CHECK(n == 17, "%d phase / goal pairs taken", n);
    }
}

int main(void)
{
    decode_formulas();
    phases();
    exhaustive();
    walks();
    if (failures) {
        printf("data_plan_probe: %d failure(s)\n", failures);
        return 1;
    }
    printf("data_plan_probe: ok\n");
    return 0;
}
