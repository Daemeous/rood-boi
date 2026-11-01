//! zig_diff spec: func_800AAE9C -> func_800AAE9C.func_800AAE9C (40564.c).
//! Inputs: the page byte D_800F2450[D_800E8F2C] zero or not; an actor whose
//! two animation slots have a row (unk0_0, zero sometimes), every mode
//! (unk0_4: 0, 1, 2 and the 3-15 that skip the switch), unk1 at 1 or not,
//! a frame index (unk2, mostly inside its row) and a timer (unk3, 255 wraps)
//! at, below and above the frame's length; the frame tables' entries at the
//! slot's frame, the next one and the row start, zero or not (the wrap to the
//! row start and the early return). The tables are split guest tables the C
//! indexes past the next symbol: their `room` covers every row and index.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AAE9C));

pub const ignore_return = "the C's bare `return;` and its end leave the int result to the register (the 64-bit object's slot index, the 32-bit one's actor pointer): every caller (44F14.c) declares the function void";

pub const classes = [_][:0]const u8{ "page-zero", "slot0-active", "slot1-active", "idle" };

fn frame(i: usize, row: usize, index: usize) *v.func_800AAE9C_t {
    if (i == 0) return psx.at(@as([*]v.func_800AAE9C_t, @ptrCast(psx.at(v.g.D_800E909C(), row))), index);
    return psx.at(@as([*]v.func_800AAE9C_t, @ptrCast(psx.at(v.g.D_800E916C(), row))), index);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const sel = ctx.range(u8, 0, 7);
    v.g.D_800E8F2C().* = sel;
    v.g.D_800F2450()[sel] = if (ctx.chance(8)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
    var i: usize = 0;
    while (i < 2) : (i += 1) {
        const slot = &actor.unk180C[i];
        const row: u4 = if (ctx.chance(6)) 0 else ctx.range(u4, 1, 15);
        psx.setBits(slot, "unk0_0", row);
        psx.setBits(slot, "unk0_4", if (ctx.chance(8)) ctx.range(u4, 3, 15) else ctx.range(u4, 0, 2));
        slot.unk1 = if (ctx.chance(4)) 1 else ctx.int(u8);
        slot.unk2 = if (ctx.chance(16)) ctx.int(u8) else ctx.range(u8, 0, 27);
        slot.unk3 = if (ctx.chance(16)) 255 else ctx.int(u8);
        const index: usize = slot.unk2;
        const p = frame(i, row, index);
        // The length test `p->unk1 < ++unk3`: equal, one less, one more, any.
        const next: u8 = slot.unk3 +% 1;
        p.unk1 = switch (ctx.range(u8, 0, 3)) {
            0 => next,
            1 => next -% 1,
            2 => next +% 1,
            else => ctx.int(u8),
        };
        p.unk0 = if (ctx.chance(5)) 0 else ctx.int(u8);
        const q = frame(i, row, index + 1);
        q.unk0 = if (ctx.chance(3)) 0 else ctx.int(u8);
        const start = frame(i, row, 0);
        if (ctx.chance(4)) start.unk0 = 0 else if (start.unk0 == 0) start.unk0 = 1;
        if (index == 0 and ctx.chance(2)) start.unk0 = p.unk0;
    }
    return .{actor};
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F2450()[v.g.D_800E8F2C().*] == 0) return 0;
    const actor = args[0].?;
    if (psx.getBits(&actor.unk180C[0], "unk0_0") != 0) return 1;
    if (psx.getBits(&actor.unk180C[1], "unk0_0") != 0) return 2;
    return 3;
}
