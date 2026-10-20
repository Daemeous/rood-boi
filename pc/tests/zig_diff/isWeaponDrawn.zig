//! zig_diff spec: func_8007C088 -> actor_weapon.isWeaponDrawn (146C.c).
//! Inputs: an actor id 0-15; every vs_battle_actors slot (16 on 64-bit) is
//! an arena actor with weaponDrawn 0, 1, odd, even, negative or any int.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").isWeaponDrawn));

pub const classes = [_][:0]const u8{ "drawn", "sheathed" };

pub fn prepare(ctx: *zd.Ctx) Args {
    for (0..16) |slot| {
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.weaponDrawn = ctx.pick(i32, &.{ 0, 1, 2, 3, -1, -2, std.math.minInt(i32), std.math.maxInt(i32) });
        v.g.vs_battle_actors()[slot] = actor;
    }
    return .{ctx.range(c_int, 0, 15)};
}

pub fn classify(args: Args) usize {
    return if (psx.at(v.g.vs_battle_actors(), args[0]).*.?.weaponDrawn & 1 != 0) 0 else 1;
}
