//! Advances a battle actor's state by its mode field (bits 28..30 of the flags
//! word at offset 8), with the shared tails of the C's gotos
//! (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A2CD4 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Runs one step of the actor's mode machine: its state change, sound and tile bookkeeping.
pub fn func_800A2CD4(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const a = arg0.?;
    const mode: c_int = @as(c_int, psx.getBits(&a.unk0, "unkB_4"));

    switch (mode) {
        1 => {
            _ = c.func_800A0204(a.unk0.unkF, 9, 0, 8);
            _ = c.func_800A9C54(a.unk0.unkF, @ptrCast(&a.unk5EC), -1);
            _ = c.func_800A9EB4(a.unk0.unkF, a.unk5EC.pad, 8);
            a.unk5CA = a.unk0.unk1A;
            a.unk0.unk1A = 0;
            if (a.unk5CA != 0) {
                psx.setBits(&a.unk0, "unkB_4", 2);
                return;
            }
            block10(a);
        },
        2 => {
            _ = c.func_800A2FBC(@ptrCast(a));
            _ = c.func_800A9378(@as(c_int, a.unk0.position.vx), @as(c_int, a.unk0.position.vy), @as(c_int, a.unk0.position.vz), 2);
            if (psx.global(c_int, "D_800F49F4").* != 0) {
                psx.setBits(&a.unk0, "unkA_5", 1);
                block17(a);
                return;
            }
            if (a.unk5CA == 0) {
                _ = c.func_800A0204(a.unk0.unkF, 0xA, 0, 8);
                psx.setBits(&a.unk0, "unkA_5", 0);
                psx.setBits(&a.unk0, "unkB_4", 3);
                return;
            }
        },
        3 => {
            // The word at 0x119C has no view field: read it at its offset.
            if (a.unk0.unk1A == 0 and (psx.byteAt(c_uint, a, 0x119C).* & 0x20000) != 0) {
                block10(a);
            }
        },
        4 => {
            _ = c.func_800A0204(a.unk0.unkF, 7, 0, 8);
            _ = c.func_800A01C8(a.unk0.unkF, 0x25, 8, 0);
            _ = c.func_800AA620(a.unk0.unkF, @ptrCast(&a.unk5EC), -1);
            _ = c.func_800A9EB4(a.unk0.unkF, a.unk5EC.pad, 8);
            const temp: u16 = a.unk0.unk1A;
            a.unk0.unk1A = 0;
            a.unk5CA = temp;
            psx.setBits(&a.unk0, "unkA_5", 1);
            if (a.unk5CA == 0) {
                a.unk6E0 = 0;
                block17(a);
                return;
            }
            psx.setBits(&a.unk0, "unkB_4", 5);
        },
        5 => {
            _ = c.func_800A2FBC(@ptrCast(a));
            _ = c.func_800A9378(@as(c_int, a.unk0.position.vx), @as(c_int, a.unk0.position.vy), @as(c_int, a.unk0.position.vz), 3);
            if (psx.global(c_int, "D_800F49F4").* == 0) {
                if (a.unk5CA == 0) {
                    block17(a);
                }
            } else {
                block17(a);
            }
        },
        6 => {
            const anim: u8 = a.animationId;
            if (anim != 0xC7 and anim != 0xC9 and a.unk5CC == 0) {
                block21(a);
            }
        },
        else => {},
    }
}

/// orig: block_10 (src/BATTLE/BATTLE.PRG/3A1A0.c, label inside func_800A2CD4)
/// Common tail of modes 1 and 3: clears the flag, runs the tile calls and sets mode 0.
fn block10(a: *v.D_800F4538_t) void {
    psx.setBits(&a.unk0, "unkA_5", 0);
    c.func_800ACF54(@ptrCast(a));
    c.func_800E4C28(@as(c_int, a.unk0.currentTileX), @as(c_int, a.unk0.currentTileZ));
    _ = c.func_800A0204(a.unk0.unkF, 1, 0, 8);
    psx.setBits(&a.unk0, "unkB_4", 0);
}

/// orig: block_16/block_17 (src/BATTLE/BATTLE.PRG/3A1A0.c, labels inside func_800A2CD4)
/// Common tail of modes 2, 4 and 5: a sound call and mode 0.
fn block17(a: *v.D_800F4538_t) void {
    _ = c.func_800A0204(a.unk0.unkF, 1, 0, 8);
    psx.setBits(&a.unk0, "unkB_4", 0);
}

/// orig: block_21 (src/BATTLE/BATTLE.PRG/3A1A0.c, label inside func_800A2CD4)
/// Clears the mode field of the flags word.
fn block21(a: *v.D_800F4538_t) void {
    psx.setBits(&a.unk0, "unkB_4", 0);
}
