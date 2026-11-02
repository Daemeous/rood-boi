//! The target cursor's step (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800C4794 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Moves the targeting cursor (D_800EB9B8) by the right stick or the d-pad: its height while picking a height
/// (or holding R2), else along the ground relative to the camera, kept on the collision map; cross toggles the
/// height pick, triangle cancels (1). Writes the cursor's point into `point`; returns 1 to confirm or cancel.
pub fn func_800C4794(point_arg: ?*v.SVECTOR) callconv(.c) c_int {
    const point = point_arg.?;
    const state = v.g.D_800EB9B8().*.?;
    var offset: v.func_800C4794_t = undefined;
    var scale: c_int = 0; // uninitialised in the original on the height path when the mode has no case below
    var stick_x: c_int = state.unk38.words[0];
    var stick_y: c_int = state.unk38.words[1];
    const flags: v.func_800C1564_flags = state.unk0.unk4.flags;
    offset.words[0] = stick_x;
    offset.words[1] = stick_y;
    point.pad = 0;
    var result: c_int = 0;
    if (state.unk2A != 0) {
        point.vx = state.unk0.unk8.vx;
        point.vy = state.unk0.unk8.vy;
        point.vz = state.unk0.unk8.vz;
        c.func_800C2B0C(@ptrCast(&state.unk0.unk8), @as(c_int, state.unk2A) - 1);
        return 0;
    }
    _ = c.func_800C282C();
    var lock_pitch: c_int = 0;
    stick_x = c.vs_battle_mapStickDeadZone(v.g.vs_main_stickPosBuf().rStickX);
    stick_y = c.vs_battle_mapStickDeadZone(v.g.vs_main_stickPosBuf().rStickY);
    point.pad = 0;
    const held = v.g.vs_main_buttonsState().*;
    if (offset.vec.pad == 0 or (held & 0x80) != 0) {
        lock_pitch = 1;
    }
    const height_unit: c_int = @as(c_int, psx.getBits(&flags, "unk1")) * 32;
    if (lock_pitch != 0) {
        stick_x = stick_y >> 3;
        if (stick_x == 0) {
            if ((held & 0x1000) != 0) {
                stick_x = -8;
            }
            if ((held & 0x4000) != 0) {
                stick_x += 8;
            }
        }
        stick_x +%= offset.vec.vy;
        stick_y = v.g.D_800EB9B8().*.?.unk0.unk0 & 0x7F;
        if (psx.conv(c_uint, stick_y - 1) < 2) {
            stick_y = height_unit;
        } else {
            stick_y = 0;
        }
        if (stick_x < -height_unit) {
            stick_x = -height_unit;
        }
        if (stick_y < stick_x) {
            stick_x = stick_y;
        }
        v.g.D_800EB9B8().*.?.unk38.vec.vy = psx.conv(i16, stick_x);
    } else {
        const old_x: c_int = offset.vec.vx;
        const old_z: c_int = offset.vec.vz;
        const cur = v.g.D_800EB9B8().*.?;
        const angle: c_int = -@as(c_int, cur.unk0.unk2) - cur.unk22;
        if (stick_x == 0 and stick_y == 0) {
            if ((held & 0x1000) != 0) {
                stick_y = -0x40;
            }
            if ((held & 0x4000) != 0) {
                stick_y = 0x40;
            }
            if ((held & 0x8000) != 0) {
                stick_x = -0x40;
            }
            if ((held & 0x2000) != 0) {
                stick_x = 0x40;
            }
        }
        // Object order: rcos then rsin, then rsin then rcos.
        const cos0 = c.rcos(angle);
        const sin0 = c.rsin(angle);
        offset.vec.vx = psx.conv(i16, @as(c_int, offset.vec.vx) +% ((-%stick_x *% cos0 -% stick_y *% sin0 +% 0x200) >> 10));
        const sin1 = c.rsin(angle);
        const cos1 = c.rcos(angle);
        offset.vec.vz = psx.conv(i16, @as(c_int, offset.vec.vz) +% ((-%stick_x *% sin1 +% stick_y *% cos1 +% 0x200) >> 10));
        scale = c.vs_gte_rsqrt(@as(c_int, offset.vec.vx) * offset.vec.vx + @as(c_int, offset.vec.vz) * offset.vec.vz); // two i16 squares: no overflow
        if (scale > 0x1000) {
            stick_x = @as(c_int, offset.vec.vx) - old_x;
            stick_y = @as(c_int, offset.vec.vz) - old_z;
            offset.vec.vx = psx.conv(i16, @divTrunc(@as(c_int, offset.vec.vx) << 12, scale));
            offset.vec.vz = psx.conv(i16, @divTrunc(@as(c_int, offset.vec.vz) << 12, scale));
            const heading = c.ratan2(offset.vec.vx, offset.vec.vz);
            scale = (heading -% c.ratan2(stick_x, stick_y)) & 0xFFF;
            if (psx.conv(c_uint, scale - 8) >= 0xFF0) {
                offset.vec.vx = psx.conv(i16, old_x);
                offset.vec.vz = psx.conv(i16, old_z);
            }
        }
        v.g.D_800EB9B8().*.?.unk38.vec.vx = offset.vec.vx;
        v.g.D_800EB9B8().*.?.unk38.vec.vz = offset.vec.vz;
    }
    if (offset.vec.pad != 0) {
        const cur = v.g.D_800EB9B8().*.?;
        c.func_800C4650(@ptrCast(psx.get(cur.unk44)), cur.unk2E);
    }
    const pressed: c_uint = v.g.vs_main_buttonsPressed().all;
    if (offset.vec.pad == 0) {
        if ((pressed & 0x40) != 0) {
            c.vs_battle_playMenuLeaveSfx();
            point.pad = 1;
            return 1;
        }
        if ((pressed & 0x20) != 0) {
            c.vs_battle_playMenuSelectSfx();
            const cur = v.g.D_800EB9B8().*.?;
            cur.unk38.vec.pad = 1;
            cur.unk0.unk0 |= 0x80;
            cur.unk10.unk0 &= 0x7F;
        }
    } else if ((pressed & 0x40) != 0) {
        c.vs_battle_playMenuLeaveSfx();
        const cur = v.g.D_800EB9B8().*.?;
        cur.unk0.unk0 &= 0x7F;
        cur.unk10.unk0 |= 0x80;
        cur.unk38.vec.pad = 0;
        var i: c_int = 0;
        while (i < v.g.D_800EB9B8().*.?.unk2E) : (i += 1) { // i < a u8 count
            const trigger = psx.at(@as([*]v.func_800C5798_t, @ptrCast(psx.get(v.g.D_800EB9B8().*.?.unk44).?)), i);
            if ((trigger.unk9 >> 4) == 0) {
                _ = c.func_8009FE74(trigger.unk9, @as(i8, @bitCast(trigger.unkA)));
            }
        }
    } else if ((pressed & 0x20) != 0) {
        c.vs_battle_playMenuSelectSfx();
        result = 1;
    }
    const m: *v.MATRIX = @ptrCast(@alignCast(c.func_800C085C(@ptrCast(&v.g.D_800EB9B8().*.?.unk0.unk4.flags), v.g.D_800EB9B8().*.?.unk0.unk2)));
    c.func_800C02A8();
    const unit: c_int = psx.getBits(&flags, "unk1");
    while (true) {
        const cur = v.g.D_800EB9B8().*.?;
        switch (cur.unk0.unk0 & 0x7F) {
            1 => {
                scale = @divTrunc(@as(c_int, cur.unk38.vec.vy) * 2, unit);
                scale = c.SquareRoot12(0x1000 -% scale *% scale);
            },
            2, 3 => scale = 0x1000,
            4 => scale = @divTrunc((unit * 32 + cur.unk38.vec.vy) << 7, unit),
            5 => scale = @divTrunc(-@as(c_int, cur.unk38.vec.vy) << 7, unit),
            else => {},
        }
        point.vx = cur.unk0.unk8.vx;
        point.vy = psx.conv(i16, @as(c_int, cur.unk0.unk8.vy) + cur.unk38.vec.vy);
        point.vz = cur.unk0.unk8.vz;
        if (point.vy < -1) {
            break;
        }
        cur.unk38.vec.vy = psx.conv(i16, -@as(c_int, cur.unk0.unk8.vy) - 2);
    }
    c.func_800C2E24(@ptrCast(point), @ptrCast(m), scale);
    var cur = v.g.D_800EB9B8().*.?;
    stick_x = (@as(c_int, cur.unk38.vec.vx) *% scale +% 0x800) >> 12;
    stick_y = (@as(c_int, cur.unk38.vec.vz) *% scale +% 0x800) >> 12;
    point.vx = psx.conv(i16, @as(c_int, point.vx) +% ((stick_x *% m.m[0][0] +% stick_y *% m.m[2][0] +% 0x800) >> 12));
    point.vz = psx.conv(i16, @as(c_int, point.vz) +% ((stick_x *% m.m[0][2] +% stick_y *% m.m[2][2] +% 0x800) >> 12));
    var value = c._getCollisionMapDimensions(0);
    const width: c_int = (value & 0xFFFF) * 8;
    const depth: c_int = (value >> 16) *% 8;
    if (psx.conv(c_uint, @as(c_int, point.vx)) >= psx.conv(c_uint, width) or psx.conv(c_uint, @as(c_int, point.vz)) >= psx.conv(c_uint, depth)) {
        if (point.vx < 0) {
            point.vx = 0;
        }
        if (point.vx >= width) {
            point.vx = psx.conv(i16, width - 1);
        }
        if (point.vz < 0) {
            point.vz = 0;
        }
        if (point.vz >= depth) {
            point.vz = psx.conv(i16, depth -% 1);
        }
        cur = v.g.D_800EB9B8().*.?;
        stick_x = @as(c_int, point.vx) - cur.unk0.unk8.vx;
        stick_y = @as(c_int, point.vz) - cur.unk0.unk8.vz;
        value = @divTrunc(@as(c_int, m.m[0][0]) * m.m[2][2] - @as(c_int, m.m[0][2]) * m.m[2][0] + 0xFFF, 4096); // i16 products: no overflow
        if (scale != 0 and value != 0) {
            cur.unk38.vec.vx = psx.conv(i16, @divTrunc(@divTrunc(stick_x *% m.m[2][2] -% stick_y *% m.m[2][0], value) << 12, scale));
            cur.unk38.vec.vz = psx.conv(i16, @divTrunc(@divTrunc(-%stick_x *% m.m[0][2] +% stick_y *% m.m[0][0], value) << 12, scale));
        }
        stick_x = (@as(c_int, cur.unk38.vec.vx) *% scale +% 0x800) >> 12;
        stick_y = (@as(c_int, cur.unk38.vec.vz) *% scale +% 0x800) >> 12;
        point.vx = psx.conv(i16, @as(c_int, cur.unk0.unk8.vx) +% ((stick_x *% m.m[0][0] +% stick_y *% m.m[2][0] +% 0x800) >> 12));
        point.vz = psx.conv(i16, @as(c_int, cur.unk0.unk8.vz) +% ((stick_x *% m.m[0][2] +% stick_y *% m.m[2][2] +% 0x800) >> 12));
    }
    c.func_800C28AC(@ptrCast(point), lock_pitch);
    cur = v.g.D_800EB9B8().*.?;
    if ((cur.unk10.unk0 & 0x80) == 0) {
        cur.unk10.unk1 = 0;
        const words: *align(2) const [2]c_int = @ptrCast(point);
        stick_x = words[0];
        stick_y = words[1];
        const target: *align(2) [2]c_int = @ptrCast(&cur.unk10.unk8);
        target[0] = stick_x;
        target[1] = stick_y;
    }
    if (result != 0) {
        c.func_800C4650(@ptrCast(psx.get(cur.unk44)), cur.unk2E);
    }
    return result;
}
