/* The Option pages' preview (option_peek.h). */
#include "option_peek.h"
#include <stdio.h>
#include <string.h>

/* photo.h (the photo mode's hidden layers) and render.h, weak: probes link without them. */
extern void pc_photo_ui_hide(const char* owner, int flags) __attribute__((weak));
extern unsigned pc_vsync_count(void) __attribute__((weak));
enum { HIDE_HUD = 1, HIDE_GAME2D = 2 }; /* photo.h PC_PHOTO_HIDE_HUD, _GAME2D */
#define OWNER "options.preview"

const char* const pc_option_peek_hint = "Hold SQUARE: preview.";

static int on;
static unsigned last; /* the vsync of the latest call */
static char label[96], value[96];

static void hide(int yes)
{
    if (pc_photo_ui_hide)
        pc_photo_ui_hide(OWNER, yes ? HIDE_HUD | HIDE_GAME2D : 0);
}

void pc_option_peek(int now, const char* l, const char* v)
{
    last = pc_vsync_count ? pc_vsync_count() : 0;
    snprintf(label, sizeof label, "%s", l ? l : "");
    snprintf(value, sizeof value, "%s", v ? v : "");
    now = now != 0;
    if (now != on) {
        on = now;
        hide(on);
    }
}

int pc_option_peek_active(void)
{
    /* An Option screen that stopped calling (the menu closed under it): the menu is back. */
    if (on && pc_vsync_count && pc_vsync_count() - last > 8) {
        on = 0;
        hide(0);
    }
    return on;
}

int pc_option_peek_text(const char** l, const char** v)
{
    if (!pc_option_peek_active())
        return 0;
    *l = label, *v = value;
    return 1;
}
