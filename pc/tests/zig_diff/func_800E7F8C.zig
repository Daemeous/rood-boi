//! zig_diff spec: func_800E7F8C -> func_800E7F8C.func_800E7F8C (7EE98.c).
//! Inputs: a non-null func_800E78F4_t in the arena whose unk54 (a func_800DEB10_t2)
//! and unk5C (a vs_battle_actor2) are valid guest pointers, the unk3C actor
//! sometimes shared. The D_800F5920 block has its counter at 0, 1, 2, -1, -2 and
//! the i16 edges (so the decrement, the transition and the signed test all run),
//! unkE zero or not, unk16 and unkA zero or not. limbs[4].hp is 2 often (the
//! `>= 2` equal edge), 1, 3, 0, negative and the i16 edges. D_800F4538[0] has a
//! position.vy and menuCameraHeightOffset at the i16/u16 edges, so the camera
//! subtraction wraps. The state flag comes from the stub: 0 and 4 (the two
//! switch cases) most often, 1 and 6 as well, and 0x84 (a negative char) for the
//! no-match path. Callees write what the C reads back (unk470, unk4E) and
//! return zero and nonzero values.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7EE98");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E7F8C));

pub const classes = [_][:0]const u8{ "counter-zero", "counter-negative", "counter-positive" };

pub const stubs = struct {
    pub fn func_800E78F4(ctx: *zd.Ctx, arg: anytype) c_int {
        const a: *v.func_800E78F4_t = @ptrCast(@alignCast(arg));
        if (ctx.value(u1) == 1) a.unk470 = ctx.value(i32);
        if (ctx.value(u8) < 128) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800D836C(ctx: *zd.Ctx, arg: anytype) void {
        const a: *v.func_800E78F4_t = @ptrCast(@alignCast(arg));
        if (ctx.value(u1) == 1) a.unk4E = ctx.value(u16);
    }

    pub fn func_800D82CC(ctx: *zd.Ctx, arg: anytype) void {
        const a: *v.func_800E78F4_t = @ptrCast(@alignCast(arg));
        if (ctx.value(u1) == 1) a.unk4E = ctx.value(u16);
    }

    pub fn func_800E7960(ctx: *zd.Ctx, arg: anytype) void {
        const a: *v.func_800E78F4_t = @ptrCast(@alignCast(arg));
        if (ctx.value(u1) == 1) a.unk470 = ctx.value(i32);
    }

    pub fn func_800E7698(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        return switch (ctx.value(u8) % 8) {
            0, 1, 2 => 0,
            3, 4 => 4,
            5 => 1,
            6 => 6,
            else => 0x84,
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.func_800E78F4_t);
    const t2 = ctx.alloc(v.func_800DEB10_t2);
    const actor = ctx.alloc(v.vs_battle_actor2);
    const other = ctx.alloc(v.vs_battle_actor2);
    const pos = ctx.alloc(v.D_800F4538_t);
    const st = ctx.alloc(v.D_800F5920_t);

    arg.unk4E = ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0x8000 });
    arg.unk470 = ctx.pick(i32, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000 });
    psx.put(&arg.unk54, t2);
    psx.put(&arg.unk5C, actor);
    psx.put(&t2.unk3C, if (ctx.chance(4)) other else actor);
    actor.limbs[4].hp = ctx.pick(i16, &.{ 2, 2, 1, 0, -1, 3, 32767, -32768 });
    other.limbs[4].hp = ctx.pick(i16, &.{ 2, 2, 1, 0, -1, 3, 32767, -32768 });

    pos.unk0.position.vx = ctx.int(i16);
    pos.unk0.position.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    pos.menuCameraHeightOffset = ctx.pick(u16, &.{ 0, 1, 2, 0xFFFF, 0x8000 });
    v.g.D_800F4538().*[0] = pos;

    st.unkA = ctx.pick(u8, &.{ 0, 1, 255 });
    st.unkB = ctx.int(u8);
    st.unkC = ctx.pick(i16, &.{ 0, 0, 1, 2, -1, -2, 900, 32767, -32768 });
    st.unkE = ctx.pick(i16, &.{ 0, 0, 1, -1, 32767 });
    st.unk16 = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    st.unk17 = ctx.int(u8);
    v.g.D_800F5920().* = st;

    return .{arg};
}

pub fn classify(args: Args) usize {
    _ = args;
    const st = v.g.D_800F5920().*.?;
    if (st.unkC == 0) return 0;
    if (st.unkC < 0) return 1;
    return 2;
}
