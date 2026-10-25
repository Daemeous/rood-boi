//! zig_diff spec: func_800A0BE0 -> func_800A0BE0.func_800A0BE0 (30DB0.c).
//! Inputs: an actor index 0..16. Its D_800F4538 slot is either null (then the
//! D_800F45E0 slot for the same index, only used in that case, is null or an
//! arena object, and index 16 never takes this path) or an arena actor whose
//! random bytes are kept, with the fields the C reads biased to their edges:
//! animationId at and around 0x47..0x55 (0x55 is the compare equal), unk9_0 at
//! 0/5/6/7 (6 is the compare equal), unkB_0 at 0..4 (3 is the compare equal),
//! the 16-bit fields at 0/0x8000/0xFFFF, unk12 at 0xFF/0x80, and unkF in 0..9 so
//! the wepModels pair it selects (indices 2*unkF and 2*unkF+1) stays inside the
//! 20-entry table. Each wepModels entry is null or an arena object with random
//! bits, so unk8_4 is both set and clear. The 32-bit word at offset 1452 (the
//! `*((int*)&unk5B4 - 2)` test) is drawn from edges that hit the boundary
//! values 0x0FFFFFFF and 0x10000000 exactly, so the 0xF0000000 compare sees
//! both sides equal, along with the sign bit set and clear.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0BE0));

pub const classes = [_][:0]const u8{ "no-actor-no-slot", "no-actor-slot", "actor-wep-empty", "actor-wep-set" };

fn maybeWep(ctx: *zd.Ctx) ?*v.vs_battle_wepModels_t {
    if (ctx.chance(3)) return null;
    return ctx.alloc(v.vs_battle_wepModels_t);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id: c_int = ctx.range(u8, 0, 16);
    const i: usize = @intCast(id);
    const actorNull = id < 16 and ctx.chance(3);
    const sub = v.g.D_800F4538();
    const slots = v.g.D_800F45E0();
    const wep = v.g.vs_battle_wepModels();
    if (actorNull) {
        sub[i] = null;
        slots[i] = if (ctx.chance(2)) null else ctx.alloc(v.D_800F45E0_t);
    } else {
        const actor = ctx.alloc(v.D_800F4538_t);
        sub[i] = actor;
        actor.animationId = ctx.pick(u8, &.{ 0x46, 0x47, 0x54, 0x55, 0x56, 0xFF });
        actor.unk0.unk12 = ctx.pick(u8, &.{ 0xFF, 0x80 });
        actor.unk0.unk1A = ctx.pick(u16, &.{ 0, 0x8000, 0xFFFF });
        actor.unk0.unk18 = ctx.pick(u16, &.{ 0, 0x8000, 0xFFFF });
        actor.unk5C8 = ctx.pick(u16, &.{ 0, 0x8000, 0xFFFF });
        actor.unk0.unkF = ctx.range(u8, 0, 9);
        psx.setBits(&actor.unk0, "unk9_0", ctx.pick(u4, &.{ 0, 5, 6, 7 }));
        psx.setBits(&actor.unk0, "unkB_0", ctx.pick(u4, &.{ 0, 1, 2, 3, 4 }));
        // The word the 0xF0000000 and 0x10000 tests read (offset 1452 = 0x5AC).
        // Edges put it exactly on 0x0FFFFFFF (the compare's equal side) and on
        // the sign-bit and sign-bit-clear boundaries.
        psx.byteAt(u32, actor, 1452).* = ctx.pick(u32, &.{
            0x0FFFFFFF, 0x10000000, 0xF0000000, 0x80000000, 0x70000000, 0, 0xFFFFFFFF,
        });
        const w0: usize = @as(usize, actor.unk0.unkF) * 2;
        wep[w0] = maybeWep(ctx);
        wep[w0 + 1] = maybeWep(ctx);
    }
    return .{id};
}

pub fn classify(args: Args) usize {
    const i: usize = @intCast(args[0]);
    const actor = v.g.D_800F4538()[i] orelse {
        return if (v.g.D_800F45E0()[i] == null) 0 else 1;
    };
    const w0: usize = @as(usize, actor.unk0.unkF) * 2;
    const wep = v.g.vs_battle_wepModels();
    return if (wep[w0] == null and wep[w0 + 1] == null) 2 else 3;
}
