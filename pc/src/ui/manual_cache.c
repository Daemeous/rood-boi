/* Modern menus, the Quick manual: the page files' cache (host code: not in
 * the game's state sections, so a snapshot never holds its pointers; see
 * ui_manual.h). The files (SMALL/HELPnn.HF0 / .HF1) are read from the disc
 * image with pc_ui_disc_read the first time a topic shows, parsed
 * (manual_parse.c, with the game's character advances the bridge hands
 * over), their words decoded (pc_ui_text_decode), and kept in host memory
 * until exit; nothing is written anywhere. The shown topic's blinking
 * elements run here too (494.c _getAnimationState, from the file's state at
 * each showing, one step a tick). */
#include "item_names.h"
#include "manual_parse.h"
#include "ui_manual.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */

#define TOPICS PC_UI_MANUAL_TOPICS

static struct {
    int state; /* 0 not read, 1 parsed, -1 unreadable */
    pc_ui_manual_topic t;
    char* pool; /* the words' UTF-8, each 0-terminated */
    int* text;  /* per run: its offset in pool, -1 none */
    pc_ui_manual_anim* sprite_anim; /* the shown topic's animations (from the file at each showing) */
    pc_ui_manual_anim* rule_anim;
    unsigned char* sprite_hidden;
    unsigned char* rule_hidden;
} topics[TOPICS];
static char names[TOPICS][48], names_raw[TOPICS][48], descs[TOPICS][192];
static int names_ready;
static int shown = -1;     /* the topic whose animations run */
static int shown_mode;     /* the mode they started in */
static unsigned generation = 1; /* bumped by a reset: the drawing drops its textures */

/* MENUE's menuText (the bridge's compiled-in copy): 0-13 the topics, 14-27 their lines. */
void pc_ui_manual_cache_names(const unsigned short* menuText)
{
    int i;
    if (names_ready)
        return;
    for (i = 0; i < TOPICS; ++i) {
        char* p;
        int first = 1;
        pc_ui_text_decode((const unsigned char*)&menuText[menuText[i]], 96, names_raw[i], sizeof names_raw[i]);
        pc_ui_text_decode((const unsigned char*)&menuText[menuText[TOPICS + i]], 384, descs[i], sizeof descs[i]);
        /* "BASIC CONTROLS" -> "Basic controls" */
        memcpy(names[i], names_raw[i], sizeof names[i]);
        for (p = names[i]; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z' && !first)
                *p = (char)(*p - 'A' + 'a');
            else if (*p >= 'a' && *p <= 'z' && first)
                *p = (char)(*p - 'a' + 'A');
            if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
                first = 0;
        }
    }
    names_ready = 1;
}

