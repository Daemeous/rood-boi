//! zig_diff spec: func_800E7698 -> func_800E7698.func_800E7698 (7EE98.c).
//! Inputs: a radialMotionState with its context in the arena; the motion block
//! that the global D_800F5920 points at, with state 0, 1 and the default branch
//! and frame 0-30 (edges 13, 14, 15, 30 included); an actor that the global
//! table D_800F4538[0] points at, with its flags masked by 0x70000 set or clear
//! and the sign bit set or clear; and the table D_800F17DC at the chosen frame,
//! with its two entries zeroed in every combination. The callee func_800DEEFC
//! changes the state's targetY and sometimes the motion frame. func_800E2CCC
//! is a recording stub that does not touch its argument: the C returns right
//! after the call, so the call trace and the return value cover it. The trig
//! and angle calls return any 32-bit value.
//!
//! Build note: the checker reported that c_7EE98's movementRecoveryState embeds
//! an opaque type (vs_battle_movementPosition). That comes from the generated
//! binding, not from this spec. This revision removes every use of that struct
//! from the spec itself (the stub takes an untyped pointer and never
//! dereferences it). If the harness still references the type through the
//! callee's signature, the fix belongs in the generated c_7EE98 binding.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7EE98");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E7698));

pub const classes = [_][:0]const u8{
    "state-0",
    "state-1-idle",
    "state-1-busy",
    "state-1-pending",
    "state-other",
};

fn motionAt() *v.radialMotion {
    const slot: *?*v.D_800F5920_t = v.g.D_800F5920();
    return @ptrCast(@alignCast(slot.*.?));
}

pub const stubs = struct {
    pub fn func_800E2CCC(ctx: *zd.Ctx, p: anytype) void {
        _ = p;
        _ = ctx.value(u1);
    }

    pub fn func_800DEEFC(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = n;
        const s: *v.radialMotionState = @ptrCast(@alignCast(p));
        s.targetY = ctx.value(i32);
        if (ctx.value(u1) == 1) {
            const m = motionAt();
            m.frame = ctx.value(u8);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.radialMotionState);
    const context = ctx.alloc(v.radialMotionContext);
    const motion = ctx.alloc(v.radialMotion);
    const actor = ctx.alloc(v.radialMotionActor);

    psx.put(&state.context, context);
    state.x = ctx.pick(i16, &.{ 0, 0x7C0, 0x7C0 + 1, -1, 32767, -32768 });
    state.y = ctx.int(i16);
    state.z = if (ctx.chance(4)) state.x else ctx.pick(i16, &.{ 0, 0x7C0, -1, 32767, -32768 });
    state.field470 = ctx.int(i32);

    const slot: *?*v.D_800F5920_t = v.g.D_800F5920();
    slot.* = @ptrCast(@alignCast(motion));

    const table4: *[17]?*v.D_800F4538_t = v.g.D_800F4538();
    table4[0] = @ptrCast(@alignCast(actor));
    actor.flags = ctx.pick(u32, &.{ 0, 0x80000000, 0x10000, 0x20000, 0x40000, 0x70000, 0x80070000, 0xFFFF_FFFF });

    motion.facing = ctx.int(i16);
    motion.state = if (ctx.chance(4)) ctx.int(u8) else ctx.range(u8, 0, 1);
    motion.flag3 = ctx.int(u8);
    motion.flagA = ctx.int(u8);
    motion.angle = ctx.int(i16);
    motion.x = ctx.int(i16);
    motion.z = ctx.int(i16);
    const edges = [_]u8{ 0, 13, 14, 15, 30 };
    motion.frame = if (ctx.chance(2))
        edges[ctx.range(usize, 0, edges.len - 1)]
    else
        ctx.range(u8, 0, 30);

    // The table has 31 entries: the frame indexes it only up to 30.
    const tbl = v.g.D_800F17DC();
    switch (ctx.range(u8, 0, 3)) {
        0 => {
            tbl[motion.frame][0] = 0;
            tbl[motion.frame][1] = 0;
        },
        1 => {
            tbl[motion.frame][0] = 0;
        },
        2 => {
            tbl[motion.frame][1] = 0;
        },
        else => {},
    }

    return .{state};
}

pub fn classify(args: Args) usize {
    _ = args;
    const m = motionAt();
    if (m.state == 0) return 0;
    if (m.state != 1) return 4;
    const tbl = v.g.D_800F17DC();
    const f = m.frame;
    if (tbl[f][0] != 0 or tbl[f][1] != 0) return 3;
    const actor: *v.radialMotionActor = @ptrCast(@alignCast(v.g.D_800F4538()[0].?));
    return if ((actor.flags & 0x70000) != 0) 2 else 1;
}
