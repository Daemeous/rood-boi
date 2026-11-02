//! Battle action record dispatch on the high nibble of byte 1 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B9C58 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the action record at `arg0`: kind 0 walks a list of effects, kinds 1 and 2 apply one effect.
pub fn func_800B9C58(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var sp18: v.func_800BB68C_t = undefined;

    switch (arg0[1] >> 4) {
        0 => {
            c.func_800BB68C(psx.conv(c_ushort, c.vs_battle_getShort(arg0 + 2)), psx.cptr(c.func_800BB68C_t, &sp18));
            while (true) {
                const temp_a2: c_int = @as(c_int, c.func_800BB788(psx.cptr(c.func_800BB68C_t, &sp18)));
                if (temp_a2 == 1) break;
                // (1 << arg0[4]) & 0x1F: the shift count is taken mod 32, as the x86 shift does.
                c.func_8007B29C(
                    @as(c_int, arg0[1] & 0xF),
                    (@as(c_int, 1) << @as(u5, @truncate(arg0[4]))) & 0x1F,
                    temp_a2,
                    psx.conv(c_short, @as(i8, @bitCast(arg0[5]))),
                    psx.conv(c_short, @as(i8, @bitCast(arg0[6]))),
                    psx.conv(c_short, @as(i8, @bitCast(arg0[7]))),
                );
            }
        },
        1 => {
            c.func_8007B1B8(
                @as(c_int, arg0[1] & 0xF),
                (@as(c_int, 1) << @as(u5, @truncate(arg0[4]))) & 0x1F,
                psx.conv(c_short, @as(i8, @bitCast(arg0[5]))),
                psx.conv(c_short, @as(i8, @bitCast(arg0[6]))),
                psx.conv(c_short, @as(i8, @bitCast(arg0[7]))),
            );
        },
        2 => {
            c.func_8007B344(
                @as(c_int, arg0[1] & 0xF),
                (@as(c_int, 1) << @as(u5, @truncate(arg0[4]))) & 0x1F,
                psx.conv(c_short, @as(i8, @bitCast(arg0[5]))),
                psx.conv(c_short, @as(i8, @bitCast(arg0[6]))),
                psx.conv(c_short, @as(i8, @bitCast(arg0[7]))),
            );
        },
        else => {},
    }
    return 0;
}
