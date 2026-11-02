//! zig_diff spec: func_800D51D8 -> func_800D51D8.func_800D51D8 (5BF94.c).
//! Inputs: a script; func_800D5170's stub returns opcodes 0..48 (past the
//! table the PC C reports pc_unimplemented, which ends the game, then calls
//! beyond the table: not built). D_800EC3F4's entries for them are a spec
//! command that counts itself in the script and returns 1 (go on) up to the
//! plan's last, which returns 2 (0 from the run), 0 or 3 (1 from the run).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D51D8));

pub const classes = [_][:0]const u8{ "stopped", "ended" };

var gctx: ?*zd.Ctx = null;
var plan_len: u32 = 1;
var last: c_int = 0;
var opcodes: [4]u8 = .{ 0, 0, 0, 0 };

fn command(script: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const k = gctx.?.calls; // this run's opcode reads so far
    script.?.unk8 +%= @truncate(k);
    return if (k >= plan_len) last else 1;
}

pub const stubs = struct {
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        _ = p;
        return opcodes[@min(ctx.calls, 3)];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    gctx = ctx;
    plan_len = ctx.range(u32, 1, 4);
    last = ctx.pick(c_int, &.{ 2, 2, 0, 3 });
    for (&opcodes) |*o| o.* = ctx.pick(u8, &.{ 0, 1, 48 }) % 49;
    const table = v.g.D_800EC3F4();
    for (opcodes) |o| table[o] = @ptrCast(&command);
    return .{ctx.alloc(v.D_800F53B8_t)};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (last == 2) 0 else 1;
}
