//! Palette fade for a battle actor's or object's colour table: steps the fade
//! value, blends the colours and loads the CLUT (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_updatePalettePulse (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Steps an actor's (or object's) palette fade, blends its colours and loads the CLUT.
pub fn vs_actors_updatePalettePulse(actor_in: ?*v.D_800F4538_t, object: [*c]u8, _: c_int) callconv(.c) void {
    const actor: [*c]u8 = @ptrCast(actor_in);
    // With no actor the fade and colours belong to the object; otherwise to the actor.
    const base: [*c]u8 = if (actor == null) object else actor;

    var count: c_int = 16;
    if (actor != null) {
        count = 160;
        if ((common.object.flags5AC(actor_in.?).* & 0x800_0000) == 0) {
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

    // The actor's colour tables are in unk1424 (the second 0x280 bytes in); the object's are at 0x12C and 0x14C.
    const src: [*]u8 = if (actor_in) |a| @ptrCast(&a.unk1424) else @as([*]u8, @ptrCast(base)) + 0x12C;
    const dst: [*]u8 = if (actor_in) |a| @as([*]u8, @ptrCast(&a.unk1424)) + 0x280 else @as([*]u8, @ptrCast(base)) + 0x14C;

    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        var color: c_int = @as(c_int, psx.fieldAt(u16, src, 2 * @as(isize, i)).*);
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
            psx.fieldAt(u16, dst, 2 * @as(isize, i)).* =
                psx.conv(u16, color | (b << 10) | (g << 5) | r);
        }
    }

    // The CLUT source is the colour table just written (object or actor).
    const img: [*c]const u16 = @ptrCast(@alignCast(dst));
    if (actor == null) {
        c.vs_main_loadClut(img, 31, psx.conv(c_uint, @as(c_int, psx.byteAt(u8, object, 15).*) *% 16), psx.conv(c_uint, count));
    } else {
        c.vs_main_loadClut(img, psx.conv(c_uint, @as(c_int, psx.byteAt(u8, actor, 15).*) +% 22), 0, psx.conv(c_uint, count));
    }
}
