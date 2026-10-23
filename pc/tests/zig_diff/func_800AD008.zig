//! zig_diff spec: func_800AD008 -> func_800AD008.func_800AD008 (40564.c).
//! Inputs: an actor with 0-42 bones (0, 1, 41 and 42 often), a random
//! animation id and random 0x5AC word, so the bit-23 call to func_800AE47C is
//! taken or not; a pose with random contents. The header the lookup writes
//! holds small offsets into a data block, and the anim block (filled with
//! small u16s past its unk0) holds small offsets for the frame-relative
//! header, so every header-relative read stays inside the harness's memory.
//! The frame byte is 0xFF, 0, 1, 2, 0xFE or random; the data block's bytes
//! are edges (0xFF, 0xFE, 1, 0, 0x80), so the 0xFF skip, the 0xFE/1 pair and
//! the signed-looking byte reads all occur. The callees write the key
//! vectors and the header entries the C reads back, the lookup returns NULL
//! sometimes, and func_800AE47C rewrites the bone count and animation id.
const zd = @import("zdiff");
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AD008));

pub const classes = [_][:0]const u8{ "no-bones", "frame-ff", "frame-other" };

const anim_size = 48 * 1024;
const data_size = 4096;
const hdr_len = 256;

var actor_obj: *v.D_800F4538_t = undefined;
var anim_obj: *v.func_800AD494_t = undefined;
var data_obj: *[data_size]u8 = undefined;
var hdr_obj: *[hdr_len]u16 = undefined;

pub const stubs = struct {
    pub fn func_800AE47C(ctx: *zd.Ctx, a: anytype) void {
        const act: *v.D_800F4538_t = @ptrCast(@alignCast(a.?));
        // The callee may change the bone count and the animation id: the C reads both back.
        act.unk0.nBones = ctx.value(u8) % 43;
        act.animationId = ctx.value(u8);
    }

    pub fn func_800AD494(ctx: *zd.Ctx, a: anytype, id: anytype, hdr: anytype) c_int {
        _ = a;
        _ = id;
        const out: *[*c]u16 = @ptrCast(@alignCast(hdr));
        out.* = &hdr_obj.*[0];
        // The out-parameter's entries the C reads back: sometimes 0 (the 0xFFFF branch).
        hdr_obj[2] = if (ctx.value(u8) < 64) 0 else ctx.value(u16) % 1000;
        hdr_obj[4] = if (ctx.value(u8) < 128) 0 else ctx.value(u16) % 1000;
        if (ctx.value(u8) < 48) return 0;
        return @bitCast(psx.addr(anim_obj));
    }

    pub fn func_800ACFA0(ctx: *zd.Ctx, key: anytype, data: anytype, frame: anytype) void {
        _ = data;
        _ = frame;
        const k: *v.SVECTOR = @ptrCast(@alignCast(key));
        k.vx = ctx.value(i16);
        k.vy = ctx.value(i16);
        k.vz = ctx.value(i16);
        k.pad = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const pose = ctx.alloc(v.D_800F4538_unkC54);
    actor_obj = actor;
    anim_obj = @ptrCast(@alignCast(ctx.allocBytes(anim_size, 16)));
    data_obj = @ptrCast(@alignCast(ctx.allocBytes(data_size, 16)));
    hdr_obj = @ptrCast(@alignCast(ctx.allocBytes(hdr_len * 2, 16)));

    // Anim block: small u16s after the data pointer (unk0 is the data address).
    const anim_bytes: *[anim_size]u8 = @ptrCast(anim_obj);
    var o: usize = 8;
    while (o + 2 <= anim_size) : (o += 2) {
        std.mem.writeInt(u16, anim_bytes[o..][0..2], ctx.range(u16, 0, 1000), .little);
    }
    anim_obj.unk0 = @bitCast(psx.addr(data_obj));

    // Bone count: edges often, otherwise any count the pose arrays hold.
    const nb: u8 = if (ctx.chance(3)) ctx.edge(u8, &.{ 0, 1, 41, 42 }) else ctx.range(u8, 0, 42);
    actor.unk0.nBones = nb;
    actor.animationId = ctx.int(u8);

    // Data block: bytes that make the 0xFF, 0xFE and 1 tests true sometimes.
    for (data_obj) |*b| b.* = ctx.pick(u8, &.{ 0xFF, 0xFE, 1, 0, 0x80 });

    // Out-parameter header: small offsets; entry 1 carries the frame (low byte)
    // and the flag byte (high byte, bit 0 selects the flagged loop).
    for (hdr_obj) |*h| h.* = ctx.range(u16, 0, 1000);
    const frame = ctx.pick(u8, &.{ 0xFF, 0, 1, 2, 0xFE });
    hdr_obj[1] = (@as(u16, ctx.int(u8)) << 8) | @as(u16, frame);
    hdr_obj[2] = if (ctx.chance(6)) 0 else ctx.range(u16, 1, 1000);
    hdr_obj[4] = if (ctx.chance(4)) 0 else ctx.range(u16, 1, 1000);

    return .{ actor, pose };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (actor_obj.unk0.nBones == 0) return 0;
    return if ((hdr_obj[1] & 0xFF) == 0xFF) 1 else 2;
}
