//! Palette fade for a battle actor's or object's colour table: steps the fade
//! value, blends the colours and loads the CLUT (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AB788 (src/BATTLE/BATTLE.PRG/40564.c)
/// Steps an actor's (or object's) palette fade, blends its colours and loads the CLUT.
pub fn func_800AB788(actor: [*c]u8, object: [*c]u8, _: c_int) callconv(.c) void {
    // With no actor the fade and colours belong to the object; otherwise to the actor.
    const base: [*c]u8 = if (actor == null) object else actor;

    var count: c_int = 16;
    if (actor != null) {
        count = 160;
        if ((psx.byteAt(c_uint, actor, 0x5AC).* & 0x800_0000) == 0) {
            count = 16;
        }
    }

    // state = base + 0x64: state[1] is the fade value, state[2] bit 1 the fade direction.
    const s1 = psx.byteAt(u8, base, 0x65);
    const s2 = psx.byteAt(u8, base, 0x66);
    var value: c_int = @as(c_int, s1.*);

    if (v.g.vs_gametime_tickspeed().* == 4) {
        if ((s2.* & 2) == 0) {
            value +%= 2;
            if (value >= 16) {
                s2.* |= 2;
            }
        } else {
            value -%= 2;
            if (value <= 0) {
                s2.* &= 0xFD;
            }
        }
    }
    if ((s2.* & 2) == 0) {
        value +%= 2;
        if (value >= 16) {
            s2.* |= 2;
        }
    } else {
        value -%= 2;
        if (value <= 0) {
            s2.* &= 0xFD;
        }
    }
    s1.* = psx.conv(u8, value);

    const src_off: isize = if (actor == null) 0x12C else 0x1424;
    const dst_off: isize = if (actor == null) 0x14C else 0x16A4;

    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        var color: c_int = @as(c_int, psx.byteAt(u16, base, @as(isize, src_off) + 2 * @as(isize, i)).*);
        if (color != 0) {
            var r: c_int = color & 31;
            var g: c_int = (color >> 5) & 31;
            var b: c_int = (color >> 10) & 31;
            color &= 0x8000;
            if (value == 16) {
                r +%= 8;
                if (r > 31) {
                    r = 31;
                }
                g +%= 18;
                if (g > 31) {
                    g = 31;
                }
                b +%= 30;
            } else {
                r -%= @divTrunc(value, 2);
                if (r < 0) {
                    r = 0;
                }
                g +%= @divTrunc(value, 2);
                if (g > 31) {
                    g = 31;
                }
                b +%= value;
            }
            if (b > 31) {
                b = 31;
            }
            psx.byteAt(u16, base, @as(isize, dst_off) + 2 * @as(isize, i)).* =
                psx.conv(u16, color | (b << 10) | (g << 5) | r);
        }
    }

    // The CLUT source is the colour table just written (object or actor).
    const img: [*c]const u16 = @ptrFromInt(@as(usize, psx.addr(base) +% @as(u32, @intCast(dst_off))));
    if (actor == null) {
        c.vs_main_loadClut(img, 31, psx.conv(c_uint, @as(c_int, psx.byteAt(u8, object, 15).*) *% 16), psx.conv(c_uint, count));
    } else {
        c.vs_main_loadClut(img, psx.conv(c_uint, @as(c_int, psx.byteAt(u8, actor, 15).*) +% 22), 0, psx.conv(c_uint, count));
    }
}
