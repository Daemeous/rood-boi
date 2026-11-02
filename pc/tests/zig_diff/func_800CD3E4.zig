//! zig_diff spec: func_800CD3E4 -> func_800CD3E4.func_800CD3E4 (5BF94.c).
//! Inputs: text box 0..7 with random geometry and brightness, its shape
//! number 0..3 picking a D_800EB588 entry that is null now and then, else an
//! arena bubble shape built valid: 3..6 outline points, 0..2 tail parts of
//! 1..3 points each (a tail's detour start and end on the outline, the end
//! not before the start: the C loops forever otherwise), 0..2 faces per
//! part (triangles or quads), the box's tail 0 (none), one of the parts or
//! past them. func_800CD3A0 returns any u16 (its bytes at 0x7F / 0x80 now and then); the OT and the packet cursor
//! are arena blocks; the points sit in the scratchpad.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CD3E4));

pub const classes = [_][:0]const u8{ "no-shape", "no-tail", "tail" };

var has_tail = false;

pub const stubs = struct {
    pub fn func_800CD3A0(ctx: *zd.Ctx, vert: anytype, flip: anytype) c_int {
        _ = vert;
        _ = flip;
        // The tail's signed bytes at their edges (0x7F, 0x80) now and then.
        return switch (ctx.value(u8) % 4) {
            0 => 0x7F7F,
            1 => 0x8080,
            else => ctx.value(u16),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F51B8().* = @ptrCast(@alignCast(ctx.allocBytes(40 * 4, 16)));
    psx.put(&v.g.vs_scratch().unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(200 * 4, 16))));
    const index = ctx.range(c_int, 0, 7);
    const box = &v.g.textBoxes()[@intCast(index)];
    const sel = ctx.range(u4, 0, 3);
    psx.setBits(&box.unk0, "unk0_8", sel);
    has_tail = false;
    if (ctx.chance(6)) {
        v.g.D_800EB588()[sel] = null;
        return .{index};
    }
    const base = ctx.range(u8, 3, 6);
    const parts = ctx.range(u8, 0, 2);
    const shape = ctx.allocBytes(128, 16);
    var total: u8 = base;
    var tris: [3]u8 = .{ ctx.range(u8, 0, 2), 0, 0 };
    var k: usize = 1;
    while (k <= parts) : (k += 1) {
        const cnt = ctx.range(u8, 1, 3);
        const start = ctx.range(u8, 0, base - 1);
        shape[4 * k] = start;
        shape[4 * k + 1] = ctx.range(u8, start, base - 1);
        shape[4 * k + 2] = cnt;
        tris[k] = ctx.range(u8, 0, 2);
        shape[4 * k + 3] = tris[k];
        total += cnt;
    }
    shape[0] = total;
    shape[1] = parts;
    shape[2] = base;
    shape[3] = tris[0];
    const verts: [*]align(1) u16 = @ptrCast(shape + (@as(usize, parts) + 1) * 4);
    for (0..total) |i| verts[i] = ctx.int(u16);
    const faces = shape + (@as(usize, parts) + 1) * 4 + @as(usize, total) * 2;
    var f: usize = 0;
    for (0..@as(usize, parts) + 1) |p| {
        for (0..tris[p]) |_| {
            for (0..3) |j| faces[4 * f + j] = ctx.range(u8, 0, total - 1);
            faces[4 * f + 3] = if (ctx.random.boolean()) 0xFF else ctx.range(u8, 0, total - 1);
            f += 1;
        }
    }
    v.g.D_800EB588()[sel] = @ptrCast(shape);
    const tail = ctx.range(u4, 0, @intCast(parts + 1));
    psx.setBits(&box.unk0, "unk0_12", tail);
    has_tail = tail >= 1 and tail <= parts;
    return .{index};
}

pub fn classify(args: Args) usize {
    const box = &v.g.textBoxes()[@intCast(args[0])];
    if (v.g.D_800EB588()[psx.getBits(&box.unk0, "unk0_8")] == null) return 0;
    return if (has_tail) 2 else 1;
}
