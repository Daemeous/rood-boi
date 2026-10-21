//! zig_diff spec: func_800A1DE8 -> bone_view_matrix.boneViewMatrix (38C1C.c).
//! Inputs: an actor id (0-16) with or without an actor behind it, an object
//! block (D_800F45E0) when there is no actor, and a weapon block
//! (vs_battle_wepModels, ids 0-9) that is null or present. Bones cover the
//! object's bones (-1..320), the weapon's 0x40 range (including -192 and
//! 0x140, which index before and past the weapon's matrices), the 255 root
//! (u16 fields read as signed, scaled by unk183C with wrap), and the other
//! bones (-256..300, out-of-range included). Blocks get room before and after
//! for the indices the C reads. The GTE calls are recording stubs;
//! func_800B07DC and func_80041C68 also write what the real ones write (the
//! scratchpad offset; the product's translation), so a port that reads those
//! before the call, rather than after it as the C does, differs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").boneViewMatrix));

pub const classes = [_][:0]const u8{ "no-actor-no-object", "object", "weapon-null", "weapon", "bone-255", "bone" };

/// A block of T with `before` bytes of filler ahead of it (for negative
/// indices) and `after` bytes behind it (for indices past its end).
fn block(ctx: *zd.Ctx, comptime T: type, before: usize, after: usize) *T {
    if (before != 0) _ = ctx.allocBytes(before, 16);
    const p = ctx.allocBytes(@sizeOf(T) + after, 16);
    return @ptrCast(@alignCast(p));
}

fn weaponBone(ctx: *zd.Ctx) c_int {
    return switch (ctx.range(u8, 0, 4)) {
        0 => 64,
        1 => 79,
        2 => 0x140,
        3 => -192,
        else => 0x40 + ctx.range(c_int, 0, 15),
    };
}

fn plainBone(ctx: *zd.Ctx) c_int {
    while (true) {
        const b = ctx.range(c_int, -256, 300);
        if ((b & 0xF0) != 0x40 and b != 255) return b;
    }
}

pub const stubs = struct {
    /// The real one rotates the scratchpad offset into world space (offsetToWorld).
    pub fn func_800B07DC(ctx: *zd.Ctx, actor: anytype) void {
        _ = actor;
        const offset: *v.SVECTOR = @ptrFromInt(0x1F8003B0); // boneMatrixScratch.offset
        offset.vx = ctx.value(i16);
        offset.vy = ctx.value(i16);
        offset.vz = ctx.value(i16);
    }

    /// The real one multiplies the camera into the bone matrix in place.
    pub fn func_80041C68(ctx: *zd.Ctx, camera: anytype, m: anytype) void {
        _ = camera;
        const product: *v.MATRIX = @ptrCast(@alignCast(m));
        product.t[0] = ctx.value(i32);
        product.t[1] = ctx.value(i32);
        product.t[2] = ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const d4538 = v.g.D_800F4538();
    const d45e0 = v.g.D_800F45E0();
    const wep = v.g.vs_battle_wepModels();
    const kind = ctx.range(u8, 0, 5);
    // D_800F4538 has 17 entries, D_800F45E0 and the weapon table only 16 and 20.
    const id: usize = switch (kind) {
        0, 1 => ctx.range(usize, 0, 15),
        2, 3, 5 => ctx.range(usize, 0, 9),
        else => ctx.range(usize, 0, 16),
    };
    var unused: ?*v.MATRIX = null;
    if (!ctx.chance(2)) unused = ctx.alloc(v.MATRIX);

    var bone: c_int = 0;
    if (kind <= 1) {
        // No actor: the object path (kind 1) or the NULL return (kind 0).
        d4538[id] = null;
        d45e0[id] = if (kind == 1) block(ctx, v.D_800F45E0_t, 4096, 12000) else null;
        bone = if (kind == 1)
            (if (ctx.chance(4)) 255 else ctx.range(c_int, -1, 320))
        else
            ctx.range(c_int, -256, 300);
    } else {
        const actor = block(ctx, v.D_800F4538_t, 16384, 12000);
        d4538[id] = actor;
        if (id < 16) d45e0[id] = null;
        actor.unk183C = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768, 255 });
        for ([_]isize{ 0x84C, 0x84E, 0x850 }) |off| {
            psx.byteAt(u16, actor, off).* = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0x0FFF, 0x1000 });
        }
        bone = switch (kind) {
            2, 3 => weaponBone(ctx),
            4 => 255,
            else => plainBone(ctx),
        };
    }

    // The weapon table entry the weapon path reads (ids 0-9 only).
    if (id <= 9) {
        wep[id * 2] = switch (kind) {
            2 => null,
            3 => block(ctx, v.vs_battle_wepModels_t, 9000, 9000),
            else => if (ctx.chance(2)) null else block(ctx, v.vs_battle_wepModels_t, 9000, 9000),
        };
    }

    return .{ @as(c_int, @intCast(id)), bone, unused };
}

pub fn classify(args: Args) usize {
    const id: usize = @intCast(args[0]);
    const bone = args[1];
    if (v.g.D_800F4538()[id] == null) return if (v.g.D_800F45E0()[id] == null) 0 else 1;
    if ((bone & 0xF0) == 0x40) return if (v.g.vs_battle_wepModels()[id * 2] == null) 2 else 3;
    return if (bone == 255) 4 else 5;
}
