//! zig_diff spec: pc_charge_skip_state -> charge_skip.state (146C.c, charge_skip.inc).
//! Inputs: the BATTLE mode 2 (or 1, 3), the action block present or not, the camera mode
//! 3..7 (the 4 and 6 edges), the current slot's action index at 255/256 and above with its
//! unk2_0 bit set or clear, the caster 0 or another, the settings (on/off, enemies too,
//! kinds mask, event script running), the holds (0 / 0.5 / 1.5 / 10.5 s), and the first
//! vsync of the hold (from_set 0 or 1) at distances around the hold (equal, one off).
//! pc_vsync_count is a stub returning 1000.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").state));

pub const classes = [_][:0]const u8{ "inactive", "no-effect", "phase4", "first-vsync", "skipping", "not-skipping" };

pub const stubs = struct {
    pub fn pc_vsync_count(ctx: *zd.Ctx) c_uint {
        _ = ctx;
        return 1000;
    }
    /// The real helpers (routed, so called through their symbols): the two sides see the same values.
    pub fn charge_skip_kind(ctx: *zd.Ctx, index: c_int) c_int {
        _ = ctx;
        return if (index < 0x8D) 1 else if (index < 0xB8) 2 else if (index < 0xE0) 4 else 8;
    }
    pub fn charge_skip_hold_vsyncs(ctx: *zd.Ctx, caster: c_int) c_int {
        _ = ctx;
        return @intCast(holdOf(@truncate(@as(c_uint, @bitCast(caster)))));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 2, 2, 2, 2, 1, 3 });
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = if (ctx.chance(10)) null else block;
    v.g._cameraMode().* = ctx.edge(u32, &.{ 3, 4, 4, 5, 5, 6, 6, 7 });
    block.unk0 = ctx.int(u32);
    const slot = &block.unk854[block.unk0 & 3];
    slot.actionIndex = ctx.pick(u16, &.{ 0, 0x40, 0x8C, 0x8D, 0xB7, 0xB8, 0xDF, 0xE0, 255, 256, 257, 0xFFFF });
    if (slot.actionIndex < 256) psx.setBits(&v.g.actions()[slot.actionIndex], "unk2_0", @intFromBool(!ctx.chance(5)));
    slot.unk4.unk0.targetActor = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 16, 255 });
    v.g.pc_qol_skip_charging().* = ctx.edge(c_int, &.{ 1, 1, 1, 0, 7 });
    v.g.pc_qol_skip_charging_who().* = ctx.edge(c_int, &.{ 0, 1, 1 });
    v.g.pc_charge_skip_kinds().* = ctx.edge(c_int, &.{ 15, 15, 15, 0, 1, 2, 4, 8, 14, 13, 11, 7 });
    v.g.D_800F4C34().* = ctx.edge(u8, &.{ 0, 0, 0, 0, 1, 255 });
    const hold: f32 = ctx.edge(f32, &.{ 0.0, 0.0, 0.5, 1.5, 10.5 });
    v.g.pc_qol_skip_charging_hold_mine().* = hold;
    v.g.pc_qol_skip_charging_hold_enemy().* = if (ctx.chance(2)) hold else 0.5;
    v.g.charge_skip_from_set().* = ctx.edge(c_int, &.{ 0, 1, 1, 1, 1, 255 });
    v.g.charge_skip_from().* = 1000 -% ctx.edge(c_uint, &.{ 0, 1, 29, 30, 31, 89, 90, 91, 599, 600, 601, 5000 });
    return .{ ctx.alloc(c_int), ctx.alloc(c_int), ctx.alloc(c_int), ctx.alloc(c_int) };
}

fn holdOf(caster: u8) c_uint {
    // (an int caster 0 is Ashley: the callers pass a u8)
    var s = if (caster == 0) v.g.pc_qol_skip_charging_hold_mine().* else v.g.pc_qol_skip_charging_hold_enemy().*;
    if (!(s > 0.0)) return 0;
    if (s > 10.0) s = 10.0;
    return @intFromFloat(s * 60.0 + 0.5);
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F196C().* != 2) return 0;
    const block = v.g.D_800F19CC().* orelse return 0;
    const mode = v.g._cameraMode().*;
    if (mode < 4 or mode > 6) return 0;
    const slot = &block.unk854[block.unk0 & 3];
    if (slot.actionIndex >= 256 or psx.getBits(&v.g.actions()[slot.actionIndex], "unk2_0") == 0) return 1;
    if (mode == 4) return 2;
    if (v.g.charge_skip_from_set().* == 0) return 3;
    const caster = slot.unk4.unk0.targetActor;
    const index = slot.actionIndex;
    const kind: c_int = if (index < 0x8D) 1 else if (index < 0xB8) 2 else if (index < 0xE0) 4 else 8;
    const on = v.g.pc_qol_skip_charging().* != 0 and (caster == 0 or v.g.pc_qol_skip_charging_who().* != 0) and
        (v.g.pc_charge_skip_kinds().* & kind) != 0 and v.g.D_800F4C34().* == 0 and
        1000 -% v.g.charge_skip_from().* >= holdOf(caster);
    return if (on) 4 else 5;
}
