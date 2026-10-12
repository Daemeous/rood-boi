/* Developer control interface: JSON-RPC 2.0 over a Unix socket.
 *
 * Enabled only when ROOD_RPC names a socket path; the socket is created with
 * mode 0600. Each request is one JSON object per line and each response is one
 * line. Requests are serviced on the game thread from pc_tick (once per
 * vsync), so every method runs at a frame boundary with no locking. Methods:
 *
 *   status                       counters, pause state, display, start state
 *   pause {paused}               stop (or resume) the game at vsync boundaries
 *   step {vsyncs}                run N vsyncs, then pause again
 *   input {buttons, vsyncs}      hold buttons ("circle", "up", ...) for N vsyncs;
 *         {right_stick: [x, y]}  and/or hold the right stick (-1..1, down +y)
 *   camera_stick {...}           the right-stick camera (camera_stick.c):
 *                                settings (enabled, speed, deadzone, curve,
 *                                invert_x, invert_y, zoom, zoom_speed,
 *                                zoom_min, zoom_max, zoom_pitch, collision,
 *                                r3_reset, idle_reset, keys), action "reset";
 *                                replies the settings and its state
 *   weapon_slots {enabled}       the QoL weapon slots (weapon_slots.c,
 *                                settings key qol_weapon_slots): sets the
 *                                option; replies it, the overlay/assign/swap
 *                                state, the field mode and each inventory
 *                                weapon's slot (gems[3]) and shield's slot
 *                                bits (battle builds)
 *   release                      drop all injected buttons
 *   screenshot {path}            write the displayed frame as a PPM
 *   present_dump {path}          the next presented window image (as seen) as a PPM
 *   anim_trace {path, count}     the object instances (bone palettes, game frame,
 *                                blend) of the next `count` presented images to a
 *                                binary file (pc/tools/anim_trace.py); no count:
 *                                the images still to trace
 *   gpu_dump {path, replay}      write the scaled hardware VRAM target as a PPM
 *                                (replay: the last interpolated frame's)
 *   gpu_draw {packet, texture_x, texture_y, texture_w, texture_h, texture,
 *             area_x, area_y, area_w, area_h, offset_x, offset_y}
 *                                tests: load 16-bit texels (hex, little-endian)
 *                                into VRAM, set the drawing area and offset,
 *                                then draw GP0 command words (hex, little-endian
 *                                32-bit) on both rasterizers (CPU VRAM, read via
 *                                the vram symbol, and the hardware target,
 *                                gpu_dump); the game's next frame resets the area
 *   scene                        captured room: triangle count, bounds, camera
 *                                (the camera the presented picture was drawn with)
 *   gi_dump {path, count, light} Vulkan: the last GI update's world position,
 *                                normal and shadow visibility per GI pixel
 *                                (vk_rt.inc pc_vk_gi_dump; header: eye,
 *                                replay, camera); count: the next N updates
 *                                to path-000.bin, path-001.bin, ...;
 *                                light: the indirect light instead (raw,
 *                                displayed, NRD history; pc_vk_gi_dump_light)
 *   probe_gi {dump}              probe GI (gi_method probes, probe_gi.inc):
 *                                grid, probe count, active probes, backend
 *                                (1 Vulkan ray queries, 2 Vulkan compute BVH,
 *                                3 OpenGL compute BVH), updates, resets;
 *                                dump: the field to a file (pc_probe_dump)
 *   scene_dump {path}            captured room as OBJ with vertex colours
 *   lights {action, baked}       room light list; action "suggest" (fit from
 *                                vertex colours), "save" or "add" (a point
 *                                light at position [x, y, z], selected);
 *                                select: a light's index; baked lighting scale
 *   actors {action, ...}         light actors (prefab instances) in the room:
 *                                list (default); add {prefab, position | x, y
 *                                | u, v, modes, rotation, params, glow, seed}
 *                                (without a position: placed by snapping at
 *                                the window pixel, as a drop would); set
 *                                {index, position, rotation, enabled, params
 *                                {key: value | null}, reset [keys] | true,
 *                                glow, prefab}; remove / select {index};
 *                                sample {index, time} (its lights at time t);
 *                                suggest {accept} (actors for the room's
 *                                glows); attach {glow, prefab}; save;
 *                                save_prefab {index, name}; apply_prefab
 *                                {index}; from_lights {name, lights};
 *                                time: fixes the animation clock (null: live);
 *                                animate; glows: true lists the emitters
 *   glows {action, id, ...}      the room's glows (glows.c): list (default),
 *                                select / solo / reset {id}, set {id, enabled,
 *                                intensity, tint, size, reach}; smoothing
 *                                (seconds); fragments: this update's glowing
 *                                triangles; replies selected, energy, glows
 *   prefabs {reload}             the prefab library (built-in and user)
 *   editor_place {item | prefab | light, phase, x, y | u, v}  a drag from
 *                                Place Actors: phase begin, move, drop,
 *                                cancel, double_click (at the view's centre)
 *                                or state
 *   graphics {scale, filter, aspect, ...}  get/set internal resolution
 *                                (1-8), presentation filter (0 nearest,
 *                                1 bilinear, 2 supersample), aspect (0 native,
 *                                1 stretch, 2 integer) and the flags
 *                                widescreen, perspective, show_depth,
 *                                subpixel, interpolate, interp_skeleton
 *                                (characters' in-betweens: rotations slerped
 *                                through the skeleton, else linear),
 *                                interp_uv (room texture scrolls: 0 step,
 *                                1 drawn partway between game frames, 2
 *                                smooth through the frames they hold),
 *                                interp_holds (poses the game holds for 1-3
 *                                frames between changes blended across the
 *                                hold), present_rate (images per vsync, 1-8:
 *                                in-betweens at fractions of a vsync), aa (FXAA),
 *                                texture_filter, ui_upscale ("nearest" | "mmpx"),
 *                                ui_subpixel, ui_text ("original" | "font"),
 *                                post, drop_shadows; path-
 *                                traced GI: gi, gi_debug, gi_denoise
 *                                (on/off), gi_denoiser ("nrd": NVIDIA NRD
 *                                RELAX for the indirect light, Vulkan built
 *                                with NRD, else the filter; "filter": our
 *                                temporal + a-trous; "raw"), nrd_glow (the
 *                                glows' light through NRD too), nrd_frames,
 *                                nrd_tune ("key=value,..." RELAX settings,
 *                                vk_nrd.cpp; experiments),
 *                                gi_bounces, gi_rays, gi_strength,
 *                                gi_resolution, gi_baked_bounce,
 *                                gi_budget (ms of path tracer GPU time per
 *                                present before it runs every n-th present
 *                                and the rest reuse its result; 0 every
 *                                present, -1 auto; ptgi.c cadence);
 *                                rt_shadows (ray-traced, else cube maps),
 *                                baked_shadows, shadow_samples,
 *                                emitter_light, contact_radius, fog_density;
 *                                probe GI (gi_method probes; probe_spacing
 *                                and probe_rays are lighting keys):
 *                                probe_hysteresis, probe_budget, probe_show
 *                                (editor: 1 active, 2 all probes);
 *                                native_room (0 legacy room packets, 1 native
 *                                room meshes, 2 native without the reference
 *                                packets), native_occlusion, native_actors
 *                                (0 legacy character/weapon/prop packets, 1
 *                                native meshes with bone palettes, 2 also
 *                                without the reference packets' CPU
 *                                rasterization; drop shadows and particles
 *                                follow it),
 *                                actor_order (native characters paint
 *                                themselves in PS1 order, else depth-test),
 *                                native_blend (0 semi-transparent room and
 *                                object polygons from packets, 1 native at
 *                                translucent markers), native_objects (0
 *                                other room blocks from packets, 1 native),
 *                                native_shadows (0 drop shadows from packets,
 *                                1 native), native_effects (0 particles from
 *                                packets, 1 native sprites), reconstruct
 *                                (1 keep the GTE projection log and the 3D
 *                                reconstruction of packets; 0 retire it
 *                                while every native switch is on);
 *                                gameplay_walls ("original" / "smart", or
 *                                0 / 1: the game's facing-mask cutaway, or
 *                                walls drawn unless they get in the way),
 *                                gameplay_walls_ghost (smart: occluding
 *                                walls as ghosts), gameplay_walls_opacity,
 *                                gameplay_walls_radius;
 *                                editor_hide_masks, editor_game_cutaway,
 *                                editor_cutaway, editor_cutaway_radius (the
 *                                free camera's view into rooms); the editor
 *                                viewport's overlays and snapping (the
 *                                settings keys of editor_overlays.h):
 *                                editor_wireframe, editor_points,
 *                                editor_character_wireframe, editor_snap_show,
 *                                editor_occluded_opacity, editor_snap_grid,
 *                                editor_snap_move, editor_snap_point,
 *                                editor_snap_edge, editor_snap_midpoint,
 *                                editor_snap_surface, editor_snap_radius,
 *                                editor_snap_highlight, editor_snap_offset
 *   uv_effects {effect, speed, freeze}  the current room's texture effects
 *                                that rewrite UVs (animated materials:
 *                                type, slot, polygon, modelled, running,
 *                                game/drawn offsets, motion, window); with
 *                                effect (1-based) sets its speed/freeze
 *                                override for the room
 *   room_groups {hide, show, show_all}  the current room's groups (flags,
 *                                polygons, black shell, hidden, cutaway,
 *                                drawn in the latest frame) and the facing
 *                                mask that frame tested; hide/show take a
 *                                list of group indices (free camera view only)
 *   editor_view {focus}          the free camera's cutaway focus: [x, y, z]
 *                                fixes it, null returns to the automatic one;
 *                                replies whether surfaces hide it (hidden) and
 *                                how far the ghosting cut has faded in (amount)
 *   editor_undo {action, steps, name, done}  the editor's undo history
 *                                (editor_undo.h): undo / redo (steps), list,
 *                                begin / end (one transaction across
 *                                requests, as a drag), jump (to `done`)
 *   editor_input {keys, text, ctrl, shift, focus}  keys pressed and released
 *                                through the window's event queue (as typed),
 *                                text typed, keyboard focus ("search": the
 *                                Outliner's search field, "none")
 *   editor_ui {visible, help, tab}  open or close the editor (as Esc/F1) and its
 *                                Keyboard shortcuts window; tab "outliner" or
 *                                "place" (Place Actors) beside it; returns the states
 *   editor_overlay {points, edges, update}  the viewport overlays' state
 *                                (room points and edges shown, the occlusion
 *                                test, the last frame's cost) and up to N
 *                                shown points / edges (point_samples,
 *                                edge_samples) with their window pixels;
 *                                brings the occlusion up to date unless
 *                                update is false
 *   editor_snap {x, y | u, v, modes, apply, highlight, clear}  the snap
 *                                target at a window pixel (or u, v of the
 *                                view) as a light drag would find it: modes
 *                                a list of "point", "edge", "midpoint",
 *                                "surface" (default: the editor's toggles);
 *                                the snapped point, its type and the points
 *                                lit around it with their weights; apply
 *                                moves the selected light there, highlight
 *                                keeps it drawn (clear stops)
 *   render_stats                native vs legacy submissions in the last frame
 *   read {address, length}       hex bytes from guest RAM, scratchpad or host data
 *   write {address, hex}         patch guest RAM or scratchpad
 *   state                        Vagrant Story snapshot (zone, room, player, music)
 *   quit                         exit the game
 *
 * pc/tools/rood_rpc.py is the client (CLI, Python API and a stdin/stdout
 * JSON-RPC bridge); it resolves symbol names to addresses with nm. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "pc_paths.h"
#include "room_effects.h"
#include "render.h"
#include "room_editor.h"
#include "editor_overlays.h"
#include "probe_gi.h"
#include "ui_upscale.h"
#include "ui_text.h"
#include "camera_stick.h"
/* The editor (editor_overlays.cpp, lights.cpp). */
extern int pc_overlay_wire __attribute__((weak)), pc_overlay_points __attribute__((weak)),
    pc_overlay_actor_wire __attribute__((weak)), pc_overlay_snap_show __attribute__((weak)),
    pc_snap_point __attribute__((weak)), pc_snap_edge __attribute__((weak)), pc_snap_midpoint __attribute__((weak)),
    pc_snap_surface __attribute__((weak)), pc_lights_snap __attribute__((weak));
extern float pc_overlay_occluded __attribute__((weak)), pc_snap_radius __attribute__((weak)),
    pc_snap_highlight __attribute__((weak)), pc_snap_offset __attribute__((weak)), pc_lights_snap_move __attribute__((weak));
extern unsigned pc_snap_modes(int) __attribute__((weak));
extern void pc_overlay_update(const pc_overlay_view*) __attribute__((weak));
extern void pc_overlay_get_stats(pc_overlay_stats*) __attribute__((weak));
extern void pc_overlay_hold_snap(const pc_snap_result*) __attribute__((weak));
extern int pc_overlay_neighbours(const float*, float, float (*)[4], int) __attribute__((weak));
extern int pc_overlay_sample_points(const pc_overlay_view*, float (*)[3], float (*)[2], int*, int) __attribute__((weak));
extern int pc_overlay_sample_edges(const pc_overlay_view*, float (*)[6], float (*)[4], int*, int) __attribute__((weak));