void pc_ui_manual_cache_load(int i, const unsigned char* widths, unsigned nwidths)
{
    char path[32];
    unsigned char *hf0 = NULL, *hf1 = NULL;
    unsigned n0 = 0, n1 = 0, size = 0;
    int k;
    if (i < 0 || i >= TOPICS || topics[i].state)
        return;
    snprintf(path, sizeof path, "SMALL/HELP%02d.HF0", i + 1);
    if (!pc_ui_disc_read(path, &hf0, &n0)) {
        fprintf(stderr, "[ui] quick manual: cannot read %s\n", path);
        topics[i].state = -1;
        return;
    }
    snprintf(path, sizeof path, "SMALL/HELP%02d.HF1", i + 1);
    if (!pc_ui_disc_read(path, &hf1, &n1)) {
        fprintf(stderr, "[ui] quick manual: cannot read %s (no illustrations)\n", path);
        hf1 = NULL, n1 = 0;
    }
    if (!pc_ui_manual_parse(hf0, n0, hf1, n1, widths, nwidths, &topics[i].t)) {
        fprintf(stderr, "[ui] quick manual: SMALL/HELP%02d.HF0 is not a page file\n", i + 1);
        free(hf0);
        free(hf1);
        topics[i].state = -1;
        return;
    }
    /* the words' text, decoded while the file is here */
    topics[i].text = malloc(sizeof(int) * (size_t)(topics[i].t.nruns ? topics[i].t.nruns : 1));
    for (k = 0; k < topics[i].t.nruns; ++k)
        if (topics[i].t.runs[k].kind == PC_UI_MANUAL_WORD)
            size += topics[i].t.runs[k].len * 3u + 1u;
    topics[i].pool = malloc(size ? size : 1);
    {
        int ns = topics[i].t.nsprites, nr = topics[i].t.nrules;
        topics[i].sprite_anim = calloc((size_t)(ns ? ns : 1), sizeof(pc_ui_manual_anim));
        topics[i].rule_anim = calloc((size_t)(nr ? nr : 1), sizeof(pc_ui_manual_anim));
        topics[i].sprite_hidden = calloc((size_t)(ns ? ns : 1), 1);
        topics[i].rule_hidden = calloc((size_t)(nr ? nr : 1), 1);
    }
    if (!topics[i].text || !topics[i].pool || !topics[i].sprite_anim || !topics[i].rule_anim
        || !topics[i].sprite_hidden || !topics[i].rule_hidden) {
        /* out of memory: the parsed topic and whatever was allocated go too (state -1 is never reloaded) */
        pc_ui_manual_free(&topics[i].t);
        free(topics[i].text), topics[i].text = NULL;
        free(topics[i].pool), topics[i].pool = NULL;
        free(topics[i].sprite_anim), topics[i].sprite_anim = NULL;
        free(topics[i].rule_anim), topics[i].rule_anim = NULL;
        free(topics[i].sprite_hidden), topics[i].sprite_hidden = NULL;
        free(topics[i].rule_hidden), topics[i].rule_hidden = NULL;
        free(hf0);
        free(hf1);
        topics[i].state = -1;
        return;
    }
    size = 0;
    for (k = 0; k < topics[i].t.nruns; ++k) {
        const pc_ui_manual_run* r = &topics[i].t.runs[k];
        topics[i].text[k] = -1;
        if (r->kind != PC_UI_MANUAL_WORD)
            continue;
        topics[i].text[k] = (int)size;
        size += (unsigned)pc_ui_text_decode(hf0 + r->off, (int)r->len, topics[i].pool + size, (int)(r->len * 3 + 1)) + 1u;
    }
    free(hf0);
    free(hf1);
    topics[i].state = 1; /* published last: the model is complete and never changes again */
}

/* The shown topic's animations from the file (494.c: a page's file is read at each showing). */
static void anims_start(int i)
{
    int k;
    if (topics[i].state != 1)
        return;
    for (k = 0; k < topics[i].t.nsprites; ++k) {
        topics[i].sprite_anim[k] = topics[i].t.sprites[k].a;
        topics[i].sprite_hidden[k] = 0;
    }
    for (k = 0; k < topics[i].t.nrules; ++k) {
        topics[i].rule_anim[k] = topics[i].t.rules[k].a;
        topics[i].rule_hidden[k] = 0;
    }
}
static int anims_step(int i)
{
    int k, hidden = 0;
    if (topics[i].state != 1)
        return 0;
    for (k = 0; k < topics[i].t.nsprites; ++k)
        if (topics[i].sprite_anim[k].enabled)
            hidden += topics[i].sprite_hidden[k] = (unsigned char)pc_ui_manual_anim_step(&topics[i].sprite_anim[k]);
    for (k = 0; k < topics[i].t.nrules; ++k)
        if (topics[i].rule_anim[k].enabled)
            hidden += topics[i].rule_hidden[k] = (unsigned char)pc_ui_manual_anim_step(&topics[i].rule_anim[k]);
    return hidden;
}

