//! zig_diff spec: func_800E6158 -> func_800E6158.func_800E6158 (7CD68.c).
//! The C body is one call to func_800E6F9C and nothing else: no arguments,
//! no branches, no globals read here. The inputs are the random state the
//! harness fills; the callee is a recording stub that writes nothing, so the
//! port is checked on the call itself (its presence, its count, its order
//! relative to the state it runs against) and on the whole-state comparison
//! after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_7CD68");
const c = @import("c_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E6158));

pub const classes = [_][:0]const u8{"call"};

pub const stubs = struct {
    // func_800E6F9C takes no arguments and its effects are not modelled in
    // this unit: it returns nothing and changes nothing the stub can see.
    // Any store it makes is still compared through the state snapshot.
    pub fn func_800E6F9C(ctx: *zd.Ctx) void {
        _ = ctx;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
