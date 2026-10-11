/* The editor's undo history (pc/src/editor_undo.cpp): transactions over
 * everything the editor edits, as Unreal's. A transaction holds the state
 * before and after one user action (a click, a gizmo or slider drag from
 * press to release, a typed value on Enter, one RPC edit); undo puts back
 * the parts the action changed (lights, light actors, the room's settings,
 * glow overrides, texture effect overrides, room group visibility, the
 * graphics and lighting quality settings) and the selection it had.
 *
 * Changes are found by comparing snapshots, so every edit path is covered
 * without each widget registering itself; code that knows a better name for
 * what it is doing says so with pc_undo_hint. The history is per room visit:
 * a room change, Reload area or the room file changing on disk clears it
 * (its states belong to the room's file as loaded); saving keeps it. */
#ifndef ROOD_EDITOR_UNDO_H
#define ROOD_EDITOR_UNDO_H
#ifdef __cplusplus
extern "C" {
#endif

/* The overlay's frame: before the editor's panels run, and after (busy: an
 * item or the gizmo is held, so the action is not over yet). */
void pc_undo_frame_begin(void);
void pc_undo_frame_end(void);
/* An RPC edit: the state before it (prepare), then commit what it changed
 * (flush; `name` names it when nothing better is known). */
void pc_undo_prepare(void);
void pc_undo_flush(const char* name);
/* The next transaction's name ("Light all fixtures"; the latest hint wins). */
void pc_undo_hint(const char* name);
/* Hold a transaction open across several edits (RPC editor_undo begin/end:
 * a drag made of many moves is one transaction). */
void pc_undo_begin(const char* name);
void pc_undo_end(void);
/* Undo or redo the latest transaction; 0 when there is none. */
int pc_undo_undo(void);
int pc_undo_redo(void);
/* Names: the transaction Ctrl+Z would undo (redo: Ctrl+Y would redo), or NULL. */
const char* pc_undo_name(int redo);
/* The history: count, and entry i (0 oldest); *current: how many are done. */
int pc_undo_history(int* current);
const char* pc_undo_entry(int i);
/* Undo or redo until `done` transactions are done (Edit > History). */
void pc_undo_jump(int done);
/* Forget the history (a new room visit); the next frame takes a new base. */
void pc_undo_reset(void);
/* Keep the history, but take the state now as the base (the editor opens). */
void pc_undo_rebase(void);
/* RPC editor_undo: the history as JSON. */
int pc_undo_json(char* out, int size);
/* The most transactions kept (oldest dropped first). */
#define PC_UNDO_LIMIT 200

#ifdef __cplusplus
}
#endif
#endif
