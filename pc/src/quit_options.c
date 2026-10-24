/* Quit to title / Quit to desktop (quit_options.h). */
#include "quit_options.h"
#include "host_format.h"

extern int pc_game_phase __attribute__((weak)); /* boot_runtime.c: 1 title, 2 idle demo, 0 a game */
extern void pc_quit(void) __attribute__((weak)); /* dev_rpc.c: the window close button's path */
extern void pc_ui_settings_flush(void) __attribute__((weak)); /* ui_settings.cpp */

static int pending;       /* PC_QUIT_*: the question is up */
static int title_queued;  /* a quit to title waiting for the game's pad poll */

const char* pc_quit_prompt_text(void) { return "Unsaved progress will be lost."; }

int pc_quit_at_title(void) { return &pc_game_phase && pc_game_phase == 1; }

int pc_quit_needs_confirm(int to)
{
    (void)to;
    return !(&pc_game_phase && pc_game_phase != 0); /* (the title and its idle demos have no progress to lose) */
}

/* (a soft reset or the like leaving the game meanwhile: the question is moot) */
int pc_quit_pending(void) { return pending && pc_quit_needs_confirm(pending) ? pending : PC_QUIT_NONE; }
void pc_quit_cancel(void) { pending = PC_QUIT_NONE; }
int pc_quit_title_queued(void) { return title_queued; }

int pc_quit_now(int to)
{
    pending = PC_QUIT_NONE;
    if (to == PC_QUIT_TITLE) {
        if (pc_quit_at_title())
            return 0; /* already there */
        title_queued = 1;
        pc_log_line("[quit] to title (the soft reset's path)");
        return 1;
    }
    if (to != PC_QUIT_DESKTOP)
        return 0;
    pc_log_line("[quit] to desktop");
    /* what the window's close button does (boot_window.c): a change still waiting for its debounced save first */
    if (pc_ui_settings_flush)
        pc_ui_settings_flush();
    if (pc_quit)
        pc_quit();
    return 1;
}

int pc_quit_ask(int to)
{
    if (to != PC_QUIT_TITLE && to != PC_QUIT_DESKTOP)
        return 0;
    if (!pc_quit_needs_confirm(to)) {
        pc_quit_now(to);
        return 1;
    }
    pending = to;
    return 0;
}

void pc_quit_answer(int yes)
{
    int to = pending;
    pending = PC_QUIT_NONE;
    if (yes && to)
        pc_quit_now(to);
}

int pc_quit_title_poll(void)
{
    if (!title_queued)
        return 0;
    title_queued = 0;
    if (pc_quit_at_title())
        return 0;
    /* the settings are saved as the soft reset leaves (the System page's changes are debounced) */
    if (pc_ui_settings_flush)
        pc_ui_settings_flush();
    return 1;
}
