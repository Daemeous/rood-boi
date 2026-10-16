/* The native Score screen's numbers (pc/src/ui/score_format.c) against MENU9's
 * printers (src/MENU/MENU9.PRG/238.c): the fastest clear at 0, 59, 5999 and
 * 6000+ minutes (the 99:59 cap), the percentages at 51 / 52 and 360 / 361,
 * the time attack's packing and its 0x800000 sentinel, the title rows, the
 * Records rows' sources (unarmed last) and the score's grouping. */
#include "score_format.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            ++failures;                                                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                        \
            printf(__VA_ARGS__);                                                                               \
            printf("\n");                                                                                      \
        }                                                                                                      \
    } while (0)

static void clear_time(unsigned rounds, unsigned minutes, const char* want)
{
    char buf[32];
    pc_score_fastest_clear(rounds, minutes, buf, sizeof buf);
    CHECK(!strcmp(buf, want), "fastest clear (%u, %u): \"%s\", want \"%s\"", rounds, minutes, buf, want);
}

static void record(int row, const pc_score_inputs* in, const char* want, int none)
{
    char buf[32];
    int r = pc_score_record_text(row, in, buf, sizeof buf);
    CHECK(r == none && !strcmp(buf, want), "record %d: \"%s\" (%d), want \"%s\" (%d)", row, buf, r, want, none);
}

int main(void)
{
    char buf[32];
    clear_time(0, 0, "R1 00:00");
    clear_time(0, 59, "R1 00:59");
    clear_time(2, 60, "R3 01:00");
    clear_time(0, 5999, "R1 99:59");
    clear_time(0, 6000, "R1 99:59");
    clear_time(9, 123456, "R10 99:59");
    clear_time(0, 420, "R1 07:00");

    CHECK(pc_score_percent_short(51, PC_SCORE_CHESTS) == 98, "51/52");
    CHECK(pc_score_percent_short(52, PC_SCORE_CHESTS) == 100, "52/52");
    CHECK(pc_score_percent_short(360, PC_SCORE_MAP) == 99, "360/361");
    CHECK(pc_score_percent_short(361, PC_SCORE_MAP) == 100, "361/361");
    CHECK(pc_score_percent_u32(51, PC_SCORE_CHESTS) == 98 && pc_score_percent_u32(361, PC_SCORE_MAP) == 100,
        "the scoredata percentages");
    /* the original's u_int arithmetic wraps (n * 100 mod 2^32) */
    CHECK(pc_score_percent_u32(0x80000000u, PC_SCORE_CHESTS) == 0, "u32 wrap");

    CHECK(pc_score_trial_time(0x800000, buf, sizeof buf) == 0 && !strcmp(buf, "--:--:--"), "sentinel: %s", buf);
    CHECK(pc_score_trial_time(0x011E09, buf, sizeof buf) == 1 && !strcmp(buf, "01:30:09"), "packing: %s", buf);
    CHECK(pc_score_trial_time(0x000000, buf, sizeof buf) == 1 && !strcmp(buf, "00:00:00"), "zero: %s", buf);
    CHECK(pc_score_trial_time(0x7FFFFF, buf, sizeof buf) == 1 && !strcmp(buf, "127:255:255"), "max: %s", buf);
    pc_score_reference_time("$01:15:00", buf, sizeof buf);
    CHECK(!strcmp(buf, "01:15:00"), "reference: %s", buf);

    unsigned char unlocked[32];
    CHECK(pc_score_title_rows(0, unlocked) == 0, "no titles");
    CHECK(pc_score_title_rows(0xFFFFFFFFu, unlocked) == 32 && unlocked[31], "all titles");
    CHECK(pc_score_title_rows(0x80000005u, unlocked) == 3 && unlocked[0] && !unlocked[1] && unlocked[2] && unlocked[31],
        "some titles");
    CHECK(pc_score_rank_titles(0xFFFF0001u) == 1, "rank counts bits 0..15 only");

    pc_score_grouped(0, buf, sizeof buf);
    CHECK(!strcmp(buf, "0"), "group 0: %s", buf);
    pc_score_grouped(999, buf, sizeof buf);
    CHECK(!strcmp(buf, "999"), "group 999: %s", buf);
    pc_score_grouped(1000, buf, sizeof buf);
    CHECK(!strcmp(buf, "1,000"), "group 1000: %s", buf);
    pc_score_grouped(999999999, buf, sizeof buf);
    CHECK(!strcmp(buf, "999,999,999"), "group cap: %s", buf);

    pc_score_inputs in;
    memset(&in, 0, sizeof in);
    record(0, &in, "0", 0);
    record(1, &in, "", 1); /* no clear: noneClear */
    in.clear_now = 1, in.rounds = 1, in.minutes = 539;
    record(1, &in, "R2 08:59", 0);
    in.chests_now = 51, in.chests_best = 52, in.map_now = 360, in.map_best = 361, in.max_chain = 31;
    record(2, &in, "98%", 0);
    record(3, &in, "100%", 0);
    record(4, &in, "99%", 0);
    record(5, &in, "100%", 0);
    record(6, &in, "31", 0);
    for (int i = 0; i < 6; ++i)
        in.kills[i] = 100 + i;
    for (int i = 0; i < 10; ++i)
        in.attacks[i] = 200 + i;
    for (int i = 0; i < 6; ++i) {
        snprintf(buf, sizeof buf, "%d", 100 + i);
        record(7 + i, &in, buf, 0);
    }
    for (int i = 1; i < 10; ++i) {
        snprintf(buf, sizeof buf, "%d", 200 + i);
        record(12 + i, &in, buf, 0);
    }
    record(22, &in, "200", 0); /* unarmed: weaponAttacks[0] */
    CHECK(pc_score_record_text(23, &in, buf, sizeof buf) == -1, "row 23 out of range");
    printf("score_format_probe: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
