//! Unpacks two fields of arg0[1] into the table at D_800F4B28 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800B9724 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Writes two bytes of the table at D_800F4B28, selected by the top bits of arg0[1].
pub fn func_800B9724(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const table: [*]u16 = v.g.D_800F4B28();
    const b: c_int = arg0[1];

    // The C adds the index to a u_short*, so it advances in 2-byte steps.
    const temp: [*]u8 = @ptrCast(table + @as(usize, @intCast(b >> 6)));
    temp[0] = psx.conv(u8, (b >> 4) & 3);

    var var_v1: c_int = b & 7;
    if ((b & 8) != 0) {
        var_v1 |= 0xF8;
    }
    temp[1] = psx.conv(u8, var_v1);
    return 0;
}
