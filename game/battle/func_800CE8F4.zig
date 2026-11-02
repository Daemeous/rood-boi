//! Frees an effect script (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// psx_set of a guest address: the 4-byte field's address on 64-bit, the pointer's bits on 32-bit (stored as
/// the C stores it: by its bits, so an unaligned address does not trap).
fn setAddress(field: anytype, address: u32) void {
    const F = @typeInfo(@TypeOf(field)).pointer.child;
    switch (@typeInfo(F)) {
        .optional, .pointer => @as(*usize, @ptrCast(field)).* = address,
        else => field.raw = address,
    }
}

/// orig: func_800CE8F4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Unlinks the script from D_800F53B8's list, frees its particles (func_800D2904) and effects (each renderer called with 4, then func_800CF70C), and frees it.
pub fn func_800CE8F4(script_arg: ?*v.D_800F53B8_t) callconv(.c) void {
    const Renderer = *const fn (*align(4) v.func_800D4910_t, c_uint, c_int) callconv(.c) c_int; // effectExec
    const script = script_arg.?;
    var link = v.g.D_800F53B8().*;
    if (link == script) {
        v.g.D_800F53B8().* = psx.get(script.next);
    } else {
        while (psx.get(link.?.next)) |next| {
            if (next == script) {
                setAddress(&link.?.next, psx.rawOf(script.next));
                break;
            }
            link = next;
        }
    }
    c.func_800D2904(@ptrCast(script));
    var node = psx.get(script.unkD1C.unk34);
    while (node) |n| {
        const next = n.next;
        const renderer: ?Renderer = @ptrCast(n.renderer);
        _ = renderer.?(n, 4, 0);
        c.func_800CF70C(@ptrCast(script), @ptrCast(@alignCast(n)));
        node = next;
    }
    c.vs_main_freeHeapR(script);
}
