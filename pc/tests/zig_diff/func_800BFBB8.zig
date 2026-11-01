//! zig_diff spec: func_800BFBB8 -> func_800BFBB8.func_800BFBB8 (573B8.c).
//! Inputs: a script of 1-6 two-byte instructions in the arena, each an
//! opcode whose table entry is one of six spec opcodes: "next" (returns 0:
//! the C advances by func_800BFE34, whose stub returns the instruction's
//! length byte), "jump" (returns the guest address two bytes on: the
//! default case), and the four that end the run (1 wait-in-place, 2 yield,
//! 3 end the script, 4 advance and stop). The last instruction always ends
//! the run. Both tables are filled: the scene table D_800F4C28 maps the
//! ending opcodes to other ones (3<->4, 1<->2), so a run through the wrong
//! table differs. The START skip: arg1 0 or not, D_800F4C2C 0, 1, 2, the
//! START bit and D_800F4C68 each on or off, D_800F4C20 on or off. Each
//! opcode logs arg1 and its kind into an arena word.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_573B8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BFBB8));

/// The relocation check's message is a string literal: compared by its text.
pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 1 and std.mem.eql(u8, call, "pc_require_native_callback");
}

pub const classes = [_][:0]const u8{ "skip", "scene-table", "main-table" };

const Op = *const fn ([*c]u8, c_short) callconv(.c) c_int;

var sink: *[8]i32 = undefined;

fn log(kind: i32, arg1: c_short) void {
    sink[@intCast(kind)] +%= @as(i32, arg1) +% 1;
}

fn opNext(s: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = s;
    log(0, arg1);
    return 0;
}
fn opWait(s: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = s;
    log(1, arg1);
    return 1;
}
fn opYield(s: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = s;
    log(2, arg1);
    return 2;
}
fn opEnd(s: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = s;
    log(3, arg1);
    return 3;
}
fn opStep(s: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = s;
    log(4, arg1);
    return 4;
}
fn opJump(s: [*c]u8, arg1: c_short) callconv(.c) c_int {
    log(5, arg1);
    return @bitCast(psx.addr(s + 2));
}

const ops = [_]Op{ opNext, opWait, opYield, opEnd, opStep, opJump };
/// The scene table's kind for each main-table kind.
const scene_kind = [_]usize{ 0, 2, 1, 4, 3, 5 };

pub const stubs = struct {
    pub fn func_800BFE34(ctx: *zd.Ctx, script: anytype) c_int {
        _ = ctx;
        const s: [*c]const u8 = @ptrCast(script);
        return s[1];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    sink = ctx.alloc([8]i32);
    const table = psx.globalMany(?Op, "_opcodeFunctionTable");
    const scene: [*]?Op = @ptrCast(@alignCast(ctx.allocBytes(256 * @sizeOf(?Op), 16)));
    psx.global(?[*]?Op, "D_800F4C28").* = scene;

    const count = ctx.range(usize, 1, 6);
    const script: [*]u8 = ctx.allocBytes(2 * count + 2, 16);
    var k: usize = 0;
    while (k < count) : (k += 1) {
        const kind: usize = if (k + 1 == count) ctx.range(usize, 1, 4) else if (ctx.chance(2)) 0 else 5;
        const opcode: u8 = @intCast(kind * 40 + ctx.range(usize, 0, 39));
        table[opcode] = ops[kind];
        scene[opcode] = ops[scene_kind[kind]];
        script[2 * k] = opcode;
        script[2 * k + 1] = 2;
    }
    const holder = ctx.alloc([*c]u8);
    holder.* = script;

    v.g.D_800F4C2C().* = ctx.edge(u8, &.{ 0, 1, 1, 2 });
    v.g.vs_main_buttonsPressed().all = if (ctx.chance(2)) 1 << 11 else ctx.int(u32) & ~@as(u32, 1 << 11);
    v.g.D_800F4C68().* = if (ctx.chance(2)) 0 else ctx.int(u8) | 1;
    v.g.D_800F4C20().* = if (ctx.chance(2)) 0 else ctx.int(u8) | 1;
    const arg1: c_short = if (ctx.chance(2)) 0 else ctx.int(c_short);
    return .{ @ptrCast(holder), arg1 };
}

pub fn classify(args: Args) usize {
    const flag = v.g.D_800F4C2C().*;
    if (args[1] == 0 and flag == 1 and ((v.g.vs_main_buttonsPressed().all & (1 << 11)) != 0 or v.g.D_800F4C68().* != 0)) return 0;
    return if (flag == 2) 1 else 2;
}
