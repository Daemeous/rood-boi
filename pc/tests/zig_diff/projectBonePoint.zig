//! zig_diff spec: func_800A141C -> project_bone_point.projectBonePoint (38C1C.c).
//! Inputs: arg0 selects a table slot (0..15, or 16 where only D_800F4538 holds
//! it), and the slot holds a block (primary), is null with a D_800F45E0 block
//! behind it (fallback), or is null in both tables (returns -1). arg1 covers
//! bone and armature indices -4..63 inside the block the harness owns, and the
//! armature's unk0 takes edge values (zero, negative, odd, 16-bit wrap points,
//! INT_MIN). arg3 is 0, 1, 2, -1 or 3. arg2 points at arena output space.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").projectBonePoint));

pub const classes = [_][:0]const u8{
    "primary-arg3-0",
    "primary-arg3-1",
    "primary-arg3-other",
    "fallback-D_800F45E0",
    "both-null",
};

const armature_edges = [_]i32{ 0, 1, -1, 2, -3, 0x7FFF, -0x8000, 0xFFFF, 0x10000, 0x7FFF_FFFF, -0x7FFF_FFFF - 1 };
const bone_edges = [_]c_int{ -4, -1, 0, 41, 42, 63 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const g4 = v.g.D_800F4538();
    const g5 = v.g.D_800F45E0();

    const roll = ctx.range(u8, 0, 99);
    const mode: u8 = if (roll < 15) 1 else if (roll < 22) 2 else 0;
    var idx: usize = ctx.range(usize, 0, 15);
    if (mode == 0 and ctx.chance(10)) idx = 16;

    const arg1: c_int = if (ctx.chance(2)) bone_edges[ctx.range(usize, 0, bone_edges.len - 1)] else ctx.range(c_int, -4, 63);

    const block: *v.D_800F4538_t = ctx.alloc(v.D_800F4538_t);
    const tables: *v.D_800F4538_unk68 = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));
    psx.put(&block.unk0.unk68, tables);

    if (mode == 0 and ctx.chance(2)) {
        psx.byteAt(i32, tables, 64 + 16 * @as(isize, arg1)).* = ctx.pick(i32, &armature_edges);
    }

    switch (mode) {
        0 => {
            g4[idx] = block;
        },
        1 => {
            g4[idx] = null;
            const alt: *v.D_800F45E0_t = @ptrCast(@alignCast(block));
            g5[idx] = alt;
        },
        else => {
            g4[idx] = null;
            g5[idx] = null;
        },
    }

    const out = ctx.allocBytes(8, 4);
    const arg2: c_int = @bitCast(psx.addr(out));
    const arg3: c_int = switch (ctx.range(u8, 0, 4)) {
        0 => 0,
        1 => 1,
        2 => 2,
        3 => -1,
        else => 3,
    };

    return .{ @as(c_int, @intCast(idx)), arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    const g4 = v.g.D_800F4538();
    const g5 = v.g.D_800F45E0();
    if (g4[idx] == null and g5[idx] == null) return 4;
    if (g4[idx] == null) return 3;
    return switch (args[3]) {
        0 => 0,
        1 => 1,
        else => 2,
    };
}
