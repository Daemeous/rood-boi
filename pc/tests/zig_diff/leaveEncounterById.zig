//! zig_diff spec: func_800D82A8 -> pc.leave_encounter_by_id.leaveEncounterById (D2: pc/src/battle_movement.c).
//! Inputs: the AI entity table D_800F5878 with each of its 17 slots empty a
//! fifth of the time, else a random-filled block; an actor id 0..16. The stub of
//! func_800D82CC records the entity it is handed, so id 0, an empty slot and
//! a wrong slot show.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").leaveEncounterById));

pub const classes = [_][:0]const u8{ "id-zero", "empty-slot", "present" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        if (ctx.chance(5)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x40, 16);
            for (block[0..0x40]) |*byte| byte.* = ctx.int(u8);
            table[i] = @ptrCast(block);
        }
    }
    // Slot 0 is filled most of the time: id 0 must still do nothing.
    if (ctx.chance(2)) table[0] = @ptrCast(ctx.allocBytes(0x40, 16));
    return .{if (ctx.chance(6)) 0 else ctx.range(c_int, 1, 16)};
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    return if (v.g.D_800F5878()[@intCast(args[0])] == null) 1 else 2;
}
