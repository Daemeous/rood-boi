//! Turns an actor toward a target point and starts its pose animation
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009E2E0 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Faces actor `slot` toward `target` (phase 0 only) and starts its pose animation for `phase`.
pub fn func_8009E2E0(slot: c_int, target: ?*v.SVECTOR, phase: c_int) callconv(.c) void {
    const sp10 = [2]u8{ 0x55, 0x58 };
    const sp18 = [2]u8{ 0x57, 0x59 };
    var sp20: [*c]c.u_short = null;

    const actor = psx.at(v.g.D_800F4538(), slot).* orelse return;

    if (phase == 0) {
        var diff: c_int = undefined;
        if (target) |vec| {
            const dx: c_int = @as(c_int, vec.vx) -% @as(c_int, actor.unk0.position.vx);
            const dz: c_int = @as(c_int, vec.vz) -% @as(c_int, actor.unk0.position.vz);

            var angle: c_int = if (dx == 0 and dz == 0)
                @as(c_int, actor.unk0.facing)
            else
                (0xC00 -% c.ratan2(dz, dx)) & 0xFFF;

            angle -%= @as(c_int, actor.unk0.facing);

            if (angle > 4096 / 2) {
                angle -%= 4096;
            } else if (angle < -4096 / 2) {
                angle +%= 4096;
            }

            diff = angle;
        } else {
            diff = 0;
        }

        actor.unk0.unk3E = psx.conv(i16, diff);
        actor.unk0.unk18 = 10;

        if (@as(c_int, actor.unk63C) > 0x80) {
            actor.unk0.unk18 = 20;
        }
    }

    // phase is 0 or 1 in the game; other values read past the end of sp10, as the C does.
    var anim: c_int = @as(c_int, psx.at(&sp10, phase).*);
    if (psx.getBits(&actor.unk0, "weaponDrawn") != 0) {
        anim +%= 100;
    }

    _ = c.func_800AD494(@ptrCast(actor), anim, &sp20);

    if (sp20 == null) {
        anim = @as(c_int, psx.at(&sp18, phase).*);
        if (psx.getBits(&actor.unk0, "weaponDrawn") != 0) {
            anim +%= 100;
        }
    }

    _ = c.func_800A0204(slot, anim, 0, 6);
    c.func_8009FD38(@ptrCast(actor));
}
