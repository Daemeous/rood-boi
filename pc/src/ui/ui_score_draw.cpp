// Modern menus: the native Score screen's drawing (ui_score.h), the whole view while it shows (no tab
// strip: L1 / R1 are its sections). Top: the score and the Risk Breaker rank; under it the section strip
// (Titles, Records, Time Attack, Bestiary, each with its count); then the page: Home's summary card and
// the four sections, or a section's list and its detail card; the footer. Every rect, size and the lists'
// rows are theme data (theme.json "score", ui::ScoreTheme), per layout width.
#include "ui/ui_draw_internal.h"
#include "ui/ui_score.h"
#include "ui/score_format.h"
#include "ui/score_nav.h"
#include "button_prompts.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace uidraw {
namespace {

const char* const SECTION_LABEL[4] = {"Titles", "Records", "Time Attack", "Bestiary"};
const char* const SECTION_HELP[4] = {"The titles Ashley has earned and what each asks for.",
    "Clears, progress, chains, foes slain and weapon use.", "The boss rush: your best times and the times to beat.",
    "The foes Ashley has defeated, by class."};
// The Records page's groups (238.c _renderBossRushDetails' rows): caption, first row, rows
struct Group {
    const char* caption;
    int first, count;
};
const Group GROUPS[] = {{"Progress", 0, 6}, {"Combat", 6, 1}, {"Foes", 7, 6}, {"Weapons", 13, 10}};

struct Hits {
    std::string json = "[";
    void add(const char* kind, int index, float x0, float y0, float x1, float y1)
    {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s{\"kind\":\"%s\",\"index\":%d,\"x0\":%.1f,\"y0\":%.1f,\"x1\":%.1f,\"y1\":%.1f}",
            json.size() > 1 ? "," : "", kind, index, x0, y0, x1, y1);
        json += buf;
    }
};

struct Anim {
    float scroll[4] = {}; // each list's first shown line (rows; the Records: layout units)
    int frame = -10;
} anim;

void panel(const Frame& f, const ui::Theme& t, float x0, float y0, float x1, float y1)
{
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y1), t.panel);
    f.dl->AddRect(f.p(x0, y0), f.p(x1, y1), t.rule, 0.0f, f.s(1));
}

// A small count chip ("12/32") right-aligned at x, centred on cy; returns its width.
float chip(const Frame& f, const ui::Theme& t, float x, float cy, const char* s, unsigned col)
{
    float cs = t.score.chip_size, w = measure(f, fonts.data, cs, s) + cs;
    f.dl->AddRect(f.p(x - w, cy - cs * 0.85f), f.p(x, cy + cs * 0.85f), fade(col, 0.6f), f.s(10), 0, f.s(1));
    text(f, fonts.data, cs, x - w + cs * 0.5f, cy - cs * 0.65f, col, s);
    return w;
}

// A small triangle centred at (cx, cy) pointing up (dir -1) or down (1): more rows that way.
void arrow(const Frame& f, float cx, float cy, int dir, unsigned col)
{
    float h = 4.0f * dir;
    f.dl->AddTriangleFilled(f.p(cx - 6, cy - h), f.p(cx + 6, cy - h), f.p(cx, cy + h), col);
}

// The list's first row so `focus` shows with a row of margin (rows of equal height).
float follow(float scroll, int focus, int rows, int visible)
{
    if (rows <= visible)
        return 0;
    float lo = (float)focus - (float)visible + 2, hi = (float)focus - 1;
    if (scroll < lo)
        scroll = lo;
    if (scroll > hi)
        scroll = hi;
    float max = (float)(rows - visible);
    return scroll < 0 ? 0 : scroll > max ? max : scroll;
}

