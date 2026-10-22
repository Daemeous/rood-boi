//! zig_diff spec: func_800DD000 -> func_800DD000.func_800DD000 (74088.c).
//! Inputs: an actionReactionState (id zero or not, modes, flags, targets) and
//! an actionStatApplication with 0-4 entries (matching or not, skipped or not,
//! hp of 0, 1, -1, 32767, -32768). The action index 0-255 sets both effect words:
//! effect1 bad, effect2 bad, or neither, so the `bad` early return is taken and
//! both halves of its `||` are tested. D_800F5878[source] is a valid arena
//! state (source 0 about two thirds of the time), D_800F5920 is null or a valid
//! object, and other->targets[own] is sometimes cleared so its counter test
//! takes both sides. The callees are recording stubs that return zero and
//! nonzero results and write the state fields the C reads back. The stubs
//! take the state by address and reach it through the views type, so the C
//! state type's opaque bitfield array is never resolved.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DD000));

pub const classes = [_][:0]const u8{ "no-app", "skip", "own-zero", "bad-action", "no-entry", "applied" };

/// The state behind a stub's pointer argument, as views bytes. Only the
/// address is used, so the C-side pointer type is never laid out.
fn stateOf(st: anytype) *v.actionReactionState {
    const addr = @intFromPtr(st);
    return @ptrFromInt(addr);
}

/// The packed target word `idx` of a state (targets is 4 bytes per entry).
fn targetP(s: *v.actionReactionState, idx: usize) *v.actionReactionTarget_p {
    const base: [*]u8 = @ptrCast(&s.targets);
    return @ptrCast(@alignCast(base + 4 * idx));
}

pub const stubs = struct {
    pub fn func_800DC48C(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        if (ctx.value(u8) < 100) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800DC484(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        if (ctx.value(u8) < 100) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800DC210(ctx: *zd.Ctx, st: anytype, x: anytype) void {
        _ = x;
        const s = stateOf(st);
        s.flag13 = ctx.value(u8);
        psx.setBits(targetP(s, ctx.value(u4)), "classification", ctx.value(u2));
    }

    pub fn func_800DC19C(ctx: *zd.Ctx, st: anytype) void {
        const s = stateOf(st);
        s.flag30 = ctx.value(u8);
        s.flag27 = ctx.value(u8);
    }

    pub fn func_800DC784(ctx: *zd.Ctx, st: anytype, src: anytype) void {
        _ = src;
        const s = stateOf(st);
        s.flag20 = ctx.value(u8);
        psx.setBits(targetP(s, ctx.value(u4)), "counter", ctx.value(u7));
    }
};

fn effOff(action: usize) usize {
    return action * @sizeOf(v.vs_action_t);
}

fn effect(action: usize, off: usize) u32 {
    const base: [*]u8 = @ptrCast(v.g.actions());
    return std.mem.readInt(u32, (base + effOff(action) + off)[0..4], .little);
}

fn badAction(action: usize) bool {
    const e1 = effect(action, 20);
    const e2 = effect(action, 24);
    return ((e1 >> 7) & 63) == 3 or ((e2 >> 7) & 63) == 3;
}

fn entriesOf(app: *v.actionStatApplication) [*]v.actionStatDelta {
    return @ptrCast(&app.entries[0]);
}

fn setField(x: u32, field: u32) u32 {
    return (x & ~(@as(u32, 63) << 7)) | (field << 7);
}

fn nonBad(ctx: *zd.Ctx) u32 {
    const f = ctx.range(u32, 0, 63);
    return if (f == 3) 4 else f;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state: *v.actionReactionState = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));
    const other: *v.actionReactionState = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));
    const app: *v.actionStatApplication = @ptrCast(@alignCast(ctx.allocBytes(
        @sizeOf(v.actionStatApplication) + 4 * @sizeOf(v.actionStatDelta),
        16,
    )));

    state.id = if (ctx.chance(12)) 0 else ctx.pick(u8, &.{ 1, 2, 15, 128, 255 });
    state.mode = ctx.pick(u8, &.{ 1, 1, 2, 0, 3 });
    state.flag13 = if (ctx.chance(2)) 0 else ctx.int(u8);
    state.flag20 = if (ctx.chance(2)) 0 else ctx.int(u8);
    state.flag27 = if (ctx.chance(3)) 0 else ctx.int(u8);
    state.flag30 = if (ctx.chance(3)) 0 else ctx.int(u8);

    other.targets[0].b.flags = if (ctx.chance(3)) 192 + @as(u8, ctx.int(u6)) else ctx.int(u8);
    if (ctx.chance(2)) psx.setBits(targetP(other, state.id), "counter", 0);

    const source: u8 = if (ctx.chance(3)) ctx.range(u8, 0, 15) else 0;
    v.g.D_800F5878()[source] = @as(?*v.func_800DEEA4_t2, @ptrCast(other));
    v.g.D_800F5920().* = if (ctx.chance(4)) null else ctx.alloc(v.D_800F5920_t);

    app.first.skip = if (ctx.chance(10)) ctx.pick(i32, &.{ 1, -1, std.math.minInt(i32) }) else 0;
    app.first.actor = source;
    app.action = ctx.range(u16, 0, 255);
    app.count = ctx.range(u16, 0, 4);

    const ents = entriesOf(app);
    for (0..4) |i| {
        const e = &ents[i];
        e.actor = if (ctx.chance(if (i == 0) 2 else 4)) state.id else ctx.int(u8);
        e.skip = if (ctx.chance(5)) ctx.int(i32) else 0;
        e.hp = ctx.pick(i16, &.{ 0, 0, 0, 1, -1 });
        e.modes = ctx.pick(u32, &.{ 0, 1, 2, 3, 4, 0xFFFF_FFFF });
    }

    const kind = ctx.range(u8, 0, 9);
    const base: [*]u8 = @ptrCast(v.g.actions());
    const e1 = setField(ctx.int(u32), if (kind == 0) 3 else nonBad(ctx));
    const e2 = setField(ctx.int(u32), if (kind == 1) 3 else nonBad(ctx));
    std.mem.writeInt(u32, (base + effOff(app.action) + 20)[0..4], e1, .little);
    std.mem.writeInt(u32, (base + effOff(app.action) + 24)[0..4], e2, .little);

    return .{ state, if (ctx.chance(20)) null else app };
}

pub fn classify(args: Args) usize {
    const app = args[1] orelse return 0;
    if (app.first.skip != 0) return 1;
    const state = args[0].?;
    if (state.id == 0) return 2;
    if (badAction(app.action)) return 3;
    const count: usize = app.count;
    const ents = entriesOf(app);
    var i: usize = 0;
    while (i < count) : (i += 1) {
        const e = ents[i];
        if (e.skip == 0 and e.actor == state.id and app.first.actor != state.id) return 4;
    }
    return 5;
}
