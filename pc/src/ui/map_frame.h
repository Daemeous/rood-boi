/* Modern menus, the Map frame (pc/docs/Modern_Menus.md, plan
 * pc/docs/plans/2026-10-14-modern-menus-map.md): the game's own map screen
 * (MENU5, src/MENU/MENU5.PRG/4D8.c) keeps running, rendering and owning the
 * pad; the shell draws native chrome around its 3D map (ui_map_draw.cpp).
 *
 *   4D8.c (ROOD_PC)   stamps every vs_menu5_exec call, latches the frame at
 *                     its state 3 (pc_ui_map_frame_begin), publishes its
 *                     statics once per _menuManager frame, takes the
 *                     frame's requests at its own input point (processInput)
 *                     and runs the frame's area list in its displayMapMenu
 *   map_bridge.c      the game side: lifecycle, snapshot (areas, rooms,
 *                     completion), the area list (MAINMENU 58EC.c's rows
 *                     list ported), the request mailbox's game end
 *   ui_map.cpp        pure state: the model the bridge fills, the pointer,
 *                     the mailbox, JSON (RPC ui_menu "map")
 *
 * C, included by 4D8.c (game flags: no system headers), the bridge and the
 * C++ side. */
#ifndef PC_UI_MAP_FRAME_H
#define PC_UI_MAP_FRAME_H
#ifdef __cplusplus
extern "C" {
#endif

/* MENU5's statics, read once per _menuManager frame (4D8.c _pcPublish). */
typedef struct {
    int exec_state, menu_state, scene, scene_row, room, is_current_scene;
    int yaw, pitch, yaw0, distance, gx, gy;
    int hide_ui, is_paling, paled, quit_state, cmd_state;
    int room_count;
    const void* rooms;      /* vs_battle_room[room_count] */
    const void* room_names; /* _roomNamesTable (vs_battle_roomName[room_count]) */
    const unsigned short* paling_text;
} pc_ui_map_game;

/* A request of the frame (mouse, prompts) for MENU5. a, b by kind:
 * ORBIT (dyaw, dpitch), ZOOM (ddistance), PAN (dx, dy) game units; ROOM
 * (room index); STEP (+1 next, -1 previous); AREA (scene id); QUIT (1 back to
 * the shell, 2 close the menu); TAB (shell tab); LIST (Circle: the area
 * list); PAD (PC_UI_* bits, for the area list). */
enum {
    PC_UI_MAP_REQ_NONE, PC_UI_MAP_REQ_ORBIT, PC_UI_MAP_REQ_ZOOM, PC_UI_MAP_REQ_PAN, PC_UI_MAP_REQ_ROOM,
    PC_UI_MAP_REQ_STEP, PC_UI_MAP_REQ_RESET, PC_UI_MAP_REQ_HIDE, PC_UI_MAP_REQ_AREA, PC_UI_MAP_REQ_QUIT,
    PC_UI_MAP_REQ_TAB, PC_UI_MAP_REQ_LIST, PC_UI_MAP_REQ_PAD
};
typedef struct {
    int kind, a, b;
} pc_ui_map_req;

/* 4D8.c -> map_bridge.c */
/* vs_menu5_exec state 3: 1 when the shell armed the frame (this run is framed). */
int pc_ui_map_frame_begin(int run);
/* Every vs_menu5_exec call (the frame's freshness). */
void pc_ui_map_frame_stamp(int exec_state);
void pc_ui_map_frame_publish(const pc_ui_map_game* g);
/* The next request: discrete_ok 1: a discrete one (AREA, LIST, ROOM, STEP,
 * RESET, HIDE, QUIT, TAB); 0: rotate, zoom or pan. Returns 0 when none. */
int pc_ui_map_frame_take(pc_ui_map_req* out, int discrete_ok);
/* Circle: the area list opens (scene ids by row, the current area's row or
 * 0x80, _configureMapMenu's cursor memories). */
void pc_ui_map_frame_list_open(const int* scene_ids, int count, int current_row, int cm0, int cm1);
/* One displayMapMenu frame of the list: -1 stay, -2 back (Cross), -3 close
 * (Triangle), else the confirmed row (_mapSelectionMenu(0)'s protocol); the
 * frame's own -4: leave for a tab (quit as Cross, the tab kept). */
int pc_ui_map_frame_list(void);
/* vs_menu5_exec state 10 (the run's exit code). */
void pc_ui_map_frame_finish(int quit_state);
/* 1 while the bridge frames run `run` (MENU5's latch alone is not enough: a
 * save state restores it without the host's model). */
int pc_ui_map_framed(int run);
/* processInput, after MENU5's own input (not on a paled area, not leaving):
 * the right stick and the triggers by System > Controls "Map controls"
 * (map_pad.h), framed or not; returns the commands state. */
int pc_ui_map_pad(int cmd, short* pitch, short* yaw, int* distance, int* gx, int* gy);
/* _renderScreen drew MENU5's own controls help (_drawUIControls): counted
 * (ui_menu JSON "map_controls"; never while framed). */
void pc_ui_map_note_controls(void);

/* The frame's model (map_bridge.c fills it, ui_map.cpp reads it). */
enum { PC_UI_MAP_AREAS = 31, PC_UI_MAP_ROOMS = 96, PC_UI_MAP_NAME = 64 };
typedef struct {
    int id, current, shown, paling;
    char name[PC_UI_MAP_NAME];
} pc_ui_map_area;
typedef struct {
    int visited;
    char name[PC_UI_MAP_NAME];
} pc_ui_map_room;
typedef struct {
    int armed;             /* the shell opened the map (deliver 6); no MENU5 run yet */
    unsigned armed_vsync;
    int framed;            /* a framed MENU5 run is going (begin .. end) */
    int run;               /* that run's id */
    int stamped;           /* the run has stamped */
    unsigned stamp;        /* vsync of the last stamp */
    int finished;          /* state 10 passed (quit_state is the exit code) */
    int published;         /* _menuManager has published this run */
    int exec_state, menu_state, scene, scene_row, current_scene, room, is_current_scene;
    int yaw, pitch, yaw0, distance, gx, gy;
    int hide_ui, is_paling, paled, quit_state, cmd_state;
    int completion;        /* the game's map completion % (no write) */
    int area_count;
    pc_ui_map_area areas[PC_UI_MAP_AREAS];
    int room_count, rooms_seen;
    pc_ui_map_room rooms[PC_UI_MAP_ROOMS];
    int ring_count;        /* visited rooms in the next ring's order */
    int ring[PC_UI_MAP_ROOMS];
    char paling_text[128];
    struct {
        int open, count, cursor, scroll, current_row;
        int ids[PC_UI_MAP_AREAS];
    } list;
} pc_ui_map_model;

/* ui_map.cpp */
pc_ui_map_model* pc_ui_map_state(void);
/* The chrome draws: framed with a stamp at most 8 vsyncs old, or armed and
 * not stamped yet for up to 30 vsyncs. */
int pc_ui_map_visible(void);
/* A request (drops it, counted as busy, when MENU5 is not where it takes
 * one); rotate, zoom and pan add up until taken. */
void pc_ui_map_post(int kind, int a, int b);
/* The mailbox's game end: discrete or continuous (map_frame_take). */
int pc_ui_map_take(pc_ui_map_req* out, int discrete_ok);
/* Every pending request dropped (MENU5 not taking any now). */
void pc_ui_map_drop(void);
/* A new frame (or none): the mailbox and the rail's scroll start empty. */
void pc_ui_map_reset_mailbox(void);
/* The pointer (ui_draw.cpp): kind PC_UI_MOUSE_MAP_*, index; click; wheel in
 * notches; dx, dy a drag's move in layout units with button 1 left, 2
 * middle; right a right click. */
void pc_ui_map_mouse(int kind, int index, int click, float wheel, float dx, float dy, int button, int right);
/* The open frame as ",\"map\":{...}" (or ",\"map\":null"). */
void pc_ui_map_json(char* out, unsigned size);

#ifdef __cplusplus
}
#endif
#endif
