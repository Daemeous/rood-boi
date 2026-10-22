//! zig_diff spec: func_800C02A8 -> func_800C02A8.func_800C02A8 (573B8.c).
//! Inputs: none. The C takes no arguments and reads no memory of its own: it
//! passes the scratchpad address 0x1F800014 to SetRotMatrix, then to
//! SetTransMatrix. Both calls are recording stubs that change nothing the C
//! reads back (they load GTE registers, which the C never reads), so the
//! checks are the two calls' arguments, their order and their count, and the
//! state left after them.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C02A8));

pub const classes = [_][:0]const u8{"no-input"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
