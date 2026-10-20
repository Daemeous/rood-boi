//! zig_diff spec: func_8007357C -> field_menu.enterMenuMode (146C.c).
//! Inputs: any two (unused) ints; vs_battle_characterState an arena actor
//! with any weaponDrawn. The two calls are recording stubs (with the state
//! hash, so a store moved across a call shows).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").enterMenuMode));

pub const classes = [_][:0]const u8{ "drawn", "sheathed" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.vs_battle_actor);
    actor.weaponDrawn = ctx.pick(i32, &.{ 0, 1, 2, 3, -1 });
    v.g.vs_battle_characterState().* = actor;
    return .{ ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(_: Args) usize {
    return if (v.g.vs_battle_characterState().*.?.weaponDrawn & 1 != 0) 0 else 1;
}
