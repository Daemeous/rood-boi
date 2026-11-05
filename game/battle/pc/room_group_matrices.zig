//! Per-group view matrices of a room (D2: 2EA3C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const psx = @import("common").psx;

fn word(address: usize) *align(1) c_uint {
    return @ptrFromInt(address);
}

/// orig: func_800983F8 (pc/src/boot_sdk.c)
/// Fills each group's view matrix (the group records of 0x40 bytes after the
/// count word, matrix at +0x20) unless the group's flag 0x100 is set: from the
/// group's position and angles, or for a camera-facing plane (flag 0x400) from
/// the camera-space position with only the rotation entries the original
/// writes (the scratchpad's angle at word 0x5C and scale at 0x64).
pub fn roomGroupMatrices(geometry: ?*anyopaque) callconv(.c) void {
    const base: usize = @intFromPtr(geometry.?);
    const scratch: [*]const c_uint = @ptrFromInt(0x1f800000);
    const count = word(base).*;
    var group: usize = base + 4;
    const angle: c_int = @bitCast(scratch[0x5c / 4]);
    const scale: c_int = @bitCast(scratch[0x64 / 4]);
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
            c.func_800985AC(@ptrFromInt(group + 4), @ptrFromInt(group + 0x10), result);
        } else {
            var zero = c.VECTOR{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
            var camera: c.MATRIX = undefined; // filled whole by func_800985AC
            c.func_800985AC(&zero, &zero, &camera);
            c.SetRotMatrix(&camera);
            c.SetTransMatrix(&camera);
            c.MTC2((word(group + 4).* & 0xffff) | (word(group + 8).* << 16), 0);
            c.MTC2(word(group + 12).*, 1);
            _ = c.doCOP2(0x0480012); // gte_mvmva(1, 0, 0, 0, 0)
            result.*.m[0][0] = psx.conv(i16, cosine);
            result.*.m[0][1] = psx.conv(i16, -%sine);
            result.*.m[1][0] = psx.conv(i16, sine);
            result.*.m[1][1] = psx.conv(i16, cosine);
            result.*.m[2][2] = psx.conv(i16, scale);
            result.*.t[0] = @bitCast(c.MFC2(25));
            result.*.t[1] = @bitCast(c.MFC2(26));
            result.*.t[2] = @bitCast(c.MFC2(27));
            c.SetRotMatrix(@ptrFromInt(0x1f800014));
            c.SetTransMatrix(@ptrFromInt(0x1f800014));
        }
    }
}
