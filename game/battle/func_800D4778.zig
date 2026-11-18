//! An effect-script opcode: spawns a child script (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
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

/// orig: func_800D4778 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// vs_effects_createPrimaryEffectInstance on a spawn record of the script's byte pointer, its next halfword and its unkD1C block; returns 1.
pub fn func_800D4778(script: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    const s = script.?;
    var spawn: v.vs_effects_EffectSpawnRequest = undefined; // the C leaves the other fields uninitialised
    setAddress(&spawn.script, @intFromPtr(psx.get(s.unkC)));
    spawn.scriptOffset = c.vs_effects_readScriptHalfword(@ptrCast(s));
    psx.put(&spawn.source, &s.context);
    _ = c.vs_effects_createPrimaryEffectInstance(@ptrCast(&spawn));
    return 1;
}
