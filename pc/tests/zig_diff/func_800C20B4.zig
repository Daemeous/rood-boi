//! zig_diff spec: func_800C20B4 -> func_800C20B4.func_800C20B4 (58578.c).
//! Inputs: one D_800EB9B8_t in the arena, pointed to by the global, with its
//! 24 row entries. Each run picks a mode (no step entries, case 2 only, case 3
//! only, both), and each entry's type and temp come from edge values (160,
//! 0xA0, the 0x1BE window, -126, the i16 extremes) or from the step table
//! itself, so the ordered comparisons see equal sides. Two rows are forced
//! to non-matching nonzero kinds (1, 4, 255, 5) so the switch's no-match
//! exit is taken in every run. The step table holds ascending u8 values, so
//! the row search breaks early, runs to its end (16), or hits index 0. The
//! callee stub rewrites the entry's unk0 and unk4 at random.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C20B4));

pub const classes = [_][:0]const u8{ "no-step", "case-2", "case-3", "case-2-and-3" };

pub const stubs = struct {
    pub fn func_800C1DC4(ctx: *zd.Ctx, p: anytype) void {
        const e: *v.D_800EB9B8_unk990 = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) e.unk0 = ctx.value(u8);
        if (ctx.value(u1) == 1) e.unk4 = ctx.value(i16);
    }
};

fn tempFor(ctx: *zd.Ctx, steps: [*]u8, kind: u8) i16 {
    if (ctx.chance(3)) {
        const k = ctx.range(usize, 0, 15);
        switch (kind) {
            2 => return @as(i16, steps[k]) + 248,
            3 => return -@as(i16, steps[k]) - 4,
            else => {},
        }
    }
    return ctx.pick(i16, &.{ 160, 159, 0xA0, 248, 288, 320, 0x1BE, -126, 319, 32767, -32768, 0, -1, 352, 0xF8, -4, 3, 0x20 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = obj;

    const steps = v.g.vs_battle_rowAnimationSteps();
    var s: u8 = ctx.range(u8, 0, 40);
    for (0..20) |i| {
        steps[i] = s;
        s +|= ctx.range(u8, 0, 20);
    }

    const mode = ctx.range(u8, 0, 3);
    for (&obj.unk990) |*e| {
        const menu: [5]u8 = switch (mode) {
            0 => .{ 0, 1, 4, 255, 4 },
            1 => .{ 0, 1, 2, 2, 0 },
            2 => .{ 0, 1, 3, 3, 0 },
            else => .{ 0, 2, 3, 1, 255 },
        };
        const kind = menu[ctx.range(usize, 0, 4)];
        e.unk0 = kind;
        e.unk4 = tempFor(ctx, steps, kind);
    }

    // Guaranteed no-match kinds: the switch falls out without a case.
    obj.unk990[22].unk0 = 1;
    obj.unk990[22].unk4 = tempFor(ctx, steps, 1);
    obj.unk990[23].unk0 = ctx.pick(u8, &.{ 4, 5, 255 });
    obj.unk990[23].unk4 = tempFor(ctx, steps, 0);

    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const obj = v.g.D_800EB9B8().*.?;
    var has2 = false;
    var has3 = false;
    for (obj.unk990) |e| {
        if (e.unk0 == 2) has2 = true;
        if (e.unk0 == 3) has3 = true;
    }
    if (has2 and has3) return 3;
    if (has3) return 2;
    if (has2) return 1;
    return 0;
}