// A list of `rows` entries in [x0, x1] from y down to y1 (the content band's bottom) with the theme's rows;
// draws each through `row`. The rows shown are the theme's visible_rows, fewer if they would pass y1 (the
// down arrow included).
template <class Row>
void list(const Frame& f, const ui::Theme& t, int which, float x0, float x1, float y, float y1, int rows, int focus,
    Pointer& m, Hits& hits, bool enabled, Row row)
{
    const ui::ScoreTheme& st = t.score;
    int visible = (int)(st.visible_rows + 0.5f);
    float rh = st.row_h;
    int fit_rows = (int)std::floor((y1 - y - 18) / rh);
    if (visible > fit_rows)
        visible = fit_rows;
    if (visible < 1)
        visible = 1;
    anim.scroll[which] = follow(anim.scroll[which], focus, rows, visible);
    int first = (int)anim.scroll[which];
    for (int i = first; i < rows && i < first + visible; ++i) {
        float ry = y + (i - first) * rh;
        bool over = enabled && m.over(x0, ry, x1, ry + rh);
        if (i == focus)
            focus_band(f, t, x0, ry, x1, ry + rh);
        else if (over)
            f.dl->AddRectFilled(f.p(x0, ry), f.p(x1, ry + rh), fade(t.accent_soft, 0.5f));
        if (enabled) {
            m.hit(PC_UI_MOUSE_SCORE_ROW, i, x0, ry, x1, ry + rh);
            hits.add("row", i, x0, ry, x1, ry + rh);
        }
        row(i, ry, rh, i == focus);
    }
    // more above / below
    if (first > 0)
        arrow(f, x1 - 14, y - 9, -1, t.ash);
    if (first + visible < rows)
        arrow(f, x1 - 14, y + visible * rh + 9, 1, t.ash);
}

void header(const Frame& f, const ui::Theme& t, const ui::ScoreLayout& L, const pc_ui_score& s)
{
    const ui::ScoreTheme& st = t.score;
    float y0 = L.header[0], y1 = L.header[1], xl = t.margin_x, xr = f.W - t.margin_x;
    char num[24];
    pc_score_grouped(s.valid ? s.score : 0, num, sizeof num);
    label(f, t, xl, y0 + 4, "Score");
    text(f, fonts.data, st.score_size, xl, y1 - st.score_size - 8, t.bone, num);
    text_right(f, fonts.label, t.label_size, xr, y0 + 4, t.ash, "RISK BREAKER RANK", 0.26f);
    // the game's "Lv.N Title" (its "Lv." a glyph the menus' fonts do not carry: spelled out)
    std::string rank_text = s.valid ? s.rank_text : "";
    if (!rank_text.empty() && rank_text[0] >= '0' && rank_text[0] <= '9')
        rank_text = "Lv." + rank_text;
    std::string rank = fit(f, fonts.name, st.rank_size, rank_text.c_str(), (xr - xl) * 0.55f);
    text_right(f, fonts.name, st.rank_size, xr, y1 - st.rank_size - 10, t.gold, rank.c_str());
    f.dl->AddLine(f.p(xl, y1), f.p(xr, y1), t.rule, f.s(1));
}

// The strip: each section with its count; the shown one lit (Home: the focused one, underlined only).
void strip(const Frame& f, const ui::Theme& t, const ui::ScoreLayout& L, const pc_ui_score& s,
    const pc_ui_score_view& v, Pointer& m, Hits& hits)
{
    float y0 = L.strip[0], y1 = L.strip[1], x = t.margin_x, cy = (y0 + y1) * 0.5f;
    unsigned char unlocked[32];
    int titles = pc_score_title_rows(s.titles, unlocked), trials = 0;
    for (int i = 0; i < PC_UI_SCORE_N_TRIALS; ++i)
        trials += s.trial_has[i] != 0;
    char counts[4][16];
    std::snprintf(counts[0], sizeof counts[0], "%d/32", titles);
    std::snprintf(counts[1], sizeof counts[1], "23");
    std::snprintf(counts[2], sizeof counts[2], "%d/8", trials);
    if (s.mon_ok)
        std::snprintf(counts[3], sizeof counts[3], "%d/78", s.unlocked);
    else
        std::snprintf(counts[3], sizeof counts[3], "-");
    for (int i = 0; i < 4; ++i) {
        std::string name = upper(SECTION_LABEL[i]);
        float tw = measure(f, fonts.label, t.tab_size, name.c_str(), 0.3f);
        float cw = measure(f, fonts.data, 12, counts[i]) + 12;
        float w = tw + 10 + cw + 28;
        bool lit = i == v.section, in = v.mode != PC_UI_SCORE_HOME;
        bool over = m.over(x, y0, x + w, y1);
        unsigned col = lit && in ? t.bone : over || lit ? t.ash : t.dim;
        text(f, fonts.label, t.tab_size, x, cy - t.tab_size * 0.6f, col, name.c_str(), 0.3f);
        chip(f, t, x + tw + 10 + cw, cy, counts[i], col);
        if (lit)
            f.dl->AddRectFilled(f.p(x, y1 - 3), f.p(x + tw, y1 - 1), in ? t.accent : fade(t.accent, 0.5f));
        m.hit(PC_UI_MOUSE_SCORE_SECTION, i, x, y0, x + w, y1);
        hits.add("section", i, x, y0, x + w, y1);
        x += w;
    }
    f.dl->AddLine(f.p(t.margin_x, y1), f.p(f.W - t.margin_x, y1), t.rule, f.s(1));
}