/* The page's ten lines as a JSON array (words joined by one space; buttons as [triangle] ...). */
static void page_json(int i, int line, char* out, int size)
{
    static const char* const buttons[4] = {"[triangle]", "[square]", "[circle]", "[cross]"};
    static const char* const arrows[4] = {"\xe2\x86\x90", "\xe2\x86\x92", "\xe2\x86\x91", "\xe2\x86\x93"};
    int n = 0, l, k;
    const pc_ui_manual_topic* t = &topics[i].t;
#define PUT(s)                                                                                                  \
    do {                                                                                                     \
        const char* s_ = (s);                                                                                \
        while (*s_ && n < size - 8)                                                                          \
            out[n++] = *s_++;                                                                                \
    } while (0)
    out[n++] = '[';
    for (l = line; l < line + PC_UI_MANUAL_PAGE_LINES; ++l) {
        int first = 1;
        if (l > line)
            out[n++] = ',';
        out[n++] = '"';
        if (topics[i].state == 1 && l < t->lines)
            for (k = t->line_first[l]; k < t->line_first[l + 1]; ++k) {
                const pc_ui_manual_run* r = &t->runs[k];
                const char* s = r->kind == PC_UI_MANUAL_WORD ? topics[i].pool + topics[i].text[k]
                    : r->kind == PC_UI_MANUAL_GLYPH         ? buttons[r->glyph & 3]
                    : r->kind == PC_UI_MANUAL_ARROW         ? arrows[r->glyph & 3]
                                                            : "\xe2\x98\x85";
                if (!*s)
                    continue;
                if (!first)
                    PUT(" ");
                first = 0;
                for (; *s && n < size - 8; ++s) {
                    if (*s == '"' || *s == '\\')
                        out[n++] = '\\';
                    out[n++] = *s;
                }
            }
        out[n++] = '"';
        if (n >= size - 8)
            break;
    }
    out[n++] = ']';
    out[n] = 0;
#undef PUT
}


int pc_ui_manual_cache_state(int i) { return i >= 0 && i < TOPICS ? topics[i].state : -1; }
int pc_ui_manual_cache_line_count(int i) { return pc_ui_manual_cache_state(i) == 1 ? topics[i].t.line_count : 0; }

/* After the shell's tick: the shown topic's animations (restarted when the topic or mode changes) and its
 * text for the JSON. topic -1: nothing shows. */
void pc_ui_manual_cache_show(int topic, int mode, int line)
{
    static char lines[16384];
    int hidden;
    if (topic < 0 || topic >= TOPICS) {
        shown = -1;
        return;
    }
    if (topic != shown || mode != shown_mode) {
        shown = topic, shown_mode = mode;
        anims_start(shown);
    }
    hidden = anims_step(shown);
    page_json(topic, line, lines, sizeof lines);
    pc_ui_manual_set_text(names_raw[topic], descs[topic], topics[topic].state == 1 ? topics[topic].t.lines : 0, lines,
        topics[topic].state == 1 ? topics[topic].t.nsprites : 0, topics[topic].state == 1 ? topics[topic].t.nrules : 0,
        hidden, topics[topic].state == 1 ? topics[topic].t.unknown : 0);
}

/* A soft reset: nothing shows; the drawing rebuilds its textures. The files stay cached. */
void pc_ui_manual_cache_reset(void)
{
    shown = -1;
    ++generation;
}

/* ---- for the drawing (ui_manual_draw.cpp, through pc_ui_manual_source) ---- */
static const char* src_name(int i) { return names_ready && i >= 0 && i < TOPICS ? names[i] : ""; }
static const char* src_desc(int i) { return names_ready && i >= 0 && i < TOPICS ? descs[i] : ""; }
static const pc_ui_manual_topic* src_model(int i)
{
    return i >= 0 && i < TOPICS && topics[i].state == 1 ? &topics[i].t : NULL;
}
static const char* src_run_text(int i, int run)
{
    if (!src_model(i) || run < 0 || run >= topics[i].t.nruns || topics[i].text[run] < 0)
        return "";
    return topics[i].pool + topics[i].text[run];
}
static int src_hidden(int i, int rule, int k)
{
    if (!src_model(i) || i != shown)
        return 0;
    if (rule)
        return k >= 0 && k < topics[i].t.nrules ? topics[i].rule_hidden[k] : 0;
    return k >= 0 && k < topics[i].t.nsprites ? topics[i].sprite_hidden[k] : 0;
}
static unsigned src_generation(void) { return generation; }
static const pc_ui_manual_source source = { src_name, src_desc, src_model, src_run_text, src_hidden,
    src_generation };
const pc_ui_manual_source* pc_ui_manual_cache_source(void) { return &source; }
