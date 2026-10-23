//! Per-tick update of one battle actor: its movement or action, its position
//! and its current tile (src/BATTLE/BATTLE.PRG/3A1A0.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

// Called by this unit but not declared in c_3A1A0: the same symbols, declared here.
extern fn func_800A4A88(?*anyopaque, c_int) callconv(.c) void;
extern fn func_800A2CD4(?*anyopaque) callconv(.c) void;

/// The actor's status word: the u32 at offset 8 (skip, visible, the unk9_0 and unkA_0 fields).
fn statusWord(actor: *v.D_800F4538_t) c_uint {
    return psx.byteAt(c_uint, actor, 8).*;
}

/// orig: func_800A4E68 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Runs one actor's movement or action for the tick, then its position and current tile.
pub fn func_800A4E68(arg0: c_int) callconv(.c) void {
    const s: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*.?;
    var movement: v.SVECTOR = std.mem.zeroes(v.SVECTOR);

    const flags0 = statusWord(s);
    if (((flags0 & 0xF00) != 0) and (((flags0 >> 8) & 0xF) < 6)) return;

    if (arg0 == 0) {
        func_800A4A88(s, 0);
        const usable = ((statusWord(s) & 0x70000) == 0) or (s.unk181A == 0);
        // goto block_14 when not usable: the actor's call-out runs without the flag test.
        var callAC690 = !usable;
        if (usable) {
            var tail = false;
            switch (s.unk1848.unk0) {
                0, 7 => {
                    // movement.vx is not set on this path: uninitialised in the original.
                    movement.pad = 0;
                    tail = true;
                },
                1 => {
                    movement.pad = 1;
                    movement.vx = s.unk1848.unk6;
                    tail = true;
                },
                2 => c.func_800A25EC(@ptrCast(s)),
                else => {},
            }
            if (tail) {
                _ = c.func_800A5280(
                    0,
                    psx.conv(c_int, psx.conv(i16, s.unk1848.unk4)),
                    psx.conv(c_int, s.unk1848.unk8),
                    psx.cptr(c.SVECTOR, &movement),
                );
                if ((statusWord(s) & 0x70000) != 0) callAC690 = true;
            }
        }
        if (callAC690) c.func_800AC690(0, @ptrCast(s));
        func_800A4A88(s, 1);
        func_800A4A88(s, 2);
    } else {
        func_800A4A88(s, 0);
        if (s.unk1848.unk0 == 2) {
            c.func_800A25EC(@ptrCast(s));
        } else {
            const a0 = statusWord(s);
            // goto block_34 when the flags are set: the call-out runs directly.
            var callAC690 = (a0 & 0x70000) != 0;
            if (!callAC690) {
                if ((a0 & 0x0F000000) != 0) {
                    c.func_800A29A0(@ptrCast(s));
                } else if ((a0 & 0x70000000) != 0) {
                    func_800A2CD4(s);
                } else {
                    switch (s.unk1848.unk0) {
                        1 => {
                            if ((a0 & 0x8000_0000) == 0 and (a0 & 0x200000) == 0) {
                                movement.pad = 1;
                                movement.vx = s.unk1848.unk6;
                                _ = c.func_800A5280(
                                    arg0,
                                    psx.conv(c_int, psx.conv(i16, s.unk1848.unk4)),
                                    psx.conv(c_int, s.unk1848.unk8),
                                    psx.cptr(c.SVECTOR, &movement),
                                );
                                if ((statusWord(s) & 0x70000) != 0) callAC690 = true;
                            }
                        },
                        3 => c.func_800A2790(@ptrCast(s)),
                        4 => c.func_800A291C(@ptrCast(s)),
                        else => {},
                    }
                }
            }
            if (callAC690) c.func_800AC690(arg0, @ptrCast(s));
        }

        const tile6E4 = s.unk6E4;
        if (tile6E4 != 0) {
            if (@as(c_int, psx.getBits(&s.unk0, "unkC_0")) == @as(c_int, tile6E4)) {
                psx.setBits(&s.unk0, "unkC_0", 0);
            }
            if (c.func_800A51A0(s.unk6E4, psx.cptr(c.SVECTOR, &s.unk0.position)) == 0) {
                s.unk6E4 = 0;
                c.func_800E6B24(s.unk0.unkF);
            }
            if (s.unk181A == 1) {
                s.unk1814 = s.unk0.position.vx;
                s.unk1818 = s.unk0.position.vz;
            }
        }

        func_800A4A88(s, 1);
        func_800A4A88(s, 2);

        var tx: c_int = s.unk0.position.vx;
        if (tx < 0) tx +%= 0x7F;
        var tz: c_int = s.unk0.position.vz;
        s.unk0.currentTileX = psx.conv(u8, tx >> 7);
        if (tz < 0) tz +%= 0x7F;
        s.unk0.currentTileZ = psx.conv(u8, tz >> 7);
    }

    c.func_800A525C(@ptrCast(s));
}
