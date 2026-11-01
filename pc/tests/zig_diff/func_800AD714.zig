//! zig_diff spec: func_800AD714 -> func_800AD714.func_800AD714 (44F14.c).
//! Inputs: an animation script of 1-6 commands (any opcode 0-70, biased to
//! the 0-64 the interpreter knows) with random parameters, each followed by
//! its delay byte (0xFF-extended sometimes) and ended by a long delay and a
//! command 0, built from a command length table (D_800E9298) the spec
//! fills; the frame counter (unk540, or unk5BC in modes 2 and 3) around
//! the delays; every mode 0-3 (and 4); the actor's id 0-16 with target
//! actors whose spectator and pan fields vary; weapons for the trail
//! commands owned by the actor or not; the vertex buffer for the nudges.
//! The animation loaders (func_800A0204, func_800AD008) point the state at
//! a fresh one-command script, as the real ones load a new animation.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AD714));

pub const classes = [_][:0]const u8{ "mode0", "mode1", "mode2", "mode3" };

var the_state: *v.D_800F4538_unkC54 = undefined;
var fresh: [*]u8 = undefined;

fn reloadScript(ctx: *zd.Ctx) void {
    psx.put(&the_state.unk544, &fresh[0]);
    the_state.unk542 = ctx.value(u8);
}

