//! zig_diff spec: func_8009E700 -> func_8009E700.func_8009E700 (30DB0.c).
//! Inputs: an actor id 0-15. Either its D_800F4538 entry is null (then the
//! D_800F45E0 entry is an arena object, null or not, with unk1A from an edge
//! list and byte 0x170 zero or not), or it is an arena actor whose animation
//! id comes from an edge list that includes the reaction-range boundaries
//! (0x47, 0x4B, 0x4C, 0x4F, 0x52, 0x53, 0x54 and 0x46), whose unkF is zero
//! most of the time, and whose flags word has its action, state, mode and
//! bits 16-20 drawn from edge lists so the gates are usually taken.
//! func_800A2C48 may rewrite the flags word, unk18 and animationId, as the
//! real callee can, and the C reads them back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E700));

pub const classes = [_][:0]const u8{ "no-actor", "anim-excluded", "gated-out", "action-goto", "movement-step", "walk" };

// 0x4A and 0x52 are the upper ends of the two reaction windows (reaction - 0x47 == 3, - 0x4F == 3): listed
// several times so the equal sides of those unsigned compares occur in over 1% of the runs.
const anim_edges = [_]u8{ 0x47, 0x4A, 0x4A, 0x4A, 0x4B, 0x4C, 0x4F, 0x52, 0x52, 0x52, 0x53, 0x54, 0x46, 0x62, 0x64, 0x65, 0xC6, 0xC8, 0xFF, 0x00 };

fn writeFlags(a: *v.D_800F4538_t, word: u32) void {
    psx.byteAt(u32, a, 8).* = word;
}

fn asActor(p: anytype) *v.D_800F4538_t {
    if (@typeInfo(@TypeOf(p)) == .optional) return @ptrCast(@alignCast(p.?));
    return @ptrCast(@alignCast(p));
}

pub const stubs = struct {
    pub fn func_800A2C48(ctx: *zd.Ctx, actor: anytype) void {
        const a = asActor(actor);
        if (ctx.value(u1) == 1) writeFlags(a, ctx.value(u32));
        if (ctx.value(u1) == 1) a.unk0.unk18 = ctx.value(u16);
        if (ctx.value(u1) == 1) a.animationId = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(u4, 0, 15);
    const actorId: c_int = id;
    const animation = ctx.int(c_int);
    const actors = v.g.D_800F4538();
    const objects = v.g.D_800F45E0();

    if (ctx.chance(5)) {
        actors[id] = null;
        objects[id] = null;
        if (!ctx.chance(3)) {
            const object = ctx.alloc(v.D_800F45E0_t);
            object.unk1A = ctx.pick(i16, &.{ 0xF9, 0xFF, 0xF8, 0, -7 });
            psx.byteAt(u8, object, 0x170).* = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
            objects[id] = object;
        }
        return .{ actorId, animation };
    }

    const actor = ctx.alloc(v.D_800F4538_t);
    actors[id] = actor;
    objects[id] = null;
    actor.animationId = if (ctx.chance(6)) ctx.pick(u8, &anim_edges) else ctx.edge(u8, &anim_edges);
    actor.unk0.unkF = if (ctx.chance(3)) ctx.pick(u8, &.{ 1, 0xFF }) else 0;
    if (ctx.chance(3)) {
        actor.unk0.unk18 = 0;
        actor.unk0.unk1A = 0;
    }
    actor.unk0.facing = ctx.pick(i16, &.{ -512, 0, 511, -1, 0x7FFF, -32768 });

    var f = ctx.int(u32);
    f = (f & ~@as(u32, 0x0F000000)) | (@as(u32, ctx.edge(u4, &.{ 0, 0, 0, 3, 1, 2, 4, 15 })) << 24);
    f = (f & ~@as(u32, 0x70000000)) | (@as(u32, ctx.edge(u3, &.{ 0, 1, 3, 4, 6, 7, 2, 5 })) << 28);
    f = (f & ~@as(u32, 0x00070000)) | (@as(u32, ctx.edge(u3, &.{ 0, 1, 0, 1, 2, 7 })) << 16);
    f = (f & ~@as(u32, 0x00180000)) | (@as(u32, ctx.edge(u2, &.{ 0, 1, 2, 3 })) << 19);
    if (!ctx.chance(4)) f &= ~@as(u32, 0x0F00);
    writeFlags(actor, f);

    return .{ actorId, animation };
}

pub fn classify(args: Args) usize {
    const id: usize = @intCast(args[0]);
    const actor = v.g.D_800F4538()[id] orelse return 0;
    if (actor.animationId == 0xC6 or actor.animationId == 0xC8) return 1;
    const flags = psx.byteAt(u32, actor, 8).*;
    if (actor.unk0.unkF == 0 and (flags & 0x184000) == 0x80000) return 2;
    const state = flags & 0x70000000;
    if (((flags >> 16) & 7) >= 2 or state == 0x20000000 or state == 0x50000000) return 2;
    const action = flags & 0x0F000000;
    if (action != 0 and action != 0x3000000) return 3;
    if ((flags & 0x180000) == 0x100000) return 4;
    return 5;
}
