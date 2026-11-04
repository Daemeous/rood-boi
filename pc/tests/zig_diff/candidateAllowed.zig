//! zig_diff spec: func_800DBCB4 -> pc.candidate_allowed.candidateAllowed (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose halfword 0x90 is 0 or any nonzero value
//! (including one whose low byte is 0) and whose timer byte 0x13F is 0 or
//! set; a candidate record. The stub of func_800DBC60 answers 0 or 1 from the
//! spec and records (state, candidate).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").candidateAllowed));

pub const classes = [_][:0]const u8{ "blocked-by-0x90", "not-needed", "timer-running", "timer-idle" };

var needs: c_int = 0;

pub const stubs = struct {
    pub fn func_800DBC60(ctx: *zd.Ctx, state: anytype, candidate: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = candidate;
        return needs;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x200, 16);
    for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
    std.mem.writeInt(u16, state[0x90..][0..2], ctx.edge(u16, &.{ 0, 0, 0, 1, 0x100, 0x8000, 0xFFFF }), .little);
    state[0x13f] = ctx.edge(u8, &.{ 0, 0, 1, 2, 0x80, 0xFF });
    const candidate = ctx.allocBytes(0x20, 16);
    for (candidate[0..0x20]) |*byte| byte.* = ctx.int(u8);
    needs = ctx.edge(c_int, &.{ 0, 1, 2, -1, 0x100 });
    return .{ state, candidate };
}

pub fn classify(args: Args) usize {
    if (std.mem.readInt(u16, args[0][0x90..][0..2], .little) != 0) return 0;
    if (needs == 0) return 1;
    return if (args[0][0x13f] != 0) 2 else 3;
}
