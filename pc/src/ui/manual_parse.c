/* Modern menus, the Quick manual's page files and the original's rules
 * (manual_parse.h). */
#include "manual_parse.h"
#include <stdlib.h>
#include <string.h>

/* ---- the original's rules ---- */

int pc_ui_manual_line_count(int n)
{
    int count = n, rem = n % 10;
    if (rem > 0)
        count += 10 - rem;
    count -= 10;
    return count < 0 ? 0 : count;
}

void pc_ui_manual_seed(int row, int* cm4, int* cm5)
{
    const int rows = PC_UI_MANUAL_TOPICS;
    if (rows < 9 || row < 8) {
        *cm4 = row, *cm5 = 0;
    } else if (row >= rows - 8) {
        *cm4 = row - (rows - 9), *cm5 = rows - 9;
    } else {
        *cm4 = 4, *cm5 = row - 4;
    }
}

/* 58EC.c vs_mainMenu_renderItemsList case 17 with rowCount 14, rowInfo 1: the window shows rows 0..8,
 * its scroll goes to 14 + 1 - 10 = 5; inside it the cursor keeps a row of margin. */
int pc_ui_manual_list_step(int* row, int* page, int up_rep, int up_press, int down_rep, int down_press)
{
    const int count = PC_UI_MANUAL_TOPICS, info = 1, last_page = count + info - 10;
    int before = *row + *page;
    if (up_rep) {
        if (*page == 0) {
            if (*row == 0) {
                if (up_press)
                    *row = 9 - info, *page = last_page;
            } else {
                --*row;
            }
        } else if (*row == 1) {
            --*page;
        } else {
            --*row;
        }
    }
    if (down_rep) {
        if (*page == last_page) {
            if (*row == 9 - info) {
                if (down_press)
                    *row = 0, *page = 0;
            } else {
                ++*row;
            }
        } else if (*row == 8 - info) {
            ++*page;
        } else {
            ++*row;
        }
    }
    return *row + *page != before;
}

/* 494.c showSubMenu: the scroll step of one frame (lines, or pages while Square is held). */
int pc_ui_manual_step(int* scroll, int line_count, int square_held, int start, int up_rep, int up_press,
    int down_rep, int down_press)
{
    int page = *scroll / 10, last_page = line_count / 10;
    if (start) {
        *scroll = 0;
        return 0;
    }
    if (square_held) {
        if (up_press && page == 0 && last_page > 0) {
            *scroll = last_page * 10;
            return 1;
        }
        if (down_press && page == last_page && last_page > 0) {
            *scroll = 0;
            return 1;
        }
        if (up_rep && page > 0) {
            *scroll = (page - 1) * 10;
            return 1;
        }
        if (down_rep && page < last_page) {
            *scroll = (page + 1) * 10;
            return 1;
        }
        return 0;
    }
    if (up_press && *scroll == 0 && line_count > 0) {
        *scroll = line_count;
        return 1;
    }
    if (down_press && *scroll == line_count && *scroll > 0) {
        *scroll = 0;
        return 1;
    }
    if (up_rep && *scroll > 0) {
        --*scroll;
        return 1;
    }
    if (down_rep && *scroll < line_count) {
        ++*scroll;
        return 1;
    }
    return 0;
}

int pc_ui_manual_anim_step(pc_ui_manual_anim* a)
{
    int next;
    if (a->cur >= 16)
        return a->active != 0;
    next = ((unsigned short)a->mask >> a->cur) & 1;
    if (++a->timer >= a->duration) {
        a->timer = 0;
        if (++a->cur >= 16 && a->repeat != 0)
            a->cur = 0;
    }
    a->active = (short)(next ^ 1);
    return a->active != 0;
}

/* ---- the files ---- */

