//! zig_diff spec: pc_charge_skip -> charge_skip.skip (146C.c, charge_skip.inc).
//! Inputs: the phase 5 (and 4, 6, 0, -1), the skip decision (charge_skip_now stub), the
//! action block's caster (0..16) with an actor slot empty or holding an actor whose turn is
//! pending (unk18 set or not, angle sums crossing 0xFFF), the pump setting, and the release
//! conditions (an empty object slot, the effect's files in): all four combinations.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").skip));

pub const classes = [_][:0]const u8{ "wrong-phase", "not-skipping", "no-actor", "no-turn", "released", "not-released" };

var now_result: c_int = 0;
var slot_result: c_int = 0;
var files_result: c_int = 0;

pub const stubs = struct {
    pub fn charge_skip_now(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return now_result;
    }
    pub fn vs_battle_getEmptyObjectDataSlot(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return slot_result;
    }
    pub fn func_800CEEBC(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return files_result;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    now_result = ctx.edge(c_int, &.{ 0, 1, 1, 1, 1, 7, -1 });
    slot_result = ctx.edge(c_int, &.{ 0, 0, 1, -1 });
    files_result = ctx.edge(c_int, &.{ 0, 1, 1, 9 });
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    block.unk0 = ctx.int(u32);
    // The other action slots name casters too (all in range, their actors absent: the table is cleared), so
    // a port that reads the wrong slot takes another path rather than a wild pointer.
    for (v.g.D_800F4538().*[0..]) |*e| e.* = null;
    for (&block.unk854) |*slot| slot.unk4.unk0.targetActor = ctx.range(u8, 0, 16);
    const caster = ctx.range(u8, 0, 16);
    block.unk854[block.unk0 & 3].unk4.unk0.targetActor = caster;
    if (ctx.chance(5)) {
        v.g.D_800F4538().*[caster] = null;
    } else {
        const actor = ctx.alloc(v.D_800F4538_t);
        v.g.D_800F4538().*[caster] = actor;
        actor.unk0.unk18 = if (ctx.chance(4)) 0 else ctx.int(u16) | 1;
        actor.unk0.pitch = ctx.pick(i16, &.{ 0, 0xFFF, 0x1000, -1, -0x1000, 0x7FFF, -0x8000 });
        actor.unk0.facing = ctx.pick(i16, &.{ 0, 0xFFF, 0x1000, -1, 0x7FFF, -0x8000 });
        actor.unk0.roll = ctx.pick(i16, &.{ 0, 0xFFF, 0x1000, -1, 0x7FFF, -0x8000 });
        actor.unk0.unk3C = ctx.pick(i16, &.{ 0, 1, 0xFFF, -1, 0x7FFF, -0x8000 });
        actor.unk0.unk3E = ctx.pick(i16, &.{ 0, 1, 0xFFF, -1, 0x7FFF, -0x8000 });
        actor.unk0.unk40 = ctx.pick(i16, &.{ 0, 1, 0xFFF, -1, 0x7FFF, -0x8000 });
    }
    v.g.pc_charge_skip_pump().* = ctx.edge(c_int, &.{ 0, 1, 1, 3 });
    return .{ctx.edge(c_int, &.{ 5, 5, 5, 5, 4, 6, 0, -1 })};
}

pub fn classify(args: Args) usize {
    if (args[0] != 5) return 0;
    if (now_result == 0) return 1;
    const block = v.g.D_800F19CC().*.?;
    const actor = v.g.D_800F4538().*[block.unk854[block.unk0 & 3].unk4.unk0.targetActor] orelse return 2;
    if (actor.unk0.unk18 == 0) return 3;
    return if (slot_result == 0 and files_result != 0) 4 else 5;
}
