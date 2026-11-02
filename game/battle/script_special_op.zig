//! The event script's special operations (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_specialOp (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs special operation script[1] with argument script[2]: the ending (1, over two steps),
/// action unlocks (3), the SCREFF2 border (15), a sub-script (16: returns D_800E9B5C's address
/// and keeps script + 6 in D_800F4C64), and others; returns 1 while waiting, else 0.
pub fn script_specialOp(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var buf: [32]u8 = undefined;
    switch (script[1]) {
        0 => _ = c.func_8007C8F8(script[2]),
        1 => {
            if (v.g.D_800F4BF8().* != 0) {
                if (c.func_8007B9FC() != 0) return 1;
                c.vs_battle_setStateFlag(0xB6, 0);
                v.g.D_800F4BF8().* = 0;
                c.vs_main_showEndingAndReturnToTitle();
            } else {
                c.vs_battle_setStateFlag(0xB6, 3);
                c.func_8007B9A0();
                v.g.D_800F4BF8().* = 1;
                return 1;
            }
        },
        2 => v.g.D_800F4C68().* = 1,
        3 => {
            if (c.vs_battle_isActionUnlocked(script[2]) != 0) {
                c.vs_battle_setStateFlag(0xB1, 0);
            } else {
                c.vs_battle_setActionUnlocked(script[2]);
                c.vs_battle_setStateFlag(0xB1, 1);
            }
        },
        4 => c.vs_battle_setStateFlag(0xB1, psx.conv(u8, c.func_8007CF80(4))),
        5 => c.func_800BAF6C(script[2]),
        6 => c.vs_main_freeHeapR(v.g.D_800F4C24().*),
        7 => c.func_800BBE04(script[2]),
        8 => c.func_8007BD3C(script[2]),
        9 => c.func_800BE3D0(script[2]),
        // script[2] is not bounded: the table's length is not the C's concern.
        10 => c.func_8007CE74(psx.at(v.g.D_800E9C00(), script[2]).*),
        11 => c.func_800BE5A4(script[2]),
        12 => c.func_8008D5A0(script[2]),
        13 => c.func_8008C40C(),
        14 => c.func_8008D594(script[2]),
        15 => c.pc_screff2_set_border(script[2]),
        16 => {
            v.g.D_800F4C64().* = @bitCast(psx.addr(script + 6));
            return @bitCast(psx.addr(psx.global(u8, "D_800E9B5C")));
        },
        17 => c.func_8007C4D4(),
        else => _ = c.sprintf(&buf, @constCast("<SPECIAL=%d> IS UNDEFINED!"), @as(c_int, script[1])),
    }
    return 0;
}