static unsigned u16at(const unsigned char* p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static int s16at(const unsigned char* p) { return (short)u16at(p); }
static unsigned u32at(const unsigned char* p) { return u16at(p) | u16at(p + 2) << 16; }

void pc_ui_manual_free(pc_ui_manual_topic* t)
{
    int i;
    if (t->rgba)
        for (i = 0; i < t->ntex; ++i)
            free(t->rgba[i]);
    free(t->rgba);
    free(t->tex_w);
    free(t->tex_h);
    free(t->tex_art);
    free(t->line_first);
    free(t->runs);
    free(t->sprites);
    free(t->rules);
    memset(t, 0, sizeof *t);
}

/* One line's runs (text_lines.c's walk: x from the widths, 0x8F a 6-pixel space, FA n an x advance, FB a
 * colour or font table); a word is a span of charset bytes. */
typedef struct {
    pc_ui_manual_run* runs;
    int n, cap;
} runs_t;

static int push_run(runs_t* r, pc_ui_manual_run run)
{
    if (r->n == r->cap) {
        int cap = r->cap ? r->cap * 2 : 256;
        pc_ui_manual_run* p = realloc(r->runs, (size_t)cap * sizeof *p);
        if (!p)
            return 0;
        r->runs = p, r->cap = cap;
    }
    r->runs[r->n++] = run;
    return 1;
}

static int line_runs(const unsigned char* buf, unsigned size, unsigned at, const unsigned char* widths,
    unsigned nwidths, runs_t* r, int* unknown)
{
    int x = 0, colour = 0, table = 0, word = -1, word_x = 0, word_gap = 0;
    int last_end = 0, start = 1; /* where the line's last run ended (none yet: start) */
    unsigned i = at;
    pc_ui_manual_run run;
#define WIDTH(c) ((unsigned)(c) + (unsigned)table * 189u < nwidths ? widths[(c) + table * 189] : 0)
/* How a run starting at x0 joins the one before it on its line (PC_UI_MANUAL_GAP_*), by the original's
 * pixels between them (a space is 6, an FA advance its operand). */
#define GAP(x0)                                                                                                 \
    (start                               ? PC_UI_MANUAL_GAP_APART                                            \
        : (x0) - last_end <= 0              ? PC_UI_MANUAL_GAP_NONE                                          \
        : (x0) - last_end <= PC_UI_MANUAL_JOIN_PX ? PC_UI_MANUAL_GAP_SPACE                                   \
                                                  : PC_UI_MANUAL_GAP_APART)
#define END_WORD()                                                                                              \
    do {                                                                                                     \
        if (word >= 0) {                                                                                     \
            memset(&run, 0, sizeof run);                                                                     \
            run.kind = PC_UI_MANUAL_WORD, run.colour = (unsigned char)colour;                                \
            run.gap = (unsigned char)word_gap;                                                               \
            run.x = (unsigned short)(word_x < 0 ? 0 : word_x), run.w = (unsigned short)(x - word_x);        \
            run.off = (unsigned)word, run.len = i - (unsigned)word;                                          \
            if (!push_run(r, run))                                                                           \
                return 0;                                                                                    \
            word = -1, last_end = x, start = 0;                                                              \
        }                                                                                                    \
    } while (0)
    for (;;) {
        unsigned c;
        if (i >= size) { /* no terminator: the line ends at the buffer */
            ++*unknown;
            END_WORD();
            return 1;
        }
        c = buf[i];
        if (c == 0xE7) {
            END_WORD();
            return 1;
        }
        if (c >= 0xEC) { /* a control code and its operand (MENUE's _vsStrcpy copies both) */
            int op;
            END_WORD();
            if (i + 1 >= size) {
                ++*unknown;
                return 1;
            }
            op = buf[i + 1];
            i += 2;
            if (c == 0xFA) {
                x += op >= 240 ? op - 256 : op;
            } else if (c == 0xFB) {
                if (op <= 3)
                    colour = op;
                else if (op == 6)
                    table = 0;
                else if (op == 5)
                    table = 1;
                else
                    ++*unknown; /* justify, a y offset: not in the manual's files */
            } else {
                ++*unknown;
            }
            continue;
        }
        if (c >= 0xE5) { /* page end, arrow, new line, padding: none inside a manual line */
            END_WORD();
            ++*unknown;
            ++i;
            continue;
        }
        if (c == 0x8F) {
            END_WORD();
            x += 6;
            ++i;
            continue;
        }
        if (c >= 0xAD && c <= 0xB8) { /* pictographs */
            int w = WIDTH(c);
            END_WORD();
            memset(&run, 0, sizeof run);
            run.colour = (unsigned char)colour;
            run.x = (unsigned short)(x < 0 ? 0 : x), run.w = (unsigned short)w;
            run.off = (unsigned)i, run.len = 1;
            run.gap = (unsigned char)GAP(x);
            if (c >= 0xAE && c <= 0xB1)
                run.kind = PC_UI_MANUAL_GLYPH, run.glyph = (unsigned char)(c - 0xAE);
            else if (c >= 0xB2 && c <= 0xB5)
                run.kind = PC_UI_MANUAL_ARROW, run.glyph = (unsigned char)(c - 0xB2);
            else if (c == 0xB7)
                run.kind = PC_UI_MANUAL_STAR;
            else
                run.kind = 0xFF; /* the note, Lv., the block: not drawn */
            x += w;
            if (run.kind == 0xFF) {
                ++*unknown;
            } else {
                if (!push_run(r, run))
                    return 0;
                last_end = x, start = 0;
            }
            ++i;
            continue;
        }
        if (word < 0)
            word = (int)i, word_x = x, word_gap = GAP(x);
        x += WIDTH(c);
        ++i;
    }
#undef END_WORD
#undef GAP
#undef WIDTH
}

/* One sprite's pixels as 494.c _drawSprite draws them: 16-high bands of 8 (8bpp) or 16 (4bpp) wide
 * blocks, block id k at HF1 block k; 8bpp index p reads CLUT line n + p / 16 entry p % 16 (the 256
 * colours of the CLUT row at x 768 + (n % 16) * 16, as uploaded by _copyCluts), 4bpp CLUT line n. */
static unsigned char* composite(const unsigned char* sp, unsigned room, const unsigned char* hf1, unsigned n1,
    int w, int h, int colors, int clut, int nblocks, int* unknown)
{
    unsigned nb, nc;
    const unsigned char *blocks, *cluts;
    unsigned char* rgba;
    int stride = colors == 16 ? 16 : 8, b = 0, y0, x0;
    if (!hf1 || n1 < 8)
        return NULL;
    nb = u32at(hf1), nc = u32at(hf1 + 4), blocks = hf1 + 8;
    if (nb > (n1 - 8) / 128 || nc > (n1 - 8 - nb * 128) / 32)
        return NULL;
    cluts = blocks + nb * 128;
    if (w <= 0 || h <= 0 || w > 1024 || h > 512)
        return NULL;
    rgba = calloc((size_t)w * (size_t)h, 4);
    if (!rgba)
        return NULL;
    for (y0 = 0; y0 < h; y0 += 16)
        for (x0 = 0; x0 < w; x0 += stride, ++b) {
            unsigned id;
            const unsigned char* block;
            int px, py;
            if (b >= nblocks || (unsigned)(b + 1) * 2 > room) {
                ++*unknown;
                continue;
            }
            id = (unsigned)s16at(sp + b * 2);
            if (id >= nb) {
                ++*unknown;
                continue;
            }
            block = blocks + id * 128;
            for (py = 0; py < 16 && y0 + py < h; ++py)
                for (px = 0; px < stride && x0 + px < w; ++px) {
                    unsigned index, line, entry, colour;
                    unsigned char* out = rgba + ((size_t)(y0 + py) * (size_t)w + (size_t)(x0 + px)) * 4;
                    if (stride == 8) {
                        index = block[py * 8 + px];
                        line = (unsigned)clut + index / 16, entry = index % 16;
                    } else {
                        index = block[py * 8 + px / 2];
                        index = px & 1 ? index >> 4 : index & 15;
                        line = (unsigned)clut, entry = index;
                    }
                    if (line >= nc)
                        continue; /* past the CLUTs: transparent */
                    colour = u16at(cluts + line * 32 + entry * 2);
                    if (colour == 0)
                        continue; /* 0x0000: transparent (the STP bit alone is black) */
                    out[0] = (unsigned char)((colour & 31) << 3 | (colour & 31) >> 2);
                    out[1] = (unsigned char)((colour >> 5 & 31) << 3 | (colour >> 5 & 31) >> 2);
                    out[2] = (unsigned char)((colour >> 10 & 31) << 3 | (colour >> 10 & 31) >> 2);
                    out[3] = 255;
                }
        }
    return rgba;
}

unsigned long long pc_ui_manual_art_hash(const unsigned char* rgba, int w, int h)
{
    unsigned long long hash = 14695981039346656037ull;
    unsigned char head[4];
    size_t i, n = (size_t)(w > 0 ? w : 0) * (size_t)(h > 0 ? h : 0) * 4;
    head[0] = (unsigned char)w, head[1] = (unsigned char)(w >> 8), head[2] = (unsigned char)h;
    head[3] = (unsigned char)(h >> 8);
    for (i = 0; i < 4; ++i)
        hash = (hash ^ head[i]) * 1099511628211ull;
    for (i = 0; rgba && i < n; ++i)
        hash = (hash ^ rgba[i]) * 1099511628211ull;
    return hash;
}

/* The retail (SLUS-01040) images, from pc/tools/manual_dump.py --art. Another disc's art simply does not
 * match: it is drawn as is. */
static const struct {
    unsigned long long hash;
    signed char art;
} button_art[] = {
    {0x246821d4c833c3a6ull, PC_UI_MANUAL_ART_TRIANGLE}, /* HELP01 sprite 0 */
    {0x33e368dcd580ce89ull, PC_UI_MANUAL_ART_CROSS},    /* 1 */
    {0xf0f01423b35d9b07ull, PC_UI_MANUAL_ART_CIRCLE},   /* 2 */
    {0xc8bbfbe884f6b56eull, PC_UI_MANUAL_ART_SQUARE},   /* 3 */
    {0x1bca6fb6fd274dd6ull, PC_UI_MANUAL_ART_DPAD},     /* 4 */
    {0xe3fa864a1dc91f00ull, PC_UI_MANUAL_ART_R2},       /* 5 */
    {0x6d2fbdee63a945d2ull, PC_UI_MANUAL_ART_L2},       /* 6 */
    {0x265e09e998f5d468ull, PC_UI_MANUAL_ART_R1},       /* 7 */
    {0x688206f5e085d9e6ull, PC_UI_MANUAL_ART_L1},       /* 8 */
    {0x878e5ebdb591015eull, PC_UI_MANUAL_ART_START},    /* 13 */
    {0x4e00b6fbbe202e13ull, PC_UI_MANUAL_ART_SELECT},   /* 14 */
};

int pc_ui_manual_button_art(const unsigned char* rgba, int w, int h)
{
    unsigned long long hash;
    size_t i;
    if (!rgba || w <= 0 || h <= 0 || w > 64 || h > 64)
        return -1;
    hash = pc_ui_manual_art_hash(rgba, w, h);
    for (i = 0; i < sizeof button_art / sizeof button_art[0]; ++i)
        if (button_art[i].hash == hash)
            return button_art[i].art;
    return -1;
}

static void read_anim(const unsigned char* p, pc_ui_manual_anim* a)
{
    a->enabled = (short)s16at(p);
    a->mask = (short)s16at(p + 2);
    a->duration = (short)s16at(p + 4);
    a->repeat = (short)s16at(p + 6);
    a->cur = a->timer = a->active = a->pad = 0; /* 494.c: zero in the file, counted from the load */
}

int pc_ui_manual_parse(const unsigned char* hf0, unsigned n0, const unsigned char* hf1, unsigned n1,
    const unsigned char* widths, unsigned nwidths, pc_ui_manual_topic* out)
{
    unsigned spr, lin, lin_size, n, i;
    runs_t r = {0};
    memset(out, 0, sizeof *out);
    if (!hf0 || n0 < 18)
        return 0;
    spr = u32at(hf0), lin = u32at(hf0 + 4), lin_size = u32at(hf0 + 8);
    n = u16at(hf0 + 16);
    if (18 + n * 2 > n0)
        return 0;
    out->line_first = calloc(n + 1, sizeof *out->line_first);
    if (!out->line_first)
        return 0;
    out->lines = (int)n;
    for (i = 0; i < n; ++i) {
        unsigned at = 16 + u16at(hf0 + 18 + i * 2) * 2;
        out->line_first[i] = r.n;
        if (at >= n0) {
            ++out->unknown;
            continue;
        }
        if (!line_runs(hf0, n0, at, widths, nwidths, &r, &out->unknown)) {
            free(r.runs);
            pc_ui_manual_free(out);
            return 0;
        }
    }
    out->line_first[n] = r.n;
    out->runs = r.runs, out->nruns = r.n;
    out->line_count = pc_ui_manual_line_count((int)n);
    out->pages = out->line_count / 10 + 1;
    /* the illustrations */
    if (spr <= n0 - 16 && 16 + spr + 2 <= n0) {
        const unsigned char* base = hf0 + 16 + spr;
        unsigned room = n0 - 16 - spr, count = u16at(base), k;
        const unsigned char** keys; /* identical sprites (the same blocks, size and CLUT) share an image */
        if (2 + count * 2 > room) {
            ++out->unknown;
            count = 0;
        }
        out->sprites = calloc(count ? count : 1, sizeof *out->sprites);
        out->rgba = calloc(count ? count : 1, sizeof *out->rgba);
        out->tex_w = calloc(count ? count : 1, sizeof *out->tex_w);
        out->tex_h = calloc(count ? count : 1, sizeof *out->tex_h);
        out->tex_art = calloc(count ? count : 1, sizeof *out->tex_art);
        keys = calloc(count ? count : 1, sizeof *keys);
        if (!out->sprites || !out->rgba || !out->tex_w || !out->tex_h || !out->tex_art || !keys) {
            free(keys);
            pc_ui_manual_free(out);
            return 0;
        }
        for (k = 0; k < count; ++k) {
            int off = s16at(base + 2 + k * 2) * 2, w, h, nblocks, colors, clut, t;
            const unsigned char* sp;
            pc_ui_manual_sprite* s = &out->sprites[out->nsprites];
            if (off < 0 || (unsigned)off + 30 > room) {
                ++out->unknown;
                continue;
            }
            sp = base + off;
            read_anim(sp, &s->a);
            s->x = (short)s16at(sp + 16), s->y = (short)s16at(sp + 18);
            w = s16at(sp + 20), h = s16at(sp + 22), nblocks = s16at(sp + 24), colors = s16at(sp + 26);
            clut = s16at(sp + 28);
            s->w = (short)w, s->h = (short)h;
            if (nblocks < 0 || (unsigned)off + 30 + (unsigned)nblocks * 2 > room || (colors != 16 && colors != 256)) {
                ++out->unknown;
                continue;
            }
            s->tex = -1;
            for (t = 0; t < out->ntex; ++t) {
                const unsigned char* o = keys[t];
                if (s16at(o + 20) == w && s16at(o + 22) == h && s16at(o + 24) == nblocks && s16at(o + 26) == colors
                    && s16at(o + 28) == clut && !memcmp(o + 30, sp + 30, (size_t)nblocks * 2))
                    s->tex = t;
            }
            if (s->tex < 0) {
                unsigned char* px = composite(sp + 30, room - (unsigned)off - 30, hf1, n1, w, h, colors,
                    (unsigned)clut & 0xFFFF, nblocks, &out->unknown);
                if (!px) {
                    ++out->unknown;
                    continue;
                }
                keys[out->ntex] = sp;
                out->rgba[out->ntex] = px;
                out->tex_w[out->ntex] = (short)w, out->tex_h[out->ntex] = (short)h;
                out->tex_art[out->ntex] = (signed char)pc_ui_manual_button_art(px, w, h);
                s->tex = out->ntex++;
            }
            ++out->nsprites;
        }
        free(keys);
        /* the rules (only when the header says there are: 494.c _drawContentLines) */
        if ((int)lin_size > 0) {
            if (lin > room || lin + 2 > room) {
                ++out->unknown;
            } else {
                const unsigned char* lp = base + lin;
                unsigned lroom = room - lin, nl = u16at(lp);
                if (2 + nl * 28 > lroom) {
                    ++out->unknown;
                    nl = (lroom - 2) / 28;
                }
                out->rules = calloc(nl ? nl : 1, sizeof *out->rules);
                if (!out->rules) {
                    pc_ui_manual_free(out);
                    return 0;
                }
                for (k = 0; k < nl; ++k) {
                    const unsigned char* e = lp + 2 + k * 28;
                    pc_ui_manual_rule* ru = &out->rules[out->nrules++];
                    read_anim(e, &ru->a);
                    ru->x0 = (short)s16at(e + 16), ru->y0 = (short)s16at(e + 18);
                    ru->x1 = (short)s16at(e + 20), ru->y1 = (short)s16at(e + 22);
                    ru->r = e[24], ru->g = e[25], ru->b = e[26];
                }
            }
        }
    } else {
        ++out->unknown;
    }
    out->ok = 1;
    return 1;
}
