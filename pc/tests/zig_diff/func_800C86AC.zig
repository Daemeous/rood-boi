//! zig_diff spec: func_800C86AC -> func_800C86AC.func_800C86AC (5BF94.c).
//! Inputs: the global gim_t pointer D_800EB9BC is null, or points at four
//! arena gim_t records. The head record's unk0_6 is 3 most of the time, and
//! each record's unk0_3 is 3 about half the time, with random unk0_9. The
//! stub for func_8007E0A8 may rewrite the head's unk0_3 and unk2, which the
//! loop reads back. The stub for vs_main_freeHeapR may scribble the freed
//! record's first byte, and it accepts a null argument, which a mutated C
//! may pass.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C86AC));

pub const classes = [_][:0]const u8{ "null-head", "head-not-gim", "gim-no-call", "gim-with-call" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slot = v.g.D_800EB9BC();
    if (ctx.chance(6)) {
        slot.* = null;
        return .{};
    }
    const recs: [*]v.gim_t = @ptrCast(@alignCast(ctx.allocBytes(4 * @sizeOf(v.gim_t), 16)));
    for (0..4) |i| {
        const r = &recs[i];
        psx.setBits(r, "unk0_3", if (ctx.chance(2)) 3 else ctx.int(u3));
        psx.setBits(r, "unk0_9", ctx.int(u3));
    }
    psx.setBits(&recs[0], "unk0_6", if (ctx.chance(4)) ctx.int(u3) else 3);
    slot.* = &recs[0];
    return .{};
}

pub fn classify(_: Args) usize {
    const head = v.g.D_800EB9BC().* orelse return 0;
    if (psx.getBits(head, "unk0_6") != 3) return 1;
    const recs: [*]v.gim_t = @ptrCast(head);
    for (0..3) |i| {
        if (psx.getBits(&recs[i], "unk0_3") == 3) return 3;
    }
    return 2;
}

pub const stubs = struct {
    pub fn func_8007E0A8(ctx: *zd.Ctx, a: anytype, b: anytype, n: anytype) c_int {
        _ = a;
        _ = b;
        _ = n;
        if (v.g.D_800EB9BC().*) |head| {
            if (ctx.value(u1) == 1) psx.setBits(head, "unk0_3", ctx.value(u3));
            if (ctx.value(u1) == 1) head.unk2 = ctx.value(u8);
        }
        return ctx.value(c_int);
    }

    pub fn vs_main_freeHeapR(ctx: *zd.Ctx, p: anytype) void {
        const raw: ?*anyopaque = p;
        if (ctx.value(u1) == 1) {
            if (raw) |r| {
                const b: *u8 = @ptrCast(@alignCast(r));
                b.* = ctx.value(u8);
            }
        }
    }
};
