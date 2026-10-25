//! zig_diff spec: func_8009E2E0 -> func_8009E2E0.func_8009E2E0 (30DB0.c).
//! Inputs: a D_800F4538 slot 0-16 holding an actor or null, a facing and a
//! target vector chosen so the turn angle lands on the +-2048 edges (and
//! sometimes wraps through a random ratan2 result), arg2 0 (turn and animate)
//! or 1 (animate only), a null target, a target on the actor's spot or
//! sharing one coordinate with it, unk63C around 0x80, and weaponDrawn both
//! ways. The stubs set the sp20 out-parameter to null or a block, write
//! unk18 and unk3E, and return random values where the C does not test them.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E2E0));

pub const classes = [_][:0]const u8{ "no-actor", "rotate-only", "no-target", "same-spot", "turn" };

/// The angle ratan2 returns for this iteration (set by prepare, same in both runs).
var ratan_ret: c_int = 0;

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = ctx;
        _ = y;
        _ = x;
        return ratan_ret;
    }

    pub fn func_800AD494(ctx: *zd.Ctx, actor: anytype, anim: anytype, out: anytype) c_int {
        _ = anim;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(actor.?));
        if (ctx.value(u1) == 1) a.unk0.unk18 = ctx.value(u16);
        if (ctx.value(u2) == 0) {
            out.* = null;
        } else {
            out.* = @ptrCast(@alignCast(ctx.allocBytes(16, 2)));
        }
        return ctx.value(c_int);
    }

    pub fn func_8009FD38(ctx: *zd.Ctx, actor: anytype) void {
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(actor.?));
        if (ctx.value(u1) == 1) a.unk0.unk3E = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const actor = ctx.alloc(v.D_800F4538_t);
    const target = ctx.alloc(v.SVECTOR);

    const idx: c_int = ctx.range(c_int, 0, 16);
    table[@intCast(idx)] = if (ctx.chance(8)) null else actor;

    const fs = [_]i16{ 0, 1, -1, 2047, -2047, 2048, -2048, 2049, -2049, 1000, -1000, 32767, -32768 };
    const f = ctx.edge(i16, &fs);
    actor.unk0.facing = f;

    const deltas = [_]c_int{ 2048, 2048, 2049, -2048, -2048, -2049, 2047, -2047, 0, 1, -1, 4095, -4096 };
    const a = ctx.edge(c_int, &deltas);
    const x: c_int = (a + @as(c_int, f)) & 0xFFF;
    ratan_ret = if (ctx.chance(4)) ctx.int(c_int) else 0xC00 - x;

    actor.unk0.position.vx = ctx.int(i16);
    actor.unk0.position.vz = ctx.int(i16);
    actor.unk63C = ctx.pick(u16, &.{ 0x80, 0x81, 0x7F, 0, 0xFFFF });
    psx.setBits(&actor.unk0, "weaponDrawn", @intFromBool(ctx.chance(2)));

    target.vx = if (ctx.chance(4)) actor.unk0.position.vx else ctx.int(i16);
    target.vz = if (ctx.chance(4)) actor.unk0.position.vz else ctx.int(i16);
    const arg1: ?*v.SVECTOR = if (ctx.chance(6)) null else target;
    const arg2: c_int = ctx.edge(c_int, &.{ 0, 0, 0, 1 });

    return .{ idx, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const i: usize = @intCast(args[0]);
    const actor = table[i] orelse return 0;
    if (args[2] != 0) return 1;
    const t = args[1] orelse return 2;
    if (t.vx == actor.unk0.position.vx and t.vz == actor.unk0.position.vz) return 3;
    return 4;
}
