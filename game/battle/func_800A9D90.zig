//! Calls func_800AA698 for the actor in slot `arg0`, with `arg1` taken relative to the actor's position
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A9D90 (src/BATTLE/BATTLE.PRG/40564.c)
/// Calls func_800AA698 with `arg1` relative to slot `arg0`'s position, with its field at 0x5C2 set to `arg2` unless `arg2` is -1.
pub fn func_800A9D90(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    var sp10: v.SVECTOR = undefined;
    const slots = v.g.D_800F4538();
    const actor: *v.D_800F4538_t = psx.at(slots, arg0).* orelse return;
    const from = arg1.?;

    sp10.vx = psx.conv(i16, @as(c_int, from.vx) -% @as(c_int, actor.unk0.position.vx));
    sp10.vy = psx.conv(i16, @as(c_int, from.vy) -% @as(c_int, actor.unk0.position.vy));
    sp10.vz = psx.conv(i16, @as(c_int, from.vz) -% @as(c_int, actor.unk0.position.vz));

    if (arg2 == -1) {
        c.func_800AA698(arg0, psx.cptr(c.SVECTOR, &sp10), -1);
        return;
    }

    const saved = actor.unk5C2;
    actor.unk5C2 = psx.conv(u16, arg2);

    c.func_800AA698(arg0, psx.cptr(c.SVECTOR, &sp10), -1);

    actor.unk5C2 = saved;
}
