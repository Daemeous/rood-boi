/* Save states (snapshot.c). */
#ifndef PC_SNAPSHOT_H
#define PC_SNAPSHOT_H
/* A host unit's static that is game state after all (its value ends up in
 * guest memory, e.g. a counter written into the game's packets): saved and
 * restored with the game's own statics (the section rood_state_data). */
#define PC_SAVED_STATE __attribute__((section("rood_state_data")))
#endif
