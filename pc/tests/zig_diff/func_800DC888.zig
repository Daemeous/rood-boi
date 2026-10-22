//! zig_diff spec: func_800DC888 -> func_800DC888.func_800DC888 (74088.c).
//! Inputs: a 256-row stats array in the arena, and an application block that
//! the global D_800F5900 points at. The first delta is skipped a quarter of
//! the time, with skip values of both signs and zero. Up to 8 entries follow,
//! each skipped or not, with actors 0 or 255 and random modes and status masks.
//! One extra entry past `count` has skip 0 half the time, so a loop bound that
//! runs one entry too far is visible. vs_main_actions[0 or 255] carry flags
//! whose bits 17-19 pick the cost case: 1, 3, 6, or another value. func_800DC30C
//! is a stub that rewrites hp and maxHP at random.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DC888));

pub const classes = [_][:0]const u8{ "first-skipped", "case-1-mp", "case-3-hp", "case-6-value10", "other-case" };

const entry_count = 16;
const skip_edges = [_]i32{ 1, -1, std.math.minInt(i32), std.math.maxInt(i32) };

fn allocApp(ctx: *zd.Ctx) *v.actionStatApplication {
    const size = @sizeOf(v.actionStatApplication) + (entry_count - 1) * @sizeOf(v.actionStatDelta);
    return @ptrCast(@alignCast(ctx.allocBytes(size, 16)));
}

fn entryAt(app: *v.actionStatApplication, i: usize) *v.actionStatDelta {
    const base: [*]v.actionStatDelta = @ptrCast(&app.entries);
    return &base[i];
}

fn flagBytes(idx: usize) *[4]u8 {
    const base: [*]u8 = @ptrCast(&v.g.actions()[idx]);
    return base[0..4];
}

pub const stubs = struct {
    pub fn func_800DC30C(ctx: *zd.Ctx, hp: anytype, maxHP: anytype, value: anytype, mode: anytype) void {
        _ = value;
        _ = mode;
        const h: *c_short = @ptrCast(hp);
        const m: *c_short = @ptrCast(maxHP);
        if (ctx.value(u1) == 1) h.* = ctx.value(i16);
        if (ctx.value(u1) == 1) m.* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const app = allocApp(ctx);
    const stats = ctx.alloc([256]v.actionStatSnapshot);
    v.g.D_800F5900().* = @ptrCast(app);

    app.action = ctx.pick(u8, &.{ 0, 255 });
    const cases = [_]u32{ 1, 3, 6, 0, 2, 4, 5, 7 };
    const kase = cases[ctx.range(usize, 0, cases.len - 1)];
    const word = (ctx.int(u32) & ~(@as(u32, 7) << 17)) | (kase << 17);
    std.mem.writeInt(u32, flagBytes(app.action), word, .little);

    app.first.actor = ctx.pick(u8, &.{ 0, 255 });
    app.first.skip = if (ctx.chance(4)) ctx.pick(i32, &skip_edges) else 0;

    app.count = ctx.range(u16, 0, 8);
    var i: usize = 0;
    while (i < app.count) : (i += 1) {
        const e = entryAt(app, i);
        e.actor = ctx.pick(u8, &.{ 0, 255 });
        e.skip = if (ctx.chance(3)) 0 else ctx.pick(i32, &skip_edges);
    }
    // The entry just past the last one: applied only if the C reads past count.
    const extra = entryAt(app, app.count);
    extra.skip = if (ctx.chance(2)) 0 else 1;

    return .{&stats[0]};
}

pub fn classify(args: Args) usize {
    _ = args;
    const app: *v.actionStatApplication = @ptrCast(v.g.D_800F5900().*.?);
    if (app.first.skip != 0) return 0;
    const word = std.mem.readInt(u32, flagBytes(app.action), .little);
    return switch ((word >> 17) & 7) {
        1 => 1,
        3 => 2,
        6 => 3,
        else => 4,
    };
}
