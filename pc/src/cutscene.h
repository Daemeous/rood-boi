/* The cutscene player (cutscene.cpp; pc/docs/Cutscene_Player.md): a take is a stretch of the game
 * from a start frame, with automatic rewind marks (state_rewind.c's take slots) and a log of every
 * input the game read per frame (pad, camera stick, the scanline estimate), so any earlier frame can
 * be gone back to and played forward exactly. The Cutscene window (cutscene_panel.cpp) and RPC
 * `cutscene` drive it; the instant replay is a take that always runs (a setting). */
#ifndef ROOD_CUTSCENE_H
#define ROOD_CUTSCENE_H
#ifdef __cplusplus
extern "C" {
#endif

/* ---- dev_rpc.c's poll, at the vsync boundary (may rewind: does not return then) ---- */
void pc_take_vsync(void); /* once per vsync, before a step runs it */
void pc_take_idle(void);  /* in the paused loop */
/* ---- input_record.c, boot_runtime.c: the take's log (record live frames, give replayed ones back) ---- */
int pc_take_active(void);
int pc_take_buttons(unsigned vsync, unsigned* pad);
int pc_take_stick(unsigned vsync, float stick[2], int* valid);
int pc_take_scanlines(int value);
/* ---- boot_window.c: the pacing (0 unpaced: a seek; 1; 2 and 4 slow motion) ---- */
int pc_take_pace(void);
/* ---- the instant replay key (bind_key_replay; dev_overlay.cpp) ---- */
void pc_take_hotkey(void);
/* ---- RPC cutscene (dev_rpc.c): params a JSON object; the reply in out ---- */
int pc_take_rpc(const char* params, char* out, int size);

/* Tunables (settings.cfg; the window; RPC cutscene {action: "settings"}). */
extern int pc_take_interval;           /* cutscene_mark_interval: a mark every N frames */
extern int pc_take_marks_max;          /* cutscene_marks_max: the ring's size */
extern int pc_take_budget_mb;          /* cutscene_budget_mb: the marks' memory */
extern int pc_instant_replay;          /* instant_replay: the rolling take on */
extern int pc_instant_replay_seconds;  /* instant_replay_seconds */
extern int pc_instant_replay_interval; /* instant_replay_interval: frames between its marks */
extern int pc_cutscene_auto_advance;   /* cutscene_auto_advance: a scene's dialogue goes on by itself */
extern int pc_cutscene_dialogue_hold;  /* cutscene_dialogue_hold: vsyncs a box waits before it goes on */

#ifdef __cplusplus
}
#endif
#endif
