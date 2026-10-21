/* Game state snapshot for the developer tools (dev_state.c). */
#ifndef PC_DEV_STATE_H
#define PC_DEV_STATE_H
#ifdef __cplusplus
extern "C" {
#endif
struct pc_dev_state {
    int valid, zone, room, start_state, music_id, music_slot;
    int hp, max_hp, mp, max_mp, risk, has_player, x, y, z, facing;
    int game_over, intro_state; /* vs_main_stateFlags: Game Over screen up; prologue */
};
void pc_dev_state(struct pc_dev_state* out) __attribute__((weak));
/* The room's characters (battle builds; weak elsewhere), for the
 * interactive fog (fog_sim.cpp): at most `max`, Ashley (slot 0) first;
 * returns how many. */
struct pc_dev_actor {
    int slot; /* the actor slot D_800F4538[slot]: 0 Ashley */
    float pos[3]; /* its feet, world units (y down) */
    float radius; /* its drop shadow's radius (world units): its footprint */
    int facing; /* 0..4095 */
};
int pc_dev_actors(struct pc_dev_actor* out, int max) __attribute__((weak));
/* The battle action running now (one at a time, any actor; scene_submit.inc
 * pc_battle_action): returns 0 when none. */
struct pc_dev_action {
    int actor; /* the performer's slot */
    int action; /* vs_main_actions index */
    int phase; /* _cameraMode: 5 an effect action casts, 7 a swing or a release starts, 8 a chain step, 9 ending */
    int chain; /* the chain step */
    int type; /* vs_main_actions type: 1 spell, 2 ability, 3 break art, 5 item, 6 base attack, 7 trap */
    int plain; /* a plain attack (no effect) */
    float aim[3]; /* its aim point, world units */
    int target; /* its first target's actor slot (-1: none, or a point) */
};
int pc_dev_battle_action(struct pc_dev_action* out) __attribute__((weak));
/* What BATTLE's camera steps toward now (scene_submit.inc
 * pc_battle_camera_focus), for the depth of field's camera subject
 * (post_profile.cpp): returns 0 outside the battle overlay's states. */
struct pc_dev_camera {
    int state; /* D_800F196C: 2 the action state */
    int mode; /* _cameraMode */
    int candidate; /* modes 1 and 3: a target candidate is chosen (the targeting sphere's) */
    float candidate_pos[3]; /* its point (the body part's or the actor's), world units */
    int candidate_actor; /* its actor slot (-1: none) */
    int area; /* mode 2: an area point */
    float area_pos[3];
    float look_at[3]; /* the camera's look-at, world units */
};
int pc_dev_battle_camera(struct pc_dev_camera* out) __attribute__((weak));
#ifdef __cplusplus
}
#endif
#endif