// A detail card's title, a rule, the body; returns the y after the body.
float card_head(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const char* title, const char* body,
    unsigned title_col)
{
    const ui::ScoreTheme& st = t.score;
    std::string ti = fit(f, fonts.name, st.title_size, title, x1 - x0);
    text(f, fonts.name, st.title_size, x0, y, title_col, ti.c_str());
    float by = y + st.head_rule;
    f.dl->AddLine(f.p(x0, by), f.p(x1, by), t.rule, f.s(1));
    float h = paragraph(f, t, x0, by + st.body_gap, x1 - x0, t.ash, body);
    return by + st.body_gap + h;
}

void draw_home(const Frame& f, const ui::Theme& t, const ui::ScoreLayout& L, const pc_ui_score& s,
    const pc_ui_score_view& v, Pointer& m, Hits& hits)
{
    const ui::ScoreTheme& st = t.score;
    float y0 = L.content[0], y1 = L.content[1];
    // the summary card
    float cx0 = L.home_card[0], cx1 = L.home_card[1];
    panel(f, t, cx0, y0, cx1, y1);
    m.hit(PC_UI_MOUSE_SCORE_PANEL, 0, cx0, y0, cx1, y1);
    float x0 = cx0 + st.pad, x1 = cx1 - st.pad, y = y0 + st.label_top + 2;
    label(f, t, x0, y, "Journey");
    y += st.line_gap;
    unsigned char unlocked[32];
    char a[48], b[48];
    struct Line {
        const char* name;
        std::string value, best;
    } lines[5];
    std::snprintf(a, sizeof a, "%u", s.clears);
    lines[0] = {"Clears", a, ""};
    std::snprintf(a, sizeof a, "%d%%", pc_score_percent_short(s.map_now, PC_SCORE_MAP));
    std::snprintf(b, sizeof b, "best %u%%", pc_score_percent_u32(s.map_best, PC_SCORE_MAP));
    lines[1] = {"Map", a, b};
    std::snprintf(a, sizeof a, "%d%%", pc_score_percent_short(s.chests_now, PC_SCORE_CHESTS));
    std::snprintf(b, sizeof b, "best %u%%", pc_score_percent_u32(s.chests_best, PC_SCORE_CHESTS));
    lines[2] = {"Chests", a, b};
    std::snprintf(a, sizeof a, "%u", s.max_chain);
    lines[3] = {"Max chain", a, ""};
    std::snprintf(a, sizeof a, "%d/32", pc_score_title_rows(s.titles, unlocked));
    lines[4] = {"Titles", a, ""};
    float lh = st.row_h;
    for (const Line& l : lines) {
        if (y + lh > y1 - 8)
            break;
        text(f, fonts.name, st.name_size, x0, y + (lh - st.name_size) * 0.5f - 2, t.ash, l.name);
        float bw = l.best.empty() ? 0
                                  : text_right(f, fonts.data, st.help_size, x1, y + (lh - st.help_size) * 0.5f, t.ash,
                                        l.best.c_str()) + st.inset;
        text_right(f, fonts.data, st.value_size, x1 - bw, y + (lh - st.value_size) * 0.5f - 1, t.bone,
            l.value.c_str());
        f.dl->AddLine(f.p(x0, y + lh), f.p(x1, y + lh), t.rule, f.s(1));
        y += lh;
    }
    // the sections
    float lx0 = L.home_list[0], lx1 = L.home_list[1], ly = y0;
    for (int i = 0; i < 4; ++i) {
        float rh = lh * 1.6f;
        bool over = m.over(lx0, ly, lx1, ly + rh);
        if (i == v.home_row)
            focus_band(f, t, lx0, ly, lx1, ly + rh);
        else if (over)
            f.dl->AddRectFilled(f.p(lx0, ly), f.p(lx1, ly + rh), fade(t.accent_soft, 0.5f));
        m.hit(PC_UI_MOUSE_SCORE_SECTION, i, lx0, ly, lx1, ly + rh);
        hits.add("home", i, lx0, ly, lx1, ly + rh);
        // on the bare ground (no panel): the help in ash, readable at any menu opacity
        text(f, fonts.name, st.section_size, lx0 + st.inset, ly + 8, i == v.home_row ? t.bone : t.ash, SECTION_LABEL[i]);
        std::string help = fit(f, fonts.name, st.help_size, SECTION_HELP[i], lx1 - lx0 - 2 * st.inset);
        text(f, fonts.name, st.help_size, lx0 + st.inset, ly + 8 + st.section_size + 7, t.ash, help.c_str());
        ly += rh;
    }
}

