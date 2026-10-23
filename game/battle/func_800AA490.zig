//! Turns a battle actor toward a tile (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AA490 (src/BATTLE/BATTLE.PRG/40564.c)
/// Sets actor `slot`'s turn toward the tile `target` names: at once (mode 0), over a turn count (mode -1, scaled by rate), or with a fixed count (other modes).
pub fn func_800AA490(slot: c_int, target: ?*v.func_8006EBF8_t_fields, mode: c_int, rate: c_int) callconv(.c) void {
    const actor = psx.at(v.g.D_800F4538(), slot).* orelse return;
    const t = target.?;

    var angle: c_int = @as(c_int, psx.getBits(t, "unk0_0")) -% @as(c_int, actor.unk0.currentTileX);
    const dz: c_int = @as(c_int, psx.getBits(t, "unk0_16")) -% @as(c_int, actor.unk0.currentTileZ);

    if (angle == 0 and dz == 0) {
        angle = @as(c_int, psx.getBits(t, "unk0_24")) *% 16;
    } else {
        angle = 0xC00 -% c.ratan2(dz, angle);
        angle = @rem(angle, 4096);
    }

    angle -%= @as(c_int, actor.unk0.facing);

    if (angle >= 4096 / 2) {
        angle -%= 4096;
    }
    if (angle < -4096 / 2) {
        angle +%= 4096;
    }

    actor.unk0.unk3E = psx.conv(i16, angle);
    actor.unk0.unk3C = 0;
    actor.unk0.unk40 = 0;

    if (angle == 0) return;

    turn: {
        if (mode == 0) {
            actor.unk0.unk18 = 0;
            break :turn;
        }
        if (mode == -1) {
            if (angle < 0) {
                angle = -%angle;
            }
            angle *%= rate;
            actor.unk0.unk18 = psx.conv(u16, @divTrunc(angle, 4096));
            if ((angle & 0xFFF) != 0) {
                actor.unk0.unk18 = psx.conv(u16, @divTrunc(angle, 4096) +% 1);
            }
            if (actor.unk0.unk18 == 0) {
                break :turn;
            }
            return;
        }
        actor.unk0.unk18 = psx.conv(u16, mode);
        return;
    }

    actor.unk0.facing = psx.conv(i16, (@as(c_int, actor.unk0.facing) +% @as(c_int, actor.unk0.unk3E)) & 0xFFF);
}
