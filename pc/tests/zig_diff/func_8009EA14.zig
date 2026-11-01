//! zig_diff spec: func_8009EA14 -> func_8009EA14.func_8009EA14 (30DB0.c).
//! Inputs: the actor id (the player 0, or 1..16) with its D_800F4538 entry
//! null 1 time in 8, else an arena actor with unk5AC_3, unkA_3 (0..3),
//! unkC_0, unkB_0 (3 or not) and unkF (0 or not) at their edges and a
//! facing at 0, +-0x800, 0xFFF or random; the source point null 1 time in 4,
//! else on the actor's x/z (dx = dz = 0) or off it. ratan2 gives the
//! player's elevation at the 0x44/0x45 thresholds (-0xE5..-0xE3,
//! 0xAB..0xAD) and the bearing so that the turn meets 0x800/0x801 and the
//! +-0x800 wraps exactly. On the unkA_3 == 2 path the C passes an
//! uninitialised blend (func_800A0204's argument 3): not compared there.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009EA14));

pub const classes = [_][:0]const u8{ "no-actor", "flagged", "player", "other", "state-2" };

var elevation: c_int = 0;
var bearing: c_int = 0;
var player_with_source: bool = false;
var state2: bool = false;

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        // The player's elevation is the third call (after vs_gte_rsqrt and func_800A1AF8).
        if (player_with_source and ctx.calls == 2) return elevation;
        return bearing;
    }
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
    pub fn func_800A1AF8(ctx: *zd.Ctx, a: anytype, l: anytype, out: anytype, m: anytype) void {
        _ = a;
        _ = l;
        _ = m;
        const p: *v.SVECTOR = @ptrCast(@alignCast(out));
        p.vx = ctx.value(i16);
        p.vy = ctx.value(i16);
        p.vz = ctx.value(i16);
    }
};

pub fn uninitialisedArgument(call: []const u8, index: usize) bool {
    return state2 and index == 3 and std.mem.eql(u8, call, "func_800A0204");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id: c_int = if (ctx.chance(2)) 0 else ctx.range(c_int, 1, 16);
    const actors = v.g.D_800F4538();
    var source: ?*v.SVECTOR = null;
    state2 = false;
    player_with_source = false;
    if (ctx.chance(8)) {
        actors[@intCast(id)] = null;
    } else {
        const a = ctx.alloc(v.D_800F4538_t);
        psx.setBits(a, "unk5AC_3", @intFromBool(ctx.chance(6)));
        psx.setBits(&a.unk0, "unkA_3", ctx.int(u2));
        psx.setBits(&a.unk0, "unkC_0", if (ctx.chance(2)) 0 else ctx.int(u4));
        psx.setBits(&a.unk0, "unkB_0", if (ctx.chance(3)) 3 else ctx.int(u4));
        a.unk0.unkF = if (ctx.chance(2)) 0 else ctx.int(u8);
        a.unk0.facing = ctx.pick(i16, &.{ 0, 0, 0x800, -0x800, 0xFFF });
        if (!ctx.chance(4)) {
            const s = ctx.alloc(v.SVECTOR);
            s.vx = if (ctx.chance(3)) a.unk0.position.vx else ctx.int(i16);
            s.vz = if (ctx.chance(3)) a.unk0.position.vz else ctx.int(i16);
            if (ctx.chance(4)) {
                s.vx = a.unk0.position.vx;
                s.vz = a.unk0.position.vz;
            }
            source = s;
        }
        actors[@intCast(id)] = a;
        state2 = psx.getBits(&a.unk0, "unkA_3") == 2;
        player_with_source = id == 0 and source != null and psx.getBits(a, "unk5AC_3") == 0;
    }
    elevation = ctx.pick(c_int, &.{ -0xE5, -0xE4, -0xE3, 0xAB, 0xAC, 0xAD, 0 });
    // With facing 0: 0x400 -> turn 0x800 + 0x200 (the > 0x800 edge), 0x600 -> 0x800, 0x5FF -> 0x801.
    bearing = ctx.pick(c_int, &.{ 0x400, 0x600, 0x5FF, 0x601, 0xC00, 0x3FF });
    return .{ id, source };
}

pub fn classify(args: Args) usize {
    const a = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    if (psx.getBits(&a.unk0, "unkA_3") == 2) return 4;
    if (psx.getBits(a, "unk5AC_3") != 0) return 1;
    return if (args[0] == 0) 2 else 3;
}
