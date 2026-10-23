//! zig_diff spec: func_800A4A88 -> func_800A4A88.func_800A4A88 (3A1A0.c).
//! Inputs: a D_800F4538_t in the arena with random bytes everywhere, its
//! contact id (unkC_0, 0 or 1-15), previous id (unkC_4, equal about a third of
//! the time), movement bits (unkA_0 at offset 8, so the masked word hits
//! 0x20000, 0x30000 and the rest), unkF (zero about half the time), and the
//! contact short at unk17EC[6] equal to the id, equal to the id with a high
//! byte, or random. The contact object returned by func_800A4A24 holds edge
//! values (0, -1, 32767, -32768). Mode is 0, 1 or 2: any other mode leaves the
//! C's `contact` uninitialised and is not compared. The callees write back the
//! fields the C reads after them (unkF, movement bits, unkC_0, unk17EC[6]).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A4A88));

pub const classes = [_][:0]const u8{
    "mode0-no-contact",
    "mode0-contact",
    "mode1-masked",
    "mode1-no-contact",
    "mode1-contact",
    "mode2-changed",
    "mode2-same",
};

var actor_ptr: ?*v.D_800F4538_t = null;
var contact_obj: ?*v.func_8008D2C0_t = null;

pub const stubs = struct {
    pub fn func_800A4A24(ctx: *zd.Ctx, id: anytype) *v.func_8008D2C0_t {
        _ = ctx;
        _ = id;
        return contact_obj.?;
    }

    pub fn func_8008C49C(ctx: *zd.Ctx, id: anytype, flag: anytype) void {
        _ = id;
        _ = flag;
        const a = actor_ptr.?;
        if (ctx.value(u1) == 1) a.unk0.unkF = ctx.value(u8);
        if (ctx.value(u1) == 1) psx.setBits(&a.unk0, "unkA_0", ctx.value(u3));
        if (ctx.value(u1) == 1) psx.setBits(&a.unk0, "unkC_0", ctx.value(u4));
        if (ctx.value(u1) == 1) std.mem.writeInt(i16, a.unk17EC[6..8], ctx.value(i16), .little);
    }

    pub fn func_800E4BD8(ctx: *zd.Ctx, id: anytype) void {
        _ = id;
        if (ctx.value(u1) == 1) std.mem.writeInt(i16, actor_ptr.?.unk17EC[6..8], ctx.value(i16), .little);
    }

    pub fn func_800E4BE0(ctx: *zd.Ctx, id: anytype) void {
        _ = id;
        if (ctx.value(u1) == 1) actor_ptr.?.unk0.unkF = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const contact = ctx.alloc(v.func_8008D2C0_t);
    actor_ptr = actor;
    contact_obj = contact;

    contact.unk0 = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
    contact.unk2 = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
    contact.unk4 = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });

    const mode: c_int = ctx.range(c_int, 0, 2);
    const id: u4 = if (ctx.chance(5)) 0 else ctx.range(u4, 1, 15);
    const prev: u4 = if (ctx.chance(3)) id else ctx.int(u4);
    psx.setBits(&actor.unk0, "unkC_0", id);
    psx.setBits(&actor.unk0, "unkC_4", prev);
    psx.setBits(&actor.unk0, "unkA_0", ctx.pick(u3, &.{ 0, 1, 2, 3, 4, 7 }));
    actor.unk0.unkF = if (ctx.chance(2)) 0 else ctx.int(u8);

    const low: i16 = @intCast(id);
    const shortVal: i16 = switch (ctx.range(u2, 0, 2)) {
        0 => low,
        1 => low | (1 << 8),
        else => ctx.int(i16),
    };
    std.mem.writeInt(i16, actor.unk17EC[6..8], shortVal, .little);

    return .{ actor, mode };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    const id = psx.getBits(&actor.unk0, "unkC_0");
    const prev = psx.getBits(&actor.unk0, "unkC_4");
    const mov = psx.getBits(&actor.unk0, "unkA_0");
    return switch (args[1]) {
        0 => if (id == 0) 0 else 1,
        1 => if (mov == 2 or mov == 3) 2 else if (id == 0) 3 else 4,
        else => if (id != prev) 5 else 6,
    };
}
