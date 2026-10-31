//! zig_diff spec: func_80092A18 -> func_80092A18.func_80092A18 (2842C.c).
//! Inputs: a count of 0 (early return) or 1..32 (the table has 96 entries and
//! entry i+64 must stay below it). Each live entry is freed through a nonzero
//! unk0, through unequal guest pointers (one null or both distinct), or kept
//! with unk0 zero and equal pointers; unk0 takes 0, 1, 0x80 and 0xFF. The
//! slot D_800F2258 is -1 or an edge value (-2, 0, 1, 32767, -32768), and the
//! room's section14 is null or an arena object. The stub of vs_main_freeHeap
//! changes the count, an entry, the slot and section14, which the C reads back
//! after the loop; the room call (func_8004644C) is a plain recording stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092A18));

pub const classes = [_][:0]const u8{ "empty", "room-call", "no-slot", "no-room" };

/// The spare section14 target prepare sets (stubs use it, no allocation in stubs).
var spare: *u32 = undefined;

pub const stubs = struct {
    pub fn vs_main_freeHeap(ctx: *zd.Ctx, p: anytype) void {
        _ = p;
        if (ctx.value(u8) < 96) v.g.D_800F225B().* = ctx.value(u8) & 31;
        if (ctx.value(u8) < 96) {
            const table = v.g.D_800F1DD8();
            table[ctx.value(u8) % 96].unk0 = ctx.value(u8);
        }
        if (ctx.value(u8) < 96) v.g.D_800F2258().* = ctx.value(i16);
        if (ctx.value(u8) < 96) {
            const room = v.g.vs_battle_roomData();
            psx.put(&room.section14, if (ctx.value(u1) == 1) null else spare);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const count: u8 = if (ctx.chance(8)) 0 else ctx.range(u8, 1, 32);
    v.g.D_800F225B().* = count;
    const table = v.g.D_800F1DD8();
    var i: usize = 0;
    while (i < count) : (i += 1) {
        const e = &table[i + 64];
        const obj = ctx.alloc(v.D_800F1DD8_t2);
        const other = ctx.alloc(v.D_800F1DD8_t2);
        switch (ctx.range(u8, 0, 3)) {
            0 => {
                e.unk0 = 0;
                psx.put(&e.unk4, obj);
                psx.put(&e.unk8, obj);
            },
            1 => {
                e.unk0 = 0;
                psx.put(&e.unk4, obj);
                psx.put(&e.unk8, if (ctx.chance(4)) null else other);
            },
            2 => {
                e.unk0 = ctx.pick(u8, &.{ 1, 0x80, 0xFF });
                psx.put(&e.unk4, if (ctx.chance(4)) null else obj);
                psx.put(&e.unk8, obj);
            },
            else => {
                e.unk0 = ctx.int(u8);
                psx.put(&e.unk4, if (ctx.chance(2)) null else other);
                psx.put(&e.unk8, if (ctx.chance(2)) null else obj);
            },
        }
    }
    const room = v.g.vs_battle_roomData();
    spare = ctx.alloc(u32);
    psx.put(&room.section14, if (ctx.chance(3)) null else spare);
    v.g.D_800F2258().* = ctx.pick(i16, &.{ -1, -1, 0, 1, -2, 32767, -32768 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F225B().* == 0) return 0;
    if (v.g.D_800F2258().* == -1) return 2;
    return if (psx.get(v.g.vs_battle_roomData().section14) != null) 1 else 3;
}
