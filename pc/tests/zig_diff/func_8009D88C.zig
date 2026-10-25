//! zig_diff spec: func_8009D88C -> func_8009D88C.func_8009D88C (30DB0.c).
//! Inputs: an index 0-16 into D_800F4538 with a null or a random actor; the
//! actor's weapon-model slot (unkF 0-9, so the 2-entry pair stays inside the
//! 20-entry vs_battle_wepModels table); each of the two table entries null,
//! or one of three arena models with unkD from {0, 1, 0x7F, 0x80, 0xFF} and
//! a random unk8_4 bit. The two entries may alias the same model. The C makes
//! no calls, so no stubs are needed.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D88C));

pub const classes = [_][:0]const u8{ "no-actor", "no-model", "bit-set", "bit-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.range(c_int, 0, 16);
    const table = v.g.D_800F4538();
    const wep = v.g.vs_battle_wepModels();
    for (table) |*e| e.* = null;
    for (wep) |*e| e.* = null;

    if (!ctx.chance(6)) {
        const actor = ctx.alloc(v.D_800F4538_t);
        table[@intCast(arg0)] = actor;
        actor.unk0.unkF = ctx.range(u8, 0, 9);

        var pool: [3]*v.vs_battle_wepModels_t = undefined;
        for (&pool) |*slot| {
            const m = ctx.alloc(v.vs_battle_wepModels_t);
            m.unkD = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
            psx.setBits(m, "unk8_4", ctx.int(u1));
            slot.* = m;
        }

        const slot: usize = @as(usize, actor.unk0.unkF) * 2;
        for (0..2) |i| {
            var m: ?*v.vs_battle_wepModels_t = null;
            if (!ctx.chance(5)) m = pool[ctx.range(usize, 0, 2)];
            wep[slot + i] = m;
        }
    }
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const actor = table[@intCast(args[0])] orelse return 0;
    const wep = v.g.vs_battle_wepModels();
    const slot: usize = @as(usize, actor.unk0.unkF) * 2;
    for (0..2) |i| {
        const m = wep[slot + i] orelse continue;
        if (m.unkD == 0) continue;
        return if (psx.getBits(m, "unk8_4") != 0) 2 else 3;
    }
    return 1;
}
