/* Modern menus, the native Score screen's numbers (score_format.h). */
#include "score_format.h"
#include <stdio.h>
#include <string.h>

static void put(char* out, int size, const char* s)
{
    if (size > 0)
        snprintf(out, (size_t)size, "%s", s);
}

void pc_score_fastest_clear(unsigned rounds, unsigned minutes, char* out, int size)
{
    /* _printFastestClearTime(buf, int rounds, int totalSeconds): int arithmetic */
    int total = (int)minutes, r = (int)rounds;
    int h = total / 60, m;
    if (h >= 100)
        h = 99, m = 59;
    else
        m = total % 60;
    if (size > 0)
        snprintf(out, (size_t)size, "R%d %02d:%02d", r + 1, h, m);
}

int pc_score_percent_short(unsigned n, int total) { return (int)(n & 0xFFFF) * 100 / total; }

unsigned pc_score_percent_u32(unsigned n, unsigned total) { return (unsigned)(n * 100u) / total; }

int pc_score_record_text(int row, const pc_score_inputs* in, char* out, int size)
{
    char buf[32];
    if (size > 0)
        out[0] = 0;
    switch (row) {
    case 0:
        snprintf(buf, sizeof buf, "%d", (int)(in->clear_now & 0xFFFF));
        break;
    case 1:
        if ((in->clear_now & 0xFFFF) == 0)
            return 1;
        pc_score_fastest_clear(in->rounds, in->minutes, buf, sizeof buf);
        break;
    case 2:
        snprintf(buf, sizeof buf, "%d%%", pc_score_percent_short(in->chests_now, PC_SCORE_CHESTS));
        break;
    case 3:
        snprintf(buf, sizeof buf, "%u%%", pc_score_percent_u32(in->chests_best, PC_SCORE_CHESTS));
        break;
    case 4:
        snprintf(buf, sizeof buf, "%d%%", pc_score_percent_short(in->map_now, PC_SCORE_MAP));
        break;
    case 5:
        snprintf(buf, sizeof buf, "%u%%", pc_score_percent_u32(in->map_best, PC_SCORE_MAP));
        break;
    case 6:
        snprintf(buf, sizeof buf, "%d", (int)(in->max_chain & 0xFFFF));
        break;
    default:
        if (row < 0 || row > 22)
            return -1;
        if (row < 13) /* enemyKills[row - 7] */
            snprintf(buf, sizeof buf, "%d", (int)(in->kills[row - 7] & 0xFFFF));
        else /* weaponAttacks[row - 12], the 10th (row 22) is [0]: unarmed */
            snprintf(buf, sizeof buf, "%d", (int)(in->attacks[row - 12 == 10 ? 0 : row - 12] & 0xFFFF));
        break;
    }
    put(out, size, buf);
    return 0;
}

int pc_score_trial_time(unsigned time24, char* out, int size)
{
    int t = (int)(time24 & 0xFFFFFF);
    if (t == PC_SCORE_SENTINEL) {
        put(out, size, "--:--:--");
        return 0;
    }
    if (size > 0)
        snprintf(out, (size_t)size, "%02d:%02d:%02d", t >> 16, (t >> 8) & 0xFF, t & 0xFF);
    return 1;
}

void pc_score_reference_time(const char* raw, char* out, int size)
{
    if (!raw)
        raw = "";
    if (*raw == '$')
        ++raw;
    put(out, size, raw);
}

int pc_score_title_rows(unsigned mask, unsigned char unlocked[32])
{
    int i, n = 0;
    for (i = 0; i < 32; ++i) {
        unlocked[i] = (unsigned char)((mask >> i) & 1u);
        n += unlocked[i];
    }
    return n;
}

int pc_score_rank_titles(unsigned mask)
{
    int i, n = 0;
    for (i = 0; i < 16; ++i)
        n += (mask >> i) & 1u;
    return n;
}

void pc_score_grouped(unsigned v, char* out, int size)
{
    char digits[16], buf[24];
    int n = snprintf(digits, sizeof digits, "%u", v), i, o = 0;
    for (i = 0; i < n; ++i) {
        if (i && (n - i) % 3 == 0)
            buf[o++] = ',';
        buf[o++] = digits[i];
    }
    buf[o] = 0;
    put(out, size, buf);
}
