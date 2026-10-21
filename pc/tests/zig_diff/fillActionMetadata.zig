//! zig_diff spec: func_800E5710 -> fill_action_metadata.fillActionMetadata (7CD68.c).
//! Inputs: a state in the arena, id zero or not (the early return), and a stats
//! block whose 24 slots name actions 0-255. Every action gets random flags with
//! a random enemy-class nibble at bits 20-23 (so every `mask` bit is hit) and a
//! random action type at bits 17-19 (so `0x20000` and `0x60000` both occur), and
//! random flagsC (bit 29 set or clear). D_800F58BC points at an arena object whose
//! byte 10 is 0, 1, 0x80 or 0xFF. The three callees write `maxActionCost` back
//! (the C reads it after the loop) and return range squared values that hit the
//! 0x51000 edge, plus random radii and rsqrt results.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").fillActionMetadata));

pub const classes = [_][:0]const u8{ "id-zero", "special-on", "special-off" };

/// The state prepare built, so the stubs can reach it through the C's pointer.
var cur_state: ?*v.actionMetadataState = null;

pub const stubs = struct {
    pub fn func_800E5698(ctx: *zd.Ctx, p: anytype, id: anytype) c_int {
        _ = p;
        _ = id;
        const state = cur_state.?;
        if (ctx.value(u8) < 24) state.maxActionCost = ctx.value(u8); // re-read by the C
        if (ctx.value(u8) < 96) return 0x51000; // the C's range test: equal sides
        return ctx.value(c_int);
    }

    pub fn func_800E5600(ctx: *zd.Ctx, a0: anytype, id: anytype, r: anytype) c_int {
        _ = a0;
        _ = id;
        _ = r;
        const state = cur_state.?;
        if (ctx.value(u8) < 32) state.maxActionCost = ctx.value(u8); // re-read by the C
        return ctx.value(c_int);
    }

    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.actionMetadataState);
    const stats = ctx.alloc(v.actionMetadataStats);
    cur_state = state;
    psx.put(&state.stats, stats);
    state.id = if (ctx.chance(6)) 0 else ctx.range(u8, 1, 255);

    for (0..6) |row| for (0..4) |column| {
        stats.slots[row][column].id = ctx.pick(u16, &.{ 0, 1, 255, 256, 0xFFFF }) & 0xFF;
    };

    // Actions 0-255 (the C indexes them with a slot id, always below 256 here).
    const actions = v.g.actions();
    for (actions) |*act| {
        const a: *v.actionMetadataAction = @ptrCast(@alignCast(act));
        a.flags.raw = (ctx.int(u32) & ~@as(u32, 0xE0000)) | (@as(u32, ctx.range(u3, 0, 7)) << 17);
        a.flagsC = ctx.int(u32);
    }

    // The byte the C reads at D_800F58BC + 10: zero or nonzero, with and without the sign bit.
    const bp = ctx.alloc(v.D_800F58BC_t);
    bp.unk4[6] = switch (ctx.range(u8, 0, 3)) {
        0 => 0,
        1 => 1,
        2 => 0x80,
        else => 0xFF,
    };
    v.g.D_800F58BC().* = bp;

    return .{state};
}

pub fn classify(args: Args) usize {
    if (args[0].?.id == 0) return 0;
    return if (v.g.D_800F58BC().*.?.unk4[6] != 0) 1 else 2;
}
