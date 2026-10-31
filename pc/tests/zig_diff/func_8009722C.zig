//! zig_diff spec: func_8009722C -> func_8009722C.func_8009722C (2842C.c).
//! The C takes no arguments, reads no object and no global, and has no branch.
//! Its one effect is the store of 1 to the i16 global D_800F227A. Each run
//! starts with a random value there (the harness randomises every global the
//! unit reaches), so a dropped or misvalued store shows as a state mismatch.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009722C));

pub const classes = [_][:0]const u8{"always"};

pub fn prepare(ctx: *zd.Ctx) Args {
    // The global is random bytes already; nothing to bias. Touch the accessor
    // so the global is part of the spec's view of the unit.
    const flag = v.g.D_800F227A();
    _ = flag;
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
