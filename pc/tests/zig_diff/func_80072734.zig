//! zig_diff spec: func_80072734 -> func_80072734.func_80072734 (146C.c).
//! Inputs: D_800F19CC an arena block (with room after it: the C does not
//! bound the target count) whose attacker is actor 0..3 and whose action
//! is any of the 256; the action kinds' three tables random (each entry
//! zero half the time); actors 0..9 present or not with random allegiance
//! (0x8000 set sometimes); func_800A0BE0's stub returns statuses with the
//! low bit and the 0x3000000 bits in each combination; func_800A152C's
//! stub a part or -1; func_800A1108's stub the attacker's facing;
//! unk2984 0 or not.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80072734));

pub const classes = [_][:0]const u8{ "bodies", "limbs" };

pub const stubs = struct {
    pub fn func_800A0BE0(ctx: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        return ctx.edge(c_uint, &.{ 1, 1, 1, 0, 0x1000001, 0x2000001, 0x3000001, 0xFFFF_FFFF, 0x10001 });
    }
    pub fn func_800A152C(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype) c_int {
        _ = a;
        _ = b;
        _ = cc;
        return ctx.edge(c_int, &.{ -1, 0, 1, 5, -2 });
    }
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const actor: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        actor.unk0.unk4.pad = ctx.value(i16);
        return 0;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const raw = ctx.allocBytes(@sizeOf(v.D_800F19CC_t) + 60 * @sizeOf(v.D_800F19CC_t5), 16);
    const block: *v.D_800F19CC_t = @ptrCast(@alignCast(raw));
    v.g.D_800F19CC().* = block;
    block.unk8.unk4.unk0.targetActor = ctx.range(u8, 0, 3);
    block.unk8.actionIndex = ctx.range(u16, 0, 255);
    block.unk2984 = if (ctx.chance(2)) 0 else 1;
    for (0..16) |k| {
        psx.globalMany(u8, "D_800E81B4")[k] = if (ctx.chance(2)) 0 else ctx.int(u8);
        psx.globalMany(u8, "D_800E81D4")[k] = if (ctx.chance(4)) 0 else ctx.int(u8);
        v.g.D_800E81C4()[k] = if (ctx.chance(2)) 0 else ctx.int(u8);
    }
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    for (0..10) |k| {
        if (k > 3 and ctx.chance(3)) continue;
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.stats.raw = psx.addr(ctx.alloc(v.vs_battle_actor2));
        actor.allegiance = if (ctx.chance(4)) ctx.int(u16) | 0x8000 else ctx.int(u16) & 0x7FFF;
        v.g.vs_battle_actors()[k] = actor;
    }
    return .{ctx.int(c_int)};
}

pub fn classify(_: Args) usize {
    const block = v.g.D_800F19CC().*.?;
    const kind = psx.getBits(&v.g.actions()[block.unk8.actionIndex], "unk2_4");
    return if (v.g.D_800E81C4()[kind] != 0) 1 else 0;
}
