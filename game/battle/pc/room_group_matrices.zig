//! Per-group view matrices of a room (D2: 2EA3C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const common = @import("common");
const psx = common.psx;

fn word(address: usize) *align(1) c_uint {
    return @ptrFromInt(address);
}

/// orig: vs_render_updateRoomGroupTransforms (pc/src/boot_sdk.c)
/// Fills each group's view matrix (the group records of 0x40 bytes after the
/// count word, matrix at +0x20) unless the group's flag 0x100 is set: from the
/// group's position and angles, or for a camera-facing plane (flag 0x400) from
/// the camera-space position with only the rotation entries the original
/// writes (the view's camera angle and far-clip scale).
pub fn roomGroupMatrices(geometry: ?*anyopaque) callconv(.c) void {
    const base: usize = @intFromPtr(geometry.?);
    const count = word(base).*;
    var group: usize = base + 4;
    const angle: c_int = common.view.at(c_int, 0x4C).*;
    const scale: c_int = common.view.at(c_int, common.view.far_clip_offset).*;
    const sine: c_int = @as(c_int, @bitCast(@as(c_uint, @bitCast(c.rsin(angle))) *% @as(c_uint, @bitCast(scale)))) >> 12;
    const cosine: c_int = @as(c_int, @bitCast(@as(c_uint, @bitCast(c.rcos(angle))) *% @as(c_uint, @bitCast(scale)))) >> 12;
    if (count > 4096) c.pc_unimplemented("room group count");
    var i: c_uint = 0;
    while (i < count) : ({
        i +%= 1;
        group +%= 0x40;
    }) {
        const flags = word(group).*;
        if (flags & 0x100 != 0) continue;
        const result: [*c]c.MATRIX = @ptrFromInt(group + 0x20);
        if (flags & 0x400 == 0) {
            c.vs_render_buildRoomGeometryViewMatrix(@ptrFromInt(group + 4), @ptrFromInt(group + 0x10), result);
        } else {
            var zero = c.VECTOR{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
            var camera: c.MATRIX = undefined; // filled whole by vs_render_buildRoomGeometryViewMatrix
            c.vs_render_buildRoomGeometryViewMatrix(&zero, &zero, &camera);
            const view = common.gte.viewOf(&camera, common.gte.no_screen);
            const vector = [3]i16{ psx.conv(i16, word(group + 4).*), psx.conv(i16, word(group + 8).*), psx.conv(i16, word(group + 12).*) };
            const moved = common.gte.transform(&view, &vector); // gte_mvmva(1, 0, 0, 0, 0)
            result.*.m[0][0] = psx.conv(i16, cosine);
            result.*.m[0][1] = psx.conv(i16, -%sine);
            result.*.m[1][0] = psx.conv(i16, sine);
            result.*.m[1][1] = psx.conv(i16, cosine);
            result.*.m[2][2] = psx.conv(i16, scale);
            result.*.t[0] = moved.mac[1];
            result.*.t[1] = moved.mac[2];
            result.*.t[2] = moved.mac[3];
        }
    }
}
