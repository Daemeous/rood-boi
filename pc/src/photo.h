/* The photo mode foundation, the host side (pc/src/photo.cpp; pc/docs/Extensions.md
 * "Photo mode foundation"; Lua: pc/src/lua/lua_api_photo.cpp). What a plugin may hold,
 * each by its id (the owner), each given back by pc_photo_release (its release, an
 * unload or reload, a fault that disables it, an engine reload):
 *
 *   pause    the game held at a vsync boundary (dev_rpc.c's poll loop): no vsync
 *            advances, no game-time Lua, no audio fed; the window, the overlay, Lua's
 *            frames (real time) and the post stack go on;
 *   camera   the editor's live free camera (editor_view.cpp) driving the view while
 *            paused, with roll, field of view and optional collision;
 *   ui       the HUD, the game's 2D layer and the developer overlay hidden;
 *   capture  the presented picture (post applied, before the overlay) as a PNG;
 *   input    pad, keyboard and RPC input to the plugin instead of the game.
 *
 * A paused game is redrawn from its frozen moment M ("a render"): the game runs a few
 * vsyncs from M with nothing to see or hear (no presents, no audio, no input, no Lua
 * ticks, deterministic timing, no memory card writes) under the camera and the hidden
 * layers, then its state is rewound to M (state_rewind.c, slot PC_REWIND_PHOTO) while
 * the picture stays. So everything the game holds stays M's: the vsync count, the
 * state digest, the queries; only the picture moves. Every call is on the game thread. */
