//! Resets the battle status-text queue (src/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// sizeof *D_800F1BAC: the queue is D_800F1BAC_t[32] (the views give its pointer opaque).
const queue_size = 32 * @sizeOf(v.D_800F1BAC_t);

/// orig: func_80093AB4 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Clears the queue's counters and flags and zeroes its 32 entries.
pub fn func_80093AB4() callconv(.c) void {
    v.g.D_800F2270().* = 0;
    v.g.D_800F2272().* = 0;
    v.g.D_800F227E().* = 0;
    v.g.D_800F2278().* = 0;
    v.g.D_800F227A().* = 0;
    c.vs_main_bzero(v.g.D_800F1BAC().*, queue_size);
}
