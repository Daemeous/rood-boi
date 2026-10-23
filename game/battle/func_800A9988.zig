//! Points actor `slot` at a target and starts its move, or turns it in place
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: scanFloor (src/BATTLE/BATTLE.PRG/40564.c)
/// Finds the lowest live actor standing on tile (arg0, arg1) and returns its height offset, at most 0.
fn scanFloor(arg0: c_int, arg1: c_int) c_int {
    var var_t0: c_int = 0xBB8;
    v.g.D_800F49F8().* = 0;

    const slots = v.g.D_800F45E0();
    var i: c_int = 0;
    while (i < 16) : (i += 1) {
        const temp_v1 = slots[@intCast(i)] orelse continue;

        if (psx.getBits(temp_v1, "unk8_0") == 0 and
            @as(c_int, temp_v1.unk5C) == arg0 and
            @as(c_int, temp_v1.unk5E) == arg1 and
            psx.getBits(temp_v1, "unk9_0") == 0 and
            @as(c_int, temp_v1.unk1A) != 0xFE and
            @as(c_int, temp_v1.unk1E) < var_t0)
        {
            var_t0 = @as(c_int, temp_v1.unk1E);
            v.g.D_800F49F8().* = psx.conv(u8, i);
        }
    }

    var_t0 -%= 128;

    if (var_t0 > 0) {
        var_t0 = 0;
    }

    return var_t0;
}

/// orig: func_800A9988 (src/BATTLE/BATTLE.PRG/40564.c)
/// Turns actor `slot` toward a target point, or starts its move to it with the given frames.
pub fn func_800A9988(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int, arg3: c_int) callconv(.c) void {
    // ROOD_PC: empty actor slot (func_800A97EC).
    const temp_s0 = psx.at(v.g.D_800F4538(), arg0).* orelse return;

    if (arg2 == 0 and arg3 == 0) {
        const s = arg1.?;
        const temp_a2: u16 = psx.conv(u16, s.pad);
        if (temp_a2 == 0x8000) {
            var angle: c_int = @as(c_int, s.vx) -% @as(c_int, temp_s0.unk0.position.vx);
            const a0_2: c_int = @as(c_int, s.vz) -% @as(c_int, temp_s0.unk0.position.vz);
            if (angle == 0 and a0_2 == 0) {
                angle = @as(c_int, psx.conv(i16, @as(c_int, temp_a2)));
            } else {
                angle = 0xC00 -% c.ratan2(a0_2, angle);
                angle &= 0xFFF;
            }
            temp_s0.unk0.facing = psx.conv(i16, angle);
        } else {
            temp_s0.unk0.facing = psx.conv(i16, @as(c_int, temp_a2));
        }
        temp_s0.unk0.unk18 = 0;
        c.func_800A9D90(arg0, psx.cptr(c.SVECTOR, s), 0);
        return;
    }

    const s = arg1.?;
    var angle: c_int = @as(c_int, s.vx) -% @as(c_int, temp_s0.unk0.position.vx);
    var a0_2: c_int = @as(c_int, s.vz) -% @as(c_int, temp_s0.unk0.position.vz);
    if (angle == 0 and a0_2 == 0) {
        const temp_v1: u16 = psx.conv(u16, s.pad);
        if (temp_v1 == 0x8000) {
            return;
        }
        angle = @as(c_int, psx.conv(i16, @as(c_int, temp_v1)));
    } else {
        angle = 0xC00 -% c.ratan2(a0_2, angle);
        angle &= 0xFFF;
    }

    // block_15
    const angle16: i16 = psx.conv(i16, angle);
    c.func_800A9EB4(arg0, angle16, arg3);
    if (arg2 == -1) {
        temp_s0.unk5C4 = temp_s0.unk5C2;
    } else {
        temp_s0.unk5C4 = psx.conv(u16, arg2);
    }
    if (arg3 == -1) {
        temp_s0.unk5C6 = temp_s0.unk5C0;
    } else {
        temp_s0.unk5C6 = psx.conv(u16, arg3);
    }
    temp_s0.unk5EC.vx = psx.conv(i16, psx.conv(u16, s.vx));
    temp_s0.unk5EC.vz = psx.conv(i16, psx.conv(u16, s.vz));

    angle = @as(c_int, psx.conv(u16, s.vy));
    if (angle == 0x8000) {
        angle = @as(c_int, c.func_8008DA24(@as(c_int, s.vx), @as(c_int, s.vz)));
        // angle <<= 17; angle >>= 17; the left shift wraps as GNU C does.
        angle = @bitCast(@as(c_uint, @bitCast(angle)) << 17);
        angle >>= 17;
        const floorX: c_int = @divTrunc(@as(c_int, s.vx), 128);
        a0_2 = scanFloor(floorX, floorX);
        if (a0_2 != 0) {
            angle = a0_2;
        }
        temp_s0.unk5EC.vy = psx.conv(i16, angle);
    } else {
        temp_s0.unk5EC.vy = psx.conv(i16, angle);
    }

    temp_s0.unk5EC.pad = psx.conv(i16, psx.conv(u16, s.pad));
    psx.setBits(temp_s0, "unk5AC_0", 1);
    psx.setBits(temp_s0, "unk5AC_2", 0);
}
