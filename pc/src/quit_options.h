/* Quit to title and Quit to desktop (pc/src/quit_options.c): the System page, the
 * classic Option screen, the F1 overlay's File menu and the RPC `quit {to}`.
 *
 * Quit to title is the game's own soft reset: processPadState (main.c, ROOD_PC)
 * takes the request where it takes L1+L2+R1+R2+Select+Start, so the game ends
 * in exactly the state a soft reset gives (vs_main_resetGame, then
 * vs_overlay_jumpToTitle). Quit to desktop is the window's close button
 * (settings saved, pc_quit). Both ask first ("Unsaved progress will be lost.",
 * Yes / No, No by default) except where nothing can be lost: on the title. */
#ifndef PC_QUIT_OPTIONS_H
#define PC_QUIT_OPTIONS_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_QUIT_NONE = 0, PC_QUIT_TITLE = 1, PC_QUIT_DESKTOP = 2 };

/* The title screen (and the idle demo before a game starts): nothing to lose. */
int pc_quit_at_title(void);
/* 1 when quitting `to` needs the question. */
int pc_quit_needs_confirm(int to);
/* The UIs' entry: asks (pc_quit_pending() == to; the UI answers) or, where nothing can be lost, quits. 1: quit now. */
int pc_quit_ask(int to);
int pc_quit_pending(void);
void pc_quit_answer(int yes);
void pc_quit_cancel(void);
/* Quits without asking (the RPC, a Yes). Quit to title on the title does nothing; returns 0 then. */
int pc_quit_now(int to);
/* Game thread, every pad poll (main.c processPadState, ROOD_PC): 1 when the soft reset is due now. */
int pc_quit_title_poll(void);
/* 1 while a quit to title waits for the game's next pad poll. */
int pc_quit_title_queued(void);
const char* pc_quit_prompt_text(void);

#ifdef __cplusplus
}
#endif
#endif
