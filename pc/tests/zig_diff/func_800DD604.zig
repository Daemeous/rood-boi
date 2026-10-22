//! zig_diff spec: func_800DD604 -> func_800DD604.func_800DD604 (74088.c).
//! Inputs: an actionChoiceState in the arena whose stats pointer targets an arena
//! vs_battle_actor2 with random armor ids. Ids below action_limit (the entries of
//! vs_main_actions the C may index without leaving the table) are candidates;
//! the others, including -1 and 256, are not, and the candidate stub returns 0 for
//! them, so the C never reads past the table. Classes: every cell non-candidate
//! (the best == 0 path), candidates with no usable slot, and candidates with a usable
//! slot. Config values are drawn from a small set plus the sign-bit edges, so
//! `best == config->value` ties happen often and `best` takes values with bit 31 set.
//! The candidate stub also rewrites usable and value sometimes, as a real callee may.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DD604));

/// Ids below this are candidates: (id + 1) * sizeof(actionChoiceInfo) must stay inside vs_main_actions.
const action_limit: u16 = @intCast(@min(256, (256 * @sizeOf(v.vs_action_t) - 16) / @sizeOf(v.actionChoiceInfo)));
const hi: u8 = @intCast(action_limit - 1);

pub const classes = [_][:0]const u8{ "no-allowed-action", "no-usable-slot", "candidates" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.actionChoiceState);
    const stats = ctx.alloc(v.vs_battle_actor2);
    psx.put(&state.stats, stats);
    const bad = ctx.chance(8);
    const none_usable = ctx.chance(4);
    for (0..6) |r| {
        for (0..4) |k| {
            const allowed = !bad and !ctx.chance(6);
            const id: i16 = if (allowed)
                @as(i16, @intCast(ctx.pick(u8, &.{ 0, 1, 128, hi })))
            else
                @bitCast(ctx.pick(u16, &.{ 256, 0x7FFF, 0x8000, 0xFFFF }));
            stats.armor[r][k].id = id;
            const cfg = &state.configs[r][k];
            cfg.value = ctx.pick(u32, &.{ 0, 1, 2, 3, 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF });
            psx.setBits(cfg, "usable", @intFromBool(!none_usable and !ctx.chance(4)));
        }
    }
    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    const stats = psx.get(state.stats).?;
    var allowed = false;
    var usable = false;
    for (0..6) |r| {
        for (0..4) |k| {
            const id: u16 = @bitCast(stats.armor[r][k].id);
            if (id >= action_limit) continue;
            allowed = true;
            if (psx.getBits(&state.configs[r][k], "usable") != 0) usable = true;
        }
    }
    if (!allowed) return 0;
    if (!usable) return 1;
    return 2;
}

pub const stubs = struct {
    pub fn func_800E4DF8(ctx: *zd.Ctx, p: anytype, row: anytype, col: anytype) c_int {
        const s: *v.actionChoiceState = @ptrCast(@alignCast(p));
        const r: usize = @intCast(row);
        const k: usize = @intCast(col);
        const id: u16 = @bitCast(psx.get(s.stats).?.armor[r][k].id);
        if (id >= action_limit) return 0;
        if (ctx.value(u2) == 0) return 0;
        if (ctx.value(u3) == 0) psx.setBits(&s.configs[r][k], "usable", ctx.value(u1));
        if (ctx.value(u2) == 0) s.configs[r][k].value = ctx.value(u32);
        return @bitCast(ctx.value(u32) | 1);
    }
};
