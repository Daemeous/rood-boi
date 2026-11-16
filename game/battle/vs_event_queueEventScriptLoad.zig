//! Queues a read of the current stage's .EVT file (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const psx = @import("common").psx;
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: vs_event_queueEventScriptLoad (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Sets up the CD request for the .EVT file (6144 bytes) and queues it into `arg1`.
pub fn vs_event_queueEventScriptLoad(_: c_int, arg1: ?*anyopaque) callconv(.c) void {
    v.g.vs_event_eventFileDiscLocation().size = 6144;
    // _evtFile is an i16: widen to int before the multiply and add, then wrap as C does.
    v.g.vs_event_eventFileDiscLocation().lba = (@as(c_int, v.g._evtFile().*) *% 3) +% 110000;
    v.g.vs_event_eventFileDiscRequest().* = @ptrCast(c.vs_main_allocateCdQueueSlot(@ptrCast(v.g.vs_event_eventFileDiscLocation())));
    c.vs_main_cdEnqueue(@ptrCast(v.g.vs_event_eventFileDiscRequest().*), arg1);
}
