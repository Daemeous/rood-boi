/* The extension system's entry points for the game (pc/src/lua;
 * pc/docs/Extensions.md "Where Lua runs"). Every call is on the game thread. */
#ifndef ROOD_LUA_RUNTIME_H
#define ROOD_LUA_RUNTIME_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

void pc_lua_init(void);       /* main(), after the settings load: finds and loads the plugins */
void pc_lua_shutdown(void);   /* atexit: writes what plugins stored; runs no Lua */
void pc_lua_tick(void);       /* pc_tick: once per game vsync while the game runs */
void pc_lua_frame(double dt); /* pc_overlay_frame: every presented image, the ImGui frame open */
/* RPC method lua (dev_rpc.c): the params object's text; the reply's JSON in out
 * (its length), or < 0 with the error's text in out. */
int pc_lua_rpc(const char* params, char* out, int size);
/* A core event to every plugin (payload: JSON, or NULL); returns how many handlers ran. */
int pc_lua_emit_json(const char* event, const char* json);
/* settings.cfg (project_settings.cpp): plugins_elevated, plugin_enabled, plugin_order,
 * plugin_grant, plugin_budget_ms, ... begin/end bracket a whole file's load. */
void pc_lua_settings_begin(void);
int pc_lua_settings_parse(const char* key, const char* rest); /* 1: a plugin line */
void pc_lua_settings_end(void);
void pc_lua_settings_write(FILE* f);
/* The dev overlay's Lua console (lua_console.cpp). */
void pc_lua_console_menu_item(void);
void pc_lua_console_window(void);

#ifdef __cplusplus
}
#endif
#endif
