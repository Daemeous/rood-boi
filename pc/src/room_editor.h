/* The editor's view into rooms while the free camera flies (room_native.c
 * for room groups, render.c for the cutaway; Native_Rendering.md "The
 * editor's free camera"). Gameplay rendering never uses any of it. */
#ifndef ROOD_ROOM_EDITOR_H
#define ROOD_ROOM_EDITOR_H
#ifdef __cplusplus
extern "C" {
#endif

/* Switches (settings file keys editor_hide_masks, editor_game_cutaway,
 * editor_cutaway, editor_cutaway_radius; RPC graphics, same names). */
extern int pc_editor_hide_masks; /* leave out the black shell groups */
extern int pc_editor_game_cutaway; /* the game's facing cutaway from the free camera's yaw */
extern int pc_editor_cutaway; /* cut room polygons between the eye and the focus */
extern float pc_editor_cutaway_radius; /* world units */
extern float pc_editor_cutaway_opacity; /* share of a cut surface still drawn (a ghost; 0: a hole) */
extern int pc_editor_cutaway_auto; /* cut only while the focus is hidden from the eye */
/* A plugin's free camera (the photo mode's rood.camera, the cutscene player's;
 * editor_view.cpp `photo`) sees the whole room: every group the room has
 * (not hidden by the room's own flags, the black shells left out), no facing
 * cutaway, no Outliner hides, no cut to focus (settings key photo_full_room,
 * RPC photo full_room; 0: the editor's view rules above). */
extern int pc_photo_full_room;
/* editor_view.cpp: a plugin's free camera flies (not the editor's own). */
int pc_editor_photo_camera(void);
/* 1 while a plugin's free camera flies with photo_full_room on. */
int pc_room_photo_whole(void);

/* render.c: the cutaway for the frames drawn from now on (the free camera's
 * focus in world space; on 0 while the game camera is in use; amount 0..1
 * fades it in and out). */
void render_cutaway(int on, const float focus[3], float amount);
/* editor_view.cpp: whether room surfaces hide the focus from the eye now
 * (after hysteresis) and how far the cut has faded in (0..1). */
int pc_editor_cutaway_state(int* hidden, float* amount);

typedef struct {
    unsigned flags; /* group flags (low byte: facing mask) */
    int polygons;
    int black; /* a black shell group */
    int hidden; /* hidden in the Outliner */
    int game_hidden; /* hidden by the room (flags 0x1100) */
    int cutaway; /* left out by the facing mask for the free camera's yaw */
    int position[3];
} pc_room_group_info;
int pc_room_editor_groups(pc_room_group_info* out, int max);
void pc_room_editor_set_hidden(int index, int hidden); /* index -1: show all */
unsigned pc_room_editor_facing(void);
int pc_room_editor_hidden(const void* data, unsigned index);

#ifdef __cplusplus
}
#endif
#endif
