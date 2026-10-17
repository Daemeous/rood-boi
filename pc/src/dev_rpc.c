/* Developer control interface: JSON-RPC 2.0 over a Unix socket (or TCP).
 *
 * Enabled only when ROOD_RPC names a socket path; the socket is created with
 * mode 0600. ROOD_RPC=tcp:PORT listens on 127.0.0.1:PORT instead (Windows
 * has no Unix sockets for Python clients); lines that are not JSON objects
 * close such a connection. Any local program can reach a TCP port, so
 * ROOD_RPC=tcp:PORT:TOKEN makes each connection's first request auth {token}
 * with that token (anything else closes it); rood_rpc.py does this, and
 * Game.launch() makes up a token. Without a token, TCP is for single-user
 * machines only. Each request is one JSON object per line and each response
 * is one line. Requests are serviced on the game thread from pc_tick (once
 * per vsync), so every method runs at a frame boundary with no locking.
 * Methods:
 *
 *   auth {token}                 a TCP connection's first request when
 *                                ROOD_RPC=tcp:PORT:TOKEN
 *   status                       counters (vsync, battle frame, presents: images
 *                                presented, window_presents: of them, those that
 *                                reached a real window; device_lost: Vulkan
 *                                stopped rendering), pause state, display, start state
 *   pause {paused}               stop (or resume) the game at vsync boundaries
 *   step {vsyncs}                run N vsyncs, then pause again
 *   wait {timeout_ms}            reply (as status) once the game is paused with
 *                                no step left, or after timeout_ms (default
 *                                1000, at most 60000): a step's end without
 *                                polling status (rood_rpc.Game.wait_paused)
 *   input {buttons, vsyncs}      hold buttons ("circle", "up", ...) for N vsyncs;
 *         {right_stick: [x, y]}  and/or hold the right stick (-1..1, down +y)
 *         {triggers: [l2, r2]}   and/or the triggers' analog travel (0..1 past
 *                                the press point; the map's zoom, map_pad.h;
 *                                the L2 / R2 bits still come from buttons)
 *   camera_stick {...}           the right-stick camera (camera_stick.c):
 *                                settings (enabled, speed, deadzone, curve,
 *                                invert_x, invert_y, zoom, zoom_speed,
 *                                zoom_min, zoom_max, zoom_pitch, collision,
 *                                r3_reset, idle_reset, keys), action "reset";
 *                                replies the settings and its state (with
 *                                the first-person look: first_person,
 *                                fp_looks, fp_step)
 *   camera_options {map_invert, map_controls, fp_invert_y}  settings
 *                                qol_map_invert (the map screen's panning),
 *                                qol_map_controls (its right stick and
 *                                triggers: 0 Modern, 1 Original) and
 *                                qol_fp_invert_y (first-person up/down):
 *                                sets them, replies all three
 *   button_roles {confirm_cross, field_cross_attack, symbol_exceptions}  settings
 *                                qol_confirm_cross and qol_field_cross_attack
 *                                (button_roles.c, battle builds): sets them;
 *                                replies them, the game's context now
 *                                (menus, field, targeting, game), whether
 *                                Circle and Cross are exchanged there (swap),
 *                                each role's button and the game's own ○ / ×
 *                                drawn since start (drawn: for text and
 *                                sprite, each as shown and how many were
 *                                swapped); symbol_exceptions false (tests)
 *                                exchanges the ○ / × that name buttons the
 *                                roles never move too
 *   weapon_slots {enabled}       the QoL weapon slots (weapon_slots.c,
 *                                settings key qol_weapon_slots): sets the
 *                                option; replies it, the overlay/assign/swap
 *                                state, the field mode and each inventory
 *                                weapon's slot (gems[3]) and shield's slot
 *                                bits (battle builds)
 *   skip_charging {mode, who, name, kinds, reset}  battle charging skip
 *                                (charge_skip.c, settings keys
 *                                qol_skip_charging, qol_skip_charging_who,
 *                                qol_skip_charging_name: name, seconds 0..2
 *                                the name window stays up at least);
 *                                mode 0/"off" or 1/"skip"; who 0/"mine"
 *                                (Ashley's actions) or 1/"all"; kinds (a
 *                                developer mask, default 15: 1 spells, 2
 *                                enemies' arts, 4 Break Arts, 8 items). Replies
 *                                them, whether this vsync is in a wind-up
 *                                (charging, its phase = _cameraMode 4..6),
 *                                skipped, hidden (not presented), and the
 *                                counters: wind-ups seen, their vsyncs (per
 *                                phase too), skipped, hidden, presented, wall
 *                                ms, the last one's actor and action, the
 *                                view at the last release (lookAt xyz,
 *                                position xyz, distance, yaw, the caster's
 *                                facing); reset: true zeroes them after the
 *                                reply. Tests: pin (a seed; -1 off) seeds the
 *                                random numbers whenever an action ends;
 *                                pump false loads the effect's files a step
 *                                a frame, as the game does (default true)
 *   fast_travel {set, press, audit}  fast travel (fast_travel.c): set {enabled,
 *                                cost (0..3 or free / flat / scaled / game),
 *                                flat, scale, prompt} (settings qol_fast_travel*);
 *                                press: the Travel button once, as its key or
 *                                pad button; audit: each destination's Circle
 *                                object read from the disc; replies settings,
 *                                whose unlock (off / port / game), unlock, the
 *                                state, blocked and why (the Travel button's
 *                                reason, "" when it opens the list), each
 *                                point's state / raw / cost / current /
 *                                refused, counters, the last trip (battle builds)
 *   ui_menu {modern}             modern menus (pc/src/ui): sets ui_menus,
 *                                returns the shell (visible, menus, tab,
 *                                tabs, rows, row; loot while the Loot screen
 *                                shows; backdrop_kept: the presenter holds a
 *                                picture of the world for the Ashley tab;
 *                                backdrop_built: [keeps, need] at the
 *                                Ashley model's last build;
 *                                battle builds); {text, key}: typed
 *                                into the Workshop's name field as the
 *                                keyboard types (key: backspace, delete,
 *                                left, right, home, end, enter, escape)
 *   loot {hex, chest}            tests: open the loot menu as the game does
 *                                (whole 0x220-byte vs_battle_loot lists, as
 *                                after a battle; chest: the first as a
 *                                chest's); only while no menu is open
 *   system {window_mode, master_volume, brightness, bind, key, pad, virtual_pad, down,
 *           mouse: [x, y], button}   (mouse: window pixels, button 1-3 with
 *                                down, or 0: a wheel step of `wheel`)
 *                                System settings (pc/src/ui/ui_settings.h hooks):
 *                                set window_mode (windowed | borderless |
 *                                fullscreen), master_volume, brightness; bind
 *                                "circle" starts the Bindings page's capture for
 *                                that button; key "m" / pad "east" with down
 *                                pushes that SDL event (real input path);
 *                                virtual_pad "back" with down sets that element
 *                                on an SDL virtual gamepad the game polls (tests);
 *                                returns the values, the window's state, the
 *                                audio gain (frames recorded with ROOD_AUDIO=wav)
 *                                and the capture state
 *   spu                          the software SPU (audio_spu.cpp): voices keyed
 *                                and sounding, reverb sends and master enable,
 *                                the address interrupt (enabled, address, raised)
 *   release                      drop all injected buttons
 *   screenshot {path}            write the displayed frame as a PPM
 *   present_dump {path}          the next presented window image (as seen) as a PPM
 *   anim_trace {path, count}     the object instances (bone palettes, game frame,
 *                                blend) of the next `count` presented images to a
 *                                binary file (pc/tools/anim_trace.py); no count:
 *                                the images still to trace
 *   ui_bubbles {path}            vector talk bubbles (ui_bubbles.h): counters,
 *                                line segments drawn, recent bubbles (shape,
 *                                bounds drawn and the game's, outline);
 *                                path N: bubble N's outline points
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
 *                                vertex colours), "save", "reload" (read the
 *                                room's files from disk now, dropping unsaved
 *                                edits; "loads" counts layer loads) or "add"
 *                                (a point light at position [x, y, z], selected);
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
 *                                ui_lines ("joined" | "legacy"),
 *                                ui_outline_boost (0-3), ui_bubbles
 *                                ("original" | "vector"), ui_bubble_outline,
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
 *                                packets), native_occlusion, room_order, room_texture_lod
 *                                (coplanar room surfaces in PS1 order;
 *                                room_slack, room_slack_units: how far
 *                                behind the nearest surface still counts as
 *                                on it, relative and GTE units), native_actors
 *                                (0 legacy character/weapon/prop packets, 1
 *                                native meshes with bone palettes, 2 also
 *                                without the reference packets' CPU
 *                                rasterization; drop shadows and particles
 *                                follow it),
 *                                actor_order (native characters paint
 *                                themselves in PS1 order, else depth-test),
 *                                menu_studio (the menu's model view lit by
 *                                its own studio light, 1 default; 0 the
 *                                game's colours and the room's light),
 *                                menu_light (its rig, ui/menu_light.h: an
 *                                object of fields to set, "reset", "save"
 *                                into the theme file, or true to read; the
 *                                reply's menu_light then has every field),
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
 *                                list of group indices (free camera view only);
 *                                room: "ready", or "loading" (no groups while
 *                                a room loads)
 *   editor_view {focus}         the free camera's cutaway focus: [x, y, z]
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
 *   editor_ui {visible, help, tab, details}  open or close the editor (as
 *                                Esc/F1) and its Keyboard shortcuts window; tab
 *                                "outliner" or "library" ("place" too) beside it;
 *                                details: a Details tab by name (details,
 *                                lighting, inspector, atmosphere, post, graphics,
 *                                emissive, diagnostics); replies the states and
 *                                the Details tab shown
 *   editor_select {action, ids, source, x, y | u, v, shift, ctrl, ...}  the
 *                                editor's selection (editor_scene.cpp): list,
 *                                set / add / toggle / remove {ids}, clear;
 *                                replies the selection, its primary, the
 *                                Outliner's row, the hidden set and every
 *                                object with its window pixel
 *   editor_schema {action, id, values, fields}  the editor's property schema
 *                                (editor_schema.cpp): kinds, lint, get {id},
 *                                set {id, values} (a field with Use global,
 *                                as a fog volume's disturb weights: "global",
 *                                null or a negative number follows it; get
 *                                replies the weight in effect), reset {id,
 *                                fields}; ui
 *                                (Details' state: its object, fields, their
 *                                window rectangles, the value ladder)
 *   lua {action, ...}            the extension system (pc/src/lua/lua_rpc.cpp,
 *                                pc/docs/Extensions.md): eval {code, plugin},
 *                                console {input}, plugins, reload {plugin},
 *                                rescan, enable {plugin, enabled}, grant
 *                                {plugin, capability, revoke, elevated} (dev
 *                                mode), budget {...}, test {plugin, filter},
 *                                test_status {run}, log {since}, audit, emit
 *                                {event, payload}, registry {kind}
 *   editor_library {action, ...}  the editor's library (editor_library.cpp):
 *                                list | filter | favourite | view | thumbs |
 *                                save (Save as prefab) | edit_prefab |
 *                                rename | delete | file (an entry's menu)
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
 *   state_flag {id, value}       read (and with value, set) a story/state flag,
 *                                e.g. to get past the prologue in tests
 *   input_record                 recording/replay state (input_record.c):
 *                                mode, file, end vblank, current vblank
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
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#endif
#include "pc_paths.h"
#include "room_effects.h"
#include "render.h"
#include "room_editor.h"
#include "editor_overlays.h"
#include "probe_gi.h"
#include "ui_upscale.h"
#include "ui_text.h"
#include "ui_bubbles.h"
#include "button_prompts.h"
#include "boot_controller.h" /* pc_pad_button_index */
#include "camera_stick.h"
#include "fast_travel.h"
#include "ui/ui_settings.h" /* system: the Bindings page */
/* The editor (editor_overlays.cpp, lights.cpp). */
extern int pc_overlay_wire __attribute__((weak)), pc_overlay_points __attribute__((weak)),
    pc_overlay_actor_wire __attribute__((weak)), pc_overlay_snap_show __attribute__((weak)),
    pc_snap_point __attribute__((weak)), pc_snap_edge __attribute__((weak)), pc_snap_midpoint __attribute__((weak)),
    pc_snap_surface __attribute__((weak)), pc_lights_snap __attribute__((weak)), pc_lights_gizmo_space __attribute__((weak));
