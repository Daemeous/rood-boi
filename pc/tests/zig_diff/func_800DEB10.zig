//! zig_diff spec: func_800DEB10 -> func_800DEB10.func_800DEB10 (74088.c).
//! Inputs: a func_800DEEA4_t with room for 17 func_800DEB10_t elements. Its busy
//! word is 0 most times and a signed edge otherwise; its table index is 0..17.
//! Every table entry prepare sets is a real object: the C reads entry->unk54
//! before its NULL test, so a null entry faults in the C itself and cannot be
//! a valid input. Entries have unk9 = 24 most times, and 0..16 candidate blocks
//! (or -1). Slot m matches the key in all three fields, the other slots match
//! in a random subset of fields. The 17 elements have unk40 = 0 most times and
//! unk0 in 0..15. D_800F45E0 slots are null, actor A or actor B, and the
//! actorIds of A and B are drawn from all eight values, so both the hit (2, 4)
//! and miss cases occur. Stubs write entry fields the C never reads back, which
//! the state hash at each call still exposes. The one bit test's value is 0 or 1
//! by construction ((0x14 >> a 3-bit field) & 1), so it has no sign bit to see.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DEB10));

pub const classes = [_][:0]const u8{
    "null-arg",
    "busy",
    "wrong-type",
    "no-match",
    "match-no-hit",
    "match-hit",
};

const elem_count = 17;

fn pickFrom(ctx: *zd.Ctx, comptime T: type, edges: []const T) T {
    return edges[ctx.range(usize, 0, edges.len - 1)];
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const self: *v.func_800DEEA4_t = @ptrCast(@alignCast(ctx.allocBytes(
        @sizeOf(v.func_800DEEA4_t) + elem_count * @sizeOf(v.func_800DEB10_t),
        16,
    )));
    self.unk44 = if (ctx.chance(6)) pickFrom(ctx, i32, &.{ 1, -1, 0x100, -0x8000_0000 }) else 0;
    self.unk4 = ctx.range(u8, 0, 17);
    self.unk0 = if (ctx.chance(5)) ctx.int(u16) else ctx.range(u16, 0, 255);
    self.unk4A = ctx.range(u16, 0, 16);

    const elems: [*]v.func_800DEB10_t = @ptrCast(&self.unk4C);
    for (0..elem_count) |j| {
        elems[j].unk40 = if (ctx.chance(3)) ctx.int(i32) else 0;
        elems[j].unk0 = ctx.range(u8, 0, 15);
    }

    const actor_a = ctx.alloc(v.D_800F45E0_t);
    const actor_b = ctx.alloc(v.D_800F45E0_t);
    const ids = [_]u3{ 2, 4, 0, 1, 3, 5, 6, 7 };
    psx.setBits(&actor_a.unk6C[8], "actorId", pickFrom(ctx, u3, &ids));
    psx.setBits(&actor_b.unk6C[8], "actorId", pickFrom(ctx, u3, &ids));
    const slots = v.g.D_800F45E0();
    for (slots) |*s| {
        const choice: ?*v.D_800F45E0_t = if (ctx.chance(6)) null else if (ctx.chance(2)) actor_a else actor_b;
        s.* = choice;
    }

    const entry = ctx.alloc(v.func_800DEEA4_t2);
    const count: i32 = if (ctx.chance(4)) pickFrom(ctx, i32, &.{ 0, -1, 1, 2, 16 }) else ctx.range(i32, 0, 16);
    entry.unk470 = count;

    const t1 = ctx.alloc(v.func_800DEB10_t2);
    psx.put(&entry.unk54, t1);
    t1.unk9 = if (ctx.chance(4)) ctx.int(u8) else 24;
    t1.unkC = if (ctx.chance(4)) ctx.int(u16) else ctx.range(u16, 0, 7);
    t1.unkE = if (ctx.chance(4)) ctx.int(u16) else ctx.range(u16, 0, 3);

    const m = ctx.range(usize, 0, 15);
    for (&entry.unk430, 0..) |*slot, s| {
        const t = ctx.alloc(v.func_800DCAA0_t);
        const mask: u8 = if (s == m) 7 else ctx.range(u8, 0, 7);
        if ((mask & 1) != 0) psx.setBits(t, "unk4_5", @truncate(self.unk0));
        if ((mask & 2) != 0) psx.setBits(t, "unk4_0", @truncate(t1.unkC));
        if ((mask & 4) != 0) psx.setBits(t, "unk4_3", @truncate(t1.unkE));
        psx.put(slot, t);
    }

    // Always a real entry: the C dereferences entry->unk54 before its NULL test.
    v.g.D_800F5878()[self.unk4] = entry;

    return .{if (ctx.chance(12)) null else self};
}

pub fn classify(args: Args) usize {
    const self = args[0] orelse return 0;
    if (self.unk44 != 0) return 1;
    const entry = v.g.D_800F5878()[self.unk4].?;
    const t1 = psx.get(entry.unk54).?;
    if (t1.unk9 != 24) return 2;

    const elems: [*]v.func_800DEB10_t = @ptrCast(&self.unk4C);
    var matched = false;
    var hit = false;
    var i: i32 = 0;
    while (i < entry.unk470) : (i += 1) {
        const t = psx.get(entry.unk430[@intCast(i)]).?;
        if (self.unk0 != @as(u16, psx.getBits(t, "unk4_5"))) continue;
        if (t1.unkC != @as(u16, psx.getBits(t, "unk4_0"))) continue;
        if (t1.unkE != @as(u16, psx.getBits(t, "unk4_3"))) continue;
        matched = true;
        var j: usize = 0;
        while (j < self.unk4A) : (j += 1) {
            const e = &elems[j];
            if (e.unk40 != 0) continue;
            const a = v.g.D_800F45E0()[e.unk0] orelse {
                hit = true;
                break;
            };
            if (((@as(u8, 0x14) >> psx.getBits(&a.unk6C[8], "actorId")) & 1) != 0) {
                hit = true;
                break;
            }
        }
    }
    if (hit) return 5;
    if (matched) return 4;
    return 3;
}

pub const stubs = struct {
    pub fn func_800E4C8C(ctx: *zd.Ctx, entry: anytype) void {
        const e: *v.func_800DEEA4_t2 = @ptrCast(@alignCast(entry));
        e.unk9A = ctx.value(i16);
    }

    pub fn func_800E4CE8(ctx: *zd.Ctx, entry: anytype) void {
        const e: *v.func_800DEEA4_t2 = @ptrCast(@alignCast(entry));
        e.unk9A = ctx.value(i16);
        e.unk138 = ctx.value(u16);
    }
};