extern unsigned pc_battle_frame_count(void);
extern unsigned pc_vsync_count(void);
extern int pc_capture_display(const char* path, int* width, int* height);
extern void pc_window_tick(void);
extern void pc_window_redraw(void);
extern int pc_hw_dump(const char* path, int replay) __attribute__((weak));
extern int pc_hw_set_scale(int scale) __attribute__((weak));
extern int pc_hw_active __attribute__((weak)), pc_hw_scale __attribute__((weak));
extern int pc_gl_filter __attribute__((weak)), pc_gl_aspect __attribute__((weak));
extern int pc_gl_aa __attribute__((weak));
/* Anti-aliasing (antialias.h). */
extern int pc_aa_parse(const char*) __attribute__((weak));
extern const char* pc_aa_string(void) __attribute__((weak));
extern int pc_aa_downsample __attribute__((weak)), pc_aa_geometry_edges __attribute__((weak));
extern const char* const pc_aa_downsample_names[3] __attribute__((weak));
extern int pc_aa_msaa_active(void) __attribute__((weak)); /* gpu_hw.c: samples the 3D is drawn with now */
extern int pc_aa_msaa_max(void) __attribute__((weak)); /* gpu_hw.c: the most samples this backend runs */
extern int pc_gl_finish __attribute__((weak));
extern int pc_room_exact_depth __attribute__((weak));
extern int pc_baked_estimate __attribute__((weak));
extern int pc_gl_view_mode __attribute__((weak));
extern int pc_glow_shadows __attribute__((weak));
extern int pc_hw_texture_filter __attribute__((weak));
extern int pc_gi_enabled __attribute__((weak)), pc_gi_debug __attribute__((weak)), pc_gi_bounces __attribute__((weak));
extern int pc_gi_rays __attribute__((weak)), pc_gi_denoise __attribute__((weak));
extern int pc_gi_denoiser_active(void) __attribute__((weak));
extern int pc_nrd_glow __attribute__((weak)), pc_nrd_max_frames __attribute__((weak));
extern int vk_nrd_built(void) __attribute__((weak));
extern int vk_nrd_available(void) __attribute__((weak));
extern int pc_gi_trace_debug __attribute__((weak)), pc_rt_shadows __attribute__((weak));
extern float pc_gi_strength __attribute__((weak)), pc_gi_resolution __attribute__((weak));
extern float pc_gi_baked_bounce __attribute__((weak));
extern int pc_baked_shadows __attribute__((weak)), pc_shadow_samples __attribute__((weak));
extern float pc_emitter_light __attribute__((weak)), pc_contact_radius __attribute__((weak));
extern float pc_fog_density __attribute__((weak));
/* Probe GI (probe_gi.inc). */
extern int pc_probe_rays __attribute__((weak)), pc_probe_budget __attribute__((weak));
extern int pc_probe_show __attribute__((weak));
extern float pc_probe_spacing __attribute__((weak)), pc_probe_hysteresis __attribute__((weak));
extern void pc_probe_get_stats(pc_probe_stats* out) __attribute__((weak));
extern int pc_probe_dump(const char* path) __attribute__((weak));
extern int pc_gte_widescreen;
/* Lighting quality presets (lighting_quality.cpp; absent in unit probes). */
extern int pc_lighting_set(const char*, const char*) __attribute__((weak));
extern int pc_lighting_key_count(void) __attribute__((weak));
extern const char* pc_lighting_key_name(int) __attribute__((weak));
extern int pc_lighting_json(char*, int) __attribute__((weak));
extern void pc_lighting_sync(void) __attribute__((weak));
extern int pc_lighting_autodetect(int, int) __attribute__((weak));
extern const char* pc_lighting_preset_name(int) __attribute__((weak));
extern int pc_perf_json(char*, int, int) __attribute__((weak));
extern int pc_hw_perspective __attribute__((weak)), pc_gl_show_depth __attribute__((weak));
extern int pc_post_flags __attribute__((weak));
extern float pc_normal_smoothing __attribute__((weak));
extern int pc_hw_subpixel __attribute__((weak));
extern int pc_hw_interpolate __attribute__((weak));
extern int pc_interp_skeleton __attribute__((weak));
extern int pc_interp_holds __attribute__((weak));
extern int pc_interp_uv __attribute__((weak));
extern int pc_drop_shadow_mode __attribute__((weak));
extern int pc_editor_hide_masks __attribute__((weak)), pc_editor_game_cutaway __attribute__((weak));
extern int pc_editor_cutaway __attribute__((weak));
extern float pc_editor_cutaway_radius __attribute__((weak));
extern float pc_editor_cutaway_opacity __attribute__((weak));
extern int pc_editor_cutaway_auto __attribute__((weak));
extern int pc_editor_cutaway_state(int*, float*) __attribute__((weak));
extern int pc_gameplay_walls __attribute__((weak)), pc_gameplay_walls_ghost __attribute__((weak));
extern float pc_gameplay_walls_opacity __attribute__((weak)), pc_gameplay_walls_radius __attribute__((weak));
extern int pc_gameplay_walls_info(unsigned, float*, int*, int*) __attribute__((weak));
extern int pc_scene_triangles(void) __attribute__((weak));
extern int pc_lights_describe(char*, int) __attribute__((weak));
extern void pc_lights_suggest(void) __attribute__((weak));
extern void pc_lights_save(void) __attribute__((weak));
extern float pc_lights_baked __attribute__((weak));
extern int pc_scene_serial(void) __attribute__((weak));
extern int pc_scene_dump(const char*) __attribute__((weak));
extern void pc_scene_bounds(float*, float*) __attribute__((weak));
extern int pc_scene_camera_get(float (*)[3], float*, float*, float*, float*) __attribute__((weak));
extern void pc_rpc_state(char* out, unsigned size) __attribute__((weak)); /* dev_state.c */
extern int pc_qol_weapon_slots; /* project_settings.cpp */
extern void pc_weapon_slots_json(char* out, unsigned size) __attribute__((weak)); /* weapon_slots.c */
extern int pc_native_room __attribute__((weak)), pc_native_occlusion __attribute__((weak)),
    pc_native_actors __attribute__((weak)), pc_native_actor_order __attribute__((weak)),
    pc_native_blend __attribute__((weak)), pc_native_objects __attribute__((weak)),
    pc_native_shadows __attribute__((weak)), pc_native_effects __attribute__((weak)),
    pc_reconstruct __attribute__((weak));
extern int pc_gte_log __attribute__((weak));
extern void render_stats_get(render_stats*) __attribute__((weak));

#define MAX_CLIENTS 4
#define LINE_MAX_BYTES 262144 /* the rooms list and lighting layers fit */

static int listener = -1, initialized;
static struct client {
    int fd;
    size_t used;
    char buffer[LINE_MAX_BYTES];
} clients[MAX_CLIENTS];
static int paused;
static long step_budget;
static struct hold {
    unsigned mask;
    long vsyncs;
} holds[16];
static float stick_hold[2]; /* input {right_stick: [x, y]} */
static long stick_vsyncs;

/* ---- minimal JSON reading: enough for flat request objects ---- */

