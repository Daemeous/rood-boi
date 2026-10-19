//! The panic handler of every owned object: a report through the port's
//! crash path (ownership.c pc_zig_panic), not the std's I/O.
const std = @import("std");

extern fn pc_zig_panic(msg: [*]const u8, len: usize, return_address: usize) callconv(.c) noreturn;

fn fail(msg: []const u8, first_trace_address: ?usize) noreturn {
    pc_zig_panic(msg.ptr, msg.len, first_trace_address orelse @returnAddress());
}

pub const Panic = std.debug.FullPanic(fail);
