//! zig_diff spec: func_800E4CF4 -> pc.candidate_record.canTargetActor (D2: pc/src/battle_helpers.c).
//! Inputs: the AI entity table D_800F5878 (each slot null or a random block
//! whose word at 0x10 gives the target mask), a state block (relation words
//! at 0x1A4 + id * 4 with bits 6-7 any of 0..3, slot flags at 0x230 + slot *
//! 8 with bit 1 random, the own id at 0x88 equal to `id` half the time), a
//! candidate (word 1: slot bits and the action index in bits 5-12; word 4:
//! the option bits 0x20 and 0x10) and the action table vs_main_actions
//! (random flags halfwords). Each way out of the C is reached: no peer,
//! relation under 2, relation 3 with option 0x20, an action whose mask
//! rejects, option 0x10, the self-target, and allowed.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").canTargetActor));

pub const classes = [_][:0]const u8{
    "no-peer", "relation-low", "option-20-relation-3", "option-20-mask", "option-10", "self", "allowed",
};

var which: usize = 0;

fn word(block: [*]const u8, offset: usize) u32 {
    return std.mem.readInt(u32, block[offset..][0..4], .little);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        if (ctx.chance(6)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x40, 16);
            for (block[0..0x40]) |*byte| byte.* = ctx.int(u8);
            table[i] = @ptrCast(block);
        }
    }
    const id = ctx.range(c_int, 0, 16);
    const state = ctx.allocBytes(0x340, 16);
    for (state[0..0x340]) |*byte| byte.* = ctx.int(u8);
    if (ctx.chance(2)) state[0x88] = @intCast(id);
    const candidate = ctx.allocBytes(0x20, 16);
    for (candidate[0..0x20]) |*byte| byte.* = ctx.int(u8);
    const entry: [*]const u8 = candidate;
    const word1 = word(entry, 4);
    const options = word(entry, 16);
    const slot: usize = (word1 & 7) * 4 + ((word1 >> 3) & 3);
    const relation = (word(state, 0x1a4 + @as(usize, @intCast(id)) * 4) >> 6) & 3;
    // Build the action table flags the check reads: random, or with 0x100 set or clear.
    const action = (word1 >> 5) & 0xff;
    const flags_at = v.g.actions() + action * 0x34 + 0xe;
    if (ctx.chance(3)) std.mem.writeInt(u16, flags_at[0..2], std.mem.readInt(u16, flags_at[0..2], .little) & ~@as(u16, 0x100), .little);

    which = blk: {
        const peer = table[@intCast(id)] orelse break :blk 0;
        if (relation < 2) break :blk 1;
        if (options & 0x20 != 0) {
            const peer_flags = word(@ptrCast(peer), 0x10);
            const mask = ((peer_flags >> 7) & 2) | ((peer_flags >> 14) & 4) | (peer_flags & 1);
            const action_flags = std.mem.readInt(u16, flags_at[0..2], .little);
            if (relation >= 3) break :blk 2;
            if (action_flags & 0x100 == 0 and (action_flags >> 9) & mask == 0) break :blk 3;
        }
        if (options & 0x10 != 0 and relation < 3) break :blk 4;
        if ((word(state, 0x230 + slot * 8) >> 1) & 1 != 0 and id == state[0x88]) break :blk 5;
        break :blk 6;
    };
    return .{ state, @ptrCast(candidate), id };
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
