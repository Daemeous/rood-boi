/* Save states (snapshot.c). */
#ifndef PC_SNAPSHOT_H
#define PC_SNAPSHOT_H
/* A host unit's static that is game state after all (its value ends up in
 * guest memory, e.g. a counter written into the game's packets): saved and
 * restored with the game's own statics (the section rood_state_data; on
 * Windows the grouped .data$rood.state.m between snapshot.c's markers, so
 * give it an initialiser: it must not be a .bss). */
#ifdef _WIN32
#define PC_SAVED_STATE __attribute__((section(".data$rood.state.m")))
#else
#define PC_SAVED_STATE __attribute__((section("rood_state_data")))
#endif
#endif
