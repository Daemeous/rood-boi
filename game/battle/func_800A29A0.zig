//! Steps an actor's state machine: its unkB_0 state and the status word at +8 (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A29A0 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Runs one step of the actor's state machine, dispatching on its unkB_0 state.
pub fn func_800A29A0(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const a = arg0.?;
    const word: *align(1) c_uint = psx.byteAt(c_uint, a, 8);
    switch (@as(c_int, psx.getBits(&a.unk0, "unkB_0"))) {
        1 => {
            word.* &= 0xFFDF_FFFF;
            _ = c.func_800A0204(@as(c_int, a.unk0.unkF), 0x13, 0, 8);
            _ = c.func_800A01C8(@as(c_int, a.unk0.unkF), 0x14, 8, 0);
            psx.byteAt(u8, a, 0x57).* = 0;
            word.* = (((word.* & 0xF0FF_FFFF) | 0x0200_0000) & ~@as(c_uint, 0xF00)) | 0x600;
        },
        2 => {
            if (((word.* >> 8) & 0xF) >= 8) {
                var take: bool = true;
                var v2: c_int = 0;
                if (a.unk0.unk13 == 0xFC) {
                    const d = psx.at(v.g.D_800F4538(), a.unk0.unk12).*.?;
                    v2 = @intFromBool((((psx.byteAt(c_uint, d, 8).* >> 8) & 0xF)) < 9);
                } else if ((psx.byteAt(c_uint, a, 0x119C).* & 0x20000) != 0) {
                    v2 = a.unk5CC;
                } else {
                    take = false;
                }
                if (take and v2 == 0) {
                    word.* = (word.* & ~@as(c_uint, 0xF00)) | 0x900;
                    _ = c.func_800A9C54(a.unk0.unkF, @ptrCast(psx.byteAt(u8, a, 0x5EC)), 0);
                    a.unk0.facing = psx.conv(i16, psx.byteAt(u16, a, 0x5F2).*);
                    _ = c.func_800A0204(@as(c_int, a.unk0.unkF), 0x15, 0, 8);
                    _ = c.func_800A01C8(@as(c_int, a.unk0.unkF), 0x16, 8, 0);
                    word.* = (word.* & 0xF0FF_FFFF) | 0x0300_0000;
                }
            }
        },
        3 => {
            if (((word.* & 0xF00)) != 0) return;
            const zero: bool = if (a.unk0.unk13 == 0xFC) blk: {
                const d = psx.at(v.g.D_800F4538(), a.unk0.unk12).*.?;
                break :blk (psx.byteAt(c_uint, d, 8).* & 0xF00) == 0;
            } else a.unk5CC == 0;
            if (!zero) return;
            _ = c.func_800A0204(@as(c_int, a.unk0.unkF), 1, 0, 8);
            case4(a);
            case5(a, word);
        },
        4 => {
            case4(a);
            case5(a, word);
        },
        5 => case5(a, word),
        else => return,
    }
}

/// Calls the controller hooks for the actor's state 4 (case 4 of the original switch).
fn case4(a: *v.D_800F4538_t) void {
    _ = c.func_800ACF54(@ptrCast(a));
    _ = c.func_800E4C28(@as(c_int, a.unk0.currentTileX), @as(c_int, a.unk0.currentTileZ));
}

/// Case 5 of the original switch: clears the status's state nibble, or sets it to 5 when the target is busy.
fn case5(a: *v.D_800F4538_t, word: *align(1) c_uint) void {
    if (a.unk17FD >= 2) {
        const d = psx.at(v.g.D_800F4538(), a.unk17FD).*.?;
        if ((psx.byteAt(c_uint, d, 8).* & 0x0F00_0000) != 0) {
            word.* = (word.* & 0xF0FF_FFFF) | 0x0500_0000;
            return;
        }
    }
    word.* &= 0xF0FF_FFFF;
}
