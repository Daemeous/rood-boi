//! zig_diff spec: func_8006CE50 -> room_timer.startSection12Timer (146C.c).
//! No inputs: one call of func_8008D5A0 with 180 (a recording stub).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startSection12Timer));

pub fn prepare(_: *zd.Ctx) Args {
    return .{};
}
