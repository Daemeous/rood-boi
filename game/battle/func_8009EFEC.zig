//! Turns an actor to face a target, or picks its reaction animation and a
//! height class for a target (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009EFEC (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Mode 0 turns actor `actorId` to face `target`; mode 1 picks its reaction to `target`.
pub fn func_8009EFEC(actorId: c_int, target: ?*v.SVECTOR, mode: c_int) callconv(.c) void {
    const actor: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), actorId).*;

    if (mode == 0) {
        // The C dereferences actor unconditionally in this mode.
        const a = actor.?;
        var facingDelta: c_int = 0;
        if (target) |t| {
            const dx: c_int = @as(c_int, t.vx) -% @as(c_int, a.unk0.position.vx);
            const dz: c_int = @as(c_int, t.vz) -% @as(c_int, a.unk0.position.vz);
            var angle: c_int = undefined;
            if (dx == 0 and dz == 0) {
                angle = @as(c_int, a.unk0.facing);
            } else {
                angle = (0xC00 -% c.ratan2(dz, dx)) & 0xFFF;
            }
            angle -%= @as(c_int, a.unk0.facing);
            if (angle > 0x800) {
                angle -%= 0x1000;
            } else if (angle < -0x800) {
                angle +%= 0x1000;
            }
            facingDelta = angle;
        }
        a.unk0.unk3E = psx.conv(i16, facingDelta);
        a.unk0.unk18 = 0xA;
        if (a.unk63C >= 0x80) {
            a.unk0.unk18 = 0x14;
        }
        var animation: [*c]u16 = null;
        _ = c.func_800AD494(@ptrCast(actor), 0xBF, &animation);
        var value: c_int = 0xBF;
        if (animation == null) {
            value = 0xC1;
        }
        _ = c.func_800A0204(actorId, value, 0, 8);
        _ = c.func_8009FD38(@ptrCast(a));
        return;
    } else if (mode == 1) {
        var animation: [*c]u16 = null;
        _ = c.func_800AD494(@ptrCast(actor), 0xFF, &animation);
        if (animation == null) {
            // Re-read the slot: the call above may have changed it.
            const fallback: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), actorId).*;
            if (fallback) |fb| {
                fb.unk0.unk3E = psx.conv(i16, c.func_8009E180(@ptrCast(fb), @ptrCast(target)));
                fb.unk0.unk18 = 0xA;
                if (fb.unk63C >= 0x81) {
                    fb.unk0.unk18 = 0x14;
                }
                const pitch: c_int = c.func_8009E228(@ptrCast(fb), @ptrCast(target));
                var reactionAnimation: c_int = 0x48;
                if (pitch >= -0xE4) {
                    reactionAnimation = 0x49;
                    if (pitch >= 0xAC) {
                        reactionAnimation = 0x4A;
                    }
                }
                _ = c.func_800A0204(actorId, reactionAnimation, 0, 6);
                _ = c.func_8009FD38(@ptrCast(fb));
                return;
            }
        } else {
            // The C dereferences actor unconditionally in this branch.
            const act = actor.?;
            var origin: v.SVECTOR = undefined;
            var verticalAngle: c_int = 0;
            if (target) |t| {
                var value: c_int = @as(c_int, t.vx) -% @as(c_int, act.unk0.position.vx);
                value = value *% value;
                verticalAngle = @as(c_int, t.vz) -% @as(c_int, act.unk0.position.vz);
                if (value < 0) {
                    value = -%value;
                }
                verticalAngle = verticalAngle *% verticalAngle;
                if (verticalAngle < 0) {
                    verticalAngle = -%verticalAngle;
                }
                value = c.vs_gte_rsqrt(value +% verticalAngle);
                _ = c.func_800A1AF8(@as(c_int, act.unk0.unkF), 0, @ptrCast(&origin), 0);
                verticalAngle = c.ratan2(
                    @as(c_int, t.vy) -% @as(c_int, origin.vy),
                    value,
                );
            }
            if (verticalAngle < -0xE4) {
                v.g.D_800F49E4().* = 0;
            } else {
                if (verticalAngle >= 0xAC) {
                    v.g.D_800F49E4().* = 2;
                } else {
                    v.g.D_800F49E4().* = 1;
                }
            }
            _ = c.func_800A0204(actorId, 0xFF, 0, 8);
            return;
        }
    }
}
