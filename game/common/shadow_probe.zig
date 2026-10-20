//! Real ReleaseSafe panic exercised by the shadow transaction CTest.
pub const panic = @import("panic.zig").Panic;

export fn shadow_probe_overflow(context: *anyopaque) callconv(.c) void {
    const output: *i32 = @ptrCast(@alignCast(context));
    output.* += 1;
}
