//! Room packet tint (D2: 2EA3C.s; oracle pc/src/room_geometry.c).
const c = @import("c_room_geometry");
const psx = @import("common").psx;

const NativeTint = *const fn ([*c]const c_uint, c_int, c_int, c_int, c_int) callconv(.c) void;
const TranslucentMarker = *const fn ([*c]const c_uint, [*c]c_uint, [*c]c_int, [*c]c_int) callconv(.c) c_int;

/// orig: tint_color (pc/src/room_geometry.c)
/// Fades one packet colour word toward its luminance with GTE DPCS, adds the
/// per-channel offsets and clamps each channel to 0..255.
fn tintColor(color_in: c_uint, fade: c_int, dr: c_int, dg: c_int, db: c_int) c_uint {
    var color = color_in;
    var r: c_int = @intCast(color & 0xff);
    var g: c_int = @intCast((color >> 8) & 0xff);
    var b: c_int = @intCast((color >> 16) & 0xff);
    if (fade != 0) {
        const luminance: c_uint = @intCast(r * 5 + g * 9 + b * 2); // channels are 0..255
        c.CTC2(luminance, 21);
        c.CTC2(luminance, 22);
        c.CTC2(luminance, 23);
        c.MTC2(color, 6);
        c.MTC2(@bitCast(fade), 8);
        _ = c.doCOP2(0x0780010); // gte_dpcs
        color = c.MFC2(22);
        r = @intCast(color & 0xff);
        g = @intCast((color >> 8) & 0xff);
        b = @intCast((color >> 16) & 0xff);
    }
    r +%= dr;
    g +%= dg;
    b +%= db;
    r = if (r < 0) 0 else if (r > 0xff) 0xff else r;
    g = if (g < 0) 0 else if (g > 0xff) 0xff else g;
    b = if (b < 0) 0 else if (b > 0xff) 0xff else b;
    return (color & 0xff000000) | @as(c_uint, @intCast(r)) | (@as(c_uint, @intCast(g)) << 8) | (@as(c_uint, @intCast(b)) << 16);
}

/// orig: func_80098D6C (pc/src/room_geometry.c)
/// Tints the room packets from `start` to `end` (9-word triangles and
/// 12-word quads; any other packet ends the pass).
pub fn tintRoomPackets(start: [*]c_uint, end: [*]c_uint, params: [*]const i8, flags: c_int) callconv(.c) void {
    const dr: c_int = @as(c_int, params[0]) *% 4;
    const dg: c_int = @as(c_int, params[1]) *% 4;
    const db: c_int = @as(c_int, params[2]) *% 4;
    const fade: c_int = @as(*align(1) const i16, @ptrCast(params + 4)).*;
    const extra: usize = @intCast((flags & 2) * 4);
    if (psx.weak(NativeTint, "pc_room_native_tint")) |tint| tint(start, dr, dg, db, fade);
    var packet: [*]u8 = @ptrCast(start);
    while (@intFromPtr(packet) != @intFromPtr(end)) {
        const words: [*]c_uint = @ptrCast(@alignCast(packet));
        const length: c_uint = words[0] >> 24;
        var count: c_int = undefined;
        var list: c_uint = undefined;
        var first: c_int = undefined;
        var entries: c_int = undefined;
        // The native room's translucent markers sit between the packets (port only).
        if (psx.weak(TranslucentMarker, "render_translucent_marker")) |marker| {
            if (marker(words, &list, &first, &entries) != 0) {
                packet = @ptrFromInt(@intFromPtr(packet) +% 20);
                continue;
            }
        }
        if (length == 9) {
            count = 3;
        } else if (length == 12) {
            count = 4;
        } else return;
        var i: usize = 0;
        while (i < count) : (i += 1) // count is 3 or 4
            words[1 + i * 3] = tintColor(words[1 + i * 3], fade, dr, dg, db);
        packet = @ptrFromInt(@intFromPtr(packet) +% (if (count == 3) @as(usize, 0x28) else 0x34) +% extra);
    }
}
