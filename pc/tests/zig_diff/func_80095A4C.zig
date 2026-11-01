//! zig_diff spec: func_80095A4C -> func_80095A4C.func_80095A4C (2842C.c).
//! Inputs: x and y (edges, and with bits above 16), the entry's frame unkB
//! (0..5, 86..90: the slides and their bounds) and count unkC (1, 2, 0,
//! 65535, random). The scratchpad's packet cursor and OT pointers are arena
//! blocks. vs_battle_renderTextRawColor gets the text in a stack buffer
//! (the trace tags it): its stub copies the text into an arena record and
//! moves the packet cursor, so pc_text_status's two pointers differ.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80095A4C));

pub const classes = [_][:0]const u8{ "slide-in", "slide-out", "still-one", "still-many" };

var text_record: [*]u8 = undefined;

pub const stubs = struct {
    pub fn vs_battle_renderTextRawColor(ctx: *zd.Ctx, text: anytype, xy: anytype, color: anytype, next: anytype) void {
        _ = xy;
        _ = color;
        _ = next;
        const s: [*]const u8 = @ptrCast(text);
        // Up to the terminator: the buffer's later bytes are uninitialised.
        for (0..16) |i| {
            text_record[i] = s[i];
            if (s[i] == 0) break;
        }
        const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
        psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(32, 16))));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    text_record = ctx.allocBytes(16, 16);
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(32, 16))));
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ctx.allocBytes(64, 16))));
    const p = ctx.alloc(v.D_800F1BAC_t);
    p.unkB = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5, 86, 87, 88, 89, 90, 40 });
    p.unkC = ctx.pick(u16, &.{ 1, 1, 2, 0, 65535, 10 });
    const x = ctx.pick(c_int, &.{ 0, 160, 0xFFFF, 0x10000, -1, 64 });
    const y = ctx.pick(c_int, &.{ 0, 120, 0xFFFF, -1, 0x8000 });
    return .{ x, y, p };
}

pub fn classify(args: Args) usize {
    const p = args[2];
    if (p.unkB < 4 and p.unkC == 1) return 0;
    if (p.unkB >= 87) return 1;
    return if (p.unkC == 1) 2 else 3;
}
