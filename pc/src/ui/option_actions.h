/* Option page actions (pc/src/ui/option_actions.c; plan
 * pc/docs/plans/2026-10-18-lua-m1-options.md, "Permissions"): what an action
 * row of an Option page runs (option_pages.h PC_OPT_ACTION). Two kinds:
 *   - core actions, a table here, ids without a slash ("settings.save"), each
 *     flagged plugin_ok: whether a third-party plugin's page may offer it
 *     (the shipped rood.* plugins may offer every one);
 *   - a plugin's own, "<plugin id>/<name>" (rood.menu.action), run in that
 *     plugin's sandbox under its budget through the provider the Lua host
 *     sets (lua_api_menu.cpp); a page offers only its own plugin's.
 * Every call is on the game thread. */
#ifndef PC_OPTION_ACTIONS_H
#define PC_OPTION_ACTIONS_H
#ifdef __cplusplus
extern "C" {
#endif

/* 1: a core action; 2: a plugin's (the provider knows it); 0: none. */
int pc_option_action_exists(const char* id);
/* A core action a third-party plugin's page may offer. */
int pc_option_action_plugin_ok(const char* id);
/* Its label and help (core: the table's; a plugin's: as it registered them;
 * NULL when it gave none or there is no such action). */
const char* pc_option_action_label(const char* id);
const char* pc_option_action_help(const char* id);
/* Why it cannot run now (its row greyed with that reason); NULL when it can. */
const char* pc_option_action_unavailable(const char* id);
/* Runs it; 1 when it ran. */
int pc_option_action_run(const char* id);

/* The core actions, for listings (rood.menu.actions): index 0..count-1. */
int pc_option_action_count(void);
const char* pc_option_action_id(int i);

/* Plugins' actions (lua_api_menu.cpp). NULL: none. */
typedef struct {
    int (*exists)(const char* id);
    const char* (*label)(const char* id);
    const char* (*help)(const char* id);
    const char* (*unavailable)(const char* id);
    int (*run)(const char* id);
} pc_option_action_provider;
void pc_option_actions_set_provider(const pc_option_action_provider* p);

#ifdef __cplusplus
}
#endif
#endif
