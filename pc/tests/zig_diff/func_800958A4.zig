//! zig_diff spec: func_800958A4 -> func_800958A4.func_800958A4 (2842C.c).
//! Inputs: x and y (edges, and with bits above 16), the entry's frame unkB
//! (0..5, 40..45: the slides and their bounds), its result unkC 0..4 and
//! praise displayTextLen 0..7. The text argument is compared by its bytes
//! (the C's string literals and the port's are the same text at other
//! addresses). The scratchpad's packet cursor and OT pointers are arena
//! blocks; the text stub moves the cursor, so pc_text_status's two
//! pointers differ.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800958A4));

pub const classes = [_][:0]const u8{ "slide-in", "slide-out", "still", "on-time" };

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "vs_battle_renderTextRawColor");
}

pub const stubs = struct {
    pub fn vs_battle_renderTextRawColor(ctx: *zd.Ctx, text: anytype, xy: anytype, color: anytype, next: anytype) void {
        _ = text;
        _ = xy;
        _ = color;
        _ = next;
        const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
        psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(32, 16))));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(32, 16))));
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ctx.allocBytes(64, 16))));
    const p = ctx.alloc(v.D_800F1BAC_t);
    p.unkB = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5, 40, 41, 42, 43, 44, 45, 20 });
    p.unkC = ctx.range(u16, 0, 4);
    p.displayTextLen = ctx.range(u8, 0, 7);
    const x = ctx.pick(c_int, &.{ 0, 160, 0xFFFF, 0x10000, -1, 64 });
    const y = ctx.pick(c_int, &.{ 0, 120, 0xFFFF, -1, 0x8000 });
    return .{ x, y, p };
}

pub fn classify(args: Args) usize {
    const p = args[2];
    if (p.unkC == 2) return 3;
    if (p.unkB < 4) return 0;
    if (p.unkB >= 42) return 1;
    return 2;
}
