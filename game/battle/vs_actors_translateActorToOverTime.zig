//! Places a point relative to a placed object's position and passes it on
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_translateActorToOverTime (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Passes `point` minus object `index`'s position on to vs_actors_setActorTranslation, if the object exists.
pub fn vs_actors_translateActorToOverTime(index: c_int, point: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    const temp = psx.at(v.g.vs_actors_actorRecords(), index).*;
    if (temp) |t| {
        const p = point.?;
        // The pad field is never written by the C and reads as 0 in its object.
        var sp10: v.SVECTOR = std.mem.zeroes(v.SVECTOR);
        sp10.vx = psx.conv(i16, @as(c_int, p.vx) -% @as(c_int, t.unk0.position.vx));
        sp10.vy = psx.conv(i16, @as(c_int, p.vy) -% @as(c_int, t.unk0.position.vy));
        sp10.vz = psx.conv(i16, @as(c_int, p.vz) -% @as(c_int, t.unk0.position.vz));

        c.vs_actors_setActorTranslation(index, psx.cptr(c.SVECTOR, &sp10), arg2);
    }
}
