//! Draws the striped scanline primitives for the battle screen and sets the draw tpage
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BB874 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Advances the stripe phase, adds a flat line primitive per shaded row to the ordering table, then sets the draw tpage.
pub fn func_800BB874() callconv(.c) void {
    // Scratchpad slots hold 4-byte guest addresses: slot 0 is the primitive cursor, slot 2 the ordering table.
    const scratch: *[3]u32 = @ptrFromInt(0x1F800000);
    var line: *v.LINE_F2 = @ptrFromInt(@as(usize, scratch[0]));

    v.g.D_800F4B20().* +%= 1;

    var i: i16 = 0;
    while (@as(c_int, i) < @as(c_int, v.g.D_8005DFDA().*)) : (i +%= 1) {
        const temp: c_uint = psx.conv(c_uint, ((@as(c_int, i) +% (@as(c_int, v.g.D_800F4B20().*) >> 1)) & 1) << 5);
        const shade: u8 = psx.conv(u8, temp);
        if (temp != 0) {
            c.SetLineF2(psx.cptr(c.LINE_F2, line));
            line.r0 = shade;
            line.g0 = shade;
            line.b0 = shade;
            line.code |= 0x02;
            line.x0 = 0;
            line.y0 = i;
            line.y1 = i;
            line.x1 = v.g.D_8005DFD6().*;

            c.AddPrim(@as(?*anyopaque, @ptrFromInt(@as(usize, scratch[2]))), @ptrCast(line));
            line = @ptrFromInt(@intFromPtr(line) +% @sizeOf(v.LINE_F2));
        }
    }

    c.SetDrawTPage(@ptrCast(line), 1, 1, @as(c_int, c.GetTPage(0, 2, 0, 0)));

    c.AddPrim(@as(?*anyopaque, @ptrFromInt(@as(usize, scratch[2]))), @ptrCast(line));
    scratch[0] = psx.addr(&line.x0);
}
