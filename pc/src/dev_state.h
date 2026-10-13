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
/* The room's characters' positions (world units; Ashley first), at most
 * `max`; returns how many (battle builds; weak elsewhere). */
int pc_dev_actor_positions(float (*out)[3], int max) __attribute__((weak));
#ifdef __cplusplus
}
#endif
#endif
