//! Hit test of a contact point against the actor slots in D_800F4538 (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A4494 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Finds the actor a contact point touches and sends it to the knockback state; returns 1 if one was hit.
pub fn func_800A4494(contact: c_int, point: ?*v.SVECTOR) callconv(.c) c_int {
    var i: usize = 0;
    while (i < 17) : (i += 1) {
        const actor = v.g.D_800F4538()[i] orelse continue;
        if (psx.getBits(&actor.unk0, "skip") != 0) continue;
        if (@as(c_int, psx.getBits(&actor.unk0, "unkC_0")) == contact) {
            if (psx.getBits(&actor.unk0, "unkA_3") != 2) continue;
            const sx: *align(1) i16 = @ptrCast(&actor.unk17EC[0]);
            const sz: *align(1) i16 = @ptrCast(&actor.unk17EC[4]);
            const dx: c_int = @as(c_int, point.?.vx) -% @as(c_int, sx.*);
            const dz: c_int = @as(c_int, point.?.vz) -% @as(c_int, sz.*);
            actor.unk0.position.vx = psx.conv(i16, @as(c_int, actor.unk0.position.vx) +% dx);
            actor.unk0.position.vz = psx.conv(i16, @as(c_int, actor.unk0.position.vz) +% dz);
            const hit = c.func_800A46A4(@ptrCast(actor));
            actor.unk0.position.vx = psx.conv(i16, @as(c_int, actor.unk0.position.vx) -% dx);
            actor.unk0.position.vz = psx.conv(i16, @as(c_int, actor.unk0.position.vz) -% dz);
            if (hit == 0) continue;
        } else {
            const radius: c_int = actor.unk63C;
            const x: c_int = point.?.vx;
            if (x +% 64 < @as(c_int, actor.unk0.position.vx) -% radius or
                x -% 64 > @as(c_int, actor.unk0.position.vx) +% radius) continue;
            const z: c_int = point.?.vz;
            if (z +% 64 < @as(c_int, actor.unk0.position.vz) -% radius or
                z -% 64 > @as(c_int, actor.unk0.position.vz) +% radius) continue;
            const y: c_int = actor.unk0.position.vy;
            const py: c_int = point.?.vy;
            if (py < y -% 288 or py >= y) continue;
            if (psx.getBits(&actor.unk0, "unkA_3") != 2) return 1;
        }
        // transition: the knockback state
        actor.unk0.unk34.vx = 0;
        actor.unk0.unk34.vy = 3;
        actor.unk0.unk34.vz = 0;
        actor.unk6EE = 0;
        actor.unk6EF = 0;
        psx.setBits(&actor.unk0, "unkA_0", 3);
        psx.setBits(&actor.unk0, "unkA_3", 0);
        psx.setBits(&actor.unk0, "unk9_6", 0);
        psx.setBits(&actor.unk0, "unkC_0", 0);
        _ = c.func_800A4A88(@ptrCast(actor), 2);
        return 1;
    }
    return 0;
}
