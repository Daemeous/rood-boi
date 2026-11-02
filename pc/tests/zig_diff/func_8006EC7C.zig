//! zig_diff spec: func_8006EC7C -> func_8006EC7C.func_8006EC7C (146C.c).
//! Inputs: random D-pad bits (each of the four alone too); FP_INVERT_Y 0 or
//! 1; the pad an analog one (mode 7) or not, its right stick at the dead
//! zone's edges (63, 64, 191, 192 and the extremes) on both axes; the PC
//! stick hook's yaw and pitch steps (its stub writes them: 0, small, large,
//! negative); the yaw and pitch near the wrap and the +-896 clamp; actor
//! 0's position (func_800A1108's stub) at the 16-bit edges.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8006EC7C));

pub const classes = [_][:0]const u8{ "digital", "analog" };

var steps: [2]c_int = .{ 0, 0 };
var point: [3]i16 = .{ 0, 0, 0 };

pub const stubs = struct {
    pub fn pc_camera_stick_first_person(_: *zd.Ctx, yaw: anytype, pitch: anytype) void {
        yaw[0] = steps[0];
        pitch[0] = steps[1];
    }
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const actor: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        actor.unk0.unk4.vx = point[0];
        actor.unk0.unk4.vy = point[1];
        actor.unk0.unk4.vz = point[2];
        return ctx.value(c_int);
    }
};

fn stick(ctx: *zd.Ctx) u8 {
    return ctx.pick(u8, &.{ 0, 1, 63, 64, 65, 127, 128, 129, 191, 192, 193, 255 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.vs_main_buttonsPreviousState().* = if (ctx.chance(2)) ctx.int(i32) else ctx.edge(i32, &.{ 0, 1 << 12, 1 << 13, 1 << 14, 1 << 15 });
    psx.global(c_int, "pc_qol_fp_invert_y").* = @intFromBool(ctx.chance(2));
    const port = &v.g._portInfo()[0];
    port.mode = if (ctx.chance(2)) 7 else ctx.edge(u8, &.{ 4, 5, 0x73 });
    port.rStickX = stick(ctx);
    port.rStickY = stick(ctx);
    steps[0] = ctx.pick(c_int, &.{ 0, 0, 1, -1, 4096, -5000 });
    steps[1] = ctx.pick(c_int, &.{ 0, 0, 1, -1, 4096, -5000 });
    const camera = &v.g.vs_battle_cameraCurrentSpherical().values;
    camera.yaw = ctx.pick(i32, &.{ 0, 31, 32, 4095, 4064, -4095, -32 });
    camera.pitch = ctx.pick(i32, &.{ 0, 896, 897, 864, 865, -896, -897, -864, 4095, -4095 });
    for (&point) |*p| p.* = ctx.pick(i16, &.{ 0, 180, -1, 0x7FFF, -0x8000 });
    return .{};
}

pub fn classify(_: Args) usize {
    return if (v.g._portInfo()[0].mode == 7) 1 else 0;
}
