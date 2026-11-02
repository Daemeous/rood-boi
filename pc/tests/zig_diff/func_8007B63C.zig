//! zig_diff spec: func_8007B63C -> func_8007B63C.func_8007B63C (146C.c).
//! Inputs: D_800F19CC an arena block whose action index (0, 1, 255 or any
//! below 256), the action's unlocked bit, the hit's unk40, unk2C07 and the
//! target actor each pass their test three times in four, so every term of
//! the condition fails alone; _isArtOrAbilityUnlocked's stub returns
//! values with the top bit set and halves at their edges.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8007B63C));

pub const classes = [_][:0]const u8{ "message", "unlock" };

pub const stubs = struct {
    pub fn _isArtOrAbilityUnlocked(ctx: *zd.Ctx) c_int {
        return ctx.edge(c_int, &.{ 0, 1, 0x10000, 0x7FFF_FFFF, -1, 0x1234_8765, -0x8000_0000 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    const index: u16 = if (ctx.chance(4)) 0 else ctx.pick(u16, &.{ 1, 255 }) & 0xFF;
    block.unk8.actionIndex = index;
    psx.setBits(&v.g.actions()[index], "unlocked", @intFromBool(ctx.chance(4)));
    block.unk8.unk4.unk40 = if (ctx.chance(4)) ctx.pick(i32, &.{ 1, -1 }) else 0;
    block.unk2C07 = if (ctx.chance(4)) ctx.pick(u8, &.{ 1, 0x80 }) else 0;
    block.unk8.unk4.unk0.targetActor = if (ctx.chance(4)) ctx.pick(u8, &.{ 1, 0xFF }) else 0;
    return .{};
}

pub fn classify(_: Args) usize {
    const block = v.g.D_800F19CC().*.?;
    const learned = block.unk8.actionIndex != 0 and psx.getBits(&v.g.actions()[block.unk8.actionIndex], "unlocked") == 0 and
        block.unk8.unk4.unk40 == 0 and block.unk2C07 == 0 and block.unk8.unk4.unk0.targetActor == 0;
    return if (learned) 0 else 1;
}
