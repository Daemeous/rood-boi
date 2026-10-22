//! zig_diff spec: func_800C0150 -> func_800C0150.func_800C0150 (573B8.c).
//! Inputs: none. The C takes no arguments and reads no globals, so the only
//! inputs are the stub results. prepare picks a mode per iteration:
//! func_8007CF64 returns NULL on every call, a valid arena object on every
//! call, or a mix drawn per call. The three modes are the input classes.
//! func_800A0204 is a recording stub, so its (i, 1, 0, 0) arguments and the
//! number of calls are compared in the trace.
const std = @import("std");
const zd = @import("zdiff");
const cu = @import("c_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C0150));

pub const classes = [_][:0]const u8{ "all-null", "all-valid", "mixed" };

/// Set by prepare each iteration: 0 all NULL, 1 all valid, 2 mixed.
var mode: u8 = 0;
/// The object the stub hands out, allocated by prepare.
var actor_mem: [*]u8 = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    mode = ctx.range(u8, 0, 2);
    actor_mem = ctx.allocBytes(16, 16);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}

pub const stubs = struct {
    pub fn func_8007CF64(ctx: *zd.Ctx, actorId: anytype) ?*cu.vs_battle_actor {
        _ = actorId;
        const valid = switch (mode) {
            0 => false,
            1 => true,
            else => ctx.value(u1) == 1,
        };
        if (!valid) return null;
        return @ptrCast(@alignCast(actor_mem));
    }
};
