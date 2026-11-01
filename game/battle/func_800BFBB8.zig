//! The event script interpreter's step: runs opcodes through the opcode
//! table until one yields (src/BATTLE/BATTLE.PRG/573B8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_573B8");
const c = @import("c_573B8");

const Op = *const fn ([*c]u8, c_short) callconv(.c) c_int;

/// orig: func_800BFBB8 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Runs the event script at `*arg0` (START skips a skippable scene first); 1 when an opcode waits a frame, else 0 with the resume point stored.
pub fn func_800BFBB8(arg0: [*c][*c]u8, arg1: c_short) callconv(.c) c_short {
    var script: [*c]u8 = arg0[0];

    if (arg1 == 0 and v.g.D_800F4C2C().* == 1 and
        ((v.g.vs_main_buttonsPressed().all & (1 << 11)) != 0 or v.g.D_800F4C68().* != 0))
    {
        v.g.D_800F4C68().* = 0;
        v.g.D_800F4C58()[3] = 0;
        v.g.D_800F4C58()[2] = 0;
        v.g.D_800F4C58()[1] = 0;
        v.g.D_800F4C58()[0] = 0;
        v.g.vs_battle_textBoxStatuses()[3] = 0;
        v.g.vs_battle_textBoxStatuses()[2] = 0;
        v.g.vs_battle_textBoxStatuses()[1] = 0;
        v.g.vs_battle_textBoxStatuses()[0] = 0;

        if (v.g.D_800F4C20().* != 0) {
            _ = c.func_80044DF4(0);
        }

        c.func_80045DC0();
        _ = c.func_800434A4(0, 2);
        c.func_800C0150();
        c.func_8007C46C(0, 0xFF, 0);
        c.func_8007C424();
        c.vs_battle_setStateFlag(0xA8, 1);
        v.g.D_800F4C2C().* = 2;
    }

    const table = psx.globalMany(?Op, "_opcodeFunctionTable");
    const scene = psx.global(?[*]?Op, "D_800F4C28");
    while (true) {
        // ROOD_PC: the opcode's callback must be native code (its low 32 bits, as the C casts it).
        const callback: ?Op = if (v.g.D_800F4C2C().* != 2) table[script[0]] else scene.*.?[script[0]];
        c.pc_require_native_callback(@truncate(@intFromPtr(callback)), "event opcode callback relocation", script[0]);

        const ret: c_int = if (v.g.D_800F4C2C().* != 2)
            table[script[0]].?(script, arg1)
        else
            scene.*.?[script[0]].?(script, arg1);

        switch (ret) {
            0 => {
                script = @ptrCast(psx.at(script, c.func_800BFE34(script)));
                continue;
            },
            1 => {},
            3 => {
                arg0[0] = null;
                return 0;
            },
            2 => return 1,
            4 => script = @ptrCast(psx.at(script, c.func_800BFE34(script))),
            else => {
                script = @ptrFromInt(@as(usize, @as(u32, @bitCast(ret))));
                continue;
            },
        }

        arg0[0] = script;
        return 0;
    }
}
