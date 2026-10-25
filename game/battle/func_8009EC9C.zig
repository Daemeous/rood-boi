//! Faces battle actor `arg0` toward a target and starts its pose animation
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009EC9C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Turns actor `arg0` toward `arg1` (pitch and turn) and starts pose `arg2`'s animation.
pub fn func_8009EC9C(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int, arg3: c_int) callconv(.c) void {
    // The C indexes the actor table without a bounds check.
    const actor = (psx.at(v.g.D_800F4538(), arg0).* orelse return);

    var anim: c_int = @as(c_int, psx.at(v.g.D_800E8E80(), arg2).*) *% 6 +% 0xCA;

    var sp10: v.SVECTOR = undefined;
    var pitch: c_int = 0;
    if (arg1) |target| {
        var dist: c_int = @as(c_int, target.vx) -% @as(c_int, actor.unk0.position.vx);
        dist = dist *% dist;
        const z: c_int = @as(c_int, target.vz) -% @as(c_int, actor.unk0.position.vz);

        if (dist < 0) {
            dist = 0 -% dist;
        }

        var zz: c_int = z *% z;
        if (zz < 0) {
            zz = 0 -% zz;
        }

        dist = c.vs_gte_rsqrt(dist +% zz);
        c.func_800A1AF8(@as(c_int, actor.unk0.unkF), 0, psx.cptr(c.SVECTOR, &sp10), 0);
        pitch = c.ratan2(@as(c_int, target.vy) -% @as(c_int, sp10.vy), dist);
    }

    if (pitch >= -0xE4) {
        if (pitch >= 0xAC) {
            anim +%= 4;
        } else {
            anim +%= 2;
        }
    }

    if (arg3 == 1) {
        v.g.D_800F49D8().* = arg3;
    }

    v.g.D_800F49D8().* ^= 1;
    anim +%= v.g.D_800F49D8().*;

    var turn: c_int = 0;
    if (arg1) |target| {
        const dx: c_int = @as(c_int, target.vx) -% @as(c_int, actor.unk0.position.vx);
        const dz: c_int = @as(c_int, target.vz) -% @as(c_int, actor.unk0.position.vz);

        var angle: c_int = undefined;
        if (dx == 0 and dz == 0) {
            angle = @as(c_int, actor.unk0.facing);
        } else {
            angle = (3072 -% c.ratan2(dz, dx)) & 0xFFF;
        }

        angle -%= @as(c_int, actor.unk0.facing);

        if (angle > 2048) {
            angle -%= 4096;
        } else if (angle < -2048) {
            angle +%= 4096;
        }
        turn = angle;
    }

    actor.unk0.unk3E = psx.conv(i16, turn);
    actor.unk0.unk18 = 0xA;
    _ = c.func_800A0204(arg0, anim, 0, 8);
}
