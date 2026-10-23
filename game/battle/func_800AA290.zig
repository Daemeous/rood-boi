//! Points an actor's move offset at a target tile and steps it, or sets its step count (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// Adds the move offset to the actor's position (the C's addVector, inlined there).
fn addVector(position: *v.SVECTOR, offset: *const v.SVECTOR) void {
    position.vx = psx.conv(i16, @as(c_int, position.vx) +% @as(c_int, offset.vx));
    position.vy = psx.conv(i16, @as(c_int, position.vy) +% @as(c_int, offset.vy));
    position.vz = psx.conv(i16, @as(c_int, position.vz) +% @as(c_int, offset.vz));
}

/// orig: func_800AA290 (src/BATTLE/BATTLE.PRG/40564.c)
/// Sets actor arg0's move offset toward tile (arg1) and steps it: all at once (arg2 0), by arg3 per unit of distance (arg2 -1), or by arg2 steps.
pub fn func_800AA290(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int, arg3: c_int) callconv(.c) void {
    const actor = psx.at(v.g.D_800F4538(), arg0).* orelse return;
    const field = arg1.?;

    // target.vy is uninitialised in the original; it is zeroed here.
    var target: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    target.vx = psx.conv(i16, @as(c_int, psx.getBits(field, "unk0_0")) *% 128 +% 64);
    target.vz = psx.conv(i16, @as(c_int, psx.getBits(field, "unk0_16")) *% 128 +% 64);
    actor.unk0.unk34.vx = psx.conv(i16, @as(c_int, target.vx) -% @as(c_int, actor.unk0.position.vx));
    actor.unk0.unk34.vz = psx.conv(i16, @as(c_int, target.vz) -% @as(c_int, actor.unk0.position.vz));
    actor.unk0.unk34.vy = psx.conv(i16, c.func_800A6EE8(psx.cptr(c.SVECTOR, &target), 0, 0, 4) -% @as(c_int, actor.unk0.position.vy));

    if (actor.unk0.unk34.vx == 0 and actor.unk0.unk34.vz == 0 and actor.unk0.unk34.vy == 0) {
        return;
    }

    if (arg2 == 0) {
        actor.unk0.unk1A = 0;
        addVector(&actor.unk0.position, &actor.unk0.unk34);
        return;
    }

    if (arg2 == -1) {
        var temp: c_int = @as(c_int, actor.unk0.unk34.vx);
        temp *%= temp;
        if (temp < 0) temp = -%temp;
        var dist: c_int = temp;
        temp = @as(c_int, actor.unk0.unk34.vz);
        temp *%= temp;
        if (temp < 0) temp = -%temp;
        dist +%= temp;
        temp = @as(c_int, actor.unk0.unk34.vy);
        temp *%= temp;
        if (temp < 0) temp = -%temp;
        dist = c.vs_gte_rsqrt(dist +% temp) *% arg3;
        temp = @divTrunc(dist, 128);
        actor.unk0.unk1A = psx.conv(u16, temp);
        if ((dist & 0x7F) != 0) {
            actor.unk0.unk1A = psx.conv(u16, temp +% 1);
        }
        if (actor.unk0.unk1A == 0) {
            addVector(&actor.unk0.position, &actor.unk0.unk34);
        }
        return;
    }

    actor.unk0.unk1A = psx.conv(u16, arg2);
}
