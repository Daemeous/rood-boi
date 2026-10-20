//! Routed integration fixture: native C caller, Zig wrappers, C transaction.
const route = @import("route.zig");
pub const panic = @import("panic.zig").Panic;
extern var shadow_fault: c_int;
extern var shadow_state: c_int;
const routed = struct { extern fn probe_inner(value: c_int) callconv(.c) c_int; };
const Impl = struct {
    pub fn probe_eight(a: c_int, b: c_int, c: c_int, d: c_int, e: c_int, f: c_int, g: c_int, output: *c_int) callconv(.c) c_int {
        shadow_state += 3;
        output.* = a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7;
        if (shadow_fault == 1) output.* += 1;
        if (shadow_fault == 2 or shadow_fault == 6) shadow_state += 1;
        if (shadow_fault == 5 or shadow_fault == 6) output.* = 200;
        if (shadow_fault == 3) @panic("routed owned panic");
        const value = routed.probe_inner(output.*);
        return value + @as(c_int, if (shadow_fault == 4) 1 else 0);
    }
    pub fn probe_inner(value: c_int) callconv(.c) c_int {
        return value + 7;
    }
};
comptime {
    route.exportAll(0, &.{ "probe_eight", "probe_inner" }, Impl);
}
