/* The Option pages in the game's own Option screen (pc/src/ui/classic_options.c,
 * battle builds; plan pc/docs/plans/2026-10-18-lua-m1-options.md C1/C2). MENU8
 * (src/MENU/MENU8.PRG/88.c, ROOD_PC) lists the page model's Option-screen
 * entries (option_pages.h pc_option_pages_children(-1): Gameplay, Display,
 * ..., plugins' pages) below its nine rows; picking one runs a page here,
 * drawn with the game's own pieces (its menu windows, cursor hand, font,
 * gauges, the information box) and driven by its pad input, until it returns
 * to the list or closes the menu. */
#ifndef PC_CLASSIC_OPTIONS_H
#define PC_CLASSIC_OPTIONS_H
#ifdef __cplusplus
extern "C" {
#endif

/* The entries MENU8 shows at most after its own rows. */
#define PC_OPTIONS_ENTRIES 24

/* MENU8 init: each entry's name and help (the game's encoding; strings[2i],
 * strings[2i+1], valid until the next call) and row type; returns how many. */
int pc_options_entries(char** strings, int* types, int max);
/* Circle on entry `entry` (its index among them): the page opens. MENU8 has
 * hoisted the entry's row to the title stack. */
void pc_options_open(int entry);
/* Every frame while a page is open: 0 it runs, -1 back to the Option list,
 * -2 the menu closes (Triangle). */
int pc_options_exec(void);
/* RPC options: the classic screen's state as JSON (dev_rpc.c). */
int pc_options_json(char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