static const char* skip(const char* p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    return p;
}
/* Return the end of the JSON value starting at p, or NULL if malformed. */
static const char* value_end(const char* p)
{
    p = skip(p);
    if (*p == '"') {
        for (++p; *p && *p != '"'; ++p)
            if (*p == '\\' && p[1])
                ++p;
        return *p == '"' ? p + 1 : NULL;
    }
    if (*p == '{' || *p == '[') {
        char open = *p, close = open == '{' ? '}' : ']';
        int depth = 0;
        for (; *p; ++p) {
            if (*p == '"') {
                p = value_end(p);
                if (!p)
                    return NULL;
                --p;
            } else if (*p == open)
                ++depth;
            else if (*p == close && --depth == 0)
                return p + 1;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n')
        ++p;
    return p;
}
/* Find key in the object starting at obj; returns the value start or NULL. */
static const char* member(const char* obj, const char* key)
{
    const char* p = skip(obj);
    if (*p != '{')
        return NULL;
    p = skip(p + 1);
    size_t key_len = strlen(key);
    while (*p == '"') {
        const char* name = p + 1;
        const char* name_end = value_end(p);
        if (!name_end)
            return NULL;
        p = skip(name_end);
        if (*p != ':')
            return NULL;
        const char* value = skip(p + 1);
        if ((size_t)(name_end - 1 - name) == key_len && !strncmp(name, key, key_len))
            return value;
        p = value_end(value);
        if (!p)
            return NULL;
        p = skip(p);
        if (*p == ',')
            p = skip(p + 1);
    }
    return NULL;
}
static int string_value(const char* v, char* out, size_t size)
{
    if (!v || *v != '"')
        return 0;
    size_t n = 0;
    for (++v; *v && *v != '"' && n + 1 < size; ++v)
        out[n++] = *v == '\\' && v[1] ? *++v : *v;
    out[n] = 0;
    return 1;
}
static int real_value(const char* v, double* out) { return v && sscanf(v, "%lf", out) == 1; }
static int number_value(const char* v, long long* out)
{
    if (!v)
        return 0;
    char* end;
    errno = 0;
    long long n = strtoll(v, &end, 0);
    if (end == v || errno)
        return 0;
    *out = n;
    return 1;
}
static int bool_value(const char* v, int* out)
{
    if (v && !strncmp(v, "true", 4))
        return *out = 1, 1;
    if (v && !strncmp(v, "false", 5))
        return *out = 0, 1;
    return 0;
}

/* ---- responses ---- */

static void send_line(int fd, const char* text)
{
    size_t length = strlen(text);
    while (length) {
        ssize_t n = write(fd, text, length);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return;
        text += n;
        length -= (size_t)n;
    }
}
static void reply(int fd, const char* id, const char* result)
{
    static char line[LINE_MAX_BYTES + 256];
    snprintf(line, sizeof line, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}\n", id, result);
    send_line(fd, line);
}
static void fail(int fd, const char* id, int code, const char* message)
{
    char line[1024];
    snprintf(line, sizeof line,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":%d,\"message\":\"%s\"}}\n", id, code,
        message);
    send_line(fd, line);
}

/* ---- methods ---- */

static const struct {
    const char* name;
    unsigned bit;
} button_names[] = {
    { "l2", 1u << 0 }, { "r2", 1u << 1 }, { "l1", 1u << 2 }, { "r1", 1u << 3 },
    { "triangle", 1u << 4 }, { "circle", 1u << 5 }, { "cross", 1u << 6 }, { "square", 1u << 7 },
    { "select", 1u << 8 }, { "l3", 1u << 9 }, { "r3", 1u << 10 }, { "start", 1u << 11 },
    { "up", 1u << 12 }, { "right", 1u << 13 }, { "down", 1u << 14 }, { "left", 1u << 15 },
};
static int parse_buttons(const char* v, unsigned* mask)
{
    char name[32];
    *mask = 0;
    if (!v)
        return 0;
    if (*v == '"') {
        string_value(v, name, sizeof name);
        for (size_t i = 0; i < sizeof button_names / sizeof *button_names; i++)
            if (!strcmp(name, button_names[i].name))
                return *mask = button_names[i].bit, 1;
        return 0;
    }
    if (*v != '[')
        return 0;
    const char* p = skip(v + 1);
    while (*p == '"') {
        unsigned bit;
        if (!parse_buttons(p, &bit))
            return 0;
        *mask |= bit;
        p = skip(value_end(p));
        if (*p == ',')
            p = skip(p + 1);
    }
    return *p == ']';
}

/* The right stick the RPC holds (camera_stick.c), -1..1 (down is +y). */
int pc_rpc_stick(float out[2])
{
    if (stick_vsyncs <= 0)
        return 0;
    out[0] = stick_hold[0];
    out[1] = stick_hold[1];
    return 1;
}
/* [x, y] numbers. */
static int pair_value(const char* v, double out[2])
{
    if (!v || *v != '[')
        return 0;
    const char* p = skip(v + 1);
    for (int i = 0; i < 2; i++) {
        if (!real_value(p, &out[i]))
            return 0;
        p = value_end(p);
        if (!p)
            return 0;
        p = skip(p);
        if (i == 0) {
            if (*p != ',')
                return 0;
            p = skip(p + 1);
        }
    }
    return *p == ']';
}

unsigned pc_rpc_buttons(void)
{
    unsigned mask = 0;
    for (int i = 0; i < 16; i++)
        if (holds[i].vsyncs > 0)
            mask |= holds[i].mask;
    return mask;
}

/* Readable ranges: guest RAM (and its KSEG0 view), scratchpad, host data. */
extern char __data_start[], _end[];
static int readable(unsigned long address, unsigned long length)
{
    unsigned long end = address + length;
    if (length > 65536 || end < address)
        return 0;
    if (address >= 0x80000000ul && end <= 0x80200000ul)
        return 1;
    if (address >= 0x1F800000ul && end <= 0x1F800400ul)
        return 1;
    return address >= (unsigned long)__data_start && end <= (unsigned long)_end;
}
static int writable(unsigned long address, unsigned long length)
{
    unsigned long end = address + length;
    return length <= 65536 && end >= address
        && ((address >= 0x80000000ul && end <= 0x80200000ul)
            || (address >= 0x1F800000ul && end <= 0x1F800400ul));
}

static void handle_method(int fd, const char* request)
{
    char id[64] = "null", method[64], text[512];
    const char* idv = member(request, "id");
    if (idv) {
        const char* e = value_end(idv);
        if (e && (size_t)(e - idv) < sizeof id) {
            memcpy(id, idv, (size_t)(e - idv));
            id[e - idv] = 0;
        }
    }
    if (!string_value(member(request, "method"), method, sizeof method)) {
        fail(fd, id, -32600, "invalid request");
        return;
    }
    const char* params = member(request, "params");
    long long n;
    static char result[LINE_MAX_BYTES];

    if (!strcmp(method, "status")) {
        extern int pc_display_masked __attribute__((weak)); /* SetDispMask(0) */
        snprintf(result, sizeof result,
            "{\"vsync\":%u,\"battle_frame\":%u,\"paused\":%s,\"step_budget\":%ld,\"buttons\":%u,"
            "\"display_masked\":%s}",
            pc_vsync_count(), pc_battle_frame_count(), paused ? "true" : "false", step_budget,
            pc_rpc_buttons(), &pc_display_masked && pc_display_masked ? "true" : "false");
        reply(fd, id, result);
    } else if (!strcmp(method, "pause")) {
        int value = 1;
        if (params)
            bool_value(member(params, "paused"), &value);
        paused = value;
        step_budget = 0;
        reply(fd, id, paused ? "{\"paused\":true}" : "{\"paused\":false}");
    } else if (!strcmp(method, "step")) {
        if (!params || !number_value(member(params, "vsyncs"), &n) || n < 1 || n > 100000) {
            fail(fd, id, -32602, "step needs vsyncs 1..100000");
            return;
        }
        paused = 1;
        step_budget = (long)n;
        snprintf(result, sizeof result, "{\"until_vsync\":%u}", pc_vsync_count() + (unsigned)n);
        reply(fd, id, result);
    } else if (!strcmp(method, "input")) {
        unsigned mask;
        double stick[2];
        int has_stick = params && pair_value(member(params, "right_stick"), stick);
        n = 2;
        if (params)
            number_value(member(params, "vsyncs"), &n);
        if (has_stick) {
            /* The right stick for N vsyncs (the camera, camera_stick.c);
             * buttons are optional with it. */
            if (n < 1) {
                fail(fd, id, -32602, "bad vsyncs");
                return;
            }
            for (int k = 0; k < 2; k++)
                stick_hold[k] = (float)(stick[k] < -1 ? -1 : stick[k] > 1 ? 1 : stick[k]);
            stick_vsyncs = (long)n;
            if (!member(params, "buttons")) {
                snprintf(result, sizeof result, "{\"right_stick\":[%g,%g],\"vsyncs\":%lld}", stick_hold[0],
                    stick_hold[1], n);
                reply(fd, id, result);
                return;
            }
        }
        if (!params || !parse_buttons(member(params, "buttons"), &mask)) {
            fail(fd, id, -32602, "input needs buttons (name or list of names) or right_stick [x, y]");
            return;
        }
        int slot = 0;
        while (slot < 16 && holds[slot].vsyncs > 0)
            ++slot;
        if (slot == 16 || n < 1) {
            fail(fd, id, -32000, "too many held inputs or bad vsyncs");
            return;
        }
        holds[slot].mask = mask;
        holds[slot].vsyncs = (long)n;
        snprintf(result, sizeof result, "{\"mask\":%u,\"vsyncs\":%lld}", mask, n);
        reply(fd, id, result);
    } else if (!strcmp(method, "camera_stick")) {
        /* The right-stick camera (camera_stick.c): settings and state. */
        static const struct {
            const char* key;
            int* ints;
            float* floats;
        } keys[] = {
            { "enabled", &pc_camera_stick, NULL }, { "speed", NULL, &pc_camera_stick_speed },
            { "deadzone", NULL, &pc_camera_stick_deadzone }, { "curve", NULL, &pc_camera_stick_curve },
            { "invert_x", &pc_camera_stick_invert_x, NULL }, { "invert_y", &pc_camera_stick_invert_y, NULL },
            { "zoom", &pc_camera_stick_zoom, NULL }, { "zoom_speed", NULL, &pc_camera_stick_zoom_speed },
            { "zoom_min", NULL, &pc_camera_stick_zoom_min }, { "zoom_max", NULL, &pc_camera_stick_zoom_max },
            { "zoom_pitch", NULL, &pc_camera_stick_zoom_pitch },
            { "collision", &pc_camera_stick_collision, NULL }, { "r3_reset", &pc_camera_stick_r3, NULL },
            { "idle_reset", NULL, &pc_camera_stick_idle_reset }, { "keys", &pc_camera_stick_keys, NULL },
        };
        for (size_t i = 0; params && i < sizeof keys / sizeof *keys; i++) {
            const char* v = member(params, keys[i].key);
            double real;
            int flag;
            if (!v)
                continue;
            if (keys[i].ints && bool_value(v, &flag))
                *keys[i].ints = flag;
            else if (real_value(v, &real)) {
                if (keys[i].ints)
                    *keys[i].ints = (int)real;
                else
                    *keys[i].floats = (float)real;
            }
        }
        char action[32] = "";
        if (params)
            string_value(member(params, "action"), action, sizeof action);
        if (!strcmp(action, "reset"))
            pc_camera_stick_reset();
        pc_camera_stick_json(result, sizeof result);
        reply(fd, id, result);
    } else if (!strcmp(method, "weapon_slots")) {
        /* The QoL weapon slots (weapon_slots.c): the option and the state. */
        int flag;
        if (!pc_weapon_slots_json) {
            fail(fd, id, -32000, "weapon_slots needs the battle build");
            return;
        }
        if (params && bool_value(member(params, "enabled"), &flag))
            pc_qol_weapon_slots = flag;
        pc_weapon_slots_json(result, sizeof result);
        reply(fd, id, result);
    } else if (!strcmp(method, "release")) {
        memset(holds, 0, sizeof holds);
        stick_vsyncs = 0;
        reply(fd, id, "{}");
    } else if (!strcmp(method, "screenshot")) {
        char path[400];
        snprintf(path, sizeof path, "%s", pc_out_path("rpc-screenshot.ppm"));
        int width, height;
        if (params)
            string_value(member(params, "path"), path, sizeof path);
        if (pc_capture_display(path, &width, &height)) {
            fail(fd, id, -32000, "cannot write screenshot");
            return;
        }
        snprintf(result, sizeof result, "{\"path\":\"%s\",\"width\":%d,\"height\":%d}", path, width,
            height);
        reply(fd, id, result);
    } else if (!strcmp(method, "graphics")) {
        static const char* const denoiser_names[3] = { "raw", "filter", "nrd" };
        if (!&pc_gl_filter || !&pc_hw_scale) {
            fail(fd, id, -32000, "no OpenGL presentation");
            return;
        }
        extern int pc_present_rate __attribute__((weak));
        if (params && number_value(member(params, "present_rate"), &n) && &pc_present_rate && n >= 1 && n <= 8)
            pc_present_rate = (int)n;
        if (params && number_value(member(params, "scale"), &n) && pc_hw_set_scale)
            pc_hw_set_scale((int)n);
        if (params && number_value(member(params, "filter"), &n) && n >= 0 && n <= 2)
            pc_gl_filter = (int)n;
        if (params && number_value(member(params, "aspect"), &n) && n >= 0 && n <= 2)
            pc_gl_aspect = (int)n;
        int flag;
        if (params && bool_value(member(params, "widescreen"), &flag))
            pc_gte_widescreen = flag;
        /* Widescreen fixes, off for before/after comparisons (gpu_hw.c). */
        extern int pc_widescreen_cover __attribute__((weak)), pc_widescreen_layout __attribute__((weak));
        if (params && bool_value(member(params, "widescreen_cover"), &flag) && &pc_widescreen_cover)
            pc_widescreen_cover = flag;
        if (params && bool_value(member(params, "widescreen_layout"), &flag) && &pc_widescreen_layout)
            pc_widescreen_layout = flag;
        extern int pc_widescreen_trace __attribute__((weak));
        if (params && number_value(member(params, "widescreen_trace"), &n) && &pc_widescreen_trace && n >= 0)
            pc_widescreen_trace = (int)n + 1; /* log the next N ordering tables' 2D */
        if (params && bool_value(member(params, "perspective"), &flag) && &pc_hw_perspective)
            pc_hw_perspective = flag;
        if (params && bool_value(member(params, "show_depth"), &flag) && &pc_gl_show_depth)
            pc_gl_show_depth = flag;
        if (params && bool_value(member(params, "subpixel"), &flag) && &pc_hw_subpixel)
            pc_hw_subpixel = flag;
        if (params && bool_value(member(params, "interpolate"), &flag) && &pc_hw_interpolate)
            pc_hw_interpolate = flag;
        if (params && bool_value(member(params, "interp_skeleton"), &flag) && &pc_interp_skeleton)
            pc_interp_skeleton = flag;
        if (params && bool_value(member(params, "interp_holds"), &flag) && &pc_interp_holds)
            pc_interp_holds = flag;
        if (params && bool_value(member(params, "uv_effects"), &flag))
            pc_uv_effects_enabled = flag;
        if (params && &pc_interp_uv) {
            if (bool_value(member(params, "interp_uv"), &flag))
                pc_interp_uv = flag;
            else if (number_value(member(params, "interp_uv"), &n) && n >= 0 && n <= 2)
                pc_interp_uv = (int)n;
        }
        /* aa: true/false (FXAA/off, as before) or "msaa4+smaa" ... (antialias.h). */
        if (params && bool_value(member(params, "aa"), &flag) && &pc_gl_aa) {
            pc_gl_aa = flag ? 1 : 0;
        } else if (params && pc_aa_parse) {
            char text[64];
            if (string_value(member(params, "aa"), text, sizeof text) && !pc_aa_parse(text)) {
                fail(fd, id, -32602, "aa: off, fxaa, smaa, msaa2/4/8, ssaa[factor], joined by '+'");
                return;
            }
        }
        if (params && &pc_aa_downsample) {
            char text[32];
            if (string_value(member(params, "aa_downsample"), text, sizeof text))
                for (int k = 0; k < 3; k++)
                    if (!strcmp(text, pc_aa_downsample_names[k]))
                        pc_aa_downsample = k;
            if (bool_value(member(params, "aa_geometry_edges"), &flag))
                pc_aa_geometry_edges = flag;
        }
        if (params && bool_value(member(params, "finish"), &flag) && &pc_gl_finish)
            pc_gl_finish = flag;
        if (params && bool_value(member(params, "room_depth"), &flag) && &pc_room_exact_depth)
            pc_room_exact_depth = flag;
        if (params && bool_value(member(params, "baked_estimate"), &flag) && &pc_baked_estimate)
            pc_baked_estimate = flag;
        if (params && bool_value(member(params, "glow_shadows"), &flag) && &pc_glow_shadows)
            pc_glow_shadows = flag;
        if (params && number_value(member(params, "view_mode"), &n) && &pc_gl_view_mode)
            pc_gl_view_mode = (int)n;
        if (params && bool_value(member(params, "texture_filter"), &flag) && &pc_hw_texture_filter)
            pc_hw_texture_filter = flag;
        /* 2D / UI (ui_upscale.h): ui_upscale "nearest" | "mmpx" (or 0/1), ui_subpixel. */
        if (params) {
            char name[16];
            if (string_value(member(params, "ui_upscale"), name, sizeof name)) {
                for (int k = 0; k < 2; k++)
                    if (!strcmp(name, pc_ui_upscale_names[k]))
                        pc_ui_upscale = k;
            } else if (number_value(member(params, "ui_upscale"), &n) && n >= 0 && n <= 1)
                pc_ui_upscale = (int)n;
            if (bool_value(member(params, "ui_subpixel"), &flag))
                pc_ui_subpixel = flag;
            else if (number_value(member(params, "ui_subpixel"), &n))
                pc_ui_subpixel = n != 0;
            /* ui_text "original" | "font" (or 0/1; ui_text.h). */
            if (string_value(member(params, "ui_text"), name, sizeof name)) {
                for (int k = 0; k < 2; k++)
                    if (!strcmp(name, pc_ui_text_names[k]))
                        pc_ui_text = k;
            } else if (number_value(member(params, "ui_text"), &n) && n >= 0 && n <= 1)
                pc_ui_text = (int)n;
        }
        if (&pc_gi_enabled && params) {
            double real;
            if (bool_value(member(params, "gi"), &flag))
                pc_gi_enabled = flag;
            if (number_value(member(params, "gi_debug"), &n))
                pc_gi_debug = (int)n;
            else if (bool_value(member(params, "gi_debug"), &flag))
                pc_gi_debug = flag;
            if (bool_value(member(params, "gi_denoise"), &flag))
                pc_gi_denoise = flag ? 2 : 0;
            char name[16];
            if (string_value(member(params, "gi_denoiser"), name, sizeof name)) {
                if (!strcmp(name, "nrd"))
                    pc_gi_denoise = 2;
                else if (!strcmp(name, "filter"))
                    pc_gi_denoise = 1;
                else if (!strcmp(name, "raw"))
                    pc_gi_denoise = 0;
            } else if (number_value(member(params, "gi_denoiser"), &n) && n >= 0 && n <= 2)
                pc_gi_denoise = (int)n;
            if (&pc_nrd_glow && bool_value(member(params, "nrd_glow"), &flag))
                pc_nrd_glow = flag;
            if (&pc_nrd_max_frames && number_value(member(params, "nrd_frames"), &n) && n >= 1 && n <= 255)
                pc_nrd_max_frames = (int)n;
            extern char pc_nrd_tune[160] __attribute__((weak));
            if (pc_nrd_tune)
                string_value(member(params, "nrd_tune"), pc_nrd_tune, 160);
            if (number_value(member(params, "gi_bounces"), &n))
                pc_gi_bounces = (int)n;
            if (bool_value(member(params, "rt_shadows"), &flag))
                pc_rt_shadows = flag;
            if (bool_value(member(params, "baked_shadows"), &flag))
                pc_baked_shadows = flag;
            if (number_value(member(params, "shadow_samples"), &n))
                pc_shadow_samples = (int)n;
            if (real_value(member(params, "emitter_light"), &real))
                pc_emitter_light = (float)real;
            if (real_value(member(params, "contact_radius"), &real))
                pc_contact_radius = (float)real;
            if (real_value(member(params, "fog_density"), &real))
                pc_fog_density = (float)real;
            if (number_value(member(params, "gi_trace_debug"), &n))
                pc_gi_trace_debug = (int)n;
            if (number_value(member(params, "gi_rays"), &n))
                pc_gi_rays = (int)n;
            if (real_value(member(params, "gi_strength"), &real))
                pc_gi_strength = (float)real;
            if (real_value(member(params, "gi_resolution"), &real))
                pc_gi_resolution = (float)real;
            extern float pc_gi_budget_ms __attribute__((weak));
            if (&pc_gi_budget_ms && real_value(member(params, "gi_budget"), &real))
                pc_gi_budget_ms = real < 0 ? -1.0f : (float)real;
            if (real_value(member(params, "gi_baked_bounce"), &real))
                pc_gi_baked_bounce = (float)real;
        }
        if (params && &pc_probe_spacing) {
            double real;
            if (real_value(member(params, "probe_hysteresis"), &real) && real >= 0 && real < 1)
                pc_probe_hysteresis = (float)real;
            if (number_value(member(params, "probe_budget"), &n) && n >= 8 && n <= 16384)
                pc_probe_budget = (int)n;
            if (number_value(member(params, "probe_show"), &n))
                pc_probe_show = (int)n;
            else if (bool_value(member(params, "probe_show"), &flag))
                pc_probe_show = flag;
        }
        if (params && number_value(member(params, "post"), &n) && &pc_post_flags)
            pc_post_flags = (int)n & 7;
        double value;
        const char* v = params ? member(params, "normal_smoothing") : NULL;
        if (v && sscanf(v, "%lf", &value) == 1 && &pc_normal_smoothing)
            pc_normal_smoothing = (float)value;
        if (params && number_value(member(params, "drop_shadows"), &n) && n >= 0 && n <= 3 && &pc_drop_shadow_mode)
            pc_drop_shadow_mode = (int)n;
        if (params && &pc_native_room) {
            if (number_value(member(params, "native_room"), &n) && n >= 0 && n <= 2)
                pc_native_room = (int)n;
            else if (bool_value(member(params, "native_room"), &flag))
                pc_native_room = flag;
            if (bool_value(member(params, "native_occlusion"), &flag))
                pc_native_occlusion = flag;
        }
        if (params && &pc_native_actors) {
            if (number_value(member(params, "native_actors"), &n) && n >= 0 && n <= 2)
                pc_native_actors = (int)n;
            else if (bool_value(member(params, "native_actors"), &flag))
                pc_native_actors = flag;
            if (bool_value(member(params, "actor_order"), &flag) && &pc_native_actor_order)
                pc_native_actor_order = flag;
            else if (number_value(member(params, "actor_order"), &n) && &pc_native_actor_order)
                pc_native_actor_order = n != 0;
        }
        if (params && &pc_native_blend) {
            if (bool_value(member(params, "native_blend"), &flag))
                pc_native_blend = flag;
            else if (number_value(member(params, "native_blend"), &n))
                pc_native_blend = n != 0;
            if (bool_value(member(params, "native_objects"), &flag))
                pc_native_objects = flag;
            else if (number_value(member(params, "native_objects"), &n))
                pc_native_objects = n != 0;
        }
        if (params && &pc_native_shadows) {
            if (bool_value(member(params, "native_shadows"), &flag))
                pc_native_shadows = flag;
            else if (number_value(member(params, "native_shadows"), &n))
                pc_native_shadows = n != 0;
            if (bool_value(member(params, "native_effects"), &flag))
                pc_native_effects = flag;
            else if (number_value(member(params, "native_effects"), &n))
                pc_native_effects = n != 0;
            if (bool_value(member(params, "reconstruct"), &flag))
                pc_reconstruct = flag;
            else if (number_value(member(params, "reconstruct"), &n))
                pc_reconstruct = n != 0;
        }
        /* Smart cutaway walls in gameplay (gameplay_walls.h). */
        if (params && &pc_gameplay_walls) {
            char word[32] = "";
            if (string_value(member(params, "gameplay_walls"), word, sizeof word)) {
                if (!strcmp(word, "original"))
                    pc_gameplay_walls = 0;
                else if (!strcmp(word, "smart"))
                    pc_gameplay_walls = 1;
            } else if (bool_value(member(params, "gameplay_walls"), &flag))
                pc_gameplay_walls = flag;
            else if (number_value(member(params, "gameplay_walls"), &n))
                pc_gameplay_walls = n != 0;
            if (bool_value(member(params, "gameplay_walls_ghost"), &flag))
                pc_gameplay_walls_ghost = flag;
            else if (number_value(member(params, "gameplay_walls_ghost"), &n))
                pc_gameplay_walls_ghost = n != 0;
            double real;
            if (real_value(member(params, "gameplay_walls_opacity"), &real))
                pc_gameplay_walls_opacity = (float)real;
            if (real_value(member(params, "gameplay_walls_radius"), &real))
                pc_gameplay_walls_radius = (float)real;
        }
        /* The editor's view into rooms (free camera only; room_editor.h). */
        if (params && &pc_editor_cutaway) {
            int* switches[] = { &pc_editor_hide_masks, &pc_editor_game_cutaway, &pc_editor_cutaway };
            const char* names[] = { "editor_hide_masks", "editor_game_cutaway", "editor_cutaway" };
            for (int k = 0; k < 3; k++) {
                if (bool_value(member(params, names[k]), &flag))
                    *switches[k] = flag;
                else if (number_value(member(params, names[k]), &n))
                    *switches[k] = n != 0;
            }
            double radius;
            if (real_value(member(params, "editor_cutaway_radius"), &radius))
                pc_editor_cutaway_radius = (float)radius;
            if (&pc_editor_cutaway_opacity && real_value(member(params, "editor_cutaway_opacity"), &radius))
                pc_editor_cutaway_opacity = (float)radius;
            if (&pc_editor_cutaway_auto) {
                if (bool_value(member(params, "editor_cutaway_auto"), &flag))
                    pc_editor_cutaway_auto = flag;
                else if (number_value(member(params, "editor_cutaway_auto"), &n))
                    pc_editor_cutaway_auto = n != 0;
            }
        }
        /* The editor viewport's overlays and snapping (editor_overlays.h). */
        struct {
            const char* name;
            int* flag;
            float* value;
        } editor_keys[] = {
            { "editor_wireframe", &pc_overlay_wire, NULL },
            { "editor_points", &pc_overlay_points, NULL },
            { "editor_character_wireframe", &pc_overlay_actor_wire, NULL },
            { "editor_snap_show", &pc_overlay_snap_show, NULL },
            { "editor_occluded_opacity", NULL, &pc_overlay_occluded },
            { "editor_snap_grid", &pc_lights_snap, NULL },
            { "editor_snap_move", NULL, &pc_lights_snap_move },
            { "editor_snap_point", &pc_snap_point, NULL },
            { "editor_snap_edge", &pc_snap_edge, NULL },
            { "editor_snap_midpoint", &pc_snap_midpoint, NULL },
            { "editor_snap_surface", &pc_snap_surface, NULL },
            { "editor_snap_radius", NULL, &pc_snap_radius },
            { "editor_snap_highlight", NULL, &pc_snap_highlight },
            { "editor_snap_offset", NULL, &pc_snap_offset },
        };
        int editor_count = &pc_overlay_wire ? (int)(sizeof editor_keys / sizeof editor_keys[0]) : 0;
        /* Lighting quality (lighting_quality.h): the preset first, then any
         * key (changing a key makes the preset Custom). */
        if (params && pc_lighting_set) {
            for (int k = -1; k < pc_lighting_key_count(); k++) {
                const char* name = k < 0 ? "lighting_preset" : pc_lighting_key_name(k);
                const char* v = member(params, name);
                char text[64];
                if (!v || (*v == '"' ? !string_value(v, text, sizeof text) : 0))
                    continue;
                if (*v != '"') {
                    size_t n = 0;
                    while (v[n] && v[n] != ',' && v[n] != '}' && v[n] != ' ' && n + 1 < sizeof text)
                        text[n] = v[n], n++;
                    text[n] = 0;
                }
                if (!pc_lighting_set(name, text)) {
                    char message[160];
                    snprintf(message, sizeof message, "bad value for %s: %s", name, text);
                    fail(fd, id, -32602, message);
                    return;
                }
            }
        }
        if (pc_lighting_sync)
            pc_lighting_sync();
        for (int k = 0; k < editor_count && params; k++) {
            double real;
            if (editor_keys[k].flag && bool_value(member(params, editor_keys[k].name), &flag))
                *editor_keys[k].flag = flag;
            else if (editor_keys[k].flag && number_value(member(params, editor_keys[k].name), &n))
                *editor_keys[k].flag = n != 0;
            else if (editor_keys[k].value && real_value(member(params, editor_keys[k].name), &real))
                *editor_keys[k].value = (float)real;
        }
        /* The game's glows by category (glows.c, room settings of the same
         * names): glow_<fixtures|magic|effects>_<enabled|intensity|reach|
         * shadows|smoothing> (effects' shadows stay off). */
        static const char* glow_kinds[3] = { "fixtures", "magic", "effects" };
        static const char* glow_fields[5] = { "enabled", "intensity", "reach", "shadows", "smoothing" };
        extern float pc_glow_category[3][5];
        for (int c = 0; c < 3 && params; c++)
            for (int f = 0; f < 5; f++) {
                char key[48];
                double real;
                snprintf(key, sizeof key, "glow_%s_%s", glow_kinds[c], glow_fields[f]);
                if (c == 2 && f == 3)
                    continue; /* transient effects never cast shadows */
                if (bool_value(member(params, key), &flag))
                    pc_glow_category[c][f] = flag ? 1.0f : 0.0f;
                else if (real_value(member(params, key), &real))
                    pc_glow_category[c][f] = (float)real;
            }
        int used = snprintf(result, sizeof result,
            "{\"hardware\":%s,\"scale\":%d,\"filter\":%d,\"aspect\":%d,\"widescreen\":%s,"
            "\"perspective\":%s,\"show_depth\":%s,\"native_room\":%d,\"native_occlusion\":%s,"
            "\"native_actors\":%d,\"actor_order\":%s,\"native_blend\":%d,\"native_objects\":%d,"
            "\"native_shadows\":%d,\"native_effects\":%d,\"reconstruct\":%d,\"gte_log\":%d,"
            "\"gi_denoiser\":\"%s\",\"gi_denoiser_active\":\"%s\",\"nrd_built\":%s,\"nrd_available\":%s,"
            "\"nrd_glow\":%s}",
            pc_hw_active ? "true" : "false", pc_hw_scale, pc_gl_filter, pc_gl_aspect,
            pc_gte_widescreen ? "true" : "false", pc_hw_perspective ? "true" : "false",
            pc_gl_show_depth ? "true" : "false", &pc_native_room ? pc_native_room : 0,
            &pc_native_occlusion && pc_native_occlusion ? "true" : "false", &pc_native_actors ? pc_native_actors : 0,
            &pc_native_actor_order && pc_native_actor_order ? "true" : "false",
            &pc_native_blend ? pc_native_blend : 0, &pc_native_objects ? pc_native_objects : 0,
            &pc_native_shadows ? pc_native_shadows : 0, &pc_native_effects ? pc_native_effects : 0,
            &pc_reconstruct ? pc_reconstruct : 1, &pc_gte_log ? pc_gte_log : 1,
            denoiser_names[&pc_gi_denoise ? (pc_gi_denoise < 0 ? 0 : pc_gi_denoise > 2 ? 2 : pc_gi_denoise) : 1],
            denoiser_names[pc_gi_denoiser_active ? pc_gi_denoiser_active() : 1],
            vk_nrd_built && vk_nrd_built() ? "true" : "false", vk_nrd_available && vk_nrd_available() ? "true" : "false",
            &pc_nrd_glow && pc_nrd_glow ? "true" : "false");
        if (&pc_gameplay_walls && used < (int)sizeof result - 200)
            used += snprintf(result + used - 1, sizeof result - used + 1,
                        ",\"gameplay_walls\":\"%s\",\"gameplay_walls_ghost\":%d,\"gameplay_walls_opacity\":%g,"
                        "\"gameplay_walls_radius\":%g}",
                        pc_gameplay_walls ? "smart" : "original", pc_gameplay_walls_ghost, pc_gameplay_walls_opacity,
                        pc_gameplay_walls_radius)
                - 1;
        if (&pc_probe_spacing && used < (int)sizeof result - 200)
            used += snprintf(result + used - 1, sizeof result - used + 1,
                        ",\"probe_hysteresis\":%g,\"probe_budget\":%d,\"probe_show\":%d}",
                        pc_probe_hysteresis, pc_probe_budget, pc_probe_show)
                - 1;
        if (used < (int)sizeof result - 100)
            used += snprintf(result + used - 1, sizeof result - used + 1, ",\"ui_upscale\":\"%s\",\"ui_subpixel\":%d,\"ui_text\":\"%s\"}",
                        pc_ui_upscale_names[pc_ui_upscale ? 1 : 0], pc_ui_subpixel, pc_ui_text_names[pc_ui_text ? 1 : 0])
                - 1;
        extern int pc_hw_scale_setting __attribute__((weak));
        if (pc_aa_string && used < (int)sizeof result - 200)
            used += snprintf(result + used - 1, sizeof result - used + 1,
                        ",\"aa\":\"%s\",\"aa_downsample\":\"%s\",\"aa_geometry_edges\":%d,\"msaa_active\":%d,\"msaa_max\":%d,"
                        "\"scale_setting\":%d}",
                        pc_aa_string(), pc_aa_downsample_names[pc_aa_downsample], pc_aa_geometry_edges,
                        pc_aa_msaa_active ? pc_aa_msaa_active() : 0, pc_aa_msaa_max ? pc_aa_msaa_max() : 0, &pc_hw_scale_setting ? pc_hw_scale_setting : 0)
                - 1;
        for (int k = 0; k < editor_count && used < (int)sizeof result - 64; k++)
            used += editor_keys[k].flag
                ? snprintf(result + used - 1, sizeof result - used + 1, ",\"%s\":%d}", editor_keys[k].name,
                      *editor_keys[k].flag) - 1
                : snprintf(result + used - 1, sizeof result - used + 1, ",\"%s\":%g}", editor_keys[k].name,
                      *editor_keys[k].value) - 1;
        for (int c = 0; c < 3; c++)
            for (int f = 0; f < 5 && used < (int)sizeof result - 64; f++)
                used += snprintf(result + used - 1, sizeof result - used + 1, ",\"glow_%s_%s\":%g}", glow_kinds[c],
                            glow_fields[f], c == 2 && f == 3 ? 0.0 : pc_glow_category[c][f])
                    - 1;
        if (pc_lighting_json && used < (int)sizeof result - 4096) {
            used += pc_lighting_json(result + used - 1, (int)sizeof result - used) - 1;
            snprintf(result + used, sizeof result - used, "}");
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "ui_text")) {
        /* ui_text.h: the option, its styles (font, placement), fonts loaded,
         * glyph fields made and regions drawn; {style, font, stretch, slant,
         * embolden} changes a style (as the settings keys do). */
        char style[32], text[300];
        if (params && string_value(member(params, "style"), style, sizeof style)) {
            static const char* const keys[4] = { "font", "stretch", "slant", "embolden" };
            int known = 0;
            for (int i = 0; i < PC_UI_TEXT_STYLES; i++)
                known |= !strcmp(style, pc_ui_text_styles[i].name);
            if (!known) {
                fail(fd, id, -32602, "style: dialogue, menu, info or digits");
                return;
            }
            for (int k = 0; k < 4; k++) {
                char key[64];
                double value;
                snprintf(key, sizeof key, "ui_text_%s_%s", style, keys[k]);
                if (k == 0 && string_value(member(params, "font"), text, sizeof text))
                    pc_ui_text_setting(key, text);
                else if (k && real_value(member(params, keys[k]), &value)) {
                    snprintf(text, sizeof text, "%.6g", value);
                    pc_ui_text_setting(key, text);
                }
            }
        }
        if (params && string_value(member(params, "ui_text"), style, sizeof style))
            pc_ui_text_setting("ui_text", style);
        pc_ui_text_json(result, (int)sizeof result);
        reply(fd, id, result);
    } else if (!strcmp(method, "ui_upscale")) {
        /* 2D / UI magnification statistics; {verify: true} also reads the
         * atlas back and checks it against the CPU reference of MMPX. */
        int flag = 0;
        long mismatches = -1, foreign = -1;
        int checked = -1;
        if (params && bool_value(member(params, "verify"), &flag) && flag)
            checked = pc_ui_upscale_verify(&mismatches, &foreign);
        if (params && bool_value(member(params, "reset"), &flag) && flag)
            pc_ui_upscale_reset();
        char path[400];
        int area[4] = { 0, 0, 0, 0 };
        /* {poke: [x, y, w, h, value]}: native VRAM rectangle set to `value`
         * and uploaded as LoadImage would (tests: a texture page changing
         * under the cache). */
        const char* poke = params ? member(params, "poke") : NULL;
        if (poke) {
            int r[5];
            if (sscanf(poke, " [ %d , %d , %d , %d , %d ]", &r[0], &r[1], &r[2], &r[3], &r[4]) != 5 || r[0] < 0
                || r[1] < 0 || r[2] < 1 || r[3] < 1 || r[0] + r[2] > 1024 || r[1] + r[3] > 512) {
                fail(fd, id, -32602, "poke needs [x, y, w, h, value] inside VRAM");
                return;
            }
            extern const uint16_t* pc_hw_native_vram(void);
            extern void pc_hw_upload(int x, int y, int w, int h);
            uint16_t* vram = (uint16_t*)(uintptr_t)pc_hw_native_vram();
            for (int y = r[1]; vram && y < r[1] + r[3]; y++)
                for (int x = r[0]; x < r[0] + r[2]; x++)
                    vram[y * 1024 + x] = (uint16_t)r[4];
            pc_hw_upload(r[0], r[1], r[2], r[3]);
        }
        /* {mask: PATH}: where magnified 2D was drawn since the last mask
         * (1024 x 512 PGM). */
        if (params && string_value(member(params, "mask"), path, sizeof path) && pc_ui_upscale_mask(path)) {
            fail(fd, id, -32000, "cannot write the mask");
            return;
        }
        /* {redraw: PATH}: the newest frame drawn again as recorded (with the
         * current options) and the scaled target written there (PPM). */

        if (params && string_value(member(params, "redraw"), path, sizeof path)) {
            extern int pc_hw_redraw(int area[4]);
            if (pc_hw_redraw(area) || !pc_hw_dump || pc_hw_dump(path, 1)) {
                fail(fd, id, -32000, "nothing recorded to redraw");
                return;
            }
        }
        pc_ui_upscale_stats st;
        pc_ui_upscale_get_stats(&st);
        extern void pc_hw_display_rect(int r[4]);
        int display[4];
        pc_hw_display_rect(display);
        snprintf(result, sizeof result,
            "{\"display\":[%d,%d,%d,%d],\"area\":[%d,%d,%d,%d],\"mode\":\"%s\",\"factor\":%d,\"backend\":\"%s\",\"lookups\":%ld,\"hits\":%ld,\"misses\":%ld,"
            "\"bypassed\":%ld,\"over_budget\":%ld,\"resets\":%ld,\"entries\":%ld,\"texels\":%ld,"
            "\"frame_lookups\":%ld,\"frame_misses\":%ld,\"frame_ms\":%.4f,\"miss_ms\":%.4f,\"verified\":%d,"
            "\"mismatches\":%ld,\"foreign\":%ld,\"precise\":%ld,\"fractional\":%ld}",
            display[0], display[1], display[2], display[3], area[0], area[1], area[2], area[3], pc_ui_upscale_names[pc_ui_upscale ? 1 : 0], st.factor,
            st.gpu == 1 ? "vulkan" : st.gpu == 2 ? "gl" : "cpu", st.lookups, st.hits, st.misses, st.bypassed,
            st.over_budget, st.resets, st.entries, st.texels, st.frame_lookups, st.frame_misses, st.frame_ms,
            st.miss_ms, checked, mismatches, foreign, st.precise, st.fractional);
        long long want = 0;
        if (params && number_value(member(params, "regions"), &want) && want > 0) {
            static int regions[64][5];
            int count = pc_ui_upscale_regions(regions, want > 64 ? 64 : (int)want);
            size_t used = strlen(result) - 1;
            used += snprintf(result + used, sizeof result - used, ",\"regions\":[");
            for (int k = 0; k < count && used < sizeof result - 64; k++)
                used += snprintf(result + used, sizeof result - used, "%s[%d,%d,%d,%d,%d]", k ? "," : "", regions[k][0],
                    regions[k][1], regions[k][2], regions[k][3], regions[k][4]);
            snprintf(result + used, sizeof result - used, "]}");
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "settings")) {
        /* The user settings file (ROOD_SETTINGS): {action: "save" | "load"}. */
        extern void pc_settings_save(void) __attribute__((weak));
        extern void pc_settings_load(void) __attribute__((weak));
        extern const char* pc_settings_status(void) __attribute__((weak));
        extern const char* pc_settings_path(void) __attribute__((weak));
        char action[16] = "";
        if (params)
            string_value(member(params, "action"), action, sizeof action);
        if (!pc_settings_save) {
            fail(fd, id, -32000, "no settings (overlay)");
            return;
        }
        if (!strcmp(action, "save"))
            pc_settings_save();
        else if (!strcmp(action, "load"))
            pc_settings_load();
        if (pc_lighting_sync)
            pc_lighting_sync(); /* a loaded preset applies now */
        const char* path = pc_settings_path();
        snprintf(result, sizeof result, "{\"status\":\"%s\",\"path\":\"%s\"}", pc_settings_status(), path ? path : "");
        reply(fd, id, result);
    } else if (!strcmp(method, "perf")) {
        /* GPU time per presented frame since the last {reset: true} (ROOD_PERF=1). */
        int flag = 0;
        if (params)
            bool_value(member(params, "reset"), &flag);
        if (!pc_perf_json) {
            fail(fd, id, -32000, "no renderer");
            return;
        }
        pc_perf_json(result, (int)sizeof result, flag);
        reply(fd, id, result);
    } else if (!strcmp(method, "redraw")) {
        /* The newest frame drawn again as recorded with the current drawing options (MSAA, scale, ...)
         * and presented until the game draws another (pause first): compare options on one frame. */
        extern int pc_hw_redraw_present(void) __attribute__((weak));
        if (!pc_hw_redraw_present || pc_hw_redraw_present()) {
            fail(fd, id, -32000, "nothing recorded to redraw");
            return;
        }
        reply(fd, id, "{}");
    } else if (!strcmp(method, "lighting_autodetect")) {
        /* The first-run rule for given capabilities (unit check): {ray_query, integrated}. */
        int rq = 0, integrated = 0;
        if (params) {
            bool_value(member(params, "ray_query"), &rq);
            bool_value(member(params, "integrated"), &integrated);
        }
        if (!pc_lighting_autodetect) {
            fail(fd, id, -32000, "no lighting presets");
            return;
        }
        snprintf(result, sizeof result, "{\"preset\":\"%s\"}",
            pc_lighting_preset_name(pc_lighting_autodetect(rq, integrated)));
        reply(fd, id, result);
    } else if (!strcmp(method, "uv_effects")) {
        long long which, freeze = 0;
        double speed = 1;
        int flag;
        if (params && number_value(member(params, "effect"), &which)) {
            real_value(member(params, "speed"), &speed);
            if (bool_value(member(params, "freeze"), &flag))
                freeze = flag;
            else
                number_value(member(params, "freeze"), &freeze);
            pc_uv_effect_override((int)which, (float)speed, freeze != 0);
        }
        int count = 0;
        const pc_uv_effect* e = pc_uv_effects(&count);
        extern int pc_rt_uv_updates __attribute__((weak));
        extern int pc_scene_uv_serial(void) __attribute__((weak));
        int used = snprintf(result, sizeof result,
            "{\"enabled\":%d,\"interp_uv\":%d,\"scene_uv_serial\":%d,\"rt_uv_updates\":%d,\"drawn\":[",
            pc_uv_effects_enabled, &pc_interp_uv ? pc_interp_uv : 0, pc_scene_uv_serial ? pc_scene_uv_serial() : 0,
            &pc_rt_uv_updates ? pc_rt_uv_updates : 0);
        /* the first effect's offsets as drawn (frame interpolation) since the last call */
        extern int render_uv_trace(float (*)[4], int) __attribute__((weak));
        static float trace[512][4];
        int traced = render_uv_trace ? render_uv_trace(trace, 512) : 0;
        for (int i = 0; i < traced && used < (int)sizeof result - 16384; i++)
            used += snprintf(result + used, sizeof result - used, "%s[%.3f,%.3f,%.3f]", i ? "," : "", trace[i][0],
                trace[i][1], trace[i][2]);
        used += snprintf(result + used, sizeof result - used, "],\"effects\":[");
        for (int i = 0; i < count && used < (int)sizeof result - 400; i++)
            used += snprintf(result + used, sizeof result - used,
                "%s{\"type\":%d,\"slot\":%d,\"polygon\":%u,\"quad\":%d,\"modelled\":%d,\"running\":%d,"
                "\"delay\":%d,\"tick\":%d,\"game_offset\":[%d,%d],\"offset\":[%d,%d],\"shift\":[%g,%g],"
                "\"base\":[%d,%d],\"size\":[%d,%d],\"window\":[%d,%d],\"step\":[%d,%d],\"period\":[%d,%d],"
                "\"frame\":%d,\"speed\":%g,\"freeze\":%d}",
                i ? "," : "", e[i].type, e[i].slot, e[i].polygon, e[i].quad, e[i].modelled, e[i].running, e[i].delay,
                e[i].tick, e[i].game_offset[0], e[i].game_offset[1], e[i].offset[0], e[i].offset[1], e[i].shift[0],
                e[i].shift[1], e[i].base[0], e[i].base[1], e[i].size[0], e[i].size[1], e[i].window[0], e[i].window[1],
                e[i].step[0], e[i].step[1], e[i].period[0], e[i].period[1], e[i].frame, e[i].speed, e[i].freeze);
        snprintf(result + used, sizeof result - used, "]}");
        reply(fd, id, result);
    } else if (!strcmp(method, "room_groups")) {
        extern int pc_room_editor_groups(pc_room_group_info*, int) __attribute__((weak));
        extern void pc_room_editor_set_hidden(int, int) __attribute__((weak));
        if (!pc_room_editor_groups) {
            fail(fd, id, -32000, "needs the native room");
            return;
        }
        int flag;
        if (params && bool_value(member(params, "show_all"), &flag) && flag)
            pc_room_editor_set_hidden(-1, 0);
        for (int hide = 1; hide >= 0 && params; hide--) {
            const char* list = member(params, hide ? "hide" : "show");
            if (!list || *list != '[')
                continue;
            for (const char* c = list + 1; *c && *c != ']';) {
                char* end;
                long index = strtol(c, &end, 10);
                if (end == c) {
                    ++c;
                    continue;
                }
                pc_room_editor_set_hidden((int)index, hide);
                c = end;
            }
        }
        static pc_room_group_info groups[4096];
        static unsigned char drawn[4096];
        extern int pc_room_drawn_groups(unsigned char*, int, unsigned*) __attribute__((weak));
        int count = pc_room_editor_groups(groups, 4096);
        unsigned facing = 0;
        int drawn_count = pc_room_drawn_groups ? pc_room_drawn_groups(drawn, 4096, &facing) : 0;
        /* drawn: func_8009723C drew the group in the latest frame (either
         * path); facing: the mask it tested (the game's, or the free
         * camera's while it flies; 0x10000: every group). */
        int used = snprintf(result, sizeof result, "{\"facing\":%u,\"groups\":[", facing);
        for (int i = 0; i < count && used < (int)sizeof result - 300; i++) {
            used += snprintf(result + used, sizeof result - used,
                "%s{\"flags\":%u,\"polygons\":%d,\"black\":%d,\"hidden\":%d,\"room_hides\":%d,\"cutaway\":%d,"
                "\"drawn\":%d,\"position\":[%d,%d,%d]",
                i ? "," : "", groups[i].flags, groups[i].polygons, groups[i].black, groups[i].hidden,
                groups[i].game_hidden, groups[i].cutaway, i < drawn_count ? drawn[i] : -1, groups[i].position[0],
                groups[i].position[1], groups[i].position[2]);
            /* gameplay_walls smart: shown (drawn share, 1 solid), occluding
             * (after hysteresis), and this frame's grid cells: in the focus
             * region nearer than the focus, seen in front of the play area,
             * over void, all its front-facing cells. */
            float shown;
            int occluding, cells[4];
            if (pc_gameplay_walls_info && pc_gameplay_walls_info((unsigned)i, &shown, &occluding, cells) == 1)
                used += snprintf(result + used, sizeof result - used,
                    ",\"shown\":%.3f,\"occluding\":%d,\"cells\":[%d,%d,%d,%d]", shown, occluding, cells[0],
                    cells[1], cells[2], cells[3]);
            used += snprintf(result + used, sizeof result - used, "}");
        }
        snprintf(result + used, sizeof result - used, "]}");
        reply(fd, id, result);
    } else if (!strcmp(method, "editor_view")) {
        extern void pc_editor_set_focus(int, const float*) __attribute__((weak));
        extern int pc_editor_cutaway_focus(float*) __attribute__((weak));
        if (!pc_editor_set_focus) {
            fail(fd, id, -32000, "needs the editor");
            return;
        }
        const char* f = params ? member(params, "focus") : NULL;
        float point[3] = { 0, 0, 0 };
        if (f && *f == '[' && sscanf(f, "[%f , %f , %f", &point[0], &point[1], &point[2]) == 3)
            pc_editor_set_focus(1, point);
        else if (f && !strncmp(f, "null", 4))
            pc_editor_set_focus(0, NULL);
        int has = pc_editor_cutaway_focus(point);
        /* hidden: room surfaces hide the focus from the eye (after
         * hysteresis); amount: how far the ghosting cut has faded in. */
        int hidden = 0;
        float amount = 0;
        if (pc_editor_cutaway_state)
            pc_editor_cutaway_state(&hidden, &amount);
        snprintf(result, sizeof result,
            "{\"focus\":%s[%.1f,%.1f,%.1f],\"hide_masks\":%d,\"game_cutaway\":%d,\"cutaway\":%d,\"radius\":%g,"
            "\"opacity\":%g,\"auto\":%d,\"hidden\":%d,\"amount\":%g}",
            has ? "" : "null,\"_\":", point[0], point[1], point[2], &pc_editor_hide_masks ? pc_editor_hide_masks : 0,
            &pc_editor_game_cutaway ? pc_editor_game_cutaway : 0, &pc_editor_cutaway ? pc_editor_cutaway : 0,
            &pc_editor_cutaway_radius ? pc_editor_cutaway_radius : 0,
            &pc_editor_cutaway_opacity ? pc_editor_cutaway_opacity : 0, &pc_editor_cutaway_auto ? pc_editor_cutaway_auto : 0,
            hidden, amount);
        reply(fd, id, result);
    } else if (!strcmp(method, "editor_ui")) {
        extern int pc_overlay_set_visible(int) __attribute__((weak));
        if (!pc_overlay_set_visible) {
            fail(fd, id, -32000, "needs the overlay");
            return;
        }
        extern int pc_overlay_set_help(int) __attribute__((weak));
        extern int pc_overlay_set_tab(int) __attribute__((weak));
        extern int pc_overlay_set_details_tab(const char*) __attribute__((weak));
        extern void pc_lighting_open_menu(void) __attribute__((weak));
        int flag = -1, help = -1, tab = -1, menu = 0;
        char tab_name[32] = "";
        if (params) {
            bool_value(member(params, "visible"), &flag);
            bool_value(member(params, "help"), &help);
            if (string_value(member(params, "tab"), tab_name, sizeof tab_name))
                tab = !strcmp(tab_name, "place") ? 1 : 0;
            /* details: a Details tab (lighting, inspector, atmosphere, post, graphics, diagnostics) */
            if (string_value(member(params, "details"), tab_name, sizeof tab_name) && pc_overlay_set_details_tab
                && !pc_overlay_set_details_tab(tab_name)) {
                fail(fd, id, -32602, "unknown details tab");
                return;
            }
            /* lighting_menu: open the toolbar's lighting quality dropdown (screenshots) */
            if (bool_value(member(params, "lighting_menu"), &menu) && menu && pc_lighting_open_menu)
                pc_lighting_open_menu();
            /* rooms: show or hide the Room Browser window */
            extern void pc_room_browser_show(int) __attribute__((weak));
            int rooms_window;
            if (bool_value(member(params, "rooms"), &rooms_window) && pc_room_browser_show)
                pc_room_browser_show(rooms_window);
        }
        int open = pc_overlay_set_visible(flag);
        int shown = pc_overlay_set_tab ? pc_overlay_set_tab(tab) : 0;
        snprintf(result, sizeof result, "{\"visible\":%s,\"help\":%s,\"tab\":\"%s\"}", open ? "true" : "false",
            pc_overlay_set_help && pc_overlay_set_help(help) ? "true" : "false", shown ? "place" : "outliner");
        reply(fd, id, result);
    } else if (!strcmp(method, "editor_overlay")) {
        extern int pc_lights_tools_view(pc_overlay_view*, pc_overlay_pick*) __attribute__((weak));
        pc_overlay_view view;
        if (!pc_lights_tools_view || !&pc_overlay_wire || !pc_lights_tools_view(&view, NULL)) {
            fail(fd, id, -32000, "no editor view (hardware renderer and a room needed)");
            return;
        }
        int update = 1;
        if (params)
            bool_value(member(params, "update"), &update);
        if (update)
            pc_overlay_update(&view);
        pc_overlay_stats st;
        pc_overlay_get_stats(&st);
        int used = snprintf(result, sizeof result,
            "{\"points\":%d,\"edges\":%d,\"shown_points\":%d,\"shown_edges\":%d,\"visible_samples\":%d,"
            "\"occluded_samples\":%d,\"pending_samples\":%d,\"bvh_triangles\":%d,\"character_triangles\":%d,"
            "\"occlusion_ms\":%.3f,\"draw_ms\":%.3f,\"drawn_lines\":%d,\"drawn_points\":%d,"
            "\"view\":[%g,%g,%g,%g]",
            st.points, st.edges, st.shown_points, st.shown_edges, st.visible_samples, st.occluded_samples,
            st.pending_samples, st.bvh_triangles, st.character_triangles, st.occlusion_ms, st.draw_ms, st.drawn_lines,
            st.drawn_points, view.rect[0], view.rect[1], view.rect[2], view.rect[3]);
        long long want = 0;
        static float world[256][6], screen[256][4];
        static int hidden[256];
        for (int kind = 0; kind < 2 && params; kind++) {
            if (!number_value(member(params, kind ? "edges" : "points"), &want) || want <= 0)
                continue;
            int got = kind ? pc_overlay_sample_edges(&view, world, screen, hidden, want > 256 ? 256 : (int)want)
                           : pc_overlay_sample_points(&view, (float(*)[3])world, (float(*)[2])screen, hidden,
                               want > 256 ? 256 : (int)want);
            used += snprintf(result + used, sizeof result - used, ",\"%s\":[", kind ? "edge_samples" : "point_samples");
            for (int i = 0; i < got && used < (int)sizeof result - 200; i++) {
                if (kind)
                    used += snprintf(result + used, sizeof result - used,
                        "%s{\"a\":[%.2f,%.2f,%.2f],\"b\":[%.2f,%.2f,%.2f],\"screen\":[%.1f,%.1f,%.1f,%.1f],\"hidden\":%d}",
                        i ? "," : "", world[i][0], world[i][1], world[i][2], world[i][3], world[i][4], world[i][5],
                        screen[i][0], screen[i][1], screen[i][2], screen[i][3], hidden[i]);
                else {
                    const float* w = ((float(*)[3])world)[i];
                    const float* q = ((float(*)[2])screen)[i];
                    used += snprintf(result + used, sizeof result - used,
                        "%s{\"position\":[%.2f,%.2f,%.2f],\"screen\":[%.1f,%.1f],\"hidden\":%d}", i ? "," : "", w[0],
                        w[1], w[2], q[0], q[1], hidden[i]);
                }
            }
            used += snprintf(result + used, sizeof result - used, "]");
        }
        snprintf(result + used, sizeof result - used, "}");
        reply(fd, id, result);
    } else if (!strcmp(method, "editor_snap")) {
        extern int pc_lights_tools_view(pc_overlay_view*, pc_overlay_pick*) __attribute__((weak));
        extern int pc_lights_snap_at(float, float, unsigned, int, pc_snap_result*) __attribute__((weak));
        pc_overlay_view view;
        if (!pc_lights_snap_at || !pc_lights_tools_view || !pc_lights_tools_view(&view, NULL)) {
            fail(fd, id, -32000, "no editor view (hardware renderer and a room needed)");
            return;
        }
        int flag = 0;
        if (params && bool_value(member(params, "clear"), &flag) && flag) {
            pc_overlay_hold_snap(NULL);
            reply(fd, id, "{\"cleared\":true}");
            return;
        }
        double x = view.rect[0] + view.rect[2] * 0.5, y = view.rect[1] + view.rect[3] * 0.5, u, v;
        if (params && real_value(member(params, "u"), &u))
            x = view.rect[0] + view.rect[2] * u;
        if (params && real_value(member(params, "v"), &v))
            y = view.rect[1] + view.rect[3] * v;
        if (params)
            real_value(member(params, "x"), &x), real_value(member(params, "y"), &y);
        unsigned modes = pc_snap_modes(0);
        const char* list = params ? member(params, "modes") : NULL;
        if (list && *list == '[') {
            const char* close = strchr(list, ']');
            size_t length = close ? (size_t)(close - list) : strlen(list);
            char names[256];
            snprintf(names, sizeof names, "%.*s", (int)(length < 255 ? length : 255), list);
            modes = (strstr(names, "\"point\"") ? PC_SNAP_MODE_POINT : 0) | (strstr(names, "\"edge\"") ? PC_SNAP_MODE_EDGE : 0)
                | (strstr(names, "\"midpoint\"") ? PC_SNAP_MODE_MIDPOINT : 0)
                | (strstr(names, "\"surface\"") ? PC_SNAP_MODE_SURFACE : 0);
        }
        int apply = 0, highlight = 0;
        if (params) {
            bool_value(member(params, "apply"), &apply);
            bool_value(member(params, "highlight"), &highlight);
        }
        pc_snap_result r;
        int type = pc_lights_snap_at((float)x, (float)y, modes, apply, &r);
        if (highlight)
            pc_overlay_hold_snap(type ? &r : NULL);
        static const char* types[] = { "none", "point", "midpoint", "edge", "surface" };
        int used = snprintf(result, sizeof result,
            "{\"type\":\"%s\",\"cursor\":[%.1f,%.1f],\"modes\":%u", types[type], x, y, modes);
        if (type) {
            used += snprintf(result + used, sizeof result - used,
                ",\"position\":[%.3f,%.3f,%.3f],\"screen\":[%.1f,%.1f],\"distance\":%.2f,\"depth\":%.1f,"
                "\"index\":%d,\"hidden\":%d,\"surface\":[%.3f,%.3f,%.3f],\"normal\":[%.4f,%.4f,%.4f]",
                r.position[0], r.position[1], r.position[2], r.screen[0], r.screen[1], r.distance, r.depth, r.index,
                r.occluded, r.surface[0], r.surface[1], r.surface[2], r.normal[0], r.normal[1], r.normal[2]);
            static float near[256][4];
            int count = type == PC_SNAP_SURFACE ? 0 : pc_overlay_neighbours(r.position, pc_snap_highlight, near, 256);
            used += snprintf(result + used, sizeof result - used, ",\"highlight_radius\":%g,\"neighbours\":[",
                pc_snap_highlight);
            for (int i = 0; i < count && used < (int)sizeof result - 100; i++)
                used += snprintf(result + used, sizeof result - used, "%s{\"position\":[%.2f,%.2f,%.2f],\"weight\":%.4f}",
                    i ? "," : "", near[i][0], near[i][1], near[i][2], near[i][3]);
            used += snprintf(result + used, sizeof result - used, "]");
        }
        snprintf(result + used, sizeof result - used, "}");
        reply(fd, id, result);
    } else if (!strcmp(method, "gi_probe")) {
        /* The inspector's GI probe at (u, v) of the display area. */
        extern int pc_gi_probe(float u, float v, float out[8]) __attribute__((weak));
        double pu = 0.5, pv = 0.5;
        float out[8];
        if (params) {
            real_value(member(params, "u"), &pu);
            real_value(member(params, "v"), &pv);
        }
        if (!pc_gi_probe || !pc_gi_probe((float)pu, (float)pv, out)) {
            fail(fd, id, -32000, "no GI result");
            return;
        }
        snprintf(result, sizeof result,
            "{\"indirect\":[%.4f,%.4f,%.4f],\"visibility\":%.4f,\"baked_visibility\":%.4f,\"extra\":[%.4f,%.4f,%.4f]}",
            out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
        reply(fd, id, result);
    } else if (!strcmp(method, "gi_dump")) {
        /* Vulkan: the last GI update's world positions and shadow
         * visibility per GI pixel (vk_rt.inc pc_vk_gi_dump). */
        extern int pc_vk_gi_dump(const char* path) __attribute__((weak));
        extern void pc_vk_gi_dump_burst(const char* path, int count) __attribute__((weak));
        char path[400];
        snprintf(path, sizeof path, "%s", pc_out_path("gi-dump.bin"));
        if (params)
            string_value(member(params, "path"), path, sizeof path);
        long long count = 0;
        if (params)
            number_value(member(params, "count"), &count);
        extern int pc_vk_gi_dump_light(const char* path) __attribute__((weak));
        int light = 0;
        if (params)
            bool_value(member(params, "light"), &light);
        if (light) {
            if (!pc_vk_gi_dump_light || pc_vk_gi_dump_light(path)) {
                fail(fd, id, -32000, "no GI result (needs Vulkan ray tracing)");
                return;
            }
        } else if (count > 0 && pc_vk_gi_dump_burst)
            pc_vk_gi_dump_burst(path, (int)count);
        else if (!pc_vk_gi_dump || pc_vk_gi_dump(path)) {
            fail(fd, id, -32000, "no GI result (needs Vulkan ray tracing)");
            return;
        }
        snprintf(result, sizeof result, "{\"path\":\"%s\"}", path);
        reply(fd, id, result);
    } else if (!strcmp(method, "probe_gi")) {
        if (!pc_probe_get_stats) {
            fail(fd, id, -32000, "no probe GI in this build");
            return;
        }
        char path[400] = "";
        if (params && string_value(member(params, "dump"), path, sizeof path) && pc_probe_dump(path)) {
            fail(fd, id, -32000, "no probe field to dump (gi_method probes, a room and some light)");
            return;
        }
        pc_probe_stats s;
        pc_probe_get_stats(&s);
        snprintf(result, sizeof result,
            "{\"available\":%s,\"valid\":%s,\"backend\":%d,\"counts\":[%d,%d,%d],\"probes\":%d,"
            "\"active\":%d,\"rays\":%d,\"origin\":[%.2f,%.2f,%.2f],\"spacing\":[%.3f,%.3f,%.3f],"
            "\"updates\":%u,\"converged\":%u,\"rebuilds\":%u,\"light_resets\":%u,\"sampled\":%s%s%s%s}",
            pc_probe_gi_available() ? "true" : "false", s.valid ? "true" : "false", s.backend, s.counts[0], s.counts[1],
            s.counts[2], s.probes, s.active, s.rays, s.origin[0], s.origin[1], s.origin[2], s.spacing[0],
            s.spacing[1], s.spacing[2], s.updates, s.converged, s.rebuilds, s.light_resets,
            s.sampled ? "true" : "false", *path ? ",\"dump\":\"" : "", path, *path ? "\"" : "");
        reply(fd, id, result);
    } else if (!strcmp(method, "render_stats")) {
        render_stats st;
        memset(&st, 0, sizeof st);
        if (render_stats_get)
            render_stats_get(&st);
        snprintf(result, sizeof result,
            "{\"native_room\":%d,\"lists\":%d,\"instances\":%d,\"native_triangles\":%d,"
            "\"legacy_room_triangles\":%d,\"skipped_room_triangles\":%d,\"meshes\":%d,\"mesh_polygons\":%d,\"mesh_updates\":%d,"
            "\"native_actors\":%d,\"object_lists\":%d,\"object_instances\":%d,\"object_triangles\":%d,"
            "\"legacy_object_triangles\":%d,\"skipped_object_triangles\":%d,\"object_meshes_built\":%d,"
            "\"interpolated_instances\":%d,\"native_blend\":%d,\"translucent_markers\":%d,"
            "\"translucent_polygons\":%d,\"legacy_translucent_triangles\":%d,\"skipped_translucent_triangles\":%d,"
            "\"native_objects\":%d,\"layered_lists\":%d,\"layered_instances\":%d,\"legacy_other_triangles\":%d,"
            "\"native_shadows\":%d,\"shadow_markers\":%d,\"shadow_polygons\":%d,\"native_effects\":%d,"
            "\"effect_markers\":%d,\"effect_polygons\":%d,\"skipped_effect_triangles\":%d,"
            "\"legacy_effect_triangles\":%d,\"vram_syncs\":%d,\"stale_rects\":%d,\"uv_effects\":%d,"
            "\"frame_serial\":%u}",
            &pc_native_room ? pc_native_room : 0, st.lists, st.instances, st.native_triangles,
            st.legacy_room_triangles, st.skipped_room_triangles, st.meshes, st.mesh_polygons, st.mesh_updates,
            &pc_native_actors ? pc_native_actors : 0, st.object_lists, st.object_instances, st.object_triangles,
            st.legacy_object_triangles, st.skipped_object_triangles, st.object_meshes_built,
            st.interpolated_instances, &pc_native_blend ? pc_native_blend : 0, st.translucent_markers,
            st.translucent_polygons, st.legacy_translucent_triangles, st.skipped_translucent_triangles,
            &pc_native_objects ? pc_native_objects : 0, st.layered_lists, st.layered_instances,
            st.legacy_other_triangles, &pc_native_shadows ? pc_native_shadows : 0, st.shadow_markers,
            st.shadow_polygons, &pc_native_effects ? pc_native_effects : 0, st.effect_markers, st.effect_polygons,
            st.skipped_effect_triangles, st.legacy_effect_triangles, st.vram_syncs, st.stale_rects, st.uv_effects,
            render_frame_serial());
        reply(fd, id, result);
    } else if (!strcmp(method, "scene")) {
        if (!pc_scene_triangles) {
            fail(fd, id, -32000, "scene capture needs the battle build");
            return;
        }
        float lo[3], hi[3], rot[3][3] = { { 0 } }, t[3] = { 0 }, h = 0, ox = 0, oy = 0;
        pc_scene_bounds(lo, hi);
        int camera = pc_scene_camera_get(rot, t, &h, &ox, &oy);
        snprintf(result, sizeof result,
            "{\"serial\":%d,\"triangles\":%d,\"bounds\":[[%g,%g,%g],[%g,%g,%g]],"
            "\"camera\":%s{\"rotation\":[[%g,%g,%g],[%g,%g,%g],[%g,%g,%g]],"
            "\"translation\":[%g,%g,%g],\"h\":%g,\"offset\":[%g,%g]}}",
            pc_scene_serial(), pc_scene_triangles(), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2],
            camera ? "" : "null,\"_\":", rot[0][0], rot[0][1], rot[0][2], rot[1][0], rot[1][1],
            rot[1][2], rot[2][0], rot[2][1], rot[2][2], t[0], t[1], t[2], h, ox, oy);
        reply(fd, id, result);
    } else if (!strcmp(method, "lights")) {
        if (!pc_lights_describe) {
            fail(fd, id, -32000, "lights need the editor (hardware renderer)");
            return;
        }
        double baked;
        char action[32] = "";
        if (params) {
            string_value(member(params, "action"), action, sizeof action);
            const char* b = member(params, "baked");
            if (b && sscanf(b, "%lf", &baked) == 1)
                pc_lights_baked = (float)baked;
        }
        extern int pc_lights_add(const float*) __attribute__((weak));
        extern void pc_lights_select(int) __attribute__((weak));
        float position[3];
        const char* at = params ? member(params, "position") : NULL;
        if (!strcmp(action, "suggest"))
            pc_lights_suggest();
        else if (!strcmp(action, "save"))
            pc_lights_save();
        else if (!strcmp(action, "add") && pc_lights_add) {
            if (!at || sscanf(at, "[%f , %f , %f", &position[0], &position[1], &position[2]) != 3) {
                fail(fd, id, -32602, "add needs position [x, y, z]");
                return;
            }
            pc_lights_add(position);
        }
        if (params && number_value(member(params, "select"), &n) && pc_lights_select)
            pc_lights_select((int)n);
        pc_lights_describe(result, sizeof result);
        reply(fd, id, result);
    } else if (!strcmp(method, "actors") || !strcmp(method, "prefabs") || !strcmp(method, "editor_place")
               || !strcmp(method, "glows") || !strcmp(method, "fixtures")) {
        /* Light actors (light_actors.cpp parses the params object itself). */
        extern int pc_actors_rpc(const char* method, const char* params, char* out, int size) __attribute__((weak));
        if (!pc_actors_rpc) {
            fail(fd, id, -32000, "light actors need the editor (hardware renderer)");
            return;
        }
        static char args[LINE_MAX_BYTES];
        const char* end = params ? value_end(params) : NULL;
        if (params && end && (size_t)(end - params) < sizeof args) {
            memcpy(args, params, (size_t)(end - params));
            args[end - params] = 0;
        } else {
            strcpy(args, "{}");
        }
        if (pc_actors_rpc(method, args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "ending")) {
        /* Developer jump: the field's next frame plays the ending (credits,
         * score and rank) and returns to the title (room_warp.c). The game
         * state it changes is only in memory unless the title's clear-data
         * save is accepted. */
        extern volatile int pc_ending_request __attribute__((weak));
        if (!&pc_ending_request) {
            fail(fd, id, -32000, "ending needs the battle build");
            return;
        }
        pc_ending_request = 1;
        reply(fd, id, "{\"requested\":true}");
    } else if (!strcmp(method, "rooms") || !strcmp(method, "room_goto")) {
        /* The Room Browser (room_warp.c): rooms lists every zone and room
         * read from the disc (and the warp's state); room_goto {zone, room,
         * entry, enemies, scripts, block_saves} asks the game to load one,
         * {action: "return"} goes back where the warps started, "clear"
         * ends the warped state, "cancel" drops a request. */
        extern int pc_rooms_json(char*, int) __attribute__((weak));
        extern int pc_room_browser_go(int, int, int, char*, int) __attribute__((weak));
        extern int pc_room_warp_request(int, int, int, char*, int) __attribute__((weak));
        extern int pc_room_warp_warped(int*, int*) __attribute__((weak));
        extern void pc_room_warp_clear(void) __attribute__((weak));
        extern void pc_room_warp_cancel(void) __attribute__((weak));
        extern int pc_room_warp_phase(void) __attribute__((weak));
        extern void pc_room_warp_acknowledge(void) __attribute__((weak));
        extern const char* pc_room_warp_message(void) __attribute__((weak));
        extern int pc_room_warp_enemies __attribute__((weak)), pc_room_warp_scripts __attribute__((weak)),
            pc_room_warp_block_saves __attribute__((weak));
        if (!pc_rooms_json || !pc_room_warp_request) {
            fail(fd, id, -32000, "rooms need the battle build");
            return;
        }
        if (!strcmp(method, "rooms")) {
            if (pc_room_warp_phase() == 4 && !pc_room_browser_go)
                pc_room_warp_acknowledge(); /* (no editor to finish it) */
            pc_rooms_json(result, sizeof result);
            reply(fd, id, result);
            return;
        }
        char action[32] = "", why[200] = "";
        int flag, ok = 1;
        if (params) {
            string_value(member(params, "action"), action, sizeof action);
            if (bool_value(member(params, "enemies"), &flag))
                pc_room_warp_enemies = flag;
            if (bool_value(member(params, "scripts"), &flag))
                pc_room_warp_scripts = flag;
            if (bool_value(member(params, "block_saves"), &flag))
                pc_room_warp_block_saves = flag;
            extern int pc_room_warp_cell[4] __attribute__((weak));
            const char* cell = member(params, "cell");
            if (cell && *cell == '[' && pc_room_warp_cell) {
                long long v[4] = { -1, 0, 0, 0 };
                const char* at = cell + 1;
                for (int k = 0; k < 4; k++) {
                    while (*at == ' ' || *at == ',')
                        at++;
                    if (*at != '-' && (*at < '0' || *at > '9'))
                        break;
                    v[k] = strtoll(at, (char**)&at, 10);
                }
                for (int k = 0; k < 4; k++)
                    pc_room_warp_cell[k] = (int)v[k];
            }
        }
        long long zone = -1, room = -1, entry = 0;
        int oz = -1, orr = -1;
        int warped = pc_room_warp_warped(&oz, &orr);
        if (!strcmp(action, "clear"))
            pc_room_warp_clear();
        else if (!strcmp(action, "cancel"))
            pc_room_warp_cancel();
        else if (!strcmp(action, "status"))
            ;
        else {
            if (!strcmp(action, "return")) {
                if (!warped) {
                    fail(fd, id, -32602, "not warped");
                    return;
                }
                zone = oz, room = orr;
            } else if (!params || !number_value(member(params, "zone"), &zone)
                || !number_value(member(params, "room"), &room)) {
                fail(fd, id, -32602, "room_goto needs zone and room (or action return/clear/cancel/status)");
                return;
            }
            if (params)
                number_value(member(params, "entry"), &entry);
            ok = pc_room_browser_go ? pc_room_browser_go((int)zone, (int)room, (int)entry, why, sizeof why)
                                    : pc_room_warp_request((int)zone, (int)room, (int)entry, why, sizeof why);
        }
        warped = pc_room_warp_warped(&oz, &orr);
        snprintf(text, sizeof text, "%s", why[0] ? why : pc_room_warp_message());
        for (char* c = text; *c; c++)
            if (*c == '"' || *c == '\\')
                *c = '\'';
        snprintf(result, sizeof result,
            "{\"ok\":%s,\"phase\":%d,\"warped\":%s,\"origin\":[%d,%d],\"enemies\":%d,\"scripts\":%d,"
            "\"block_saves\":%d,\"message\":\"%s\"}",
            ok ? "true" : "false", pc_room_warp_phase(), warped ? "true" : "false", oz, orr, pc_room_warp_enemies,
            pc_room_warp_scripts, pc_room_warp_block_saves, text);
        reply(fd, id, result);
    } else if (!strcmp(method, "lighting")) {
        /* Lighting packs: the room's layers (lighting_editor.cpp). */
        extern int pc_lighting_rpc(const char* params, char* out, int size) __attribute__((weak));
        if (!pc_lighting_rpc) {
            fail(fd, id, -32000, "lighting needs the editor (hardware renderer)");
            return;
        }
        static char args[LINE_MAX_BYTES];
        const char* end = params ? value_end(params) : NULL;
        if (params && end && (size_t)(end - params) < sizeof args) {
            memcpy(args, params, (size_t)(end - params));
            args[end - params] = 0;
        } else {
            strcpy(args, "{}");
        }
        if (pc_lighting_rpc(args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "editor_undo")) {
        /* The editor's undo history (editor_undo.h): undo, redo (steps
         * times), list, begin/end (hold one transaction open across
         * requests, as a mouse drag does), jump (to `done` steps). */
        extern int pc_undo_json(char*, int) __attribute__((weak));
        extern int pc_undo_undo(void) __attribute__((weak));
        extern int pc_undo_redo(void) __attribute__((weak));
        extern void pc_undo_begin(const char*) __attribute__((weak));
        extern void pc_undo_end(void) __attribute__((weak));
        extern void pc_undo_jump(int) __attribute__((weak));
        if (!pc_undo_json) {
            fail(fd, id, -32000, "needs the editor");
            return;
        }
        char action[16] = "list", name[96] = "";
        long long steps = 1, target;
        if (params) {
            string_value(member(params, "action"), action, sizeof action);
            string_value(member(params, "name"), name, sizeof name);
            number_value(member(params, "steps"), &steps);
        }
        int moved = 0;
        if (!strcmp(action, "undo"))
            for (long long i = 0; i < steps && pc_undo_undo(); i++)
                ++moved;
        else if (!strcmp(action, "redo"))
            for (long long i = 0; i < steps && pc_undo_redo(); i++)
                ++moved;
        else if (!strcmp(action, "begin"))
            pc_undo_begin(name);
        else if (!strcmp(action, "end"))
            pc_undo_end();
        else if (!strcmp(action, "jump") && params && number_value(member(params, "done"), &target))
            pc_undo_jump((int)target);
        else if (strcmp(action, "list")) {
            fail(fd, id, -32602, "action: undo, redo, list, begin, end or jump");
            return;
        }
        int used = pc_undo_json(result, (int)sizeof result - 32);
        snprintf(result + used - 1, sizeof result - (size_t)used + 1, ",\"moved\":%d}", moved);
        reply(fd, id, result);
    } else if (!strcmp(method, "editor_input")) {
        /* Keys and text as the keyboard would send them (through the
         * window's event queue, the overlay's filter and Dear ImGui), and
         * keyboard focus (focus "search": the Outliner's search field;
         * "none": nothing). */
        extern int pc_overlay_input(const char* keys, const char* text, int ctrl, int shift, const char* focus,
            char* out, int size) __attribute__((weak));
        if (!pc_overlay_input) {
            fail(fd, id, -32000, "needs the overlay");
            return;
        }
        char keys[256] = "", typed[128] = "", focus[16] = "";
        int ctrl = 0, shift = 0;
        if (params) {
            const char* list = member(params, "keys");
            const char* end = list ? value_end(list) : NULL;
            if (list && end && (size_t)(end - list) < sizeof keys) {
                memcpy(keys, list, (size_t)(end - list));
                keys[end - list] = 0;
            }
            string_value(member(params, "text"), typed, sizeof typed);
            string_value(member(params, "focus"), focus, sizeof focus);
            bool_value(member(params, "ctrl"), &ctrl);
            bool_value(member(params, "shift"), &shift);
        }
        if (pc_overlay_input(keys, typed, ctrl, shift, focus, result, (int)sizeof result) < 0) {
            fail(fd, id, -32602, result);
            return;
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "scene_dump")) {
        char path[400];
        snprintf(path, sizeof path, "%s", pc_out_path("scene.obj"));
        if (params)
            string_value(member(params, "path"), path, sizeof path);
        if (!pc_scene_dump || pc_scene_dump(path)) {
            fail(fd, id, -32000, "scene dump failed");
            return;
        }
        snprintf(result, sizeof result, "{\"path\":\"%s\"}", path);
        reply(fd, id, result);
    } else if (!strcmp(method, "anim_trace")) {
        /* Object instances (bone palettes as drawn, the game frame's and the
         * previous one's) of the next `count` presented images to a file. */
        extern int render_anim_trace(const char*, int) __attribute__((weak));
        extern int render_anim_trace_remaining(void) __attribute__((weak));
        if (!render_anim_trace) {
            fail(fd, id, -32000, "needs the native renderer");
            return;
        }
        char path[400];
        snprintf(path, sizeof path, "%s", pc_out_path("anim-trace.bin"));
        double count = -1;
        if (params) {
            string_value(member(params, "path"), path, sizeof path);
            real_value(member(params, "count"), &count);
        }
        if (count >= 0 && render_anim_trace(path, (int)count)) {
            fail(fd, id, -32000, "cannot open the trace file");
            return;
        }
        snprintf(result, sizeof result, "{\"path\":\"%s\",\"remaining\":%d}", path, render_anim_trace_remaining());
        reply(fd, id, result);
    } else if (!strcmp(method, "present_dump")) {
        /* The next presented window image (as displayed) to a PPM. */
        extern char pc_present_capture[400] __attribute__((weak));
        if (!pc_present_capture) {
            fail(fd, id, -32000, "needs the OpenGL renderer");
            return;
        }
        char path[400];
        snprintf(path, sizeof path, "%s", pc_out_path("rpc-present.ppm"));
        if (params)
            string_value(member(params, "path"), path, sizeof path);
        extern int pc_present_burst __attribute__((weak));
        double count = 0;
        if (params && &pc_present_burst && real_value(member(params, "count"), &count))
            pc_present_burst = (int)count;
        snprintf(pc_present_capture, 400, "%s", path);
        snprintf(result, sizeof result, "{\"path\":\"%s\"}", path);
        reply(fd, id, result);
    } else if (!strcmp(method, "editor_camera")) {
        /* The editor's free camera: {x, y, z, yaw, pitch} sets it (and turns
         * it on: the game renders from it); no params reads it. */
        extern int pc_editor_camera(int, float*, float*, float*) __attribute__((weak));
        extern void pc_editor_toggle(void) __attribute__((weak));
        extern int pc_editor_active(void) __attribute__((weak));
        if (!pc_editor_camera) {
            fail(fd, id, -32000, "needs the editor");
            return;
        }
        float p[3] = { 0, 0, 0 }, yaw = 0, pitch = 0;
        double v;
        int set = 0;
        if (params && real_value(member(params, "off"), &v) && v != 0) {
            if (pc_editor_active())
                pc_editor_toggle();
        } else if (params && real_value(member(params, "x"), &v)) {
            p[0] = (float)v;
            set = real_value(member(params, "y"), &v) ? (p[1] = (float)v, 1) : 0;
            set &= real_value(member(params, "z"), &v) ? (p[2] = (float)v, 1) : 0;
            if (real_value(member(params, "yaw"), &v))
                yaw = (float)v;
            if (real_value(member(params, "pitch"), &v))
                pitch = (float)v;
        }
        int on = pc_editor_camera(set, p, &yaw, &pitch);
        snprintf(result, sizeof result, "{\"active\":%s,\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,\"yaw\":%.1f,\"pitch\":%.1f}",
            on ? "true" : "false", p[0], p[1], p[2], yaw, pitch);
        reply(fd, id, result);
    } else if (!strcmp(method, "gpu_draw")) {
        extern void pc_vram_transfer(int, int, int, int, void*, int) __attribute__((weak));
        extern void pc_draw_env(int, int, int, int, int, int, int) __attribute__((weak));
        extern void pc_draw_packet(void*, int) __attribute__((weak));
        static char hex[2 * 65536 + 2];
        static uint32_t words[1 + 4096];
        static uint16_t texels[16384];
        if (!pc_vram_transfer || !pc_draw_env || !pc_draw_packet || !params) {
            fail(fd, id, -32000, "gpu_draw needs the game's GPU and params");
            return;
        }
        long long t[4] = { 0, 0, 0, 0 }, area[6] = { 0, 0, 1024, 512, 0, 0 };
        static const char* const texture_keys[4] = { "texture_x", "texture_y", "texture_w", "texture_h" };
        static const char* const area_keys[6] = { "area_x", "area_y", "area_w", "area_h", "offset_x", "offset_y" };
        for (int i = 0; i < 4; i++)
            number_value(member(params, texture_keys[i]), &t[i]);
        for (int i = 0; i < 6; i++)
            number_value(member(params, area_keys[i]), &area[i]);
        if (string_value(member(params, "texture"), hex, sizeof hex)) {
            size_t n = strlen(hex) / 4;
            if (t[2] <= 0 || t[3] <= 0 || t[0] < 0 || t[1] < 0 || t[0] + t[2] > 1024 || t[1] + t[3] > 512
                || n != (size_t)(t[2] * t[3]) || n > sizeof texels / 2) {
                fail(fd, id, -32602, "texture needs texture_x/y/w/h inside VRAM and w*h hex texels");
                return;
            }
            for (size_t i = 0; i < n; i++) {
                unsigned lo, hi;
                if (sscanf(hex + 4 * i, "%2x%2x", &lo, &hi) != 2) {
                    fail(fd, id, -32602, "bad texture hex");
                    return;
                }
                texels[i] = (uint16_t)(lo | hi << 8);
            }
            pc_vram_transfer((int)t[0], (int)t[1], (int)t[2], (int)t[3], texels, 1);
        }
        size_t count = 0;
        if (string_value(member(params, "packet"), hex, sizeof hex)) {
            count = strlen(hex) / 8;
            if (!count || count > 255 || strlen(hex) % 8) {
                fail(fd, id, -32602, "packet needs 1-255 whole words");
                return;
            }
            for (size_t i = 0; i < count; i++) {
                unsigned b[4];
                if (sscanf(hex + 8 * i, "%2x%2x%2x%2x", &b[0], &b[1], &b[2], &b[3]) != 4) {
                    fail(fd, id, -32602, "bad packet hex");
                    return;
                }
                words[1 + i] = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
            }
        }
        pc_draw_env((int)area[0], (int)area[1], (int)area[2], (int)area[3], (int)area[4], (int)area[5], 0);
        if (count) {
            words[0] = (uint32_t)count << 24 | 0xffffff;
            pc_draw_packet(words, 0);
        }
        snprintf(result, sizeof result, "{\"words\":%zu}", count);
        reply(fd, id, result);
    } else if (!strcmp(method, "gpu_dump")) {
        char path[400];
        snprintf(path, sizeof path, "%s", pc_out_path("rpc-gpu.ppm"));
        int replay = 0;
        if (params) {
            string_value(member(params, "path"), path, sizeof path);
            bool_value(member(params, "replay"), &replay);
        }
        if (!pc_hw_dump || pc_hw_dump(path, replay)) {
            fail(fd, id, -32000, "hardware renderer inactive or dump failed");
            return;
        }
        snprintf(result, sizeof result, "{\"path\":\"%s\"}", path);
        reply(fd, id, result);
    } else if (!strcmp(method, "read")) {
        long long address, length = 4;
        if (!params || !number_value(member(params, "address"), &address)) {
            fail(fd, id, -32602, "read needs address");
            return;
        }
        number_value(member(params, "length"), &length);
        if (length < 1 || !readable((unsigned long)address, (unsigned long)length)) {
            fail(fd, id, -32000, "range not readable");
            return;
        }
        size_t used = (size_t)snprintf(result, sizeof result, "{\"hex\":\"");
        for (long long i = 0; i < length && used + 4 < sizeof result; i++)
            used += (size_t)snprintf(result + used, sizeof result - used, "%02x",
                ((unsigned char*)(unsigned long)address)[i]);
        snprintf(result + used, sizeof result - used, "\"}");
        reply(fd, id, result);
    } else if (!strcmp(method, "write")) {
        long long address;
        static char hex[2 * 65536 + 2];
        if (!params || !number_value(member(params, "address"), &address)
            || !string_value(member(params, "hex"), hex, sizeof hex) || strlen(hex) % 2) {
            fail(fd, id, -32602, "write needs address and an even-length hex string");
            return;
        }
        size_t length = strlen(hex) / 2;
        if (!length || !writable((unsigned long)address, length)) {
            fail(fd, id, -32000, "range not writable");
            return;
        }
        for (size_t i = 0; i < length; i++) {
            unsigned byte;
            if (sscanf(hex + 2 * i, "%2x", &byte) != 1) {
                fail(fd, id, -32602, "bad hex");
                return;
            }
            ((unsigned char*)(unsigned long)address)[i] = (unsigned char)byte;
        }
        snprintf(result, sizeof result, "{\"written\":%zu}", length);
        reply(fd, id, result);
    } else if (!strcmp(method, "state")) {
        if (!pc_rpc_state) {
            fail(fd, id, -32000, "state needs the battle build");
            return;
        }
        pc_rpc_state(result, sizeof result);
        reply(fd, id, result);
    } else if (!strcmp(method, "quit")) {
        reply(fd, id, "{}");
        fprintf(stderr, "[rpc] quit requested\n");
        exit(0);
    } else {
        snprintf(text, sizeof text, "unknown method %s", method);
        fail(fd, id, -32601, text);
    }
}

/* Methods that edit what the editor's undo covers: each request is one
 * transaction (editor_undo begin/end holds one open across requests). */
static int edits(const char* method, const char* request)
{
    if (!strcmp(method, "actors") || !strcmp(method, "glows") || !strcmp(method, "editor_place")
        || !strcmp(method, "room_groups") || !strcmp(method, "uv_effects") || !strcmp(method, "lights"))
        return 1;
    if (!strcmp(method, "lighting")) { /* revert edits the room; the rest load or save it */
        const char* p = member(request, "params");
        char action[32] = "";
        if (p)
            string_value(member(p, "action"), action, sizeof action);
        return !strcmp(action, "revert");
    }
    /* editor_snap moves the selection only with apply. */
    const char* params = member(request, "params");
    int apply = 0;
    return !strcmp(method, "editor_snap") && params && bool_value(member(params, "apply"), &apply) && apply;
}
static void handle(int fd, const char* request)
{
    extern void pc_undo_prepare(void) __attribute__((weak));
    extern void pc_undo_flush(const char*) __attribute__((weak));
    char method[64] = "";
    string_value(member(request, "method"), method, sizeof method);
    int undoable = pc_undo_prepare && edits(method, request);
    if (undoable)
        pc_undo_prepare();
    handle_method(fd, request);
    if (undoable) {
        char name[96];
        snprintf(name, sizeof name, "%s (RPC)", method);
        pc_undo_flush(name);
    }
}

static void open_listener(void)
{
    initialized = 1;
    const char* path = getenv("ROOD_RPC");
    if (!path || !*path)
        return;
    struct sockaddr_un address;
    if (strlen(path) >= sizeof address.sun_path) {
        fprintf(stderr, "[rpc] socket path too long: %s\n", path);
        return;
    }
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    struct stat existing;
    if (!lstat(path, &existing)) {
        if (!S_ISSOCK(existing.st_mode)) {
            fprintf(stderr, "[rpc] %s exists and is not a socket; not replacing it\n", path);
            return;
        }
        unlink(path);
    }
    listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    mode_t old = umask(077);
    int bound = listener >= 0 && !bind(listener, (struct sockaddr*)&address, sizeof address);
    umask(old);
    if (!bound || listen(listener, MAX_CLIENTS)) {
        fprintf(stderr, "[rpc] cannot listen on %s: %s\n", path, strerror(errno));
        if (listener >= 0)
            close(listener);
        listener = -1;
        return;
    }
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].fd = -1;
    fprintf(stderr, "[rpc] listening on %s\n", path);
}

static void service(void)
{
    if (listener < 0)
        return;
    int fd;
    while ((fd = accept4(listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC)) >= 0) {
        int i = 0;
        while (i < MAX_CLIENTS && clients[i].fd >= 0)
            ++i;
        if (i == MAX_CLIENTS) {
            close(fd);
            continue;
        }
        clients[i].fd = fd;
        clients[i].used = 0;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct client* c = &clients[i];
        if (c->fd < 0)
            continue;
        ssize_t n = read(c->fd, c->buffer + c->used, sizeof c->buffer - 1 - c->used);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(c->fd);
            c->fd = -1;
            continue;
        }
        if (n > 0)
            c->used += (size_t)n;
        char* newline;
        c->buffer[c->used] = 0;
        while ((newline = memchr(c->buffer, '\n', c->used))) {
            *newline = 0;
            /* Blocking writes for the reply; requests are small and local. */
            int flags = fcntl(c->fd, F_GETFL);
            fcntl(c->fd, F_SETFL, flags & ~O_NONBLOCK);
            if (*skip(c->buffer))
                handle(c->fd, c->buffer);
            fcntl(c->fd, F_SETFL, flags);
            size_t rest = c->used - (size_t)(newline + 1 - c->buffer);
            memmove(c->buffer, newline + 1, rest);
            c->used = rest;
            c->buffer[c->used] = 0;
        }
        if (c->used == sizeof c->buffer - 1) {
            fail(c->fd, "null", -32700, "request line too long");
            c->used = 0;
        }
    }
}

/* Pause control shared with the developer overlay (dev_overlay.cpp). */
void pc_rpc_set_paused(int value)
{
    paused = value;
    step_budget = 0;
}
void pc_rpc_request_step(long vsyncs)
{
    paused = 1;
    step_budget += vsyncs;
}
int pc_rpc_is_paused(void) { return paused && step_budget == 0; }

/* Called once per vsync from pc_tick, before the frame is presented. While
 * paused it keeps the window (and overlay) live and serves requests. */
void pc_rpc_poll(void)
{
    if (!initialized)
        open_listener();
    for (int i = 0; i < 16; i++)
        if (holds[i].vsyncs > 0)
            --holds[i].vsyncs;
    if (stick_vsyncs > 0)
        --stick_vsyncs;
    if (listener >= 0)
        service();
    if (paused && step_budget > 0) {
        --step_budget;
        return;
    }
    while (paused && step_budget == 0) {
        struct timespec delay = { 0, 16000000 };
        pc_window_tick();
        pc_window_redraw();
        nanosleep(&delay, NULL);
        if (listener >= 0)
            service();
    }
    if (paused && step_budget > 0)
        --step_budget;
}
