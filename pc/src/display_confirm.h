/* "Keep these settings?" (pc/src/display_confirm.c; plan
 * pc/docs/plans/2026-10-18-lua-m1-options.md, "10 s confirm"): a risky
 * display change (a settings row flagged `confirm` in the catalog,
 * ui_settings.cpp: window mode now; fullscreen mode and display with their
 * back-ends) applies at once and
 * reverts by itself unless the player keeps it in time, so a mode the screen
 * cannot show never sticks. Pure logic: the clock (real time, not the game's),
 * reading and applying a setting and saving come from the caller.
 *
 *   begin(key, value): snapshot the key's value, apply the new one and start
 *     the countdown. Another change while one is pending joins it: a key
 *     already held keeps its first snapshot, a new key adds its own, and the
 *     countdown starts again. Revert restores every held key (the last
 *     changed first) to its value before the first change. Changing a held key back to its
 *     snapshot lets it go; with none left nothing is pending (nothing to keep).
 *   keep: commits (the keys save from now on; ops.save runs).
 *   revert: Cross, or tick past the timeout.
 *
 * While pending, the settings writer saves a held key's snapshot instead of
 * its live value (pc_display_confirm_held), so a save in between never
 * writes an unconfirmed mode. */
#ifndef PC_DISPLAY_CONFIRM_H
#define PC_DISPLAY_CONFIRM_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double (*now)(void* user); /* seconds, any origin, real time */
    /* The setting's value as settings.cfg writes it; 0 when unknown (begin refuses). */
    int (*get)(void* user, const char* key, char* out, int size);
    void (*set)(void* user, const char* key, const char* value); /* apply live (no save) */
    void (*save)(void* user); /* after keep: write the settings now; may be NULL */
    void* user;
} pc_display_confirm_ops;

#define PC_DISPLAY_CONFIRM_KEYS 8   /* keys one confirm holds */
#define PC_DISPLAY_CONFIRM_VALUE 96 /* a value's longest text, with its NUL */

/* Resets (nothing pending) with these callbacks (copied); NULL: none (begin refuses). */
void pc_display_confirm_init(const pc_display_confirm_ops* ops);
/* The countdown in seconds (default 10; data-driven by the caller). <= 0 restores the default. */
void pc_display_confirm_set_timeout(double seconds);
double pc_display_confirm_timeout(void);
/* Apply `value` to `key` under a confirm (which keys ask is the caller's data:
 * the catalog's `confirm` rows). 1: pending now; 0: nothing to do (the same
 * value; a held key set back to its snapshot, which ends the confirm when it
 * was the last; or reached from this module's own ops.set while it applies
 * or reverts); -1: refused (no callbacks, an unknown key, too many keys, a
 * value too long). */
int pc_display_confirm_begin(const char* key, const char* value);
int pc_display_confirm_pending(void);
/* Seconds left (0 when nothing is pending). */
double pc_display_confirm_remaining(void);
/* The held keys (for the prompt): index 0..count-1, in the order changed. */
int pc_display_confirm_count(void);
const char* pc_display_confirm_key(int i);
void pc_display_confirm_keep(void);
void pc_display_confirm_revert(void);
/* Every frame: reverts once the countdown ran out. Returns 1 when it reverted now. */
int pc_display_confirm_tick(void);
/* For the settings writer: 1 and the value to save (the snapshot) while `key`
 * is held, 0 when its live value saves. */
int pc_display_confirm_held(const char* key, char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
