/* Shader hot reload (pc/docs/Vulkan_Renderer.md "Hot reload"): saving a
 * stage or an included .glsl under pc/shaders/vulkan ($ROOD_SHADERS) shows
 * in the running game (Vulkan) within about a second. The sources are
 * polled, the changed stages (and every stage including a changed file,
 * directly or not) compiled on a background thread (shader_compiler.h),
 * and on success the new SPIR-V replaces the stages' code by name
 * (vk_shader_lookup.h) and the pipelines made from them
 * (vk_pipelines_rebuild), between frames. A stage that fails keeps its last
 * good code; its errors are shown (overlay banner, RPC shaders). With no
 * stage files in the folder (a shipped build) the service stays off. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Settings: shader_reload (0/1, default 1), shader_reload_poll_ms (default
 * 500, 100..5000). */
extern int pc_shader_reload, pc_shader_reload_poll_ms;
/* Once per presented frame, between frames: polls, and applies a finished compile. */
void pc_shader_reload_poll(void);
/* The node material graphs' generated include (material_graph.cpp): once per
 * frame before the poll above, regenerates it when the graphs or the
 * instances changed. A generated file was written: the next poll is at once. */
void pc_material_graph_poll(void);
void pc_shader_reload_request(void);
/* Stage files to reload are there (the source folder has some): pipelines
 * keep their recipes (vk_core.c). */
int pc_shader_reload_available(void);
/* Waits for a running compile and drops it: before an engine reload (the next library starts afresh). */
void pc_shader_reload_settle(void);
/* Once after an apply: a paused game draws the frame again (dev_rpc.c). */
int pc_shader_reload_redraw(void);
/* RPC shaders: {action: status | reload | reset}; the reply's JSON in out. -1 on error (out: why). */
int pc_shader_reload_rpc(const char* params, char* out, int size);
/* The Graphics panel's state as lines (watching, reloads and the last one's time, edited stages in use). */
void pc_shader_reload_summary(char* out, int size);
/* The overlay's banner: the number of errors, the first as `file:line: message`. */
int pc_shader_reload_errors(char* first, int size);

#ifdef __cplusplus
}
#endif
