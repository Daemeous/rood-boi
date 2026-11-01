//! zig_diff spec: func_800C28AC -> func_800C28AC.func_800C28AC (58578.c).
//! Inputs: a random SVECTOR position with i16 edges (0, -1, the extremes), arg1
//! 0 or 1 (the table index i + 2*arg1 stays in the 5-entry tables), the frame
//! counter at the 0xF wrap or not, a random scratchpad. The ordering table and
//! the prim buffers are arena blocks; the sprite call links into the table and
//! returns an arena block; the pc hook copies its marker into the arena so the
//! lock field is compared. The `if (pc_classic_site)` false arm is the one
//! waiver: the hook is a weak symbol the harness always links, and no input
//! can make its address null.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_58578");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C28AC));

pub const classes = [_][:0]const u8{ "lock0-wrap", "lock0", "lock1-wrap", "lock1" };

/// Arena marker the pc hook copies its record into (set by prepare each run).
var marker_sink: ?*v.pc_classic_target_marker = null;

pub const stubs = struct {
    /// The callee links the new prim into the ordering table and returns the
    /// prim it made; the C writes prim[4] through the returned pointer.
    pub fn vs_battle_setSpriteDefaultTexPage(
        ctx: *zd.Ctx,
        brightnessTransparency: anytype,
        xy: anytype,
        wh: anytype,
        nextPrim: anytype,
    ) [*c]c_uint {
        _ = brightnessTransparency;
        _ = xy;
        _ = wh;
        const link: [*c]u32 = @ptrCast(nextPrim);
        link[0] = ctx.value(u32);
        return @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    }

    /// The hook reads its record; the copy lands in the arena, which is compared.
    pub fn pc_classic_site(
        ctx: *zd.Ctx,
        kind: anytype,
        record: anytype,
        x: anytype,
        y: anytype,
        first: anytype,
        end: anytype,
    ) void {
        _ = ctx;
        _ = kind;
        _ = x;
        _ = y;
        _ = first;
        _ = end;
        const s = marker_sink orelse return;
        const raw: *anyopaque = if (@typeInfo(@TypeOf(record)) == .optional) record.? else record;
        const r: *const v.pc_classic_target_marker = @ptrCast(@alignCast(raw));
        s.* = r.*;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const position: *v.SVECTOR = ctx.alloc(v.SVECTOR);
    position.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    position.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    position.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });

    const scratch = v.g.vs_scratch();
    // ot = psx_get(unk4) - 4: the table's first word is the block's first word.
    const ot_block = ctx.allocBytes(64, 16);
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ot_block + 4)));
    // Two prims of 10 words each are written through unk0 when arg1 == 0.
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(96, 16))));

    // Keep the C's ++/-- on the GTE counter away from overflow.
    v.g.pc_gte_anchored().* = ctx.range(i32, -8, 8);
    // 15 wraps to 0 on the increment; 255 wraps the same way through the u8 promotion.
    v.g.D_800EA6AC().* = ctx.pick(u8, &.{ 0, 7, 14, 15, 255 });

    marker_sink = ctx.alloc(v.pc_classic_target_marker);

    // The tables hold 5 entries: index = i + 2*arg1 with i in 0..2 needs arg1 in {0, 1}.
    const arg1: c_int = ctx.edge(c_int, &.{ 0, 1 });
    return .{ position, arg1 };
}

pub fn classify(args: Args) usize {
    const wrap = (v.g.D_800EA6AC().* & 0xF) == 15;
    if (args[1] == 0) return if (wrap) 0 else 1;
    return if (wrap) 2 else 3;
}