void draw_titles(const Frame& f, const ui::Theme& t, const ui::ScoreLayout& L, const pc_ui_score& s,
    const pc_ui_score_view& v, Pointer& m, Hits& hits)
{
    const ui::ScoreTheme& st = t.score;
    float y0 = L.content[0], y1 = L.content[1];
    float lx0 = L.titles_list[0], lx1 = L.titles_list[1];
    list(f, t, 0, lx0, lx1, y0, y1, PC_UI_SCORE_N_TITLES, v.focus, m, hits, true, [&](int i, float ry, float rh, bool focus) {
        bool on = (s.titles >> i) & 1u;
        float a = on ? 1.0f : st.locked_alpha;
        char no[16];
        std::snprintf(no, sizeof no, "%02d", i + 1);
        text(f, fonts.data, st.number_size, lx0 + st.inset, ry + (rh - st.number_size) * 0.5f, fade(t.ash, a), no);
        const char* name = pc_ui_score_title_text(&s, i, 0);
        std::string nm = fit(f, fonts.name, st.name_size, name, lx1 - lx0 - st.name_x - st.inset);
        text(f, fonts.name, st.name_size, lx0 + st.name_x, ry + (rh - st.name_size) * 0.5f - 2,
            fade(focus ? t.bone : t.ash, a), nm.c_str());
    });
    float dx0 = L.titles_detail[0], dx1 = L.titles_detail[1];
    panel(f, t, dx0, y0, dx1, y1);
    m.hit(PC_UI_MOUSE_SCORE_PANEL, 0, dx0, y0, dx1, y1);
    int i = v.focus;
    bool on = (s.titles >> i) & 1u;
    char no[24];
    std::snprintf(no, sizeof no, "TITLE %02d / 32", i + 1);
    label(f, t, dx0 + st.pad, y0 + st.label_top, no);
    card_head(f, t, dx0 + st.pad, dx1 - st.pad, y0 + st.card_top, pc_ui_score_title_text(&s, i, 0),
        pc_ui_score_title_text(&s, i, 1), on ? t.bone : fade(t.ash, st.locked_alpha));
}

