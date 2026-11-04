//! zig_diff spec: func_800B3CF0 -> pc.build_weapon_packets.buildWeaponPackets (D2: pc/src/weapon_packets.c).
//! Inputs: the scratchpad's packet cursor (word 0), ordering table (word 4)
//! and texture word (0x390), all random; blocks for the projected vertices,
//! the object, the model (its polygon pointer at +0x3C a random guest word)
//! and a random colour word. pc_weapon_packets' stub records its nine
//! arguments (the colour word's value, the counts pointer at object + 2, the
//! depth array at projected + 0xC00, the texture word in two halves) and
//! returns an arena address, which must be stored back as the cursor; the
//! hook stub records its arguments.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").buildWeaponPackets));

pub const classes = [_][:0]const u8{"build"};

pub const stubs = struct {
    pub fn pc_weapon_packets(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype, e: anytype, f: anytype, g: anytype, h: anytype, i: anytype) [*c]c_uint {
        _ = .{ a, b, c, d, e, f, g, h, i };
        return @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: [*]volatile u32 = @ptrFromInt(0x1f800000);
    scratch[0] = ctx.int(u32);
    scratch[1] = ctx.int(u32);
    scratch[0x390 / 4] = ctx.int(u32);
    const projected: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(0x20, 16)));
    const object = ctx.allocBytes(0x20, 16);
    const color: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    color[0] = ctx.int(c_uint);
    const model = ctx.allocBytes(0x80, 16);
    for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
    return .{ projected, object, color, model };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
