/* Option page actions (option_actions.h). */
#include "option_actions.h"
#include <string.h>

/* ui_settings.cpp (every build that has the pages); weak for the probes that do not. */
void pc_ui_settings_flush(void) __attribute__((weak));
int pc_ui_settings_dirty(void) __attribute__((weak));
/* lua_plugins.cpp: every plugin loads again (weak: builds and probes without Lua). */
void pc_lua_reload_all(void) __attribute__((weak));

typedef struct {
    const char* id;
    const char* label;
    const char* help;
    int plugin_ok;
    const char* (*unavailable)(void);
    void (*run)(void);
} core_action;

static const char* save_unavailable(void)
{
    if (!pc_ui_settings_flush || !pc_ui_settings_dirty)
        return "Not available in this build";
    return pc_ui_settings_dirty() ? NULL : "Nothing to save: every change is saved";
}
static void save_run(void) { pc_ui_settings_flush(); }
static const char* reload_unavailable(void) { return pc_lua_reload_all ? NULL : "Plugins are off in this build"; }
static void reload_run(void) { pc_lua_reload_all(); }

static const core_action CORE[] = {
    {"settings.save", "Save now", "Write the settings file now (changes also save by themselves after a second).", 1,
        save_unavailable, save_run},
    /* a plugin's page offering it would reload itself from its own menu */
    {"plugins.reload", "Reload plugins", "Every plugin loads again from its files.", 0, reload_unavailable,
        reload_run},
};
enum { CORE_COUNT = sizeof CORE / sizeof CORE[0] };

static pc_option_action_provider provider;
static int have_provider;

static const core_action* core(const char* id)
{
    for (int i = 0; id && i < CORE_COUNT; i++)
        if (!strcmp(CORE[i].id, id))
            return &CORE[i];
    return NULL;
}

void pc_option_actions_set_provider(const pc_option_action_provider* p)
{
    have_provider = p && p->exists && p->run;
    if (have_provider)
        provider = *p;
    else
        memset(&provider, 0, sizeof provider);
}

int pc_option_action_exists(const char* id)
{
    if (!id)
        return 0;
    if (core(id))
        return 1;
    return strchr(id, '/') && have_provider && provider.exists(id) ? 2 : 0;
}

int pc_option_action_plugin_ok(const char* id)
{
    const core_action* a = core(id);
    return a && a->plugin_ok;
}

const char* pc_option_action_label(const char* id)
{
    const core_action* a = core(id);
    if (a)
        return a->label;
    return pc_option_action_exists(id) == 2 && provider.label ? provider.label(id) : NULL;
}

const char* pc_option_action_help(const char* id)
{
    const core_action* a = core(id);
    if (a)
        return a->help;
    return pc_option_action_exists(id) == 2 && provider.help ? provider.help(id) : NULL;
}

const char* pc_option_action_unavailable(const char* id)
{
    const core_action* a = core(id);
    if (a)
        return a->unavailable ? a->unavailable() : NULL;
    if (pc_option_action_exists(id) != 2)
        return id && strchr(id, '/') ? "Not available now (its plugin is not loaded)" : "Not available in this version";
    return provider.unavailable ? provider.unavailable(id) : NULL;
}

int pc_option_action_run(const char* id)
{
    if (pc_option_action_unavailable(id))
        return 0;
    const core_action* a = core(id);
    if (a) {
        a->run();
        return 1;
    }
    return provider.run(id);
}

int pc_option_action_count(void) { return CORE_COUNT; }
const char* pc_option_action_id(int i) { return i >= 0 && i < CORE_COUNT ? CORE[i].id : NULL; }