void draw_records(const Frame& f, const ui::Theme& t, const ui::ScoreLayout& L, const pc_ui_score& s,
    const pc_ui_score_view& v, Pointer& m, Hits& hits)
{
    const ui::ScoreTheme& st = t.score;
    float y0 = L.content[0], y1 = L.content[1];
    float lx0 = L.records_table[0], lx1 = L.records_table[1];
    float rh = st.row_h * 0.82f, ch = st.row_h * 0.7f;
    // the lines' positions (captions between the groups), then a scroll that keeps the focus in view
    float at[PC_UI_SCORE_N_STATS], cap[4], total = 0;
    for (int g = 0; g < 4; ++g) {
        cap[g] = total;
        total += ch;
        for (int k = 0; k < GROUPS[g].count; ++k)
            at[GROUPS[g].first + k] = total, total += rh;
    }
    float view = st.visible_rows * st.row_h;
    if (view > y1 - y0)
        view = y1 - y0;
    float& sc = anim.scroll[1];
    float fy = at[v.focus];
    if (fy - rh < sc)
        sc = fy - rh - (v.focus == GROUPS[0].first ? ch : 0);
    if (fy + 2 * rh > sc + view)
        sc = fy + 2 * rh - view;
    if (sc > total - view)
        sc = total - view;
    if (sc < 0)
        sc = 0;
    f.dl->PushClipRect(f.p(lx0, y0), f.p(lx1, y0 + view), true);
    for (int g = 0; g < 4; ++g) {
        float cy = y0 + cap[g] - sc;
        label(f, t, lx0 + st.inset, cy + ch - 18, GROUPS[g].caption, t.dim);
        for (int k = 0; k < GROUPS[g].count; ++k) {
            int i = GROUPS[g].first + k;
            float ry = y0 + at[i] - sc;
            if (ry + rh < y0 || ry > y0 + view)
                continue;
            bool over = m.over(lx0, ry, lx1, ry + rh) && ry >= y0 && ry + rh <= y0 + view;
            if (i == v.focus)
                focus_band(f, t, lx0, ry, lx1, ry + rh);
            else if (over)
                f.dl->AddRectFilled(f.p(lx0, ry), f.p(lx1, ry + rh), fade(t.accent_soft, 0.5f));
            if (ry >= y0 - 1 && ry + rh <= y0 + view + 1) {
                m.hit(PC_UI_MOUSE_SCORE_ROW, i, lx0, ry, lx1, ry + rh);
                hits.add("row", i, lx0, ry, lx1, ry + rh);
            }
            std::string nm = fit(f, fonts.name, st.record_size, s.stat_name[i], (lx1 - lx0) * 0.6f);
            text(f, fonts.name, st.record_size, lx0 + st.inset, ry + (rh - st.record_size) * 0.5f - 2,
                i == v.focus ? t.bone : t.ash, nm.c_str());
            const char* value = s.stat_none[i] ? s.misc[0] : s.stat_value[i];
            text_right(f, fonts.data, st.record_value_size, lx1 - st.inset, ry + (rh - st.record_value_size) * 0.5f,
                s.stat_none[i] ? t.dim : t.bone, value);
        }
    }
    f.dl->PopClipRect();
    // more above / below (as list()'s)
    if (sc > 0.5f)
        arrow(f, lx1 - 14, y0 - 9, -1, t.ash);
    if (sc + view < total - 0.5f)
        arrow(f, lx1 - 14, y0 + view + 9, 1, t.ash);
    float dx0 = L.records_detail[0], dx1 = L.records_detail[1];
    panel(f, t, dx0, y0, dx1, y1);
    m.hit(PC_UI_MOUSE_SCORE_PANEL, 0, dx0, y0, dx1, y1);
    int i = v.focus;
    float y = card_head(f, t, dx0 + st.pad, dx1 - st.pad, y0 + st.card_top, s.stat_name[i], s.stat_desc[i], t.bone);
    const char* value = s.stat_none[i] ? s.misc[0] : s.stat_value[i];
    text(f, fonts.data, st.big_value_size, dx0 + st.pad, y + 18, s.stat_none[i] ? t.dim : t.bone, value);
    for (int g = 0; g < 4; ++g)
        if (i >= GROUPS[g].first && i < GROUPS[g].first + GROUPS[g].count)
            label(f, t, dx0 + st.pad, y0 + st.label_top, GROUPS[g].caption);
}

