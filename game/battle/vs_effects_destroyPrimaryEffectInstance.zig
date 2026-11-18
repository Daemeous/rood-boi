//! Frees an effect script (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// psx_set of a guest address: the 4-byte field's address on 64-bit, the pointer's bits on 32-bit (stored as
/// the C stores it: by its bits, so an unaligned address does not trap).
fn setAddress(field: anytype, address: usize) void {
    const F = @typeInfo(@TypeOf(field)).pointer.child;
    switch (@typeInfo(F)) {
        .optional, .pointer => @as(*usize, @ptrCast(field)).* = address,
        else => field.raw = address,
    }
}

/// orig: vs_effects_destroyPrimaryEffectInstance (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Unlinks the script from vs_effects_primaryInstances's list, frees its particles (vs_effects_releaseAllEffectParticles) and effects (each renderer called with 4, then vs_effects_detachEffectRenderer), and frees it.
pub fn vs_effects_destroyPrimaryEffectInstance(script_arg: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    const Renderer = *const fn (*align(4) v.func_800D4910_t, c_uint, c_int) callconv(.c) c_int; // effectExec
    const script = script_arg.?;
    var link = v.g.vs_effects_primaryInstances().*;
    if (link == script) {
        v.g.vs_effects_primaryInstances().* = psx.get(script.next);
    } else {
        while (psx.get(link.?.next)) |next| {
            if (next == script) {
                setAddress(&link.?.next, @intFromPtr(psx.get(script.next)));
                break;
            }
            link = next;
        }
    }
    c.vs_effects_releaseAllEffectParticles(@ptrCast(script));
    var node = psx.get(script.context.unk34);
    while (node) |n| {
        const next = n.next;
        const renderer: ?Renderer = @ptrCast(n.renderer);
        _ = renderer.?(n, 4, 0);
        c.vs_effects_detachEffectRenderer(@ptrCast(script), @ptrCast(@alignCast(n)));
        node = next;
    }
    c.vs_main_freeHeapR(script);
}
