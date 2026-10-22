//! zig_diff spec: func_800AFA28 -> func_800AFA28.func_800AFA28 (44F14.c).
//! Inputs: an actor with 0-255 animation frames, shadow counters (unk4/unk5 zero
//! or not, unkA/unk10/unk12 at the i16 edges), a queued animation or none, the
//! freeze bit on or off, nBones 0-255, and the byte at +0x549 zero or not. The
//! state block has frame counters at the 0..65535 wrap edges, unk540 equal to
//! unk5BC often, unk542 equal to unk540 or not, and the skip flag (bit 16) set
//! or clear. The callees are recording stubs: func_800AD494 writes the header
//! out-parameter (root track zero or not, header byte 3 with and without bit 1)
//! and returns a data address or 0; func_800AFDE8 writes SVECTOR fields that are
//! zero a quarter of the time; func_800AD714 may move unk540, unk5BC, unk6E0 and
//! the flag bits, which the C reads back. Each stub's return type is taken from
//! the c module's prototype, so the stubs match whatever the C declares.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AFA28));

pub const classes = [_][:0]const u8{ "no-frames", "queued-return", "bit16-set", "byte549-set", "data-path" };

/// The return type the c module declares for a callee.
fn Ret(comptime F: type) type {
    return @typeInfo(F).@"fn".return_type.?;
}

/// The default result of a stub with return type R: nothing for void, 0 otherwise.
fn zeroRet(comptime R: type) R {
    return if (R == void) {} else @as(R, 0);
}

/// The header buffer func_800AD494 hands out, set by prepare each iteration.
var hdr_words: ?[*]u16 = null;

pub const stubs = struct {
    pub fn func_800AD494(ctx: *zd.Ctx, actor: anytype, id: anytype, out: anytype) Ret(@TypeOf(c.func_800AD494)) {
        const R = Ret(@TypeOf(c.func_800AD494));
        _ = actor;
        _ = id;
        const words = hdr_words.?;
        const bytes: [*]u8 = @ptrCast(words);
        words[4] = if (ctx.value(u2) == 0) 0 else ctx.value(u16);
        bytes[3] = ctx.value(u8);
        out.* = words;
        if (ctx.value(u2) == 0) return zeroRet(R);
        return @as(R, @bitCast(psx.addr(words)));
    }

    pub fn func_800AFDE8(ctx: *zd.Ctx, root: anytype, out: anytype, flag: anytype) void {
        _ = root;
        _ = flag;
        const o: *v.SVECTOR = @ptrCast(@alignCast(out));
        o.vx = if (ctx.value(u2) == 0) 0 else ctx.value(i16);
        o.vy = if (ctx.value(u2) == 0) 0 else ctx.value(i16);
        o.vz = if (ctx.value(u2) == 0) 0 else ctx.value(i16);
    }

    pub fn func_800AD714(ctx: *zd.Ctx, actor: anytype, state: anytype, mode: anytype) Ret(@TypeOf(c.func_800AD714)) {
        const R = Ret(@TypeOf(c.func_800AD714));
        _ = mode;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(actor));
        const s: *v.D_800F4538_unkC54 = @ptrCast(@alignCast(state));
        if (ctx.value(u2) == 0) s.unk540 = ctx.value(u16);
        if (ctx.value(u2) == 0) a.unk5BC = ctx.value(i16);
        if (ctx.value(u2) == 0) a.unk6E0 = ctx.value(u8);
        if (ctx.value(u1) == 1) s._bits_1352[2] = ctx.value(u8);
        return zeroRet(R);
    }

    pub fn func_800A0618(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype) Ret(@TypeOf(c.func_800A0618)) {
        const R = Ret(@TypeOf(c.func_800A0618));
        _ = ctx;
        _ = a;
        _ = b;
        _ = cc;
        _ = d;
        return zeroRet(R);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const state: *v.D_800F4538_unkC54 = @ptrCast(@alignCast(ctx.allocBytes(4096, 16)));
    hdr_words = @as([*]u16, @ptrCast(@alignCast(ctx.allocBytes(2048, 16))));

    actor.unk5B2 = if (ctx.chance(4)) 0 else ctx.pick(u8, &.{ 1, 1, 2, 3, 255 });
    actor.unk5BC = ctx.pick(i16, &.{ 0, 1, 2, 100, -1, 32767, -32768 });
    state.unk540 = ctx.pick(u16, &.{ 0, 1, 2, 99, 100, 101, 0x7FFF, 0x8000, 0xFFFF });
    if (ctx.chance(3)) state.unk540 = @bitCast(actor.unk5BC);
    state.unk542 = if (ctx.chance(2)) state.unk540 else ctx.pick(u16, &.{ 0, 1, 2, 0xFFFF });
    psx.setBits(&actor.unk0, "freeze", ctx.int(u1));
    actor.unk6E0 = if (ctx.chance(2)) 0 else ctx.int(u8);
    actor.unk0.nBones = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    actor.unk1864.unk4 = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    actor.unk1864.unk5 = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    actor.unk1864.unkA = ctx.pick(i16, &.{ 0, -1, 32767, -32768, 5 });
    actor.unk1864.unk10 = ctx.pick(i16, &.{ 0, -1, 32767, -32768, 5 });
    actor.unk1864.unk12 = ctx.pick(i16, &.{ 0, -1, 32767, -32768, 5 });
    state._bits_1352[1] = if (ctx.chance(3)) ctx.int(u8) else 0;
    state._bits_1352[2] = ctx.int(u8) & 0xFE;
    if (ctx.chance(4)) state._bits_1352[2] |= 1;

    return .{ actor, state, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    const state = args[1].?;
    if (actor.unk5B2 == 0) return 0;
    const frame: i32 = @as(i16, @bitCast(state.unk540));
    const limit: i32 = @as(u16, @bitCast(actor.unk5BC));
    if (actor.unk6E0 != 0 and frame >= limit) return 1;
    if ((state._bits_1352[2] & 1) != 0) return 2;
    if (state._bits_1352[1] != 0) return 3;
    return 4;
}
