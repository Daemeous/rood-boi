/* Option pages (pc/src/ui/option_pages.cpp; plan pc/docs/plans/2026-10-18-lua-m1-options.md):
 * the page model both Option screens draw from. A page is an id, a title, a
 * help line, an optional parent page and its place among its siblings
 * (after / before a sibling's id or "group:<name>", ties in definition
 * order), and a list of rows. A row is one of:
 *   - a setting: a catalog key (ui_settings.h, pc_ui_settings_catalog_*),
 *     resolved to its live value, whether it can change and why not;
 *   - an action: an action id (option_actions.h);
 *   - a link: opens another page;
 *   - a header: a section title;
 *   - a note: a line of help text.
 *
 * Pages come from sources ("rood.options", a plugin's id): a source owns the
 * pages it defined and only it redefines them. The C++ fallback pages are
 * source "rood.options", the same ids as the shipped Lua plugin's
 * (rood.options/gameplay, /display, /graphics, /audio, /interface, /controls,
 * /bindings), and reproduce the System pages of ui_settings.h row for row;
 * they are in place from the first call until pc_option_pages_fallback(0)
 * (the Lua plugin took over) and again after pc_option_pages_fallback(1).
 *
 * Pages without a parent are the Option screen's entries; a page whose
 * parent is gone is not listed (nor are its sub-pages). Parent and link
 * cycles are refused when a page is defined. Any change to the pages bumps
 * the generation: indices and the strings handed out stay valid until it
 * changes (a renderer keeps its focus by page and row id across it). */
#ifndef PC_OPTION_PAGES_H
#define PC_OPTION_PAGES_H
#include "ui/ui_settings.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Row forms. */
enum { PC_OPT_SETTING, PC_OPT_ACTION, PC_OPT_LINK, PC_OPT_HEADER, PC_OPT_NOTE };
/* define's results (pc_option_pages_error: the text). */
enum {
    PC_OPT_OK = 0,
    PC_OPT_ERR_ID = -1,     /* no id, or spaces in it, or longer than 64 */
    PC_OPT_ERR_TITLE = -2,  /* no title */
    PC_OPT_ERR_PARENT = -3, /* the parent is not a page */
    PC_OPT_ERR_CYCLE = -4,  /* the parent or a link leads back to the page */
    PC_OPT_ERR_OWNER = -5,  /* another source defined that page */
    PC_OPT_ERR_ROW = -6     /* a row without its reference or text, or of no known form */
};
#define PC_OPT_REFS 8 /* ids in one after / before */

typedef struct {
    int form;          /* PC_OPT_* */
    const char* ref;   /* setting: the catalog key; action: its id; link: the page id; header / note: NULL */
    const char* label; /* header / note: the text; else NULL (the setting's, action id's or page's title) or the label */
    const char* help;  /* NULL: the setting's or linked page's help */
    int where;         /* PC_UI_WHERE_*; 0: the setting's own (catalog), all screens for the other forms */
} pc_option_row_def;

typedef struct {
    const char* id;                  /* "rood.options/display", "me.tweaks/more" */
    const char* title;               /* "Display" */
    const char* help;                /* the page's help line; may be NULL */
    const char* parent;              /* NULL: an Option-screen entry */
    const char* after[PC_OPT_REFS];  /* siblings' ids or "group:<name>"; unused entries NULL */
    const char* before[PC_OPT_REFS];
    const char* group;               /* "system" (the core pages), "plugins" (after every system page); may be NULL */
    const pc_option_row_def* rows;
    int row_count;
} pc_option_page_def;

/* Define (or, from the same source, redefine in place) a page; the strings
 * are copied. Returns PC_OPT_OK or a PC_OPT_ERR_* (nothing changed then). */
int pc_option_pages_define(const char* source, const pc_option_page_def* def);
/* Remove one page (1) or every page of a source (the number removed). */
int pc_option_pages_remove(const char* id);
int pc_option_pages_remove_source(const char* source);
/* 1: the C++ fallback pages replace whatever source "rood.options" defined;
 * 0: they go (the Lua plugin defines its own). active: they are in place. */
void pc_option_pages_fallback(int on);
int pc_option_pages_fallback_active(void);
/* No pages at all, not even the fallback (probes). */
void pc_option_pages_clear(void);
/* Bumped by every define, remove, fallback and clear. */
unsigned pc_option_pages_generation(void);
const char* pc_option_pages_error(int code);

/* ---- Resolved pages ----
 * Every listed page in tree order: each Option-screen entry in its order,
 * followed by its sub-pages (depth first). Index 0..count()-1. */
typedef struct {
    const char* id;
    const char* title;
    const char* help;   /* "" when none */
    const char* source;
    const char* group;  /* "" when none */
    int parent;         /* the parent page's index; -1: an Option-screen entry */
    int depth;          /* 0: an Option-screen entry */
} pc_option_page;

int pc_option_pages_count(void);
int pc_option_pages_index(const char* id); /* -1: not listed */
int pc_option_pages_info(int page, pc_option_page* out); /* 0: no such page */
/* A page's sub-pages (parent -1: the Option-screen entries) in order, up to
 * `max` indices into `out`; returns how many there are. */
int pc_option_pages_children(int parent, int* out, int max);

/* ---- Resolved rows ----
 * The rows `where` (PC_UI_WHERE_CLASSIC / _MODERN / _ALL) shows now: a row
 * is left out when its where has none of those bits, and a setting row also
 * while the catalog does not show it (it depends on another setting). */
typedef struct {
    int form;              /* PC_OPT_* */
    const char* ref;       /* as defined (setting: the key; action: its id; link: the page id; else NULL) */
    int catalog;           /* setting: its catalog index; -1: no such key (a missing plugin's: greyed) */
    int link;              /* link: the page's index; -1: not listed (greyed) */
    /* The row as ui_settings.h describes one: kind (PC_UI_SET_*, HEADER /
     * NOTE / LINK for those forms), id (setting: the key; action / link: the
     * ref; header / note: "<page id>#<n>"), label, help, value (link: the
     * page's title), enabled and reason, a setting's numbers and choices,
     * where. */
    pc_ui_setting_row row;
} pc_option_row;

int pc_option_page_rows(int page, int where);
int pc_option_page_row(int page, int where, int row, pc_option_row* out); /* 0: no such row */
int pc_option_page_find_row(int page, int where, const char* id);        /* the row's index; -1: none */

/* Action rows take their label, help and availability from option_actions.h
 * (an action that cannot run now is greyed with its reason). */

#ifdef __cplusplus
}
#endif
#endif
