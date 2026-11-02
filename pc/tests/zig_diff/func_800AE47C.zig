//! zig_diff spec: func_800AE47C -> func_800AE47C.func_800AE47C (44F14.c).
//! Inputs: a fresh actor (animationId, unk0.unkF random). func_800AD494's
//! stub returns the address of a word in the arena that holds the address of
//! a sequence block, or 0, and points the header out-parameter at three
//! shorts (header[2] the op's offset, 0..60) or null. The op's first byte is
//! 0xFF (the escape) a third of the time. The block-scope latch `reported`
//! (globalised: views func_800AE47C__reported) is 0 half the time, else 1,
//! -1 or random: the log's sprintf runs as libc's and pc_log's line is
//! compared by its text (textArgument).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE47C));

pub const classes = [_][:0]const u8{ "no-word", "no-header", "escape", "plain" };

var word: u32 = 0;
var header: ?[*]u16 = null;

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_log");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const sequence = ctx.allocBytes(64, 4);
    const cell = ctx.alloc(u32);
    const shorts = ctx.alloc([3]u16);
    cell.* = @intCast(@intFromPtr(sequence));
    shorts[2] = ctx.range(u16, 0, 60);
    const op = sequence + shorts[2];
    op[0] = if (ctx.chance(3)) 0xFF else ctx.pick(u8, &.{ 0, 1, 0xFE });
    op[1] = ctx.pick(u8, &.{ 0, 1, 0xFF });

    word = if (ctx.chance(4)) 0 else @intCast(@intFromPtr(cell));
    header = if (ctx.chance(4)) null else @ptrCast(shorts);
    v.g.func_800AE47C__reported().* = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1, std.math.maxInt(c_int) });
    return .{actor};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (word == 0) return 0;
    if (header == null) return 1;
    const sequence: [*]const u8 = @ptrFromInt(@as(usize, @as(*const u32, @ptrFromInt(word)).*));
    return if (sequence[header.?[2]] == 0xFF) 2 else 3;
}

pub const stubs = struct {
    pub fn func_800AD494(ctx: *zd.Ctx, actor: anytype, id: anytype, out: anytype) c_int {
        _ = ctx;
        _ = actor;
        _ = id;
        out.* = @ptrCast(header);
        return @bitCast(word);
    }
};
