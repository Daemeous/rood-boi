//! Makes a point's offset from a target's position and facing, wrapped to half a turn, and calls the routine behind vs_actors_setActorRotation with it (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_rotateActorToAtRate (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Calls vs_actors_setActorRotation with the wrapped offset of `pos` from target `index`, with the target's field 0x5C0 set to `mode` for the call unless `mode` is -1.
pub fn vs_actors_rotateActorToAtRate(index: c_int, pos: ?*v.SVECTOR, mode: c_int) callconv(.c) void {
    const temp: *v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), index).*.?;
    const point = pos.?;

    var sp10: v.SVECTOR = undefined;

    sp10.vx = psx.conv(i16, @as(c_int, point.vx) -% @as(c_int, temp.unk0.pitch));
    if (@as(c_int, sp10.vx) > 2048) {
        sp10.vx = psx.conv(i16, @as(c_int, sp10.vx) -% 4096);
    }
    if (@as(c_int, sp10.vx) < -2048) {
        sp10.vx = psx.conv(i16, @as(c_int, sp10.vx) +% 4096);
    }

    sp10.vy = psx.conv(i16, @as(c_int, point.vy) -% @as(c_int, temp.unk0.facing));
    if (@as(c_int, sp10.vy) > 2048) {
        sp10.vy = psx.conv(i16, @as(c_int, sp10.vy) -% 4096);
    }
    if (@as(c_int, sp10.vy) < -2048) {
        sp10.vy = psx.conv(i16, @as(c_int, sp10.vy) +% 4096);
    }

    sp10.vz = psx.conv(i16, @as(c_int, point.vz) -% @as(c_int, temp.unk0.roll));
    if (@as(c_int, sp10.vz) > 2048) {
        sp10.vz = psx.conv(i16, @as(c_int, sp10.vz) -% 4096);
    }
    if (@as(c_int, sp10.vz) < -2048) {
        sp10.vz = psx.conv(i16, @as(c_int, sp10.vz) +% 4096);
    }

    if (mode == -1) {
        c.vs_actors_setActorRotation(index, psx.cptr(c.SVECTOR, &sp10), -1);
        return;
    }

    const saved: c_int = temp.unk5C0;
    temp.unk5C0 = psx.conv(u16, mode);

    c.vs_actors_setActorRotation(index, psx.cptr(c.SVECTOR, &sp10), -1);

    temp.unk5C0 = psx.conv(u16, saved);
}
