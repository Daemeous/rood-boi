//! zig_diff spec: func_800DEC88 -> func_800DEC88.func_800DEC88 (74088.c).
//! Inputs: 0-4 battle actors in a list (ids 0-17, flags with and without bit 5).
//! Every actor has a stats block, since the C dereferences it without a guard,
//! and hp sits at the edges around 0. The 18-entry state table is random bytes
//! whose ids mostly match their slot. The context is null one time in five,
//! otherwise it has f44 zero or not and an id matching the head's state or not.
//! The heap allocation fails one time in eight. The callees write back what the
//! C reads after them: the context id and f44 (func_800DEB10), the state id
//! (func_800DD604), the state's f13 and, rarely, the heap global itself
//! (func_800DE3E4), and the head actor's stats hp (func_800DC810).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DEC88));

pub const classes = [_][:0]const u8{ "no-actors", "null-context", "context-f44-set", "context-id-match", "context-id-differs" };

fn stateOf(id: usize) *v.actorEvaluationState {
    return @ptrCast(@alignCast(v.g.D_800F5878()[id].?));
}

fn makeActor(ctx: *zd.Ctx, next: ?*v.actorEvaluationActor, id: i32) *v.actorEvaluationActor {
    const a = ctx.alloc(v.actorEvaluationActor);
    a.id = id;
    a.flags = ctx.pick(u8, &.{ 0, 0x20, 0x40, 0x80, 0xA0, 0xFF });
    psx.put(&a.next, next);
    const s = ctx.alloc(v.actorEvaluationStats);
    s.hp = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    psx.put(&a.stats, s);
    return a;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F58BC().* = ctx.alloc(v.D_800F58BC_t);

    // Every table slot gets a state block, so any actor id 0-17 is valid.
    const table = v.g.D_800F5878();
    for (0..18) |i| {
        const st: *v.actorEvaluationState = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));
        st.id = if (ctx.chance(8)) ctx.int(u8) else @intCast(i);
        table[i] = @ptrCast(@alignCast(st));
    }

    // The actor list: empty (1 in 6), or 1-4 actors with ids 0-17.
    var head: ?*v.actorEvaluationActor = null;
    if (!ctx.chance(6)) {
        const k = ctx.range(u8, 1, 4);
        var i: usize = 0;
        while (i < k) : (i += 1) head = makeActor(ctx, head, ctx.range(i32, 0, 17));
    }
    v.g.vs_battle_actors()[0] = if (head) |h| @ptrCast(@alignCast(h)) else null;

    // The context: null one time in five.
    var arg: ?*anyopaque = null;
    if (!ctx.chance(5)) {
        const c = ctx.alloc(v.actorEvaluationContext);
        c.f44 = ctx.pick(i32, &.{ 0, 0, 0, 1, -1, std.math.minInt(i32), std.math.maxInt(i32) });
        c.id = ctx.int(u8);
        if (head) |h| if (ctx.chance(2)) {
            c.id = stateOf(@intCast(h.id)).id;
        };
        arg = c;
    }
    return .{arg};
}

pub fn classify(args: Args) usize {
    const head = v.g.vs_battle_actors()[0] orelse return 0;
    const cp = args[0] orelse return 1;
    const c: *v.actorEvaluationContext = @ptrCast(@alignCast(cp));
    if (c.f44 != 0) return 2;
    return if (c.id == stateOf(@intCast(head.id)).id) 3 else 4;
}

pub const stubs = struct {
    // The heap block: null one time in eight, else a fresh arena block.
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        if (ctx.value(u8) < 32) return null;
        return @ptrCast(ctx.allocBytes(@as(usize, size), 16));
    }

    pub fn vs_main_freeHeapR(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }

    // Changes the first actor's stats hp, which the C reads back in its hp test.
    pub fn func_800DC810(ctx: *zd.Ctx, context: anytype) void {
        _ = context;
        if (ctx.value(u1) == 0) return;
        const raw = v.g.vs_battle_actors()[0] orelse return;
        const head: *v.actorEvaluationActor = @ptrCast(@alignCast(raw));
        const s = psx.get(head.stats) orelse return;
        s.hp = ctx.value(i16);
    }

    // Changes the context's id and f44, which the C reads after the call.
    pub fn func_800DEB10(ctx: *zd.Ctx, p: anytype) void {
        if (p == null) return;
        if (ctx.value(u1) == 1) psx.byteAt(u8, p, 4).* = ctx.value(u8);
        if (ctx.value(u1) == 1) psx.byteAt(i32, p, 68).* = ctx.value(i32);
    }

    // Changes the state's id (the C compares it after the call).
    pub fn func_800DD604(ctx: *zd.Ctx, state: anytype) void {
        if (ctx.value(u1) == 1) {
            const p = state.?;
            psx.byteAt(u8, p, 136).* = ctx.value(u8);
        }
    }

    // Writes the state's f13 (zero and sign-bit cases both occur), and rarely
    // clears the heap global, which the C tests again after the loop.
    pub fn func_800DE3E4(ctx: *zd.Ctx, state: anytype) void {
        if (ctx.value(u1) == 1) psx.byteAt(u8, state, 19).* = ctx.value(u8) & ctx.value(u8);
        if (ctx.value(u4) == 0) v.g.D_800F5900().* = null;
    }
};
