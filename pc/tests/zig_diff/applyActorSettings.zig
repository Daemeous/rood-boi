//! zig_diff spec: func_800E5A9C -> apply_actor_settings.applyActorSettings (7CD68.c).
//! Inputs: a state with an arena actor (kind 0xAC/0xAD or not), a random settings
//! word, the D_80069C1C table set to the clamp edges (0x23FFFF, 0x240000, ...),
//! collisionRadius around 128, flags with and without 0xC0, the D_800F58C0 /
//! D_800F58C8 scan (off, breaking at an entry with bit 15 set, or running to the
//! end with every entry below 0x8000), and the D_800F1790 callbacks as distinct
//! arena pointers with indices 0-9. The D_800F58C8 entries are drawn from edge
//! values (0, 0x7FFF, 0x8000, 0xFFFF) half the time, so each scanned entry often
//! equals 0 and the scan's `>> 15` test sees both sides equal.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").applyActorSettings));

pub const classes = [_][:0]const u8{ "no-scan", "scan-breaks", "scan-full", "kind-ac-ad" };

pub const stubs = struct {
    /// Writes a state field the C never reads back; the state hash at the call still sees it.
    pub fn func_800E7660(ctx: *zd.Ctx, state: anytype) void {
        const s: *v.actorSettingsState = @ptrCast(@alignCast(state));
        s.f2A = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.actorSettingsState);
    const settings = ctx.alloc(v.actorSettingsWord);
    const actor = ctx.alloc(v.actorSettingsActor);
    psx.put(&state.actor, actor);

    settings.raw = ctx.int(u32);

    // D_80069C1C: 8 entries; the clamp test sees its sides equal on 0x23FFFF.
    const range_table = v.g.D_80069C1C();
    for (0..8) |k| range_table[k] = ctx.pick(u32, &.{ 0x23FFFF, 0x240000, 0x23FFFE, 0, 0xFFFF_FFFF });

    state.collisionRadius = ctx.pick(u16, &.{ 0, 127, 128, 129, 0xFFFF });
    state.flags = ctx.pick(u16, &.{ 0, 0x40, 0x80, 0xC0, 0x3F, 0xFFFF });

    actor.kind = ctx.pick(u16, &.{ 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0 });
    // The else branch indexes D_800F1790 by bits 4..8 of flags: keep them in its 10 entries.
    actor.flags.raw = (actor.flags.raw & ~@as(u32, 31 << 4)) | (ctx.range(u32, 0, 9) << 4);
    actor.flags34.raw = ctx.int(u32);

    // D_800F58C8 is an 8-entry table; D_800F58C0 selects whether the C scans it.
    // mode 0: no scan; mode 1: one entry has bit 15 set (scan breaks);
    // mode 2: every entry below 0x8000 (scan runs to the end).
    const table: [*]u16 = @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    const mode = ctx.range(u8, 0, 2);
    for (0..8) |k| {
        table[k] = if (mode == 2)
            ctx.pick(u16, &.{ 0, 0x7FFF })
        else
            ctx.pick(u16, &.{ 0, 0x7FFF, 0x8000, 0xFFFF });
    }
    if (mode == 1) table[ctx.range(usize, 0, 7)] |= 0x8000;
    v.g.D_800F58C8().* = table;
    v.g.D_800F58C0().* = if (mode == 0) 0 else ctx.pick(i32, &.{ 1, -1, std.math.minInt(i32), std.math.maxInt(i32) });

    // D_800F1790: 10 distinct guest-addressable blocks (psx_addr must fit in 32 bits).
    const callbacks = v.g.D_800F1790();
    for (0..10) |k| {
        const block: [*]u8 = ctx.allocBytes(16, 16);
        callbacks[k] = @ptrCast(block);
    }

    return .{ state, settings };
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    const actor = psx.get(state.actor).?;
    if (actor.kind == 0xAC or actor.kind == 0xAD) return 3;
    if (v.g.D_800F58C0().* == 0) return 0;
    const table = v.g.D_800F58C8().*.?;
    for (0..8) |k| {
        if (table[k] >> 15 != 0) return 1;
    }
    return 2;
}
