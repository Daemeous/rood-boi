//! zig_diff spec: func_8009DDF4 -> func_8009DDF4.func_8009DDF4 (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 (a null slot is one class: the C
//! then reads the actor through PS1 address 0), an actor with random bytes
//! whose weaponDrawn, unkF, unkA_0, unkA_5, unkA_6 and unk63C are biased so
//! both branches of the activity test occur (unk63C at and around 0x80), and
//! an optional arg1 that the callees only record. The callees are stubs that
//! write unkF and unkA_0 (the C reads them back) and return the edge values
//! -228 and 172 (the two comparisons' equal sides) as well as other values.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009DDF4));

pub const classes = [_][:0]const u8{ "null-slot", "idle", "active-far", "active-near" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx = ctx.range(c_int, 0, 16);
    const table = v.g.D_800F4538();
    const actor = ctx.alloc(v.D_800F4538_t);
    if (ctx.chance(8)) {
        table[@intCast(idx)] = null;
    } else {
        table[@intCast(idx)] = actor;
        const drawn: u1 = if (ctx.chance(6)) 0 else 1;
        psx.setBits(&actor.unk0, "weaponDrawn", drawn);
        if (ctx.chance(2)) {
            actor.unk0.unkF = 0;
        } else {
            actor.unk0.unkF = ctx.pick(u8, &.{ 1, 0x80, 0xFF });
        }
        psx.setBits(&actor.unk0, "unkA_0", ctx.range(u3, 0, 7));
        psx.setBits(&actor.unk0, "unkA_5", ctx.range(u1, 0, 1));
        psx.setBits(&actor.unk0, "unkA_6", ctx.range(u1, 0, 1));
        actor.unk63C = ctx.pick(u16, &.{ 0x7F, 0x80, 0x81, 0, 0xFFFF, 0x8000 });
    }
    const svec: ?*v.SVECTOR = if (ctx.chance(4)) null else ctx.alloc(v.SVECTOR);
    return .{ idx, svec };
}

pub fn classify(args: Args) usize {
    const p = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    const u = &p.unk0;
    const drawn = psx.getBits(u, "weaponDrawn") != 0;
    const unkA_0 = psx.getBits(u, "unkA_0");
    const unkA_5 = psx.getBits(u, "unkA_5");
    const unkA_6 = psx.getBits(u, "unkA_6");
    const active = drawn and (u.unkF == 0 or (unkA_5 == 0 and unkA_6 == 0 and unkA_0 != 2 and unkA_0 != 3));
    if (!active) return 1;
    return if (p.unk63C < 0x80) 3 else 2;
}

fn actorPtr(p: anytype) *v.D_800F4538_t {
    return @ptrCast(@alignCast(p.?));
}

pub const stubs = struct {
    pub fn func_8009E180(ctx: *zd.Ctx, p: anytype, arg1: anytype) c_int {
        _ = arg1;
        const a = actorPtr(p);
        if (ctx.value(u1) == 1) a.unk0.unkF = ctx.value(u8);
        return @as(c_int, ctx.value(i16));
    }

    pub fn func_8009E228(ctx: *zd.Ctx, p: anytype, arg1: anytype) c_int {
        _ = arg1;
        const a = actorPtr(p);
        if (ctx.value(u2) == 0) a.unk0.unkF = ctx.value(u8);
        return switch (ctx.value(u8) % 8) {
            0 => -0xE4,
            1 => 0xAC,
            2 => -0xE5,
            3 => 0xAB,
            4 => 0,
            5 => -1000,
            6 => 1000,
            else => @as(c_int, ctx.value(i16)),
        };
    }

    pub fn func_8009FD38(ctx: *zd.Ctx, p: anytype) void {
        const a = actorPtr(p);
        a.unk0.unkF = if (ctx.value(u1) == 1) 0 else ctx.value(u8);
        if (ctx.value(u1) == 1) psx.setBits(&a.unk0, "unkA_0", ctx.value(u3));
    }
};
