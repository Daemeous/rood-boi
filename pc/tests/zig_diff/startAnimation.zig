//! zig_diff spec: func_800A0204 -> actor_animation.startAnimation (30DB0.c).
//! Inputs: an actor id 0-16 whose D_800F4538 slot is an arena actor or
//! NULL (the other slots are never read); the actor's flag words (unk5AC:
//! bits 3, 15, 23, 24; unk0's stance bits unkA_0..unkA_6 and weaponDrawn)
//! random with bit 24 (stopped) rare; unk34 zero or not; animationId 84 or
//! not; unk63C around 0x80; animations 0, 1, 6, 13, 25, 26, 36, 39, 41, 59,
//! 84, -100 (+100 gives 0), any below 100, 100 and up; blendFrames at 0, 1,
//! 2, negative, 255/256 and the int edges (the doubling wraps).
//! Stubs: func_800AD494 follows a per-iteration plan (found or not, frames
//! NULL or an arena entry, D_800F49DC equal to unk6E3 or not, found equal
//! to unk187C or not) for the first call and the retry with animation 0;
//! func_800BEB00 (unprototyped in 30DB0.c) returns 4 or not;
//! func_800AC500 sets flag 15 again half the time; the others
//! are recording stubs. A failed retry is `return;` in the C: the object
//! returns func_800AD494's result.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startAnimation));

pub const classes = [_][:0]const u8{ "animation-0", "empty-slot", "stopped", "same-84", "restart", "no-restart",
    "retry-ok", "retry-fails", "turned" };

const Outcome = enum { ok, not_found, no_frames };
const Plan = struct { first: Outcome, retry: Outcome, same_found: bool, same_index: bool, mode4: bool };
var plan: Plan = undefined;
var frame_word: *u16 = undefined;

fn outcome(ctx: *zd.Ctx) Outcome {
    return switch (ctx.range(u8, 0, 5)) {
        0 => .not_found,
        1 => .no_frames,
        else => .ok,
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const actor = ctx.alloc(v.D_800F4538_t);
    v.g.D_800F4538()[@intCast(id)] = if (ctx.chance(10)) null else actor;
    const flags = psx.byteAt(u32, actor, 0x5AC);
    if (!ctx.chance(10)) flags.* &= ~@as(u32, 0x0100_0000);
    // unk34 all zero, or only vx/vy zero (the C tests the first word, then vz: the coverage lane's +85).
    if (ctx.chance(2)) actor.unk0.unk34 = .{ .vx = 0, .vy = 0, .vz = if (ctx.chance(3)) ctx.int(i16) | 1 else 0, .pad = actor.unk0.unk34.pad };
    if (ctx.chance(3)) actor.animationId = 84;
    actor.unk63C = ctx.pick(u16, &.{ 0, 0x7F, 0x80, 0xFFFF });
    frame_word = @ptrCast(@alignCast(ctx.allocBytes(2, 2)));
    plan = .{ .first = outcome(ctx), .retry = outcome(ctx), .same_found = ctx.chance(2), .same_index = ctx.chance(2),
        .mode4 = ctx.chance(4) };
    const animation: c_int = if (ctx.chance(3))
        ctx.pick(c_int, &.{ 0, 1, 6, 13, 25, 26, 36, 39, 41, 59, 84, -100 })
    else
        ctx.range(c_int, -200, 300);
    const blend = ctx.pick(c_int, &.{ 0, 1, 2, 3, -1, 255, 256, std.math.maxInt(c_int), std.math.minInt(c_int), 0x4000_0000 });
    return .{ id, animation, ctx.int(c_int), blend };
}

pub fn classify(args: Args) usize {
    if (args[1] == 0) return 0;
    const actor = v.g.D_800F4538()[@intCast(args[0])] orelse return 1;
    if (psx.byteAt(u32, actor, 0x5AC).* & 0x0100_0000 != 0) return 2;
    if (actor.animationId == 84 and psx.getBits(&actor.unk0, "weaponDrawn") == 0 and psx.getBits(actor, "unk5AC_3") != 0 and
        (args[1] == 84 or args[1] == 13)) return 3;
    if (plan.first != .ok) return if (plan.retry == .ok) 6 else 7;
    if (psx.getBits(&actor.unk0, "weaponDrawn") == 0 and
        (args[1] == 36 or args[1] == 39 or args[1] == 41 or args[1] == 59)) return 8;
    return if (plan.mode4 or !plan.same_found or !plan.same_index) 4 else 5;
}

pub const stubs = struct {
    pub fn func_800AD494(ctx: *zd.Ctx, actor_: anytype, animation: c_int, frames: anytype) c_int {
        const actor: *v.D_800F4538_t = @ptrCast(@alignCast(actor_.?));
        const step = if (animation != 0) plan.first else plan.retry;
        frames.* = if (step == .no_frames) null else @ptrCast(frame_word);
        if (step == .not_found) return 0;
        v.g.D_800F49DC().* = if (plan.same_index) actor.unk6E3 else ctx.value(u8);
        const found = if (plan.same_found) actor.unk187C else ctx.value(i32);
        return if (found == 0) 1 else found;
    }
    /// Sometimes sets the actor's flag 15 again, so the C's second test of it
    /// (after the calls) is reached with keepFlag15 clear (the coverage lane's +34).
    pub fn func_800AC500(ctx: *zd.Ctx, actor_: anytype) void {
        if (ctx.value(u8) & 1 != 0) {
            const actor: *v.D_800F4538_t = @ptrCast(@alignCast(actor_.?));
            psx.byteAt(u32, actor, 0x5AC).* |= 0x8000;
        }
    }
    pub fn func_800BEB00(ctx: *zd.Ctx) c_short {
        return if (plan.mode4) 4 else ctx.value(i16) & 0x7FF3;
    }
};