void draw_trials(const Frame& f, const ui::Theme& t, const ui::ScoreLayout& L, const pc_ui_score& s,
    const pc_ui_score_view& v, Pointer& m, Hits& hits)
{
    const ui::ScoreTheme& st = t.score;
    float y0 = L.content[0], y1 = L.content[1];
    float lx0 = L.trials_list[0], lx1 = L.trials_list[1];
    list(f, t, 2, lx0, lx1, y0, y1, PC_UI_SCORE_N_TRIALS, v.focus, m, hits, true, [&](int i, float ry, float rh, bool focus) {
        bool on = s.trial_has[i] != 0;
        float a = on ? 1.0f : st.locked_alpha;
        const char* name = on ? s.trial_name[i] : s.misc[4];
        std::string nm = fit(f, fonts.name, st.name_size, name, lx1 - lx0 - 140);
        text(f, fonts.name, st.name_size, lx0 + st.inset, ry + (rh - st.name_size) * 0.5f - 2,
            fade(focus ? t.bone : t.ash, a), nm.c_str());
        if (on)
            text_right(f, fonts.data, st.number_size, lx1 - st.inset, ry + (rh - st.number_size) * 0.5f, t.ash,
                s.trial_time[i][0]);
    });
    float dx0 = L.trials_panel[0], dx1 = L.trials_panel[1];
    panel(f, t, dx0, y0, dx1, y1);
    m.hit(PC_UI_MOUSE_SCORE_PANEL, 0, dx0, y0, dx1, y1);
    int i = v.focus;
    bool on = s.trial_has[i] != 0;
    float x0 = dx0 + st.pad, x1 = dx1 - st.pad;
    float y = card_head(f, t, x0, x1, y0 + st.card_top, on ? s.trial_name[i] : s.misc[4], on ? s.trial_desc[i] : s.misc[5],
        on ? t.bone : fade(t.ash, st.locked_alpha));
    char bossno[24];
    std::snprintf(bossno, sizeof bossno, "BOSS %d / 8", i + 1);
    label(f, t, x0, y0 + st.label_top, bossno);
    y += st.pad;
    label(f, t, x0, y, "Player time");
    y += st.line_gap - 4;
    static const char* const PLACES[3] = {"1st", "2nd", "3rd"};
    for (int k = 0; k < 3; ++k) {
        bool has = s.trial_round[i][k] > 0;
        text(f, fonts.label, st.number_size, x0, y + 4, t.dim, upper(PLACES[k]).c_str(), 0.2f);
        text(f, fonts.data, st.time_size, x0 + st.name_x + 8, y, has ? t.bone : t.dim, s.trial_time[i][k]);
        if (has) {
            char round[24];
            std::snprintf(round, sizeof round, "Round %d", s.trial_round[i][k]);
            text_right(f, fonts.name, st.note_size, x1, y + 3, t.ash, round);
        }
        y += st.place_gap;
    }
    y += 12;
    f.dl->AddLine(f.p(x0, y), f.p(x1, y), t.rule, f.s(1));
    y += st.body_gap;
    label(f, t, x0, y, "Reference time");
    text_right(f, fonts.data, st.time_size, x1, y - 4, on ? t.gold : t.dim, on ? s.trial_ref[i] : "--:--:--");
}

