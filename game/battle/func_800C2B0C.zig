//! Fills one row of the 9x9 height and collision grid around the character
//! (src/BATTLE/BATTLE.PRG/58578.c).
const psx = @import("common").psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C2B0C (src/BATTLE/BATTLE.PRG/58578.c)
/// Recomputes grid row `row` (0..8) of the height and collision tables from the map around `position`.
pub fn func_800C2B0C(position: [*c]c_ushort, row: c_uint) callconv(.c) void {
    if (row >= 9) {
        return;
    }
    // The original calls libc memset (it is built -fno-builtin): keep the call.
    const memset = @extern(*const fn (?*anyopaque, c_int, usize) callconv(.c) ?*anyopaque, .{ .name = "memset" });
    _ = memset(@ptrCast(&v.g.D_800EB9B8().*.?.unk4AB8[row *% 9]), 0, 9);

    const dims: c_int = c._getCollisionMapDimensions(0);
    const width: c_int = (dims & 0xFFFF) >> 4;
    const height: c_int = dims >> 20;

    var column: c_int = 0;
    while (column < 9) : (column +%= 1) {
        const cell: usize = @as(usize, row *% 9 +% psx.conv(c_uint, column));

        const baseX: c_int = @as(c_int, psx.conv(i16, position[0])) >> 7;
        const tileX: c_int = column -% 4;
        const x: c_int = baseX +% tileX;
        const baseZ: c_int = @as(c_int, psx.conv(i16, position[2])) >> 7;
        const tileZ: c_int = psx.conv(c_int, row -% 4);
        const z: c_int = baseZ +% tileZ;

        var value: c_int = 0x10000;
        if ((psx.conv(c_uint, x) < psx.conv(c_uint, width)) and (psx.conv(c_uint, z) < psx.conv(c_uint, height))) {
            const room: *v._mpdRoomSection3 = @ptrCast(c.func_8008B764(psx.conv(c_uint, x), psx.conv(c_uint, z), 0).?);

            if (((@as(c_int, psx.getBits(room, "unk0_5")) >> 1) & 1) != 0) {
                value = @bitCast(@as(c_uint, 0xF0800000));
                v.g.D_800EB9B8().*.?.unk4AB8[cell] = 0;
            } else if (@as(c_int, psx.getBits(room, "unk0_10")) < 5 or ((@as(c_int, psx.getBits(room, "unk0_18")) >> 1) != 0)) {
                var offset: c_int = 0;
                _ = c.func_800FA188(x, z, &offset);
                if (offset != 0) {
                    const p = psx.at(v.g.D_800F45E0(), c.func_800A91DC(x, z, 0)).*.?;
                    if (@as(c_int, psx.getBits(&p.unk6C[8], "actorId")) < 2) {
                        value = 0;
                        offset +%= 0x20;
                    } else {
                        offset +%= 0x10;
                    }
                }

                const sx: c_int = x << 7;
                const sz: c_int = z << 7;
                var low: c_int = 0x4000;
                var high: c_int = -0x4000;
                var top: c_int = -0x4000;
                var dz: c_int = 0;
                while (dz < 0x80) : (dz +%= 0x20) {
                    var dx: c_int = 0;
                    while (dx < 0x80) : (dx +%= 0x20) {
                        const height2: c_int = c.func_8008DDA8(sx +% dx, sz +% dz);
                        var t: c_int = (height2 << 17) >> 17;
                        t +%= offset;
                        if (t < low) {
                            low = t;
                        }
                        if (high < t) {
                            high = t;
                        }
                        t = (height2 << 1) >> 17;
                        if (top < t) {
                            top = t;
                        }
                    }
                }

                if (value != 0) {
                    value = @as(c_int, @intFromBool(low == high));
                }
                if (psx.conv(c_uint, @as(c_int, psx.getBits(room, "unk0_10")) -% 1) < 4) {
                    low = high;
                    v.g.D_800EB9B8().*.?.unk4AB8[cell] = psx.conv(u8, @as(c_int, psx.getBits(room, "unk0_10")) *% 6);
                }
                value |= ((top +% 0x30) << 17) | ((low & 0x7FFF) << 2);
            }
        }
        v.g.D_800EB9B8().*.?.unk4370[cell] = value;
    }
}