#ifndef ROOD_PHOTO_H
#define ROOD_PHOTO_H
#ifdef __cplusplus
extern "C" {
#endif

/* ---- read by the host's units (host-owned blocks in the split build: data, never calls per packet) ---- */
/* 1 while a render runs the game from M (boot_runtime.c: no present, no Lua tick, deterministic scanlines;
 * boot_window.c: no pacing, no input to the game; audio_output.c: nothing fed; memory_card.c: no writes). */
extern int pc_photo_rendering;
/* The game's 2D layer hidden (boot_runtime.c draw_packet): the field's ordering-table slots below
 * pc_photo_game2d_slots (the HUD and text layer, vs_scratch.unk4 +- 16 entries) are not drawn. */
extern int pc_photo_hide_game2d;
extern int pc_photo_game2d_slots;
/* Input captured by a plugin (boot_window.c): the game sees no buttons, no stick; and the buttons the
 * safety took (the Start that gave the input back), until they are let go. */
extern int pc_photo_input_captured;
extern unsigned pc_photo_input_swallowed;

/* ---- dev_rpc.c's poll (the vsync boundary) ---- */
/* At the top of every poll and in the paused loop: 1 when this vsync must run now without the RPC (a
 * render's vsync, or one starting); may rewind (does not return then). */
int pc_photo_poll(void);
/* The game is held (the paused loop runs while it is). */
int pc_photo_holding(void);
/* RPC photo: the state as JSON ({phase, owners, renders, ...}); params: {render_frames, game2d_slots}. */
int pc_photo_rpc(const char* params, char* out, int size);

/* ---- pause ---- */
/* Hold the game from the next vsync boundary. 0, or -1 with why (loading, a room transition, memory card
 * I/O, a render engine unavailable, ...). Holding twice is one hold. */
int pc_photo_pause(const char* owner, char* why, int size);
void pc_photo_resume(const char* owner);
int pc_photo_paused(void);                  /* held, or asked for */
int pc_photo_paused_by(const char* owner);  /* this owner's hold */
/* What is heard while held (this owner's wish; with several holders the quietest wins): PC_PHOTO_AUDIO_STOP
 * (silence: the default), _KEEP (the sound driver and the SPU go on in real time: music and loops play;
 * the game's logic stays held; renders put the sound's state back after their rewind), _DUCK (kept, at
 * `gain`). On resume the game goes on with the sound where it got to. */
enum { PC_PHOTO_AUDIO_STOP = 0, PC_PHOTO_AUDIO_DUCK = 1, PC_PHOTO_AUDIO_KEEP = 2 };
void pc_photo_pause_audio(const char* owner, int mode, float gain);
/* Redraw the frozen moment (a layer changed, the camera moved); nothing while running. */
void pc_photo_redraw(void);

/* ---- camera (world units: y down; angles in degrees) ---- */
typedef struct {
    float position[3];
    float target[3]; /* the orbit's pivot: a point ahead of the camera */
    float yaw, pitch, roll, fov;
    int collision;   /* keep the camera out of the room's walls (from the target) */
} pc_photo_pose;
enum { PC_POSE_POSITION = 1, PC_POSE_TARGET = 2, PC_POSE_YAW = 4, PC_POSE_PITCH = 8, PC_POSE_ROLL = 16,
    PC_POSE_FOV = 32, PC_POSE_COLLISION = 64 };
int pc_photo_camera_begin(const char* owner, char* why, int size); /* from the game's camera; needs the pause */
void pc_photo_camera_finish(const char* owner);                    /* the game's camera again, exactly */
int pc_photo_camera_owner(char* out, int size);                    /* 1 and its owner while one is active */
int pc_photo_camera_get(pc_photo_pose* out);
/* Fields of `mask`; position without target keeps the direction, target without position turns to it. */
int pc_photo_camera_set(const char* owner, const pc_photo_pose* pose, unsigned mask, char* why, int size);
/* View space: yaw and pitch about the target (degrees), the distance to it times `zoom` (1: kept). */
int pc_photo_camera_orbit(const char* owner, float yaw, float pitch, float zoom, char* why, int size);
/* View space: right, up, forward (world units); target moves along. */
int pc_photo_camera_move(const char* owner, float right, float up, float forward, char* why, int size);

/* ---- ui ---- */
enum { PC_PHOTO_HIDE_HUD = 1, PC_PHOTO_HIDE_GAME2D = 2, PC_PHOTO_HIDE_OVERLAY = 4,
    PC_PHOTO_HIDE_ASHLEY = 8, PC_PHOTO_HIDE_OTHERS = 16 /* the actors (rood.ui.hide{actors}) */ };
/* The characters not drawn (actor_native.c: their models, weapons and shields; every owner's together):
 * bit 1 Ashley (actor slot 0), bit 2 the others (enemies, NPCs). */
extern int pc_photo_hide_actors;
void pc_photo_ui_hide(const char* owner, int flags); /* this owner's (0: shown again) */
int pc_photo_ui_hidden(void);                        /* every owner's together */
int pc_photo_hide_hud(void);                         /* hud_elements.cpp, the classic hooks, dev_overlay.cpp */
int pc_photo_hide_overlay(void);                     /* dev_overlay.cpp */

/* ---- the format mask (rood.post "format"): the picture cropped to an aspect ratio ---- */
void pc_photo_format_set(const char* owner, float aspect); /* 0: this owner's none */
float pc_photo_format(void);                               /* the newest owner's, 0: the whole picture */
void pc_photo_format_colour_set(const char* owner, int rgb); /* the bars' colour, 0xRRGGBB (0: this owner's none) */
int pc_photo_format_colour(void);                            /* the newest owner's (0: black) */
void pc_photo_format_draw(void); /* dev_overlay.cpp, its ImGui frame open: the bars over the picture */

/* ---- capture ---- */
typedef struct {
    int done, ok;       /* done: written or failed */
    int width, height, scale;
    char path[512];
    char error[256];
} pc_photo_capture_result;
/* The next picture presented after the frozen moment is redrawn (what is hidden, the camera) as a PNG in
 * dir (pc/user/photos, ROOD_PHOTOS) with a timestamped name, or at path (the caller checked the grant).
 * scale > 1: not supported (the window's resolution; the result says so). Returns an id (> 0), or 0 with
 * why; *path_out: the file it writes. */
int pc_photo_capture(const char* owner, const char* path, int scale, char* path_out, int path_size, char* why,
    int why_size);
int pc_photo_capture_result_get(int id, pc_photo_capture_result* out); /* 1 when known */
const char* pc_photo_dir(void);
/* The presenters (gl_present.c, vk_present.c), the post-processed picture finished, before the overlay:
 * 1 when a capture wants this present (then they call pc_photo_capture_pixels with its view's pixels). */
int pc_photo_capture_wanted(void);
void pc_photo_capture_pixels(const unsigned char* rgba, int w, int h, int stride, int bottom_up);

/* ---- input ---- */
enum { PC_PHOTO_EVENT_BUTTON, PC_PHOTO_EVENT_KEY, PC_PHOTO_EVENT_RELEASED };
typedef struct {
    int type;
    int down;         /* button, key */
    char name[32];    /* a PS1 button ("cross", "start", ...), a key ("w", "escape"), or why released */
    char device[12];  /* "keyboard", "pad", "rpc" */
} pc_photo_event;
typedef struct {
    unsigned buttons; /* PS1 pad bits held (boot_controller.h order) */
    float left[2], right[2]; /* sticks, -1..1 */
    float l2, r2;     /* triggers 0..1 (-1: buttons) */
} pc_photo_input_state;
int pc_photo_input_capture(const char* owner, int on); /* 0, or -1: another plugin has it */
int pc_photo_input_owner(char* out, int size);
int pc_photo_input_events(pc_photo_event* out, int max); /* drained, oldest first */
void pc_photo_input_state_get(pc_photo_input_state* out);
/* boot_window.c: Escape first, before the overlay (1: taken: the safety); each SDL event while captured
 * (1: taken); each window tick (held state, RPC buttons). */
int pc_photo_input_escape(const void* sdl_event);
int pc_photo_input_event(const void* sdl_event);
void pc_photo_input_tick(void);
/* A port action's button pressed (boot_window.c, boot_gamepad.c: "photo", PC_BIND_PHOTO), captured or not:
 * queued for Lua's "input.action" event {name, device} to every plugin (lua_api_photo.cpp). */
void pc_photo_action(const char* name, const char* device);
int pc_photo_action_next(char* name, int name_size, char* device, int device_size); /* 1 and the oldest */

/* ---- everything an owner holds ---- */
void pc_photo_release(const char* owner);
void pc_photo_release_all(void);
/* What was taken back from an owner without its asking (the safety, an engine reload): 1 and the next
 * notice (owner, what: "input", "everything"; why), oldest first; 0 none. */
int pc_photo_notice_next(char* owner, int owner_size, char* what, int what_size, char* reason, int reason_size);
/* engine_reload.c: before the library goes: 0 (everything given back), or -1 with why (a render running). */
int pc_photo_engine_unload(char* why, int size);

#ifdef __cplusplus
}
#endif
#endif