extern float pc_overlay_occluded __attribute__((weak)), pc_snap_radius __attribute__((weak)),
    pc_snap_highlight __attribute__((weak)), pc_snap_offset __attribute__((weak)), pc_lights_snap_move __attribute__((weak));
extern unsigned pc_snap_modes(int) __attribute__((weak));
extern void pc_overlay_update(const pc_overlay_view*) __attribute__((weak));
extern void pc_overlay_get_stats(pc_overlay_stats*) __attribute__((weak));
extern void pc_overlay_hold_snap(const pc_snap_result*) __attribute__((weak));
extern int pc_overlay_neighbours(const float*, float, float (*)[4], int) __attribute__((weak));
extern int pc_overlay_sample_points(const pc_overlay_view*, float (*)[3], float (*)[2], int*, int) __attribute__((weak));
extern int pc_overlay_sample_edges(const pc_overlay_view*, float (*)[6], float (*)[4], int*, int) __attribute__((weak));

/* fast_travel.c (battle builds) */
extern void pc_fast_travel_report(pc_ft_report* r, int audit) __attribute__((weak));
extern void pc_fast_travel_press(void) __attribute__((weak));
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
extern int pc_haze_debug __attribute__((weak)), pc_haze_cell __attribute__((weak)); /* atmosphere.cpp */
/* Probe GI (probe_gi.inc). */
extern int pc_probe_rays __attribute__((weak)), pc_probe_budget __attribute__((weak));
extern int pc_probe_show __attribute__((weak));
extern float pc_probe_spacing __attribute__((weak)), pc_probe_hysteresis __attribute__((weak));
extern void pc_probe_get_stats(pc_probe_stats* out) __attribute__((weak));
extern int pc_probe_dump(const char* path) __attribute__((weak));
extern int pc_gte_widescreen;
extern int pc_widescreen_scene __attribute__((weak)); /* gpu_hw.c: 2D of the 3D view fills 16:9 */
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
extern void pc_post_set_flags(int) __attribute__((weak)); /* post_profile.cpp */
extern int pc_post_rpc(const char*, char*, int) __attribute__((weak));
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
extern int pc_qol_map_invert, pc_qol_fp_invert_y, pc_qol_map_controls; /* project_settings.cpp */
extern int pc_qol_confirm_cross, pc_qol_field_cross_attack; /* project_settings.cpp */
extern void pc_button_roles_json(char* out) __attribute__((weak)); /* button_roles.c */
extern void pc_button_roles_symbol_exceptions(int on) __attribute__((weak));
extern void pc_weapon_slots_json(char* out, unsigned size) __attribute__((weak)); /* weapon_slots.c */
extern int pc_ui_menus; /* project_settings.cpp */
extern void pc_ui_shell_json(char* out, unsigned size); /* ui/ui_shell.cpp */
extern void pc_ui_draw_pointer(float x, float y, int click, int right, float wheel, int down, int middle,
    int shift); /* ui/ui_draw.cpp */
extern int pc_ui_menu_active(void) __attribute__((weak)); /* ui/menu_bridge.c (battle builds) */
extern int pc_native_room __attribute__((weak)), pc_native_occlusion __attribute__((weak)),
    pc_native_actors __attribute__((weak)), pc_native_actor_order __attribute__((weak)),
    pc_native_blend __attribute__((weak)), pc_native_objects __attribute__((weak)),
    pc_native_shadows __attribute__((weak)), pc_native_effects __attribute__((weak)),
    pc_reconstruct __attribute__((weak)), pc_native_room_order __attribute__((weak)),
    pc_room_texture_lod __attribute__((weak));
extern float pc_native_room_slack[2] __attribute__((weak));
extern int pc_gte_log __attribute__((weak));
extern void render_stats_get(render_stats*) __attribute__((weak));

#define MAX_CLIENTS 4
#define LINE_MAX_BYTES 262144 /* the rooms list and lighting layers fit */

static int listener = -1, initialized;
static int listener_tcp; /* ROOD_RPC=tcp:PORT */
static char tcp_token[129]; /* ROOD_RPC=tcp:PORT:TOKEN; empty: no auth */
static struct client {
    int fd;
    int authed; /* 0 until a TCP connection sends auth {token} */
    size_t used;
    int waiting;          /* a deferred "wait" reply is owed (wait_id) */
    long long wait_until; /* its deadline, rpc_now_ms() */
    char wait_id[64];
    char buffer[LINE_MAX_BYTES];
} clients[MAX_CLIENTS];
static struct client* current_client; /* the client whose request handle() runs */
static int handled_since_redraw; /* a request was served since the last paused redraw */
/* RPC snapshot save (snapshot.c): the pending request. */
extern int pc_snapshot_available(void);
extern int pc_snapshot_point(const char* path, char* error, int size);
extern int pc_snapshot_hash(char* out, int size);
static struct client* snapshot_client;
static char snapshot_id[64], snapshot_path[1024];
static long long rpc_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Socket calls: POSIX descriptors, or Winsock sockets (kept in the same
 * int fields; Windows socket handles fit 32 bits). */
#ifdef _WIN32
static long rpc_recv(int fd, char* buffer, size_t size) { return recv((SOCKET)fd, buffer, (int)size, 0); }
static long rpc_send(int fd, const char* text, size_t size) { return send((SOCKET)fd, text, (int)size, 0); }
static void rpc_close(int fd) { closesocket((SOCKET)fd); }
static int rpc_retry(void) { return 0; }
static int rpc_would_block(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static void rpc_set_blocking(int fd, int blocking)
{
    u_long nonblocking = !blocking;
    ioctlsocket((SOCKET)fd, FIONBIO, &nonblocking);
}
static int rpc_accept(int fd)
{
    SOCKET s = accept((SOCKET)fd, NULL, NULL);
    if (s == INVALID_SOCKET)
        return -1;
    rpc_set_blocking((int)s, 0);
    return (int)s;
}
#else
static long rpc_recv(int fd, char* buffer, size_t size) { return read(fd, buffer, size); }
static long rpc_send(int fd, const char* text, size_t size) { return write(fd, text, size); }
static void rpc_close(int fd) { close(fd); }
static int rpc_retry(void) { return errno == EINTR; }
static int rpc_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
static void rpc_set_blocking(int fd, int blocking)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
}
static int rpc_accept(int fd) { return accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC); }
#endif
static int paused;
static long step_budget;
static struct hold {
    unsigned mask;
    long vsyncs;
} holds[16];
static float stick_hold[2]; /* input {right_stick: [x, y]} */
static long stick_vsyncs;
static float trigger_hold[2]; /* input {triggers: [l2, r2]} */
static long trigger_vsyncs;

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
        long n = rpc_send(fd, text, length);
        if (n < 0 && rpc_retry())
            continue;
        if (n <= 0)
            return;
        text += n;
        length -= (size_t)n;
    }
}
/* Sounds played through vs_main_playSfxDefault (main.c's ROOD_PC hook: every menu sound, the original
 * screens' and the native menus' alike), counted for the smokes' sound parity (RPC sfx). */
static unsigned sfx_menu[32], sfx_other;
void pc_sfx_played(int bank, int id)
{
    if (bank == 0x7E && id >= 0 && id < 32)
        ++sfx_menu[id];
    else
        ++sfx_other;
}

static void reply(int fd, const char* id, const char* result)
{
    static char line[LINE_MAX_BYTES + 256];
    snprintf(line, sizeof line, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}\n", id, result);
    send_line(fd, line);
}
/* A string as JSON string contents (quotes, backslashes - Windows paths -, control characters
 * escaped), in one of a few rotating buffers so a reply can take two. */
static const char* json_str(const char* text)
{
    static char buffers[4][1024];
    static int next;
    char* out = buffers[next++ & 3];
    size_t used = 0;
    for (; text && *text && used + 7 < sizeof buffers[0]; text++) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') {
            out[used++] = '\\';
            out[used++] = (char)c;
        } else if (c < 0x20)
            used += (size_t)snprintf(out + used, 7, "\\u%04x", c);
        else
            out[used++] = (char)c;
    }
    out[used] = 0;
    return out;
}

static void fail(int fd, const char* id, int code, const char* message)
{
    char line[1024];
    snprintf(line, sizeof line,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":%d,\"message\":\"%s\"}}\n", id, code,
        json_str(message));
    send_line(fd, line);
}

/* ---- methods ---- */

unsigned pc_rpc_buttons(void);
static void status_json(char* out, size_t size)
{
    extern int pc_display_masked __attribute__((weak)); /* SetDispMask(0) */
    extern unsigned pc_presents __attribute__((weak)); /* gl_present.c: images presented */
    extern unsigned pc_window_presents __attribute__((weak)); /* gl_present.c: images that reached the window */
    extern int pc_vk_device_lost(void) __attribute__((weak)); /* vk_core.c */
    snprintf(out, size,
        "{\"vsync\":%u,\"battle_frame\":%u,\"paused\":%s,\"step_budget\":%ld,\"buttons\":%u,"
        "\"display_masked\":%s,\"presents\":%u,\"window_presents\":%u,\"device_lost\":%s}",
        pc_vsync_count(), pc_battle_frame_count(), paused ? "true" : "false", step_budget,
        pc_rpc_buttons(), &pc_display_masked && pc_display_masked ? "true" : "false",
        &pc_presents ? pc_presents : 0u, &pc_window_presents ? pc_window_presents : 0u,
        pc_vk_device_lost && pc_vk_device_lost() ? "true" : "false");
}
/* Deferred "wait" replies: owed once the step is over (paused, no budget)
 * or the deadline passed. */
