//! zig_diff spec: func_80070278 -> func_80070278.func_80070278 (146C.c).
//! Inputs: D_800F19CC with the link (unk0) 0 or not and its slot (unk0 & 3)
//! holding an action index at the C's edges (0x15/0x16, 0x27/0x28, 0x35/0x36,
//! 0x8C/0x8D, 0xB7/0xB8, 0xDF/0xE0, below 256: the C indexes vs_main_actions
//! with it), target actor 0 or not, unk40 0 or not; the chain index unk2C03
//! 0..5 (the C reads mappedChainAbilities[unk2C03 - 1] unchecked: -1..4 stay
//! inside vs_main_settings); the action's unk2_0 bit as the harness fills it;
//! the charging skip's hold 0 or 1.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80070278));

pub const classes = [_][:0]const u8{ "plain", "art", "ability", "other-high", "other-low", "chain-low", "chain-high" };

pub const stubs = struct {
    pub fn pc_charge_skip_hold(ctx: *zd.Ctx) c_int {
        return ctx.edge(c_int, &.{ 0, 1 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = state;
    state.unk0 = if (ctx.chance(2)) ctx.range(u32, 0, 3) *% 4 else ctx.pick(u32, &.{ 1, 2, 3, 5 });
    const slot = &state.unk854[state.unk0 & 3];
    slot.actionIndex = ctx.edge(u16, &.{ 0, 0x15, 0x16, 0x27, 0x28, 0x35, 0x36, 0x8C, 0x8D, 0xB7, 0xB8, 0xDF, 0xE0, 0xFF, ctx.range(u16, 0, 255) });
    slot.unk4.unk40 = if (ctx.chance(2)) 0 else ctx.int(i32);
    slot.unk4.unk0.targetActor = if (ctx.chance(2)) 0 else ctx.int(u8);
    state.unk2C03 = ctx.range(u8, 0, 5);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const state = v.g.D_800F19CC().*.?;
    const slot = &state.unk854[state.unk0 & 3];
    const index = slot.actionIndex;
    if (state.unk0 != 0) return if (index < 0x28) 5 else 6;
    if (psx.getBits(&v.g.actions()[index], "unk2_0") == 0) return 0;
    if (index < 0x8D) return 4;
    if (index < 0xB8) return 1;
    if (index < 0xE0) return 2;
    return 3;
}
