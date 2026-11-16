//! Queues the room's section-10 CD files into heap blocks (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_queueTiledBackgroundLoad (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Queues the room's two section-10 CD files into fresh heap blocks; returns 1 when queued, 0 otherwise.
pub fn vs_field_queueTiledBackgroundLoad(arg0: c_int) callconv(.c) c_int {
    const temp_s0 = psx.get(v.g.vs_battle_roomData().section10);
    v.g.vs_field_legacyBackgroundTexturePage().* = psx.conv(u8, arg0);
    if (temp_s0) |s0| {
        v.g.vs_field_legacyBackgroundTextureReservationId().* = psx.conv(i16, s0.*);
        if (c.vs_render_reserveTexturePages(psx.conv(c_uint, v.g.vs_field_legacyBackgroundTexturePage().*), psx.conv(c_uint, v.g.vs_field_legacyBackgroundTextureReservationId().*), 5) != 0) {
            const new_var: c_int = 0x800;
            // s0 is a u_short*: +0x20 and +0x42 are byte offsets 0x40 and 0x84. The PS1 CD code reads these fields
            // with word loads, so the records are 4-aligned.
            const temp_s2: *align(4) v.vs_main_CdFile = @alignCast(psx.byteAt(v.vs_main_CdFile, s0, 0x40));
            const temp_s3: *align(4) v.vs_main_CdFile = @alignCast(psx.byteAt(v.vs_main_CdFile, s0, 0x84));
            var var_a0: c_int = temp_s2.size;
            var var_s0: c_int = temp_s3.size;
            if ((var_a0 & 0x7FF) != 0) {
                var_a0 = (var_a0 +% new_var) -% (var_a0 & 0x7FF);
            }
            if ((var_s0 & 0x7FF) != 0) {
                var_s0 = (var_s0 +% new_var) -% (var_s0 & 0x7FF);
            }
            const heap0: ?*align(4) anyopaque = @alignCast(c.vs_main_allocHeapR(psx.conv(c_uint, var_a0)));
            v.g.vs_field_legacyBackgroundTimBuffer().* = @ptrCast(heap0);
            const heap1: ?*align(4) anyopaque = @alignCast(c.vs_main_allocHeapR(psx.conv(c_uint, var_s0)));
            v.g.vs_field_loadedBackgroundLayout().* = @ptrCast(heap1);
            if (v.g.vs_field_legacyBackgroundTimBuffer().* == null or v.g.vs_field_loadedBackgroundLayout().* == null) {
                return 0;
            }
            v.g.vs_field_backgroundTextureDiscRequest().* = @ptrCast(c.vs_main_allocateCdQueueSlot(psx.cptr(c.vs_main_CdFile, temp_s2)));
            v.g.vs_field_backgroundLayoutDiscRequest().* = @ptrCast(c.vs_main_allocateCdQueueSlot(psx.cptr(c.vs_main_CdFile, temp_s3)));
            c.vs_main_cdEnqueue(@ptrCast(v.g.vs_field_backgroundTextureDiscRequest().*), @ptrCast(v.g.vs_field_legacyBackgroundTimBuffer().*));
            c.vs_main_cdEnqueue(@ptrCast(v.g.vs_field_backgroundLayoutDiscRequest().*), @ptrCast(v.g.vs_field_loadedBackgroundLayout().*));
            return 1;
        }
    }
    v.g.vs_field_backgroundTextureDiscRequest().* = null;
    v.g.vs_field_backgroundLayoutDiscRequest().* = null;
    return 0;
}
