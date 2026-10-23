//! zig_diff spec: func_800AB358 -> func_800AB358.func_800AB358 (40564.c).
//! Inputs: a random D_800F4538_t actor in the arena, stored in a D_800F4538
//! slot 0-16; a random 8-byte arg (arg[1] selects the part, any byte); a valid
//! DR_MOVE packet whose guest address is in scratchpad slot 0; D_800E8F2C in
//! 0-7 selecting a page byte that is zero a quarter of the time (no draw) and
//! otherwise an edge value with or without the sign bit; the actor's unk5AC_27
//! bit both ways (the /2 and /4 paths); its slot byte 0-23 (the bound of the
//! 96-byte D_800E8F30 and 24-byte D_800E8F90 tables). The rest of the state
//! (table entries, scratchpad, arg bytes, actor fields) stays random. The
//! SetDrawMove stub copies the rect it is given into the packet, so the
//! rect's contents (not only the pointer) reach the compared state.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AB358));

pub const classes = [_][:0]const u8{ "page-zero", "bit27-set", "bit27-clear" };

pub const stubs = struct {
    pub fn SetDrawMove(ctx: *zd.Ctx, p: anytype, rect: anytype, x: anytype, y: anytype) void {
        _ = x;
        _ = y;
        const pk: *v.DR_MOVE = @ptrCast(@alignCast(p));
        const r: *const v.RECT = @ptrCast(@alignCast(rect));
        // The rect is passed by pointer: write its four fields into the packet
        // so every field the C computes is part of the compared state.
        const rx: u32 = @as(u16, @bitCast(r.x));
        const ry: u32 = @as(u16, @bitCast(r.y));
        const rw: u32 = @as(u16, @bitCast(r.w));
        const rh: u32 = @as(u16, @bitCast(r.h));
        pk.code[0] = rx | (ry << 16);
        pk.code[1] = rw | (rh << 16);
        pk.tag = ctx.value(u32);
    }

    pub fn func_8007A824(ctx: *zd.Ctx, p: anytype) void {
        // Links the packet into the ordering table: it rewrites the tag.
        const pk: *v.DR_MOVE = @ptrCast(@alignCast(p));
        pk.tag = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const packet = ctx.alloc(v.DR_MOVE);
    const argp: [*c]u8 = ctx.allocBytes(8, 16);
    const index = ctx.range(usize, 0, 16);
    v.g.D_800F4538().*[index] = actor;

    // The packet slot: a guest address (4 bytes) in the scratchpad.
    @as(*align(1) u32, @ptrFromInt(zd.scratchpad)).* = psx.addr(packet);

    const sel = ctx.range(u8, 0, 7);
    v.g.D_800E8F2C().* = sel;
    const page: u8 = if (ctx.chance(4)) 0 else ctx.edge(u8, &.{ 1, 0x08, 0x0F, 0x10, 0x80, 0xFF });
    v.g.D_800F2450()[sel] = page;

    actor.unk5BB = ctx.range(u8, 0, 23);
    psx.setBits(actor, "unk5AC_27", ctx.range(u1, 0, 1));

    return .{ @as(c_int, @intCast(index)), argp };
}

pub fn classify(args: Args) usize {
    const sel = v.g.D_800E8F2C().*;
    if (v.g.D_800F2450()[sel] == 0) return 0;
    const actor = v.g.D_800F4538().*[@intCast(args[0])].?;
    return if (psx.getBits(actor, "unk5AC_27") == 1) 1 else 2;
}
