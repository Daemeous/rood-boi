/* Modern menus, the native Score screen's Bestiary: SMALL/MON.BIN read from
 * the disc image into host memory once per process, at the first open that
 * reads it (pc_ui_disc_read; never the game's heap or CD queue, nothing
 * written anywhere; a failed read is tried again at the next open), kept
 * until exit like the Quick manual's pages (manual_cache.c); each open
 * parses it (mon_parse.c) with the kill flags the bridge hands over and
 * decodes its names and descriptions into the open's snapshot. Host code:
 * not in the game's state sections, so a snapshot never holds its pointer. */
#include "item_names.h"
#include "mon_parse.h"
#include "ui_score.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */

static unsigned char* mon_data; /* the file's bytes (NULL: not read yet, or the last read failed) */
static unsigned mon_size;

static const unsigned char* mon_file(unsigned* size)
{
    if (!mon_data) {
        unsigned char* data = NULL;
        unsigned n = 0;
        if (pc_ui_disc_read("SMALL/MON.BIN", &data, &n) && data) {
            mon_data = data;
            mon_size = n;
        } else {
            fprintf(stderr, "[ui] score: SMALL/MON.BIN not read from the disc\n");
            free(data);
        }
    }
    *size = mon_data ? mon_size : 0;
    return mon_data;
}

void pc_ui_score_mon_fill(const int kill_flags[PC_MON_FLAG_WORDS], pc_ui_score* s)
{
    const unsigned char* data;
    unsigned size = 0;
    pc_mon_file* f = malloc(sizeof *f);
    int i;
    s->mon_ok = 0;
    s->unlocked = 0;
    s->mon_why[0] = 0;
    for (i = 0; i < PC_MON_ENEMIES; ++i) {
        memset(&s->mon[i], 0, sizeof s->mon[i]);
        s->mon[i].cls = s->mon[i].prev = s->mon[i].next = -1;
    }
    if (!f) {
        snprintf(s->mon_why, sizeof s->mon_why, "out of memory");
        return;
    }
    data = mon_file(&size);
    pc_mon_parse(data, size, kill_flags, f);
    if (!f->ok) {
        snprintf(s->mon_why, sizeof s->mon_why, "%s", f->why ? f->why : "MON.BIN unreadable");
    } else {
        s->mon_ok = 1;
        s->unlocked = f->unlocked;
        for (i = 0; i < PC_MON_ENEMIES; ++i) {
            const pc_mon_entry* e = &f->e[i];
            s->mon[i].zud = e->zud;
            s->mon[i].cls = e->cls;
            s->mon[i].unlocked = e->unlocked;
            s->mon[i].prev = e->prev;
            s->mon[i].next = e->next;
            /* the list's name (the viewer's from name + 2, past the spacing code: the same text decoded) */
            pc_ui_text_decode(data + e->name_at, e->name_len, s->mon[i].name, sizeof s->mon[i].name);
            if (e->desc_len > 0)
                pc_ui_text_decode(data + e->desc_at, e->desc_len, s->mon[i].desc, sizeof s->mon[i].desc);
        }
    }
    free(f);
}
