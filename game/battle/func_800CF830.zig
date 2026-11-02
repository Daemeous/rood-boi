//! Calls the trail renderers of every effect with 3 (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CF830 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// For every script on D_800F54B0's list, every effect whose renderer is func_800CE174 (the trail) is called with 3.
pub fn func_800CF830(arg0: c_int, arg1: c_int) callconv(.c) void {
    const Renderer = *const fn (*align(4) v.func_800D4910_t, c_uint, c_int) callconv(.c) c_int; // effectExec
    _ = arg0;
    _ = arg1;
    var script = v.g.D_800F54B0().*;
    while (script) |s| {
        var node = psx.get(s.unkD1C.unk34);
        while (node) |n| {
            const address: usize = if (n.renderer) |r| @intFromPtr(r) else 0;
            if (address == @intFromPtr(&c.func_800CE174)) {
                const renderer: ?Renderer = @ptrCast(n.renderer);
                _ = renderer.?(n, 3, 0);
            }
            node = n.next;
        }
        script = psx.get(s.next);
    }
}