static void serve_waiters(void)
{
    long long now = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct client* c = &clients[i];
        if (c->fd < 0 || !c->waiting)
            continue;
        if (!(paused && step_budget == 0)) {
            if (now < 0)
                now = rpc_now_ms();
            if (now < c->wait_until)
                continue;
        }
        char result[384];
        status_json(result, sizeof result);
        c->waiting = 0;
        rpc_set_blocking(c->fd, 1);
        reply(c->fd, c->wait_id, result);
        rpc_set_blocking(c->fd, 0);
    }
}

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
/* RPC input {triggers}: the triggers' analog travel while held (ui/map_bridge.c). */
int pc_rpc_triggers(float out[2])
{
    if (trigger_vsyncs <= 0)
        return 0;
    out[0] = trigger_hold[0];
    out[1] = trigger_hold[1];
    return 1;
}
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
#ifdef _WIN32
#define __data_start __data_start__
#define _end __bss_end__
#endif
extern char __data_start[], _end[];
static int readable(uintptr_t address, uintptr_t length)
{
    uintptr_t end = address + length;
    if (length > 65536 || end < address)
        return 0;
    if (address >= 0x80000000ul && end <= 0x80200000ul)
        return 1;
    if (address >= 0x1F800000ul && end <= 0x1F800400ul)
        return 1;
    return address >= (uintptr_t)__data_start && end <= (uintptr_t)_end;
}
static int writable(uintptr_t address, uintptr_t length)
{
    uintptr_t end = address + length;
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
        status_json(result, sizeof result);
        reply(fd, id, result);
    } else if (!strcmp(method, "snapshot")) {
        char action[32] = "save";
        if (params)
            string_value(member(params, "action"), action, sizeof action);
        if (!strcmp(action, "hash")) {
            pc_snapshot_hash(result, (int)sizeof result);
            reply(fd, id, result);
        } else if (!strcmp(action, "available")) {
            reply(fd, id, pc_snapshot_available() ? "{\"available\":true}" : "{\"available\":false}");
        } else if (!strcmp(action, "save")) {
            if (!pc_snapshot_available()) {
                fail(fd, id, -32000, "save states are unavailable in this process (see the log)");
                return;
            }
            if (!params || !string_value(member(params, "path"), snapshot_path, sizeof snapshot_path)
                || !snapshot_path[0] || !current_client || strlen(id) >= sizeof snapshot_id) {
                fail(fd, id, -32602, "snapshot save needs a path");
                return;
            }
            /* Taken by pc_rpc_poll once this request's handling is over (on
             * the game stack, between frames); the reply comes from there. */
            snapshot_client = current_client;
            strcpy(snapshot_id, id);
        } else
            fail(fd, id, -32602, "snapshot action: save, hash or available");
    } else if (!strcmp(method, "wait")) {
        long long timeout = 1000;
        if (params && number_value(member(params, "timeout_ms"), &n))
            timeout = n < 0 ? 0 : n > 60000 ? 60000 : n;
        if (!current_client || strlen(id) >= sizeof current_client->wait_id) {
            fail(fd, id, -32602, "wait: request id too long");
            return;
        }
        current_client->waiting = 1;
        current_client->wait_until = rpc_now_ms() + timeout;
        strcpy(current_client->wait_id, id);
        serve_waiters(); /* at once when the game is paused already */
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
        double stick[2], trig[2];
        int has_stick = params && pair_value(member(params, "right_stick"), stick);
        n = 2;
        if (params)
            number_value(member(params, "vsyncs"), &n);
        if (params && pair_value(member(params, "triggers"), trig)) {
            if (n < 1) {
                fail(fd, id, -32602, "bad vsyncs");
                return;
            }
            for (int k = 0; k < 2; k++)
                trigger_hold[k] = (float)(trig[k] < 0 ? 0 : trig[k] > 1 ? 1 : trig[k]);
            trigger_vsyncs = (long)n;
            if (!has_stick && !member(params, "buttons")) {
                snprintf(result, sizeof result, "{\"triggers\":[%g,%g],\"vsyncs\":%lld}", trigger_hold[0],
                    trigger_hold[1], n);
                reply(fd, id, result);
                return;
            }
        }
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
    } else if (!strcmp(method, "camera_options")) {
        /* Map panning, the map's stick and triggers (0 Modern, 1 Original; ui/map_pad.h) and
         * first-person up/down (MENU5, BATTLE func_8006EC7C). */
        int flag;
        if (params && bool_value(member(params, "map_invert"), &flag))
            pc_qol_map_invert = flag;
        if (params && number_value(member(params, "map_controls"), &n) && n >= 0 && n <= 1)
            pc_qol_map_controls = (int)n;
        if (params && bool_value(member(params, "fp_invert_y"), &flag))
            pc_qol_fp_invert_y = flag;
        snprintf(result, sizeof result, "{\"map_invert\":%d,\"map_controls\":%d,\"fp_invert_y\":%d}",
            pc_qol_map_invert != 0, pc_qol_map_controls, pc_qol_fp_invert_y != 0);
        reply(fd, id, result);
    } else if (!strcmp(method, "button_roles")) {
        /* Circle and Cross exchanged by the game's context (button_roles.c). */
        int flag;
        if (!pc_button_roles_json) {
            fail(fd, id, -32000, "button_roles needs the battle build");
            return;
        }
        if (params && bool_value(member(params, "confirm_cross"), &flag))
            pc_qol_confirm_cross = flag;
        if (params && bool_value(member(params, "field_cross_attack"), &flag))
            pc_qol_field_cross_attack = flag;
        if (params && bool_value(member(params, "symbol_exceptions"), &flag))
            pc_button_roles_symbol_exceptions(flag);
        pc_button_roles_json(result);
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
    } else if (!strcmp(method, "skip_charging")) {
        /* Battle charging skip (charge_skip.c, charge_skip.inc). */
        extern int pc_qol_skip_charging, pc_qol_skip_charging_who; /* project_settings.cpp */
        extern int pc_charge_skip_kinds; /* charge_skip.c */
        extern void pc_charge_skip_stats(char*, int, int);
        char name[16] = "";
        int reset = 0, flag;
        if (params && number_value(member(params, "mode"), &n) && n >= 0 && n <= 1)
            pc_qol_skip_charging = (int)n;
        else if (params && string_value(member(params, "mode"), name, sizeof name)) {
            if (strcmp(name, "off") && strcmp(name, "skip")) {
                fail(fd, id, -32602, "skip_charging mode: 0, 1, off or skip");
                return;
            }
            pc_qol_skip_charging = name[0] == 's';
        }
        if (params && number_value(member(params, "who"), &n) && n >= 0 && n <= 1)
            pc_qol_skip_charging_who = (int)n;
        else if (params && string_value(member(params, "who"), name, sizeof name)) {
            if (strcmp(name, "mine") && strcmp(name, "all")) {
                fail(fd, id, -32602, "skip_charging who: 0, 1, mine or all");
                return;
            }
            pc_qol_skip_charging_who = name[0] == 'a';
        }
        if (params && number_value(member(params, "kinds"), &n) && n >= 0 && n <= 15)
            pc_charge_skip_kinds = (int)n;
        extern float pc_qol_skip_charging_name; /* project_settings.cpp */
        double seconds;
        if (params && real_value(member(params, "name"), &seconds) && seconds >= 0 && seconds <= 2)
            pc_qol_skip_charging_name = (float)seconds;
        extern int pc_charge_skip_pump __attribute__((weak)); /* charge_skip.inc (battle builds) */
        if (params && bool_value(member(params, "pump"), &flag) && &pc_charge_skip_pump)
            pc_charge_skip_pump = flag;
        if (params && number_value(member(params, "pin"), &n)) {
            extern void pc_charge_skip_pin(int, int);
            pc_charge_skip_pin(n >= 0, n >= 0 ? (int)n : 0);
        }
        if (params)
            bool_value(member(params, "reset"), &reset);
        pc_charge_skip_stats(result, sizeof result, reset);
        reply(fd, id, result);
    } else if (!strcmp(method, "fast_travel")) {
        /* Fast travel (fast_travel.c, fast_travel_rules.c pc_ft_json). */
        static pc_ft_report report;
        const char* set = params ? member(params, "set") : NULL;
        int flag, audit = 0, press = 0;
        double real;
        if (!pc_fast_travel_report) {
            fail(fd, id, -32000, "fast_travel needs the battle build");
            return;
        }
        if (set) {
            char name[16] = "";
            if (bool_value(member(set, "enabled"), &flag))
                pc_qol_fast_travel = flag;
            if (number_value(member(set, "cost"), &n) && n >= 0 && n < PC_FT_COST_MODES)
                pc_qol_fast_travel_cost = (int)n;
            else if (string_value(member(set, "cost"), name, sizeof name)) {
                int k;
                for (k = 0; k < PC_FT_COST_MODES && strcmp(name, pc_ft_cost_names[k]); k++)
                    ;
                if (k == PC_FT_COST_MODES) {
                    fail(fd, id, -32602, "fast_travel cost: 0..3 or free, flat, scaled, game");
                    return;
                }
                pc_qol_fast_travel_cost = k;
            }
            if (number_value(member(set, "flat"), &n) && n >= 0 && n <= PC_FT_COST_MAX)
                pc_qol_fast_travel_flat = (int)n;
            if (real_value(member(set, "scale"), &real) && real >= 0 && real <= 10)
                pc_qol_fast_travel_scale = (float)real;
            if (bool_value(member(set, "prompt"), &flag))
                pc_qol_fast_travel_prompt = flag;
        }
        if (params && bool_value(member(params, "press"), &press) && press && pc_fast_travel_press)
            pc_fast_travel_press();
        if (params)
            bool_value(member(params, "audit"), &audit);
        pc_fast_travel_report(&report, audit);
        if (pc_ft_json(&report, result, (int)sizeof result) < 0) {
            fail(fd, id, -32000, "fast_travel: the report does not fit");
            return;
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "ui_menu")) {
        /* Modern menus (pc/src/ui): {modern} sets ui_menus; the shell's state. */
        int flag;
        if (!pc_ui_menu_active) {
            fail(fd, id, -32000, "ui_menu needs the battle build");
            return;
        }
        if (params && bool_value(member(params, "modern"), &flag))
            pc_ui_menus = flag;
        /* {model_heap_extra: bytes}: the Ashley model's heap check asks for
         * this much more (a huge value plays a full heap; ashley_bridge.c) */
        extern int pc_ui_ashley_heap_extra __attribute__((weak));
        if (params && number_value(member(params, "model_heap_extra"), &n) && &pc_ui_ashley_heap_extra)
            pc_ui_ashley_heap_extra = n < 0 ? 0 : n > 0x7FFFFFF ? 0x7FFFFFF : (int)n;
        /* {pointer: {x, y, click, right, wheel, down, middle, shift}}: the next
         * drawn frame's pointer in layout units (hit tests as a moved mouse;
         * down: the left button held, middle: the middle one, drags; shift:
         * Shift held; ui_draw.cpp) */
        const char* pointer = params ? member(params, "pointer") : NULL;
        if (pointer) {
            double x = -1, y = -1, wheel = 0;
            int click = 0, right = 0, down = 0, middle = 0, shift = 0;
            real_value(member(pointer, "x"), &x);
            real_value(member(pointer, "y"), &y);
            real_value(member(pointer, "wheel"), &wheel);
            bool_value(member(pointer, "click"), &click);
            bool_value(member(pointer, "right"), &right);
            bool_value(member(pointer, "down"), &down);
            bool_value(member(pointer, "middle"), &middle);
            bool_value(member(pointer, "shift"), &shift);
            pc_ui_draw_pointer((float)x, (float)y, click, right, (float)wheel, down, middle, shift);
        }
        /* {score_tune: {key, index, value} | {reset: true}}: the Score screen's theme group over the file
         * (ui/ui_theme.cpp ui::score_tune; key "row_h", or a rect's end "wide.titles_list" with index 0 / 1) */
        extern int pc_ui_score_tune(const char* key, int index, double value) __attribute__((weak));
        const char* tune = params ? member(params, "score_tune") : NULL;
        if (tune && pc_ui_score_tune) {
            char key[64];
            double value = 0;
            long long index = 0;
            int reset = 0;
            if (bool_value(member(tune, "reset"), &reset) && reset) {
                pc_ui_score_tune(NULL, 0, 0);
            } else if (!string_value(member(tune, "key"), key, sizeof key) || !real_value(member(tune, "value"), &value)
                || (number_value(member(tune, "index"), &index), !pc_ui_score_tune(key, (int)index, value))) {
                fail(fd, id, -32602, "score_tune needs a known key (and index for a rect) and a value");
                return;
            }
        }
        /* {text, key}: the Workshop's name field, as typed on the keyboard (ui/ui_workshop.cpp) */
        extern void pc_ui_workshop_text(const char* utf8) __attribute__((weak));
        extern void pc_ui_workshop_key(int key) __attribute__((weak));
        char typed[128];
        if (params && string_value(member(params, "text"), typed, sizeof typed) && pc_ui_workshop_text)
            pc_ui_workshop_text(typed);
        if (params && string_value(member(params, "key"), typed, sizeof typed) && pc_ui_workshop_key) {
            static const char* const keys[] = { "backspace", "delete", "left", "right", "home", "end", "enter",
                "escape" };
            for (int k = 0; k < (int)(sizeof keys / sizeof keys[0]); ++k)
                if (!strcmp(typed, keys[k]))
                    pc_ui_workshop_key(k);
        }
        /* {reequip: true}: tests re-equip the equipped weapon and shield from their records
         * (ui/workshop_bridge.c pc_ui_workshop_debug_reequip; only with no menu open) */
        extern int pc_ui_workshop_debug_reequip(void) __attribute__((weak));
        if (params && bool_value(member(params, "reequip"), &flag) && flag && pc_ui_workshop_debug_reequip
            && pc_ui_workshop_debug_reequip() < 0) {
            fail(fd, id, -32000, "reequip: a menu is open");
            return;
        }
        pc_ui_shell_json(result, sizeof result);
        /* backdrop_kept: the presenter holds a picture of the world for the Ashley tab's model view
         * (gl_present.c / vk_present.c "The modern menu's backdrop") */
        extern int pc_gl_menu_backdrop_kept(void) __attribute__((weak));
        extern int pc_vk_menu_backdrop_kept(void) __attribute__((weak));
        extern void pc_ui_ashley_backdrop_built(int* kept, int* need); /* ui/ashley_bridge.c */
        size_t len = strlen(result);
        if (len > 1 && result[len - 1] == '}' && len + 64 < sizeof result) {
            /* (only the active presenter keeps one) */
            int kept = (pc_gl_menu_backdrop_kept && pc_gl_menu_backdrop_kept() != 0)
                    || (pc_vk_menu_backdrop_kept && pc_vk_menu_backdrop_kept() != 0);
            int built_kept, built_need;
            pc_ui_ashley_backdrop_built(&built_kept, &built_need);
            snprintf(result + len - 1, sizeof result - len + 1, ",\"backdrop_kept\":%d,\"backdrop_built\":[%d,%d]}",
                kept, built_kept, built_need);
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "loot")) {
        /* Tests: the loot menu opened as the game opens it (ui/loot_bridge.c
         * pc_ui_loot_debug_open): hex = loot lists of 0x220 bytes each (BATTLE
         * vs_battle_loot), chest = open the first as a chest's. Only while no
         * menu is open. */
        extern int pc_ui_loot_debug_open(const unsigned char* data, int nodes, int chest) __attribute__((weak));
        enum { LOOT_BYTES = 0x220, LOOT_MAX = 8 };
        static char hex[2 * LOOT_BYTES * LOOT_MAX + 2];
        static unsigned char data[LOOT_BYTES * LOOT_MAX];
        int chest = 0;
        size_t length;
        if (!pc_ui_loot_debug_open) {
            fail(fd, id, -32000, "loot needs the battle build");
            return;
        }
        if (!params || !string_value(member(params, "hex"), hex, sizeof hex) || !(length = strlen(hex) / 2)
            || strlen(hex) % (2 * LOOT_BYTES)) {
            fail(fd, id, -32602, "loot needs hex: whole loot lists of 0x220 bytes (at most 8)");
            return;
        }
        for (size_t i = 0; i < length; i++) {
            unsigned byte;
            if (sscanf(hex + 2 * i, "%2x", &byte) != 1) {
                fail(fd, id, -32602, "bad hex");
                return;
            }
            data[i] = (unsigned char)byte;
        }
        bool_value(member(params, "chest"), &chest);
        snprintf(result, sizeof result, "{\"opened\":%s}",
            pc_ui_loot_debug_open(data, (int)(length / LOOT_BYTES), chest) ? "true" : "false");
        reply(fd, id, result);
    } else if (!strcmp(method, "chest")) {
        /* Tests: the room's chests (ui/loot_bridge.c pc_ui_loot_debug_chest);
         * open = index opens that one through the field's own chest flow
         * (func_80074050, as the field's Cross at a chest). */
        extern int pc_ui_loot_debug_chest(int index, int out[12]) __attribute__((weak));
        extern int pc_ui_loot_debug_chest_open(int index) __attribute__((weak));
        int v[12], n, open = -1, len;
        long long number;
        if (!pc_ui_loot_debug_chest) {
            fail(fd, id, -32000, "chest needs the battle build");
            return;
        }
        if (params && number_value(member(params, "open"), &number))
            open = (int)number;
        if (open >= 0) {
            snprintf(result, sizeof result, "{\"opened\":%s}", pc_ui_loot_debug_chest_open(open) ? "true" : "false");
            reply(fd, id, result);
            return;
        }
        n = pc_ui_loot_debug_chest(-1, v);
        len = snprintf(result, sizeof result, "{\"field_free\":%s,\"chests\":[", v[11] ? "true" : "false");
        for (int i = 0; i < n && len < (int)sizeof result - 256; ++i) {
            pc_ui_loot_debug_chest(i, v);
            len += snprintf(result + len, sizeof result - len,
                "%s{\"index\":%d,\"x\":%d,\"z\":%d,\"floor\":%d,\"flag\":%d,\"opened\":%d,\"key\":%d,"
                "\"weapon\":%d,\"shield\":%d,\"armor\":%d,\"gem\":%d,\"misc\":%d}",
                i ? "," : "", i, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10]);
        }
        snprintf(result + len, sizeof result - len, "]}");
        reply(fd, id, result);
    } else if (!strcmp(method, "container")) {
        /* Tests: the save room's container opened as the field opens it
         * (ui/loot_bridge.c pc_ui_container_debug_open; location: the container's
         * number, default the room's save point), where the room has no container
         * object for RPC chest open=. */
        extern int pc_ui_container_debug_open(int location) __attribute__((weak));
        long long number = -1;
        if (!pc_ui_container_debug_open) {
            fail(fd, id, -32000, "container needs the battle build");
            return;
        }
        if (params)
            number_value(member(params, "location"), &number);
        snprintf(result, sizeof result, "{\"opened\":%s}",
            pc_ui_container_debug_open((int)number) ? "true" : "false");
        reply(fd, id, result);
    } else if (!strcmp(method, "release")) {
        memset(holds, 0, sizeof holds);
        stick_vsyncs = trigger_vsyncs = 0;
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
        snprintf(result, sizeof result, "{\"path\":\"%s\",\"width\":%d,\"height\":%d}", json_str(path), width,
            height);
        reply(fd, id, result);
    } else if (!strcmp(method, "graphics")) {
        static const char* const denoiser_names[3] = { "raw", "filter", "nrd" };
        double real_emissive;
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

        if (params && bool_value(member(params, "widescreen_scene"), &flag) && &pc_widescreen_scene)
            pc_widescreen_scene = flag;
        /* Diagnostic: pixels a frame does not draw show magenta (boot_sdk.c PutDrawEnv). */
        extern int pc_undrawn_debug __attribute__((weak));
        if (params && bool_value(member(params, "undrawn_debug"), &flag) && &pc_undrawn_debug)
            pc_undrawn_debug = flag;
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
            /* ui_text_layout "line" | "glyph" (or 1/0). */
            if (string_value(member(params, "ui_text_layout"), name, sizeof name)) {
                for (int k = 0; k < 2; k++)
                    if (!strcmp(name, pc_ui_text_layout_names[k]))
                        pc_ui_text_layout = k;
            } else if (number_value(member(params, "ui_text_layout"), &n) && n >= 0 && n <= 1)
                pc_ui_text_layout = (int)n;
            /* ui_text_overlay "auto" | "off" (or 1/0). */
            if (string_value(member(params, "ui_text_overlay"), name, sizeof name))
                pc_ui_text_setting("ui_text_overlay", name);
            else if (number_value(member(params, "ui_text_overlay"), &n) && n >= 0 && n <= 1)
                pc_ui_text_overlay = (int)n;
            /* ui_text_bubble_align "left" | "centre" (or 0/1). */
            if (string_value(member(params, "ui_text_bubble_align"), name, sizeof name))
                pc_ui_text_setting("ui_text_bubble_align", name);
            else if (number_value(member(params, "ui_text_bubble_align"), &n) && n >= 0 && n <= 1)
                pc_ui_text_bubble_align = (int)n;
            /* ui_lines "joined" | "legacy", ui_outline_boost 0-3, ui_bubbles
             * "original" | "vector", ui_bubble_outline (ui_bubbles.h). */
            if (string_value(member(params, "ui_lines"), name, sizeof name)) {
                for (int k = 0; k < 2; k++)
                    if (!strcmp(name, pc_ui_lines_names[k]))
                        pc_ui_lines = k;
            } else if (number_value(member(params, "ui_lines"), &n) && n >= 0 && n <= 1)
                pc_ui_lines = (int)n;
            if (number_value(member(params, "ui_outline_boost"), &n) && n >= 0 && n <= 3)
                pc_ui_outline_boost = (int)n;
            if (string_value(member(params, "ui_bubbles"), name, sizeof name)) {
                for (int k = 0; k < 2; k++)
                    if (!strcmp(name, pc_ui_bubbles_names[k]))
                        pc_ui_bubbles = k;
            } else if (number_value(member(params, "ui_bubbles"), &n) && n >= 0 && n <= 1)
                pc_ui_bubbles = (int)n;
            double outline;
            if (real_value(member(params, "ui_bubble_outline"), &outline) && outline >= 0.25 && outline <= 8)
                pc_ui_bubble_outline = (float)outline;
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
            /* haze_debug 1: the haze's light alone; haze_cell: the froxel size (atmosphere.h). */
            if (&pc_haze_debug && number_value(member(params, "haze_debug"), &n))
                pc_haze_debug = (int)n;
            if (&pc_haze_cell && number_value(member(params, "haze_cell"), &n) && n >= 1 && n <= 16)
                pc_haze_cell = (int)n;
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
        if (params && number_value(member(params, "post"), &n) && pc_post_set_flags)
            pc_post_set_flags((int)n & 7); /* the user's gameplay layer (post.h) */
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
            if (&pc_native_room_order) {
                if (bool_value(member(params, "room_order"), &flag))
                    pc_native_room_order = flag;
                else if (number_value(member(params, "room_order"), &n))
                    pc_native_room_order = n != 0;
            }
            if (&pc_room_texture_lod) {
                if (bool_value(member(params, "room_texture_lod"), &flag))
                    pc_room_texture_lod = flag;
                else if (number_value(member(params, "room_texture_lod"), &n))
                    pc_room_texture_lod = n != 0;
            }
            if (pc_native_room_slack) {
                if (number_value(member(params, "room_slack"), &n) && n >= 0 && n <= 0.1)
                    pc_native_room_slack[0] = (float)n;
                if (number_value(member(params, "room_slack_units"), &n) && n >= 0 && n <= 64)
                    pc_native_room_slack[1] = (float)n;
            }
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
            { "editor_gizmo_space", &pc_lights_gizmo_space, NULL },
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
        /* Emissive texels' strength (emissive.cpp: 0 off, up to 2 in the menus, 4 here). */
        extern float pc_emissive __attribute__((weak));
        if (params && &pc_emissive && real_value(member(params, "emissive"), &real_emissive) && real_emissive >= 0
            && real_emissive <= 4)
            pc_emissive = (float)real_emissive;
        /* Dust motes (particles.cpp: 0 off, 1 low, 2 high). */
        extern int pc_dust __attribute__((weak));
        if (params && &pc_dust && number_value(member(params, "dust"), &n) && n >= 0 && n <= 2)
            pc_dust = (int)n;
        int used = snprintf(result, sizeof result,
            "{\"hardware\":%s,\"scale\":%d,\"filter\":%d,\"aspect\":%d,\"widescreen\":%s,\"widescreen_scene\":%d,"
            "\"perspective\":%s,\"show_depth\":%s,\"native_room\":%d,\"native_occlusion\":%s,"
            "\"room_order\":%s,\"room_slack\":%g,\"room_slack_units\":%g,"
            "\"native_actors\":%d,\"actor_order\":%s,\"native_blend\":%d,\"native_objects\":%d,"
            "\"native_shadows\":%d,\"native_effects\":%d,\"reconstruct\":%d,\"gte_log\":%d,"
            "\"gi_denoiser\":\"%s\",\"gi_denoiser_active\":\"%s\",\"nrd_built\":%s,\"nrd_available\":%s,"
            "\"nrd_glow\":%s}",
            pc_hw_active ? "true" : "false", pc_hw_scale, pc_gl_filter, pc_gl_aspect,
            pc_gte_widescreen ? "true" : "false", &pc_widescreen_scene ? pc_widescreen_scene : -2,
            pc_hw_perspective ? "true" : "false",
            pc_gl_show_depth ? "true" : "false", &pc_native_room ? pc_native_room : 0,
            &pc_native_occlusion && pc_native_occlusion ? "true" : "false",
            &pc_native_room_order && pc_native_room_order ? "true" : "false",
            pc_native_room_slack ? (double)pc_native_room_slack[0] : 0.0,
            pc_native_room_slack ? (double)pc_native_room_slack[1] : 0.0, &pc_native_actors ? pc_native_actors : 0,
            &pc_native_actor_order && pc_native_actor_order ? "true" : "false",
            &pc_native_blend ? pc_native_blend : 0, &pc_native_objects ? pc_native_objects : 0,
            &pc_native_shadows ? pc_native_shadows : 0, &pc_native_effects ? pc_native_effects : 0,
            &pc_reconstruct ? pc_reconstruct : 1, &pc_gte_log ? pc_gte_log : 1,
            denoiser_names[&pc_gi_denoise ? (pc_gi_denoise < 0 ? 0 : pc_gi_denoise > 2 ? 2 : pc_gi_denoise) : 1],
            denoiser_names[pc_gi_denoiser_active ? pc_gi_denoiser_active() : 1],
            vk_nrd_built && vk_nrd_built() ? "true" : "false", vk_nrd_available && vk_nrd_available() ? "true" : "false",
            &pc_nrd_glow && pc_nrd_glow ? "true" : "false");
        if (&pc_emissive && used < (int)sizeof result - 64)
            used += snprintf(result + used - 1, sizeof result - used + 1, ",\"emissive\":%g}", (double)pc_emissive) - 1;
        if (&pc_dust && used < (int)sizeof result - 64)
            used += snprintf(result + used - 1, sizeof result - used + 1, ",\"dust\":%d}", pc_dust) - 1;
        extern int pc_hw_menu_studio __attribute__((weak)); /* gpu_hw.c */
        if (&pc_hw_menu_studio) {
            if (params && bool_value(member(params, "menu_studio"), &flag))
                pc_hw_menu_studio = flag;
            else if (params && number_value(member(params, "menu_studio"), &n))
                pc_hw_menu_studio = n != 0;
            if (used < (int)sizeof result - 64)
                used += snprintf(result + used - 1, sizeof result - used + 1, ",\"menu_studio\":%d}", pc_hw_menu_studio) - 1;
        }
        /* The studio light's rig (ui/menu_light.h): read, set fields, "reset", "save"; only when asked. */
        extern int pc_ui_menu_light_rpc(const char*, char*, int) __attribute__((weak));
        const char* menu_light = params ? member(params, "menu_light") : NULL;
        const char* menu_light_end = menu_light ? value_end(menu_light) : NULL;
        if (menu_light_end && pc_ui_menu_light_rpc) {
            char value[2048], rig[2048];
            snprintf(value, sizeof value, "%.*s", (int)(menu_light_end - menu_light), menu_light);
            if (pc_ui_menu_light_rpc(value, rig, sizeof rig)) {
                fail(fd, id, -32602, rig);
                return;
            }
            if (used < (int)sizeof result - (int)strlen(rig) - 32)
                used += snprintf(result + used - 1, sizeof result - used + 1, ",\"menu_light\":%s}", rig) - 1;
        }
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
            used += snprintf(result + used - 1, sizeof result - used + 1,
                        ",\"ui_upscale\":\"%s\",\"ui_subpixel\":%d,\"ui_text\":\"%s\",\"ui_text_layout\":\"%s\","
                        "\"ui_text_bubble_align\":\"%s\",\"ui_text_overlay\":\"%s\"}",
                        pc_ui_upscale_names[pc_ui_upscale ? 1 : 0], pc_ui_subpixel, pc_ui_text_names[pc_ui_text ? 1 : 0],
                        pc_ui_text_layout_names[pc_ui_text_layout ? 1 : 0],
                        pc_ui_text_bubble_align_names[pc_ui_text_bubble_align ? 1 : 0],
                        pc_ui_text_overlay_names[pc_ui_text_overlay ? 1 : 0])
                - 1;
        if (used < (int)sizeof result - 160)
            used += snprintf(result + used - 1, sizeof result - used + 1,
                        ",\"ui_lines\":\"%s\",\"ui_outline_boost\":%d,\"ui_bubbles\":\"%s\",\"ui_bubble_outline\":%g}",
                        pc_ui_lines_names[pc_ui_lines ? 1 : 0], pc_ui_outline_boost,
                        pc_ui_bubbles_names[pc_ui_bubbles ? 1 : 0], pc_ui_bubble_outline)
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
    } else if (!strcmp(method, "ui_bubbles")) {
        /* ui_bubbles.h: vector talk bubbles drawn, each recent bubble's
         * shape, placement (outline bounds as drawn and the game's whole
         * pixels) and outline; {path: N} adds bubble N's outline points. */
        pc_ui_bubbles_stats st;
        pc_ui_bubbles_get_stats(&st);
        int used = snprintf(result, sizeof result,
            "{\"ui_bubbles\":\"%s\",\"frames\":%ld,\"verified\":%ld,\"mismatched\":%ld,\"drawn\":%ld,"
            "\"native_per_output\":%g,\"lines_drawn\":%ld,\"bubbles\":[",
            pc_ui_bubbles_names[pc_ui_bubbles ? 1 : 0], st.frames, st.verified, st.mismatched, st.drawn,
            pc_hw_native_per_output(), pc_hw_lines_drawn);
        for (int i = 0; i < st.count && used < (int)sizeof result - 400; i++)
            used += snprintf(result + used, sizeof result - used,
                "%s{\"id\":%d,\"shape\":%d,\"flip\":%d,\"tail\":%d,\"points\":%d,\"bounds\":[%g,%g,%g,%g],"
                "\"game_bounds\":[%d,%d,%d,%d],\"thickness\":%g,\"line_rgb\":%u}",
                i ? "," : "", st.bubble[i].id, st.bubble[i].shape, st.bubble[i].flip, st.bubble[i].tail,
                st.bubble[i].points, st.bubble[i].bounds[0], st.bubble[i].bounds[1], st.bubble[i].bounds[2],
                st.bubble[i].bounds[3], st.bubble[i].game_bounds[0], st.bubble[i].game_bounds[1],
                st.bubble[i].game_bounds[2], st.bubble[i].game_bounds[3], st.bubble[i].thickness,
                st.bubble[i].line_rgb);
        used += snprintf(result + used, sizeof result - used, "]");
        {
            long balloons, dots, marks; /* emote bubbles drawn again (ui_emotes.c) */
            pc_ui_emote_stats(&balloons, &dots, &marks);
            used += snprintf(result + used, sizeof result - used,
                ",\"emotes\":{\"balloons\":%ld,\"dots\":%ld,\"marks\":%ld}", balloons, dots, marks);
        }
        long long which;
        if (params && number_value(member(params, "path"), &which)) {
            float pts[128][2];
            int count = pc_ui_bubble_path((int)which, pts, 128);
            used += snprintf(result + used, sizeof result - used, ",\"path\":[");
            for (int i = 0; i < count && used < (int)sizeof result - 64; i++)
                used += snprintf(result + used, sizeof result - used, "%s[%g,%g]", i ? "," : "", pts[i][0], pts[i][1]);
            used += snprintf(result + used, sizeof result - used, "]");
        }
        snprintf(result + used, sizeof result - used, "}");
        reply(fd, id, result);
    } else if (!strcmp(method, "ui_text")) {
        /* ui_text.h: the option, its styles (font, placement), fonts loaded,
         * glyph fields made, regions and labels drawn; {style, font, stretch,
         * slant, embolden, aspect} changes a style (as the settings keys do). */
        char style[32], text[300];
        if (params && string_value(member(params, "style"), style, sizeof style)) {
            static const char* const keys[5] = { "font", "stretch", "slant", "embolden", "aspect" };
            int known = 0;
            for (int i = 0; i < PC_UI_TEXT_STYLES; i++)
                known |= !strcmp(style, pc_ui_text_styles[i].name);
            if (!known) {
                fail(fd, id, -32602, "style: dialogue, menu, info or digits");
                return;
            }
            for (int k = 0; k < 5; k++) {
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
        if (params && string_value(member(params, "layout"), style, sizeof style))
            pc_ui_text_setting("ui_text_layout", style);
        if (params && string_value(member(params, "bubble_align"), style, sizeof style))
            pc_ui_text_setting("ui_text_bubble_align", style);
        {
            double scale; /* the UI text scale (no setting yet; tests) */
            if (params && real_value(member(params, "scale"), &scale))
                pc_ui_text_set_scale((float)scale);
        }
        {
            int forget = 0;
            if (params && bool_value(member(params, "forget_signs"), &forget) && forget)
                pc_ui_text_forget_signs();
        }
        /* {lines: PATH}: the lines laid out in the newest frame that drew
         * text (ui_text_layout line), as JSON into PATH. Tests. */
        long lines_bytes = -1;
        {
            char path[512];
            if (params && string_value(member(params, "lines"), path, sizeof path)) {
                int cap = 8 << 20;
                char* text_out = malloc((size_t)cap);
                FILE* out = text_out ? fopen(path, "wb") : NULL;
                if (out) {
                    lines_bytes = pc_ui_text_lines_json(text_out, cap);
                    fwrite(text_out, 1, (size_t)lines_bytes, out);
                    fclose(out);
                }
                free(text_out);
            }
        }
        /* {render: [tpage, clut, u0, v0, u1, v1], factor, path}: that 2D
         * region drawn from the font as the atlas would (a glyph or a
         * label; PS1 colours, little-endian u16, row by row) into PATH;
         * the reply's "render" has its size (ok 0: not text). Tests. */
        const char* render = params ? member(params, "render") : NULL;
        int rendered = -1, rw = 0, rh = 0;
        if (render) {
            int r[6];
            long long factor = 8;
            char path[512];
            number_value(member(params, "factor"), &factor);
            if (sscanf(render, " [ %d , %d , %d , %d , %d , %d ]", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]) != 6
                || r[2] < 0 || r[3] < 0 || r[4] > 255 || r[5] > 255 || r[4] < r[2] || r[5] < r[3] || factor < 1
                || factor > 16 || !string_value(member(params, "path"), path, sizeof path)) {
                fail(fd, id, -32602, "render needs [tpage, clut, u0, v0, u1, v1] in a page, factor 1-16 and path");
                return;
            }
            extern const uint16_t* pc_hw_native_vram(void);
            rw = (r[4] - r[2] + 1) * (int)factor, rh = (r[5] - r[3] + 1) * (int)factor;
            uint32_t* texels = malloc((size_t)rw * rh * sizeof *texels);
            const uint16_t* vram = pc_hw_native_vram();
            rendered = texels && vram
                    && pc_ui_text_render(vram, (unsigned)r[0], (unsigned)r[1], r[2], r[3], r[4], r[5], (int)factor,
                        texels);
            if (rendered) /* distance texels as the PS1 colours drawn at that factor */
                pc_ui_text_resolve(vram, (unsigned)r[1], texels, rw * rh, (int)factor, -1.0f);
            FILE* out = rendered ? fopen(path, "wb") : NULL;
            for (int i = 0; out && i < rw * rh; i++) {
                unsigned char b[2] = { (unsigned char)(texels[i] & 255), (unsigned char)(texels[i] >> 8 & 255) };
                fwrite(b, 1, 2, out);
            }
            if (out)
                fclose(out);
            free(texels);
        }
        int used = pc_ui_text_json(result, (int)sizeof result);
        {
            /* The text overlay (gpu_hw.c): triangles held back now (-1: not holding), drawn into the
             * picture since (baked) and presented at the window's resolution; the presented view in
             * the window (x, y from its top-left, width, height). */
            extern void pc_hw_text_overlay_stats(long*, long*, long*) __attribute__((weak));
            extern int pc_gl_view[4] __attribute__((weak));
            long held = -1, baked = 0, presented = 0;
            if (pc_hw_text_overlay_stats)
                pc_hw_text_overlay_stats(&held, &baked, &presented);
            int view[4] = { 0, 0, 0, 0 };
            if (&pc_gl_view)
                memcpy(view, pc_gl_view, sizeof view);
            if (used > 1 && used < (int)sizeof result - 160)
                used += snprintf(result + used - 1, sizeof result - used + 1,
                            ",\"overlay\":{\"held\":%ld,\"baked\":%ld,\"presented\":%ld,\"view\":[%d,%d,%d,%d]}}",
                            held, baked, presented, view[0], view[1], view[2], view[3])
                      - 1;
        }
        if (rendered >= 0 && used > 1 && used < (int)sizeof result - 64)
            used += snprintf(result + used - 1, sizeof result - used + 1,
                        ",\"render\":{\"ok\":%d,\"w\":%d,\"h\":%d}}", rendered, rw, rh)
                  - 1;
        if (lines_bytes >= 0 && used > 1 && used < (int)sizeof result - 64)
            snprintf(result + used - 1, sizeof result - used + 1, ",\"lines_bytes\":%ld}", lines_bytes);
        reply(fd, id, result);
    } else if (!strcmp(method, "button_prompts")) {
        /* button_prompts.h: {button_prompts: mode} sets the mode; {device:
         * "keyboard" | "playstation" | "xbox" | "nintendo", variant} stands in
         * for the device used last; {bind: {"bind_key_circle": "c",
         * "bind_pad_circle": "south", ...}} applies binding settings lines. */
        char text[96];
        if (params && string_value(member(params, "button_prompts"), text, sizeof text)
            && !pc_button_prompts_setting("button_prompts", text)) {
            fail(fd, id, -32602, "button_prompts: auto, playstation, xbox, nintendo, keyboard or original");
            return;
        }
        /* {style: solid | outline | color_solid | color_outline, scale: 0.4-1}. */
        if (params && string_value(member(params, "style"), text, sizeof text)) {
            int found = -1;
            for (int k = 0; k < PC_PROMPTS_STYLES; k++)
                if (!strcmp(text, pc_button_prompts_style_names[k]))
                    found = k;
            if (found < 0) {
                fail(fd, id, -32602, "style: solid, outline, color_solid or color_outline");
                return;
            }
            pc_button_prompts_style = found;
        }
        /* {dpad: cross | dim | lit} (the shortcut cross's D-pad);
         * {cross_actions: 0-15} stands in for the arms with a usable action
         * (bits up, left, right, down) whatever the game reports, -1 ends it. */
        if (params && string_value(member(params, "dpad"), text, sizeof text)) {
            int found = -1;
            for (int k = 0; k < PC_PROMPTS_DPADS; k++)
                if (!strcmp(text, pc_button_prompts_dpad_names[k]))
                    found = k;
            if (found < 0) {
                fail(fd, id, -32602, "dpad: cross, dim or lit");
                return;
            }
            pc_button_prompts_dpad = found;
        }
        long long actions;
        if (params && number_value(member(params, "cross_actions"), &actions))
            pc_button_prompts_cross_override((int)actions);
        double prompt_scale;
        if (params && real_value(member(params, "scale"), &prompt_scale)) {
            if (prompt_scale < 0.4 || prompt_scale > 1.0) {
                fail(fd, id, -32602, "scale: 0.4-1");
                return;
            }
            pc_button_prompts_scale = (float)prompt_scale;
        }
        if (params && string_value(member(params, "device"), text, sizeof text)) {
            long long variant = 0;
            number_value(member(params, "variant"), &variant);
            int family = 0;
            for (int k = PC_PROMPTS_PLAYSTATION; k <= PC_PROMPTS_KEYBOARD; k++)
                if (!strcmp(text, pc_button_prompts_names[k]))
                    family = k;
            if (!family) {
                fail(fd, id, -32602, "device: playstation, xbox, nintendo or keyboard");
                return;
            }
            pc_button_prompts_device(family, (int)variant);
        }
        int clear = 0;
        if (params && bool_value(member(params, "clear_shown"), &clear) && clear)
            pc_button_prompts_shown_reset();
        const char* bind = params ? member(params, "bind") : NULL;
        if (bind && *(bind = skip(bind)) == '{') {
            const char* p = skip(bind + 1);
            while (*p == '"') {
                char key[64], value[96];
                string_value(p, key, sizeof key);
                p = skip(value_end(p));
                if (*p != ':' || !string_value(skip(p + 1), value, sizeof value)) {
                    fail(fd, id, -32602, "bind: an object of setting keys and string values");
                    return;
                }
                if (strncmp(key, "bind_", 5) || !pc_button_prompts_setting(key, value)) {
                    fail(fd, id, -32602, "bind: keys are bind_key_<button> or bind_pad_<button>");
                    return;
                }
                p = skip(value_end(skip(p + 1)));
                if (*p == ',')
                    p = skip(p + 1);
            }
        }
        int used = pc_button_prompts_json(result, (int)sizeof result);
        /* {icon: button name, size}: the native menus' image (pc_prompt_glyph). */
        extern pc_prompt_image pc_prompt_glyph(int, float) __attribute__((weak));
        if (params && string_value(member(params, "icon"), text, sizeof text) && pc_prompt_glyph
            && used < (int)sizeof result - 300) {
            long long size = 24;
            number_value(member(params, "size"), &size);
            int button = pc_pad_button_index(text);
            pc_prompt_image g = pc_prompt_glyph(button, (float)size);
            snprintf(result + used - 1, sizeof result - used + 1,
                ",\"icon\":{\"button\":%d,\"texture\":%s,\"w\":%g,\"h\":%g,\"uv0\":[%g,%g],\"uv1\":[%g,%g],"
                "\"generation\":%u,\"device\":%d}}",
                button, g.texture ? "true" : "false", g.w, g.h, g.uv0[0], g.uv0[1], g.uv1[0], g.uv1[1],
                pc_button_prompts_generation(), pc_button_prompts_family());
        }
        /* {mask: [u0, v0, u1, v1, factor, scale]}: which texels the icon of
         * that region of the field UI page (768, 0; CLUT 944, 223) has at that
         * atlas factor and internal scale, as rows of 0/1 (tests). */
        const char* mask_param = params ? member(params, "mask") : NULL;
        int m[6];
        if (mask_param
            && sscanf(mask_param, " [ %d , %d , %d , %d , %d , %d ]", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
            extern const uint16_t* pc_hw_native_vram(void);
            static unsigned char bits[256 * 256];
            int n = pc_hw_native_vram()
                      ? pc_button_prompts_mask(pc_hw_native_vram(), 12, 223u << 6 | 944 / 16, m[0], m[1], m[2], m[3], m[4],
                            m[5], bits)
                      : 0;
            int w = n ? (m[2] - m[0] + 1) * m[4] : 0, h = w ? n / w : 0;
            used = (int)strlen(result) - 1;
            used += snprintf(result + used, sizeof result - used, ",\"mask\":{\"w\":%d,\"h\":%d,\"rows\":[", w, h);
            for (int y = 0; y < h && used < (int)sizeof result - w - 16; y++) {
                result[used++] = y ? ',' : ' ', result[used++] = '"';
                for (int x = 0; x < w; x++)
                    result[used++] = bits[y * w + x] ? '1' : '0';
                result[used++] = '"';
            }
            snprintf(result + used, sizeof result - used, "]}}");
        }
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
    } else if (!strcmp(method, "card")) {
        /* Tests that save (memory_card.c): {fail_writes: 0|1} refuses the card's writes (as the Room
         * Browser's warp does), {unformatted: 0|1} makes the cards read as unformatted until one is
         * formatted; the result: data writes made so far ("writes") and the flags. */
        extern unsigned pc_card_write_count(void);
        extern void pc_card_test_flags(int fail, int new_card);
        extern void pc_card_test_state(int* fail, int* new_card);
        int fail = -1, fresh = -1, flag;
        if (params && bool_value(member(params, "fail_writes"), &flag))
            fail = flag;
        else if (params && number_value(member(params, "fail_writes"), &n))
            fail = n != 0;
        if (params && bool_value(member(params, "unformatted"), &flag))
            fresh = flag;
        else if (params && number_value(member(params, "unformatted"), &n))
            fresh = n != 0;
        pc_card_test_flags(fail, fresh);
        pc_card_test_state(&fail, &fresh);
        snprintf(result, sizeof result, "{\"writes\":%u,\"fail_writes\":%d,\"unformatted\":%d}",
            pc_card_write_count(), fail, fresh);
        reply(fd, id, result);
    } else if (!strcmp(method, "spu")) {
        extern int pc_spu_status_json(char*, int) __attribute__((weak)); /* audio_spu.cpp */
        if (!pc_spu_status_json) {
            fail(fd, id, -32000, "spu needs the audio build");
            return;
        }
        pc_spu_status_json(result, sizeof result);
        reply(fd, id, result);
    } else if (!strcmp(method, "system")) {
        /* System settings that live outside project_settings' variables (window mode: boot_window.c, master
         * volume: audio_output.c, brightness: gl_present.c) and the binding capture. */
        extern int pc_window_mode_get(void), pc_master_volume_get(void), pc_brightness_get(void);
        extern void pc_window_mode_set(int), pc_master_volume_set(int), pc_brightness_set(int);
        extern int pc_window_mode_json(char*, int), pc_capture_json(char*, int);
        extern int pc_window_inject_key(const char*, int), pc_window_inject_pad(const char*, int);
        extern int pc_window_inject_mouse(float, float, int, int);
        extern float pc_master_gain(void);
        extern unsigned pc_audio_output_recorded(void);
        static const char* const modes[3] = { "windowed", "borderless", "fullscreen" };
        char text[32];
        int flag = 1;
        if (params && string_value(member(params, "window_mode"), text, sizeof text))
            for (int k = 0; k < 3; k++)
                if (!strcmp(text, modes[k]))
                    pc_window_mode_set(k);
        if (params && number_value(member(params, "master_volume"), &n))
            pc_master_volume_set((int)n);
        if (params && number_value(member(params, "brightness"), &n))
            pc_brightness_set((int)n);
        if (params && string_value(member(params, "bind"), text, sizeof text)) {
            char row_id[48];
            snprintf(row_id, sizeof row_id, "bind_%s", text);
            int rows = pc_ui_settings_rows(PC_UI_SETTINGS_BINDINGS), started = 0;
            for (int r = 0; r < rows && !started; r++) {
                pc_ui_setting_row row;
                if (pc_ui_settings_row(PC_UI_SETTINGS_BINDINGS, r, &row) && !strcmp(row.id, row_id))
                    started = pc_ui_settings_activate(PC_UI_SETTINGS_BINDINGS, r);
            }
            if (!started) {
                fail(fd, id, -32000, "no binding capture for that button");
                return;
            }
        }
        if (params)
            bool_value(member(params, "down"), &flag);
        if (params && string_value(member(params, "key"), text, sizeof text) && pc_window_inject_key(text, flag)) {
            fail(fd, id, -32602, "key: a settings key name (bind_key_*)");
            return;
        }
        if (params && string_value(member(params, "pad"), text, sizeof text) && pc_window_inject_pad(text, flag)) {
            fail(fd, id, -32602, "pad: a gamepad element name (bind_pad_*)");
            return;
        }
        /* virtual_pad: the element on an SDL virtual gamepad (boot_gamepad.c), read
         * by the game as a real pad's (system {pad} only reaches the capture) */
        if (params && string_value(member(params, "virtual_pad"), text, sizeof text)) {
            extern int pc_pad_element_from_name(const char*);
            extern int pc_controller_virtual_set(int, int) __attribute__((weak));
            int element = pc_pad_element_from_name(text);
            if (!pc_controller_virtual_set || element < 0 || pc_controller_virtual_set(element, flag)) {
                fail(fd, id, -32602, "virtual_pad: a gamepad element name, and no real gamepad connected");
                return;
            }
        }
        const char* mouse = params ? member(params, "mouse") : NULL;
        if (mouse) {
            double xy[2] = { 0, 0 }, button = 1, wheel = 0;
            int got = 0;
            const char* q = mouse;
            while (*q == ' ' || *q == '[')
                q++;
            for (; got < 2 && *q; got++) {
                char* end;
                xy[got] = strtod(q, &end);
                if (end == q)
                    break;
                q = end;
                while (*q == ' ' || *q == ',')
                    q++;
            }
            real_value(member(params, "button"), &button);
            real_value(member(params, "wheel"), &wheel);
            if (got < 2 || pc_window_inject_mouse((float)xy[0], (float)xy[1], (int)button,
                               button == 0 ? (int)wheel : flag)) {
                fail(fd, id, -32602, "mouse: [x, y] in window pixels");
                return;
            }
        }
        char window_json[512], capture_json[512];
        pc_window_mode_json(window_json, sizeof window_json);
        pc_capture_json(capture_json, sizeof capture_json);
        snprintf(result, sizeof result,
            "{\"window_mode\":\"%s\",\"master_volume\":%d,\"gain\":%g,\"audio_recorded\":%u,\"brightness\":%d,"
            "\"window\":%s,\"capture\":%s}",
            modes[pc_window_mode_get() % 3], pc_master_volume_get(), pc_master_gain(), pc_audio_output_recorded(),
            pc_brightness_get(), window_json, capture_json);
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
        snprintf(result, sizeof result, "{\"status\":\"%s\",\"path\":\"%s\"}", pc_settings_status(), json_str(path));
        reply(fd, id, result);
    } else if (!strcmp(method, "sfx")) {
        /* The sounds played since the last {reset: true}: bank 0x7E (the menus') by name, every id of it,
         * and the other banks' total. */
        int flag = 0, n;
        if (params)
            bool_value(member(params, "reset"), &flag);
        n = snprintf(result, sizeof result,
            "{\"change\":%u,\"select\":%u,\"leave\":%u,\"invalid\":%u,\"other\":%u,\"menu\":[", sfx_menu[4],
            sfx_menu[5], sfx_menu[6], sfx_menu[7], sfx_other);
        for (int k = 0; k < 32 && n > 0 && n < (int)sizeof result - 16; ++k)
            n += snprintf(result + n, sizeof result - (size_t)n, k ? ",%u" : "%u", sfx_menu[k]);
        snprintf(result + n, sizeof result - (size_t)n, "]}");
        if (flag) {
            memset(sfx_menu, 0, sizeof sfx_menu);
            sfx_other = 0;
        }
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
        /* room: "ready" when the groups are the loaded room's, "loading"
         * while a room loads (or none is loaded): no groups then. */
        extern int pc_room_editor_ready(void) __attribute__((weak));
        int ready = pc_room_editor_ready && pc_room_editor_ready();
        int used = snprintf(result, sizeof result, "{\"facing\":%u,\"room\":\"%s\",\"groups\":[", facing,
            ready ? "ready" : "loading");
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
        extern const char* pc_overlay_details_tab(void) __attribute__((weak));
        extern void pc_lighting_open_menu(void) __attribute__((weak));
        int flag = -1, help = -1, tab = -1, menu = 0;
        char tab_name[32] = "";
        if (params) {
            bool_value(member(params, "visible"), &flag);
            bool_value(member(params, "help"), &help);
            if (string_value(member(params, "tab"), tab_name, sizeof tab_name))
                tab = !strcmp(tab_name, "place") || !strcmp(tab_name, "library") ? 1 : 0;
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
        snprintf(result, sizeof result, "{\"visible\":%s,\"help\":%s,\"tab\":\"%s\",\"details\":\"%s\"}",
            open ? "true" : "false", pc_overlay_set_help && pc_overlay_set_help(help) ? "true" : "false",
            shown ? "library" : "outliner", pc_overlay_details_tab ? pc_overlay_details_tab() : "");
        reply(fd, id, result);
    } else if (!strcmp(method, "editor_select") || !strcmp(method, "editor_schema") || !strcmp(method, "editor_transform")
        || !strcmp(method, "editor_handles") || !strcmp(method, "editor_library")) {
        /* The editor's selection (editor_scene.cpp), property schema
         * (editor_schema.cpp) and transform tools (lights.cpp); they parse the
         * params object themselves. */
        extern int pc_editor_select_rpc(const char*, char*, int) __attribute__((weak));
        extern int pc_editor_schema_rpc(const char*, char*, int) __attribute__((weak));
        extern int pc_editor_transform_rpc(const char*, char*, int) __attribute__((weak));
        extern int pc_editor_handles_rpc(const char*, char*, int) __attribute__((weak));
        extern int pc_editor_library_rpc(const char*, char*, int) __attribute__((weak));
        int (*handler)(const char*, char*, int) = !strcmp(method, "editor_select") ? pc_editor_select_rpc
            : !strcmp(method, "editor_schema")                                     ? pc_editor_schema_rpc
            : !strcmp(method, "editor_transform")                                  ? pc_editor_transform_rpc
            : !strcmp(method, "editor_library")                                    ? pc_editor_library_rpc
                                                                                   : pc_editor_handles_rpc;
        if (!handler) {
            fail(fd, id, -32000, "needs the editor");
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
        if (handler(args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "lua")) {
        /* The extension system; lua_rpc.cpp parses the params object itself. */
        extern int pc_lua_rpc(const char*, char*, int) __attribute__((weak));
        if (!pc_lua_rpc) {
            fail(fd, id, -32000, "no Lua runtime in this build");
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
        if (pc_lua_rpc(args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
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
        /* {x, y, z}: where that world point is on screen (ptgi.c) */
        extern int pc_gi_probe_world(const float p[3], float out[8], float uv[2]) __attribute__((weak));
        double pu = 0.5, pv = 0.5, wx, wy, wz;
        float out[8], uv[2];
        int world = params && real_value(member(params, "x"), &wx) && real_value(member(params, "y"), &wy)
            && real_value(member(params, "z"), &wz);
        if (params && !world) {
            real_value(member(params, "u"), &pu);
            real_value(member(params, "v"), &pv);
        }
        uv[0] = (float)pu, uv[1] = (float)pv;
        float p[3] = { world ? (float)wx : 0, world ? (float)wy : 0, world ? (float)wz : 0 };
        if (world ? !pc_gi_probe_world || !pc_gi_probe_world(p, out, uv)
                  : !pc_gi_probe || !pc_gi_probe((float)pu, (float)pv, out)) {
            fail(fd, id, -32000, "no GI result");
            return;
        }
        snprintf(result, sizeof result,
            "{\"indirect\":[%.4f,%.4f,%.4f],\"visibility\":%.4f,\"baked_visibility\":%.4f,\"extra\":[%.4f,%.4f,%.4f],"
            "\"u\":%.4f,\"v\":%.4f}",
            out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], uv[0], uv[1]);
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
        snprintf(result, sizeof result, "{\"path\":\"%s\"}", json_str(path));
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
        else if (!strcmp(action, "reload")) {
            extern void pc_lights_reload_disk(void) __attribute__((weak));
            if (pc_lights_reload_disk)
                pc_lights_reload_disk();
        } else if (!strcmp(action, "add")) {
            if (!at || sscanf(at, "[%f , %f , %f", &position[0], &position[1], &position[2]) != 3) {
                fail(fd, id, -32602, "add needs position [x, y, z]");
                return;
            }
            if (pc_lights_add)
                pc_lights_add(position);
        } else if (action[0]) {
            fail(fd, id, -32602, "lights action: suggest, save, reload or add");
            return;
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
    } else if (!strcmp(method, "emissive")) {
        /* Emissive texels (emissive_editor.cpp parses the params object). */
        extern int pc_emissive_rpc(const char* params, char* out, int size) __attribute__((weak));
        if (!pc_emissive_rpc) {
            fail(fd, id, -32000, "emissive needs the hardware renderer");
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
        if (pc_emissive_rpc(args, result, sizeof result) < 0) {
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
        extern int pc_room_warp_points_held(void) __attribute__((weak)); /* the savePointStates snapshot */
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
            /* A cell off the room's floor would crash the arrival: an error. */
            extern int pc_room_warp_cell_valid(int, int, char*, int) __attribute__((weak));
            extern int pc_room_warp_cell[4] __attribute__((weak));
            if (pc_room_warp_cell_valid && !pc_room_warp_cell_valid((int)zone, (int)room, why, sizeof why)) {
                pc_room_warp_cell[0] = -1;
                fail(fd, id, -32602, why);
                return;
            }
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
            "\"block_saves\":%d,\"points_held\":%d,\"message\":\"%s\"}",
            ok ? "true" : "false", pc_room_warp_phase(), warped ? "true" : "false", oz, orr, pc_room_warp_enemies,
            pc_room_warp_scripts, pc_room_warp_block_saves, pc_room_warp_points_held ? pc_room_warp_points_held() : 0,
            text);
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
    } else if (!strcmp(method, "post")) {
        /* Post-processing profiles and the post stack (post_profile.cpp, post.h). */
        if (!pc_post_rpc) {
            fail(fd, id, -32000, "post needs the editor (hardware renderer)");
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
        if (pc_post_rpc(args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "environment")) {
        /* The room's sun and sky (environment.cpp): read, set, remove. */
        extern int pc_environment_rpc(const char* params, char* out, int size) __attribute__((weak));
        if (!pc_environment_rpc) {
            fail(fd, id, -32000, "environment needs the editor (hardware renderer)");
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
        if (pc_environment_rpc(args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
        reply(fd, id, result);
    } else if (!strcmp(method, "particles")) {
        /* The port's particle system (particles.cpp): settings, emitters, motes. */
        extern int pc_particles_rpc(const char* params, char* out, int size) __attribute__((weak));
        if (!pc_particles_rpc) {
            fail(fd, id, -32000, "particles needs the editor (hardware renderer)");
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
        if (pc_particles_rpc(args, result, sizeof result) < 0) {
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
            const char* hold, int release, char* out, int size) __attribute__((weak));
        if (!pc_overlay_input) {
            fail(fd, id, -32000, "needs the overlay");
            return;
        }
        char keys[256] = "", typed[128] = "", focus[16] = "", hold[64] = "";
        int ctrl = 0, shift = 0, release = 0;
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
            const char* held = member(params, "hold");
            const char* held_end = held ? value_end(held) : NULL;
            if (held && held_end && (size_t)(held_end - held) < sizeof hold) {
                memcpy(hold, held, (size_t)(held_end - held));
                hold[held_end - held] = 0;
            }
            bool_value(member(params, "release"), &release);
        }
        if (pc_overlay_input(keys, typed, ctrl, shift, focus, hold, release, result, (int)sizeof result) < 0) {
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
        snprintf(result, sizeof result, "{\"path\":\"%s\"}", json_str(path));
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
        snprintf(result, sizeof result, "{\"path\":\"%s\",\"remaining\":%d}", json_str(path), render_anim_trace_remaining());
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
        snprintf(result, sizeof result, "{\"path\":\"%s\"}", json_str(path));
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
        snprintf(result, sizeof result, "{\"path\":\"%s\"}", json_str(path));
        reply(fd, id, result);
    } else if (!strcmp(method, "input_record")) {
        /* Input recording/replay (input_record.c, ROOD_RECORD / ROOD_REPLAY):
         * mode off|recording|replaying|replayed, the file, the replay's end. */
        extern int pc_input_record_state(const char**, unsigned*);
        static const char* const modes[] = { "off", "recording", "replaying", "replayed" };
        const char* file;
        unsigned end;
        int mode = pc_input_record_state(&file, &end);
        snprintf(result, sizeof result, "{\"mode\":\"%s\",\"file\":\"%s\",\"end\":%u,\"vsync\":%u}",
            modes[mode < 0 || mode > 3 ? 0 : mode], json_str(file), end, pc_vsync_count());
        reply(fd, id, result);
    } else if (!strcmp(method, "state_flag")) {
        /* The game's story/state flags (as room scripts read and set them;
         * 4A0A8.c): {id} reads one, {id, value} sets it first. For tests
         * that jump ahead, e.g. past the prologue. */
        extern char vs_battle_getStateFlag(short) __attribute__((weak));
        extern void vs_battle_setStateFlag(short, char) __attribute__((weak));
        long long flag, value;
        if (!vs_battle_getStateFlag || !vs_battle_setStateFlag) {
            fail(fd, id, -32000, "state_flag needs the battle build");
            return;
        }
        if (!params || !number_value(member(params, "id"), &flag) || flag < 0 || flag >= 0x440) {
            fail(fd, id, -32602, "state_flag needs id (0-0x43F)");
            return;
        }
        if (number_value(member(params, "value"), &value))
            vs_battle_setStateFlag((short)flag, (char)value);
        snprintf(result, sizeof result, "{\"id\":%lld,\"value\":%d}", flag,
            (unsigned char)vs_battle_getStateFlag((short)flag));
        reply(fd, id, result);
    } else if (!strcmp(method, "read")) {
        long long address, length = 4;
        if (!params || !number_value(member(params, "address"), &address)) {
            fail(fd, id, -32602, "read needs address");
            return;
        }
        number_value(member(params, "length"), &length);
        if (length < 1 || !readable((uintptr_t)address, (uintptr_t)length)) {
            fail(fd, id, -32000, "range not readable");
            return;
        }
        size_t used = (size_t)snprintf(result, sizeof result, "{\"hex\":\"");
        for (long long i = 0; i < length && used + 4 < sizeof result; i++)
            used += (size_t)snprintf(result + used, sizeof result - used, "%02x",
                ((unsigned char*)(uintptr_t)address)[i]);
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
        if (!length || !writable((uintptr_t)address, length)) {
            fail(fd, id, -32000, "range not writable");
            return;
        }
        for (size_t i = 0; i < length; i++) {
            unsigned byte;
            if (sscanf(hex + 2 * i, "%2x", &byte) != 1) {
                fail(fd, id, -32602, "bad hex");
                return;
            }
            ((unsigned char*)(uintptr_t)address)[i] = (unsigned char)byte;
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
        || !strcmp(method, "room_groups") || !strcmp(method, "uv_effects") || !strcmp(method, "lights")
        || !strcmp(method, "environment") || !strcmp(method, "particles") || !strcmp(method, "editor_select")
        || !strcmp(method, "editor_schema") || !strcmp(method, "editor_transform")
        || !strcmp(method, "editor_handles") || !strcmp(method, "editor_library"))
        return 1;
    if (!strcmp(method, "emissive")) { /* edits of the user layer; the rest read, pick, save, reload or paint maps */
        const char* p = member(request, "params");
        char action[32] = "";
        if (p)
            string_value(member(p, "action"), action, sizeof action);
        return !strcmp(action, "set") || !strcmp(action, "new") || !strcmp(action, "remove")
            || !strcmp(action, "duplicate") || !strcmp(action, "scope_pick");
    }
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

/* Equal strings, in a time that doesn't depend on where they differ. */
static int same_secret(const char* a, const char* b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (unsigned char)(la != lb);
    for (size_t i = 0; i < la; i++)
        diff |= (unsigned char)(a[i] ^ b[i % (lb ? lb : 1)]);
    return !diff;
}

/* A TCP connection's first request: auth {token}. 1 when it may go on. */
static int authenticate(int fd, const char* request)
{
    char method[16] = "", token[sizeof tcp_token] = "", id[64] = "null";
    const char* idv = member(request, "id");
    const char* e = idv ? value_end(idv) : NULL;
    if (e && (size_t)(e - idv) < sizeof id) {
        memcpy(id, idv, (size_t)(e - idv));
        id[e - idv] = 0;
    }
    const char* params = member(request, "params");
    string_value(member(request, "method"), method, sizeof method);
    string_value(params ? member(params, "token") : NULL, token, sizeof token);
    if (strcmp(method, "auth") || !same_secret(token, tcp_token)) {
        fail(fd, id, -32001, "authentication required: send auth {token} first (ROOD_RPC=tcp:PORT:TOKEN)");
        return 0;
    }
    reply(fd, id, "{\"ok\":true}");
    return 1;
}

/* ROOD_RPC=tcp:PORT[:TOKEN]: a TCP listener on the loopback interface only,
 * for Windows (whose Python has no AF_UNIX) or any client that prefers TCP. */
static void open_tcp_listener(const char* spec)
{
    char* end;
    unsigned long port = strtoul(spec + 4, &end, 10);
    if (*end == ':') {
        size_t length = strlen(end + 1);
        if (!length || length >= sizeof tcp_token) {
            fprintf(stderr, "[rpc] ROOD_RPC=tcp:PORT:TOKEN: the token takes 1-%d characters\n",
                (int)sizeof tcp_token - 1);
            return;
        }
        memcpy(tcp_token, end + 1, length + 1);
        end += length + 1;
    }
    if (*end || !port || port > 65535) {
        fprintf(stderr, "[rpc] ROOD_RPC=tcp:PORT: expected tcp:PORT or tcp:PORT:TOKEN\n");
        return;
    }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
        fprintf(stderr, "[rpc] Winsock unavailable\n");
        return;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    listener = s == INVALID_SOCKET ? -1 : (int)s;
#else
    listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    int reuse = 1;
    if (listener >= 0)
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
#endif
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener < 0 || bind(listener, (struct sockaddr*)&address, sizeof address)
        || listen(listener, MAX_CLIENTS)) {
        fprintf(stderr, "[rpc] cannot listen on 127.0.0.1:%lu\n", port);
        if (listener >= 0)
            rpc_close(listener);
        listener = -1;
        return;
    }
    rpc_set_blocking(listener, 0);
    listener_tcp = 1;
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].fd = -1;
    if (tcp_token[0])
        fprintf(stderr, "[rpc] listening on tcp:127.0.0.1:%lu (token required)\n", port);
    else
        fprintf(stderr, "[rpc] listening on tcp:127.0.0.1:%lu (no token: any local program can "
                        "connect; ROOD_RPC=tcp:PORT:TOKEN requires one)\n",
            port);
}

static void open_listener(void)
{
    initialized = 1;
    const char* path = getenv("ROOD_RPC");
    if (!path || !*path)
        return;
    if (!strncmp(path, "tcp:", 4)) {
        open_tcp_listener(path);
        return;
    }
#ifdef _WIN32
    fprintf(stderr, "[rpc] ROOD_RPC=%s: Windows takes tcp:PORT (no Unix sockets)\n", path);
#else
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
#endif
}

static void service(void)
{
    if (listener < 0)
        return;
    int fd;
    while ((fd = rpc_accept(listener)) >= 0) {
        int i = 0;
        while (i < MAX_CLIENTS && clients[i].fd >= 0)
            ++i;
        if (i == MAX_CLIENTS) {
            rpc_close(fd);
            continue;
        }
        clients[i].fd = fd;
        clients[i].used = 0;
        clients[i].waiting = 0;
        clients[i].authed = !listener_tcp || !tcp_token[0];
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct client* c = &clients[i];
        if (c->fd < 0)
            continue;
        long n = rpc_recv(c->fd, c->buffer + c->used, sizeof c->buffer - 1 - c->used);
        if (n == 0 || (n < 0 && !rpc_would_block())) {
            rpc_close(c->fd);
            c->fd = -1;
            continue;
        }
        if (n > 0)
            c->used += (size_t)n;
        char* newline;
        c->buffer[c->used] = 0;
        while (c->fd >= 0 && (newline = memchr(c->buffer, '\n', c->used))) {
            *newline = 0;
            /* A TCP port can be reached by any local program, a web page's
             * request included: a line that is not a JSON object (an HTTP
             * request line) closes the connection. */
            if (listener_tcp && *skip(c->buffer) && *skip(c->buffer) != '{') {
                rpc_close(c->fd);
                c->fd = -1;
                break;
            }
            /* Blocking writes for the reply; requests are small and local. */
            rpc_set_blocking(c->fd, 1);
            current_client = c;
            if (*skip(c->buffer) && !c->authed) {
                if (!authenticate(c->fd, c->buffer)) {
                    current_client = NULL;
                    rpc_close(c->fd);
                    c->fd = -1;
                    break;
                }
                c->authed = 1;
            } else if (*skip(c->buffer)) {
                handle(c->fd, c->buffer);
                handled_since_redraw = 1;
            }
            current_client = NULL;
            rpc_set_blocking(c->fd, 0);
            size_t rest = c->used - (size_t)(newline + 1 - c->buffer);
            memmove(c->buffer, newline + 1, rest);
            c->used = rest;
            c->buffer[c->used] = 0;
        }
        if (c->fd >= 0 && c->used == sizeof c->buffer - 1) {
            if (!c->authed) {
                /* Not yet authenticated: no reason to keep a slot for it. */
                rpc_close(c->fd);
                c->fd = -1;
                continue;
            }
            fail(c->fd, "null", -32700, "request line too long");
            c->used = 0;
        }
    }
    serve_waiters();
}

/* Sleep until a client (or a new connection) has something to read, or ms
 * milliseconds pass: requests are served as they arrive while paused. */
static void wait_for_requests(int ms)
{
    if (ms <= 0)
        return;
    if (listener < 0) {
        struct timespec delay = { 0, (long)ms * 1000000L };
        nanosleep(&delay, NULL);
        return;
    }
#ifdef _WIN32
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET((SOCKET)listener, &readable);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd >= 0)
            FD_SET((SOCKET)clients[i].fd, &readable);
    struct timeval timeout = { 0, ms * 1000 };
    select(0, &readable, NULL, NULL, &timeout);
#else
    struct pollfd fds[MAX_CLIENTS + 1];
    int count = 0;
    fds[count++] = (struct pollfd) { .fd = listener, .events = POLLIN };
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd >= 0)
            fds[count++] = (struct pollfd) { .fd = clients[i].fd, .events = POLLIN };
    poll(fds, (nfds_t)count, ms);
#endif
}

/* A pending RPC snapshot save, taken here on the game stack (between
 * frames, in pc_tick). In the process that resumes the file the call
 * returns again (pc_snapshot_point gives 1): no request is pending there,
 * and the game stays paused (pc_rpc_restored). */
static void take_snapshot(void)
{
    if (!snapshot_client)
        return;
    struct client* c = snapshot_client;
    snapshot_client = NULL;
    char error[256] = "";
    int status = pc_snapshot_point(snapshot_path, error, (int)sizeof error);
    if (status == 1 || c->fd < 0)
        return;
    rpc_set_blocking(c->fd, 1);
    if (status == 0) {
        char result[1200];
        snprintf(result, sizeof result, "{\"saved\":\"%s\",\"vsync\":%u}", json_str(snapshot_path),
            pc_vsync_count());
        reply(c->fd, snapshot_id, result);
    } else
        fail(c->fd, snapshot_id, -32000, error);
    rpc_set_blocking(c->fd, 0);
}
/* snapshot.c: this process resumed a save state; serve RPC, stay paused. */
void pc_rpc_restored(void)
{
    if (!initialized)
        open_listener();
    paused = 1;
    step_budget = 0;
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
    if (trigger_vsyncs > 0)
        --trigger_vsyncs;
    if (listener >= 0)
        service();
    take_snapshot();
    if (paused && step_budget > 0) {
        --step_budget;
        return;
    }
    /* Paused: the window (and overlay) redraws at about 60 Hz, and after
     * every request served, so each request still sees the frame its
     * predecessor's change produced (editor transactions, graphics
     * settings); a request is served as soon as it arrives instead of
     * after a fixed 16 ms sleep. */
    long long next_redraw = 0;
    while (paused && step_budget == 0) {
        extern int pc_present_burst __attribute__((weak));
        long long now = rpc_now_ms();
        if (now >= next_redraw || handled_since_redraw) {
            handled_since_redraw = 0;
            pc_window_tick();
            /* An armed present_dump burst takes stepped presents only: paused
             * redraws (as many as the wall clock allows between RPC calls)
             * would put repeated images into it. */
            if (!&pc_present_burst || pc_present_burst <= 0)
                pc_window_redraw();
            now = rpc_now_ms();
            next_redraw = now + 16;
        }
        wait_for_requests((int)(next_redraw - now));
        if (listener >= 0)
            service();
        take_snapshot();
    }
    if (paused && step_budget > 0)
        --step_budget;
}