void draw_bestiary(const Frame& f, const ui::Theme& t, const ui::ScoreLayout& L, const pc_ui_score& s,
    const pc_ui_score_view& v, Pointer& m, Hits& hits, ScreenOut& out)
{
    const ui::ScoreTheme& st = t.score;
    float y0 = L.content[0], y1 = L.content[1];
    float lx0 = L.bestiary_list[0], lx1 = L.bestiary_list[1];
    bool ok = s.mon_ok != 0;
    list(f, t, 3, lx0, lx1, y0, y1, PC_MON_ENEMIES, v.focus, m, hits, ok, [&](int i, float ry, float rh, bool focus) {
        bool on = ok && s.mon[i].unlocked;
        float a = on ? 1.0f : st.locked_alpha;
        char no[16];
        std::snprintf(no, sizeof no, "%03d", i + 1);
        text(f, fonts.data, st.number_size, lx0 + st.inset, ry + (rh - st.number_size) * 0.5f, fade(t.ash, a), no);
        const char* name = on ? s.mon[i].name : s.misc[3];
        float cw = 0;
        if (on && s.mon[i].cls >= 0 && s.mon[i].cls < PC_UI_CLASSES)
            cw = chip(f, t, lx1 - st.inset, ry + rh * 0.5f, CLASSES[s.mon[i].cls], t.ash) + 10;
        std::string nm = fit(f, fonts.name, st.name_size, name, lx1 - lx0 - st.name_x3 - st.inset - cw - st.inset);
        text(f, fonts.name, st.name_size, lx0 + st.name_x3, ry + (rh - st.name_size) * 0.5f - 2,
            fade(focus ? t.bone : t.ash, a), nm.c_str());
    });
    float dx0 = L.bestiary_detail[0], dx1 = L.bestiary_detail[1];
    panel(f, t, dx0, y0, dx1, y1);
    m.hit(PC_UI_MOUSE_SCORE_PANEL, 0, dx0, y0, dx1, y1);
    float x0 = dx0 + st.pad, x1 = dx1 - st.pad;
    if (!ok) {
        label(f, t, x0, y0 + st.label_top, "Bestiary");
        card_head(f, t, x0, x1, y0 + st.card_top, "Not available", s.mon_why[0] ? s.mon_why : "MON.BIN could not be read.",
            t.dim);
        return;
    }
    int i = v.focus;
    bool on = s.mon[i].unlocked != 0;
    char no[24];
    std::snprintf(no, sizeof no, "No. %03d/078", i + 1);
    label(f, t, x0, y0 + st.label_top, no);
    int cls = s.mon[i].cls;
    float y = card_head(f, t, x0, x1, y0 + st.card_top, on ? s.mon[i].name : s.misc[3], on ? s.mon[i].desc : s.misc[2],
        on ? t.bone : fade(t.ash, st.locked_alpha));
    if (on && cls >= 0 && cls < PC_UI_CLASSES) {
        y += st.pad;
        text(f, fonts.name, st.class_size, x0, y, t.ash, CLASSES[cls]);
        char line[64];
        std::snprintf(line, sizeof line, "%s defeated: %u", CLASSES[cls], s.kills[cls]);
        y += st.line_gap;
        text(f, fonts.name, st.note_size, x0, y, t.ash, line);
    }
    // the viewer is the original's (pass 1): Square opens MENU9 on its top menu, the Encyclopaedia row selected
    if (v.hint && on) {
        float hy = y1 - st.hint_y;
        float gw = glyph(f, PC_PROMPT_SQUARE, st.glyph_size, x0, hy + st.glyph_size * 0.5f);
        text(f, fonts.name, st.hint_size, x0 + gw + 10, hy, t.gold, "Original screen (Encyclopaedia)");
    }
    out.hint(PC_PROMPT_L2, PC_PROMPT_R2, "Page", PC_UI_SUB_NEXT);
    if (on && !v.hint)
        out.hint(PC_PROMPT_CIRCLE, -1, "Viewer", PC_UI_CONFIRM);
}

} // namespace

void draw_score(const Frame& f, const ui::Theme& t, float dt, Pointer& m, ScreenOut& out)
{
    (void)dt;
    pc_ui_score_view v;
    pc_ui_score_view_get(&v);
    const pc_ui_score& s = *pc_ui_score_model();
    const ui::ScoreLayout& L = t.score.layout(f.W);
    int frame = ImGui::GetFrameCount();
    if (anim.frame != frame - 1) // a first frame: the lists at their focus
        for (float& sc : anim.scroll)
            sc = 0;
    anim.frame = frame;
    Hits hits;
    header(f, t, L, s);
    strip(f, t, L, s, v, m, hits);
    out.crumb = "System > Score";
    if (!s.valid) {
        spinner(f, f.W * 0.5f, (L.content[0] + L.content[1]) * 0.5f, 10, t.gold);
    } else if (v.mode == PC_UI_SCORE_HOME) {
        draw_home(f, t, L, s, v, m, hits);
    } else {
        out.crumb += std::string(" > ") + SECTION_LABEL[v.section];
        out.hint(PC_PROMPT_L1, PC_PROMPT_R1, "Section", PC_UI_TAB_NEXT);
        switch (v.mode) {
        case PC_UI_SCORE_TITLES:
            draw_titles(f, t, L, s, v, m, hits);
            break;
        case PC_UI_SCORE_RECORDS:
            draw_records(f, t, L, s, v, m, hits);
            break;
        case PC_UI_SCORE_TRIALS:
            draw_trials(f, t, L, s, v, m, hits);
            break;
        default:
            draw_bestiary(f, t, L, s, v, m, hits, out);
            break;
        }
    }
    hits.json += "]";
    pc_ui_score_hits(hits.json.c_str());
    if (v.mode == PC_UI_SCORE_HOME)
        out.hint(PC_PROMPT_CIRCLE, -1, "Open", PC_UI_CONFIRM);
    out.hint(PC_PROMPT_SQUARE, -1, "Original screen", PC_UI_ALT);
    out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
    out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
}

} // namespace uidraw
