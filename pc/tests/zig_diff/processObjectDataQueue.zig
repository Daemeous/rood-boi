//! zig_diff spec: vs_battle_processObjectDataQueue -> process_object_data_queue.processObjectDataQueue (30DB0.c).
//! Inputs: the first object slot's dataType over every switch case and the default
//! (type 0 returns before the second VSync, unknown types return -1), small
//! D_800F4580 / D_800E8FC4 values so the timing compare is often equal, and
//! _loadShpState at 2 or not. The loader stubs return -1, -2, 0, 1 and 7, write
//! _loadShpState, D_800E8FC4, D_800F4580 and the slot's dataType, so the C's
//! re-reads after each call are observable. vs_main_memcpy copies the bytes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").processObjectDataQueue));

pub const classes = [_][:0]const u8{ "type-0", "loader", "parser", "seq-etm", "unknown" };

/// A loader's return value: -1 (early return), -2 (negative), 0, 1 or 7.
fn result(ctx: *zd.Ctx) c_int {
    return switch (ctx.value(u8) % 5) {
        0 => -1,
        1 => -2,
        2 => 0,
        3 => 1,
        else => 7,
    };
}

/// What a loader can change: the load state, the timing globals, the slot's type.
fn loadStep(ctx: *zd.Ctx, slot: anytype) c_int {
    const s: *v.vs_battle_objectData = @ptrCast(@alignCast(slot.?));
    if (ctx.value(u8) < 96) v.g._loadShpState().* = if (ctx.value(u1) == 1) 2 else ctx.value(u8);
    if (ctx.value(u8) < 96) v.g.D_800E8FC4().* = @intCast(ctx.value(u8) % 4);
    if (ctx.value(u8) < 64) v.g.D_800F4580().* = @intCast(ctx.value(u8) % 4);
    if (ctx.value(u8) < 64) s.dataType = ctx.value(u8);
    return result(ctx);
}

pub const stubs = struct {
    pub fn VSync(ctx: *zd.Ctx, mode: anytype) c_int {
        _ = mode;
        return @intCast(ctx.value(u8) % 4);
    }

    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = ctx;
        const d: [*]u8 = @ptrCast(dest.?);
        const s: [*]const u8 = @ptrCast(src.?);
        std.mem.copyForwards(u8, d[0..@as(usize, sz)], s[0..@as(usize, sz)]);
    }

    pub fn _loadShp(ctx: *zd.Ctx, slot: anytype) c_int {
        return loadStep(ctx, slot);
    }
    pub fn _loadWep(ctx: *zd.Ctx, slot: anytype) c_int {
        return loadStep(ctx, slot);
    }
    pub fn func_8009BE5C(ctx: *zd.Ctx, slot: anytype) c_int {
        return loadStep(ctx, slot);
    }
    pub fn _parseShp(ctx: *zd.Ctx, slot: anytype) c_int {
        return loadStep(ctx, slot);
    }
    pub fn _parseWep(ctx: *zd.Ctx, slot: anytype) c_int {
        return loadStep(ctx, slot);
    }
    pub fn func_8009998C(ctx: *zd.Ctx, slot: anytype) c_int {
        return loadStep(ctx, slot);
    }
    pub fn _loadSeq(ctx: *zd.Ctx, slot: anytype) c_int {
        return loadStep(ctx, slot);
    }
    pub fn _loadEtm(ctx: *zd.Ctx, slot: anytype) c_int {
        return loadStep(ctx, slot);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const slots = v.g.vs_battle_objectDataSlots();
    slots[0].dataType = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 255 });
    v.g.D_800F4580().* = ctx.range(i32, -4, 4);
    v.g.D_800E8FC4().* = ctx.range(i32, -4, 4);
    v.g._loadShpState().* = ctx.edge(u8, &.{ 2, 2, 0, 1, 255 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return switch (v.g.vs_battle_objectDataSlots()[0].dataType) {
        0 => 0,
        1, 3, 7 => 1,
        2, 4, 6 => 2,
        8, 9 => 3,
        else => 4,
    };
}