pub const stubs = struct {
    pub fn func_800A0204(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        reloadScript(ctx);
        return ctx.value(c_int);
    }
    pub fn func_800AD008(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        reloadScript(ctx);
    }
    pub fn func_800BEB00(ctx: *zd.Ctx) c_short {
        return if (ctx.value(u1) == 0) 4 else ctx.value(c_short);
    }
    pub fn func_8006C84C(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return if (ctx.value(u1) == 0) 0 else ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.D_800F4538_t);
    const st = ctx.alloc(v.D_800F4538_unkC54);
    the_state = st;
    const id: u8 = ctx.range(u8, 0, 16);
    a.unk0.unkF = id;
    a.unk17FD = ctx.pick(u8, &.{ 0, 1, 2, 16 });
    if (a.unk17FD > 16) a.unk17FD = 16;
    a.unk5B1 = ctx.pick(u8, &.{ 0, 1, 11 });
    if (a.unk5B1 > 11) a.unk5B1 = 11;
    a.animationId = ctx.pick(u8, &.{ 0, 1, 99, 100, 101, 102 });
    a.unk1864.unk8 = ctx.pick(u16, &.{ 0x900, 0x100, 0, 0x800 });
    if (ctx.chance(2)) a.unk1864.unk4 = 0;
    if (ctx.chance(2)) a.unk1864.unk5 = 0;
    a.unk0.position.vy = ctx.pick(i16, &.{ 2999, 3000, 0 });
    // The vertex buffer: 512 vertices of 8 bytes.
    const model = ctx.alloc(v.D_800F4538_unk68);
    psx.put(&a.unk0.unk68, model);
    psx.put(&model.vertexOffset, @as(*anyopaque, @ptrCast(ctx.allocBytes(512 * 8, 16))));
    // Two other actors (the arena holds few), shared by the 17 slots.
    const others = [_]*v.D_800F4538_t{ a, ctx.alloc(v.D_800F4538_t), ctx.alloc(v.D_800F4538_t) };
    var k: usize = 0;
    while (k < 17) : (k += 1) {
        const t = others[ctx.range(usize, 0, 2)];
        t.unk17E4.unk0 = if (ctx.chance(3)) 2 else ctx.int(u8);
        t.unk17E4.unk1 = if (ctx.chance(2)) 0 else ctx.int(u8);
        t.unk17E4.unk2 = if (ctx.chance(2)) 0 else ctx.int(u8);
        if (ctx.chance(3)) psx.byteAt(u32, t, 8).* = (psx.byteAt(u32, t, 8).* & ~@as(u32, 0x3F9F00)) | 0x8000;
        v.g.D_800F4538()[k] = t;
    }
    k = 0;
    while (k < 20) : (k += 1) {
        if (ctx.chance(3)) {
            v.g.vs_battle_wepModels()[k] = null;
            continue;
        }
        const w = if (k < 4) ctx.alloc(v.vs_battle_wepModels_t) else v.g.vs_battle_wepModels()[k - 4] orelse ctx.alloc(v.vs_battle_wepModels_t);
        w.nBones = if (ctx.chance(6)) 0 else 1;
        w.unkD = if (ctx.chance(6)) 0 else 1;
        psx.setBits(w, "actorId", if (ctx.chance(2)) @as(u4, @truncate(id)) else ctx.int(u4));
        v.g.vs_battle_wepModels()[k] = w;
    }
    v.g.D_800F1868().* = if (ctx.chance(2)) 1 else ctx.int(i32);

    // The command lengths, 1-6.
    var op: usize = 0;
    while (op < 256) : (op += 1) v.g.D_800E9298()[op] = ctx.range(u8, 1, 6);
    var i: usize = 0;
    while (i < 12) : (i += 1) v.g.D_800E92DC()[i] = ctx.int(u8);

    // The fresh script a loader installs: command 0, then a long delay.
    fresh = ctx.allocBytes(16, 16);
    fresh[0] = 0;
    i = 1;
    while (i < 16) : (i += 1) fresh[i] = 0xFF;

    const mode: c_int = ctx.edge(c_int, &.{ 0, 0, 1, 2, 3, 4 });
    const count = ctx.range(usize, 1, 6);
    const script = ctx.allocBytes(count * 9 + 16, 16);
    var at: usize = 0;
    var n: usize = 0;
    while (n < count) : (n += 1) {
        const code: u8 = if (ctx.chance(10)) ctx.range(u8, 0, 70) else switch (mode) {
            1 => ctx.edge(u8, &.{ 0x2C, 0x19, 0x1D, 0x2E, 0x10, 0x12, 0x33, 0x36, 0x38, 0x39, 0x20, 0x27, 0x0F, 0x13, 0x2F, 0x3A, 0x1C }),
            2 => ctx.edge(u8, &.{ 0x23, 0x24, 0x25, 0x26, 0x27, 0x28 }),
            3 => ctx.edge(u8, &.{ 1, 2, 3, 5 }),
            else => ctx.range(u8, 0, 64),
        };
        script[at] = code;
        var p: usize = 1;
        while (p < 6) : (p += 1) script[at + p] = ctx.int(u8);
        if (code == 4 and ctx.chance(2)) script[at + 1] = 0x58;
        if (code == 54 or code == 60) {
            // A heading target half a turn (and one more) either side of the current one.
            const goal: u16 = a.unk1864.unk8 +% @as(u16, @bitCast(ctx.edge(i16, &.{ 0x800, -0x800, 0x801, -0x801, 0x7FF, 0 })));
            if (code == 54) {
                script[at + 1] = @truncate(goal >> 8);
                script[at + 2] = @truncate(goal);
                if (ctx.chance(2)) script[at + 3] = 0;
            } else {
                const row = script[at + 1];
                const point: [*]u16 = @as([*]u16, @ptrCast(&v.g.D_800E9A28()[0])) + @as(usize, row) * 4;
                point[2] = goal;
            }
        }
        if ((code >= 51 and code <= 53) and ctx.chance(2)) script[at + 3] = 0;
        if (code == 53 and ctx.chance(2)) script[at + 5] = 0;
        at += v.g.D_800E9298()[code];
        if (code == 41) at += 5;
        script[at] = if (ctx.chance(8)) 0xFF else ctx.range(u8, 0, 12);
        script[at + 1] = ctx.range(u8, 0, 3);
        at += if (script[at] == 0xFF) 2 else 1;
    }
    script[at] = 0;
    i = 1;
    while (i < 10) : (i += 1) script[at + i] = 0xFF;
    psx.put(&st.unk544, &script[0]);
    st.unk542 = ctx.range(u16, 0, 12);
    // Below the end marker's 510-frame delay: the C runs off a script's end past it.
    st.unk540 = ctx.pick(u16, &.{ 0, 1, 5, 12, 0xFFFF, 300 });
    if (st.unk540 > 300 and st.unk540 != 0xFFFF) st.unk540 = 12;
    a.unk5BC = ctx.pick(i16, &.{ 0, 1, 5, 12, 300 });
    if (a.unk5BC > 300 or a.unk5BC < 0) a.unk5BC = 12;
    @as([*]u8, @ptrCast(st))[0x549] = ctx.pick(u8, &.{ 0, 1, 2 });
    return .{ a, st, mode };
}

pub fn classify(args: Args) usize {
    return @intCast(@min(args[2], 3));
}
