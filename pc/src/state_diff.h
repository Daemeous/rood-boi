/* Same-process state images and per-vsync differential digests. Host-only. */
#ifndef PC_STATE_DIFF_H
#define PC_STATE_DIFF_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char* start;
    size_t size;
    const char* name;
} pc_state_range;
/* Supplied by snapshot.c. Return total required count (even if max is too
 * small), or -1 on error. Never write more than max entries. */
int pc_state_ranges(pc_state_range* out, int max);

typedef struct pc_state_image pc_state_image;
/* Capture returns NULL on unavailable state, invalid ranges or allocation
 * failure. Images own their bytes, not the live ranges or their names.
 * Restore validates the current layout/SPU size before changing live state.
 * SPU load failure is reported, but cannot guarantee rollback of the SPU. */
pc_state_image* pc_state_capture(void);
int pc_state_restore(const pc_state_image* image);
void pc_state_image_free(pc_state_image* image);
/* Compare two images, honoring current address masks: 0 equal, 1 different,
 * -1 invalid/incompatible. SPU is compared in full, without address masks. */
int pc_state_image_compare(const pc_state_image* a, const pc_state_image* b);

void pc_state_mask_clear(void);
int pc_state_mask_add(uintptr_t start, size_t size);
/* Atomically replace up to 64 masks while idle; invalidates the baseline. */
int pc_state_masks_set(const uintptr_t* starts, const size_t* sizes, int count);
/* Start requires idle state, positive capacity for record, and at least one
 * recorded tick for compare. Compare ignores capacity except negative values
 * are invalid. alternate is -1 or a valid optional ownership subsystem.
 * Reaching record capacity/replay length stops automatically; ticks never
 * exceed the stored baseline. Masks cannot change during a run and changing
 * them afterwards invalidates the baseline. Return 0 on success, -1 on error. */
int pc_digest_start(int compare, int capacity, int alternate);
/* New masks and baseline are committed together, or the old baseline survives. */
int pc_digest_record_masks(int capacity, const uintptr_t* starts, const size_t* sizes, int count);
void pc_digest_stop(void);
/* snprintf semantics; negative size or NULL with positive size is invalid.
 * JSON includes error (0 success, -1 capture/layout failure). */
int pc_digest_result(char* out, int size);
void pc_state_diff_vsync(void);
#ifdef __cplusplus
}
#endif
#endif
