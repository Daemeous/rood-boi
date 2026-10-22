//! Queues a read of the current stage's .EVT file (src/BATTLE/BATTLE.PRG/573B8.c).
const psx = @import("common").psx;
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: func_800C00E8 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Sets up the CD request for the .EVT file (6144 bytes) and queues it into `arg1`.
pub fn func_800C00E8(_: c_int, arg1: ?*anyopaque) callconv(.c) void {
    v.g.D_800F4BF0().size = 6144;
    // _evtFile is an i16: widen to int before the multiply and add, then wrap as C does.
    v.g.D_800F4BF0().lba = (@as(c_int, v.g._evtFile().*) *% 3) +% 110000;
    v.g.D_800F4BBC().* = @ptrCast(c.vs_main_allocateCdQueueSlot(@ptrCast(v.g.D_800F4BF0())));
    c.vs_main_cdEnqueue(@ptrCast(v.g.D_800F4BBC().*), arg1);
}
