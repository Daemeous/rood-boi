//! zig_diff spec: vs_battle_renderBattleAbilityTimingResult -> render_battle_ability_timing_result.renderBattleAbilityTimingResult (2842C.c).
//! Inputs: the global entry table D_800F1BAC is an arena array of 24-byte
//! entries holding count D_800F227E, which ranges over -1, -32768, 0 and
//! 1..5 (the negative and zero counts skip the loop). Each entry's unk8 is
//! random, or forced to u16 == 4 (match) or to u8[0] == 4 with a nonzero
//! u8[1] (a byte test that the u16 test rejects). One extra entry past the
//! count exists so an off-by-one bound reads memory the harness owns. The
//! stubs write the count (the C reads it back in `++`) and return a fresh
//! arena entry from func_800962E4.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").renderBattleAbilityTimingResult));

pub const classes = [_][:0]const u8{ "no-entries", "match", "no-match" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const roll = ctx.range(u8, 0, 9);
    const count: i16 = switch (roll) {
        0 => -1,
        1 => -32768,
        2 => 0,
        else => ctx.range(i16, 1, 5),
    };
    const n: usize = if (count > 0) @intCast(count) else 0;

    // n + 1 entries: the entry at index `count` exists for an off-by-one bound.
    const arr: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(ctx.allocBytes((n + 1) * @sizeOf(v.D_800F1BAC_t), 16)));
    for (0..n + 1) |k| {
        const e = &arr[k];
        switch (ctx.range(u8, 0, 4)) {
            0 => {
                e.unk8.u8[0] = 4;
                e.unk8.u8[1] = 0;
            },
            1 => {
                e.unk8.u8[0] = 4;
                e.unk8.u8[1] = ctx.range(u8, 1, 255);
            },
            else => {},
        }
    }
    v.g.D_800F1BAC().* = @ptrCast(arr);
    v.g.D_800F227E().* = count;
    return .{ctx.int(c_int)};
}

pub fn classify(args: Args) usize {
    _ = args;
    const count = v.g.D_800F227E().*;
    if (count <= 0) return 0;
    const arr: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(v.g.D_800F1BAC().*.?));
    var k: usize = 0;
    while (k < @as(usize, @intCast(count))) : (k += 1) {
        if (arr[k].unk8.u16 == 4) return 1;
    }
    return 2;
}

pub const stubs = struct {
    // Returns a valid entry (the C writes through it); sometimes changes the count, which the C's `++` reads back.
    pub fn func_800962E4(ctx: *zd.Ctx) *v.D_800F1BAC_t {
        if (ctx.value(u2) == 0) v.g.D_800F227E().* = ctx.value(i16);
        return ctx.alloc(v.D_800F1BAC_t);
    }

    // The callee may change the count, which the C reads back after the break.
    pub fn func_80096444(ctx: *zd.Ctx, i: anytype) void {
        _ = i;
        if (ctx.value(u2) == 0) v.g.D_800F227E().* = ctx.value(i16);
    }
};
