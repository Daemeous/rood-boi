//! The GTE's maths as values: the Zig side of pc/src/gte_value_core.h. A view (rotation, translation, projection) and a
//! vector in, what the registers would hold out, on a private register set (the SDK's PsyCross arithmetic, flags and
//! saturation); a register context of its own (`Ctx`) for code that works the coprocessor's registers directly as scratch;
//! the frame view the battle's camera owns. Nothing here reads or changes shared register state: there is none.

/// vs_gte_screen: OFX and OFY as the registers hold them (16.16), H, DQA, DQB.
pub const Screen = extern struct { ofx: c_int, ofy: c_int, h: c_int, dqa: c_int, dqb: c_int, zsf3: c_int, zsf4: c_int };

/// vs_gte_view: rotation, translation, projection.
pub const View = extern struct { rot: [3][3]i16, t: [3]c_int, screen: Screen };

/// vs_gte_result: the registers an operation writes (the others are zero).
pub const Result = extern struct {
    sxy: c_int,
    sx: i16,
    sy: i16,
    sz: c_uint,
    otz: c_uint,
    ir0: i16,
    ir: [3]i16,
    mac: [4]c_int,
    flag: c_uint,
};

/// vs_gte_result4: RotAverage4's results.
pub const Result4 = extern struct { sxy: [4]c_int, ir0: i16, flag: c_uint, otz: c_uint };

const RTPS: c_int = 0x0180001;
const RTV0TR: c_int = 0x0480012;
const RTV0: c_int = 0x0486012;

extern fn vs_gte_run(op: c_int, view: *const View, v: *const [3]i16, caller: ?*const anyopaque) callconv(.c) Result;
extern fn vs_gte_run4(view: *const View, v: *const [4][3]i16, caller: ?*const anyopaque) callconv(.c) Result4;
extern fn vs_gte_nclip(sxy0: c_uint, sxy1: c_uint, sxy2: c_uint) callconv(.c) c_int;
extern fn vs_gte_avsz3(zsf3: c_int, sz1: c_uint, sz2: c_uint, sz3: c_uint) callconv(.c) c_uint;
extern fn vs_gte_op0(r11: c_uint, r22: c_uint, r33: c_uint, ir1: c_uint, ir2: c_uint, ir3: c_uint, mac: *[3]c_int) callconv(.c) void;
extern fn SetGeomOffset(ofx: c_int, ofy: c_int) callconv(.c) void;
extern fn vs_gte_light_transform(llm: *const [5]c_uint, bk: *const [3]c_uint, v: *const [3]i16, ir: *[3]i16) callconv(.c) void;
extern fn vs_gte_ctx_init(ctx: *Ctx) callconv(.c) void;
extern fn vs_gte_mtc2(ctx: *Ctx, reg: c_int, value: c_uint) callconv(.c) void;
extern fn vs_gte_ctc2(ctx: *Ctx, reg: c_int, value: c_uint) callconv(.c) void;
extern fn vs_gte_mfc2(ctx: *Ctx, reg: c_int) callconv(.c) c_uint;
extern fn vs_gte_cfc2(ctx: *Ctx, reg: c_int) callconv(.c) c_uint;
extern fn vs_gte_cop2(ctx: *Ctx, op: c_int) callconv(.c) void;
extern fn vs_gte_apply_matrix_lv(m: *anyopaque, v0: *anyopaque, v1: *anyopaque) callconv(.c) void;
extern fn vs_gte_screen_current() callconv(.c) Screen;
extern fn pc_gte_last_projection(x: *f32, y: *f32) callconv(.c) void;

/// For a transform or a rotation, which do not project.
pub const no_screen: Screen = .{ .ofx = 0, .ofy = 0, .h = 0, .dqa = 0, .dqb = 0, .zsf3 = 0, .zsf4 = 0 };

/// The last `project`'s screen position before the registers cut it to whole pixels (x, y; narrowed as the registers are in widescreen).
pub fn lastProjection() [2]f32 {
    var p: [2]f32 = undefined;
    pc_gte_last_projection(&p[0], &p[1]);
    return p;
}

/// The projection the game has set so far (SetGeomOffset, SetGeomScreen).
pub fn screenCurrent() Screen {
    return vs_gte_screen_current();
}

/// The PS1 MATRIX layout: the rotation, a pad halfword, the translation at +20.
const Matrix = extern struct { m: [3][3]i16, pad: i16, t: [3]c_int };

/// SetRotMatrix and SetTransMatrix of a MATRIX (rotation at +0, translation at +20).
pub fn viewOf(m: anytype, screen: Screen) View {
    var view: View = undefined;
    const mat: *align(1) const Matrix = @ptrCast(m);
    view.rot = mat.m;
    view.t = mat.t;
    view.screen = screen;
    return view;
}

/// SetRotMatrix of a MATRIX with the translation zero (gte_zrtr).
pub fn viewRotation(m: anytype, screen: Screen) View {
    var view = viewOf(m, screen);
    view.t = .{ 0, 0, 0 };
    return view;
}

/// RotTrans: the vector rotated and translated (MAC1-3).
pub inline fn transform(view: *const View, vec: anytype) Result {
    return vs_gte_run(RTV0TR, view, @ptrCast(vec), @ptrFromInt(@returnAddress()));
}

/// ApplyRotMatrix, ApplyMatrix: the vector rotated (MAC1-3, IR1-3).
pub inline fn rotate(view: *const View, vec: anytype) Result {
    return vs_gte_run(RTV0, view, @ptrCast(vec), @ptrFromInt(@returnAddress()));
}

/// RotTransPers: the vector projected (sxy, sz, otz, ir0).
pub inline fn project(view: *const View, vec: anytype) Result {
    return vs_gte_run(RTPS, view, @ptrCast(vec), @ptrFromInt(@returnAddress()));
}

/// RotTrans stored as a vector (gte_stlvnl): MAC1-3, the flags dropped.
pub inline fn transformTo(out: anytype, view: *const View, vec: anytype) void {
    const r = vs_gte_run(RTV0TR, view, @ptrCast(vec), @ptrFromInt(@returnAddress()));
    storeVector(out, r);
}

/// ApplyMatrix, ApplyRotMatrix stored as a vector.
pub inline fn rotateTo(out: anytype, view: *const View, vec: anytype) void {
    const r = vs_gte_run(RTV0, view, @ptrCast(vec), @ptrFromInt(@returnAddress()));
    storeVector(out, r);
}

fn storeVector(out: anytype, r: Result) void {
    const v: *align(1) [3]c_int = @ptrCast(out);
    v.* = r.mac[1..4].*;
}

/// NCLIP of three SXY words: MAC0, the sign of the screen triangle's area.
pub fn nclipOf(a: c_uint, b: c_uint, third: c_uint) c_int {
    return vs_gte_nclip(a, b, third);
}

/// AVSZ3 of three depth words, scaled by the game's current ZSF3 (OTZ as MFC2 reads it).
pub fn avsz3Of(a: c_uint, b: c_uint, third: c_uint) c_uint {
    return vs_gte_avsz3(vs_gte_screen_current().zsf3, a, b, third);
}

/// A register context of its own (vs_gte_ctx): for code that works the coprocessor's registers directly as scratch.
pub const Ctx = extern struct {
    bytes: [1024]u8 align(16),

    pub fn init() Ctx {
        var ctx: Ctx = undefined;
        vs_gte_ctx_init(&ctx);
        return ctx;
    }
    pub fn mtc2(ctx: *Ctx, reg: c_int, value: c_uint) void {
        vs_gte_mtc2(ctx, reg, value);
    }
    pub fn ctc2(ctx: *Ctx, reg: c_int, value: c_uint) void {
        vs_gte_ctc2(ctx, reg, value);
    }
    pub fn mfc2(ctx: *Ctx, reg: c_int) c_uint {
        return vs_gte_mfc2(ctx, reg);
    }
    pub fn cfc2(ctx: *Ctx, reg: c_int) c_uint {
        return vs_gte_cfc2(ctx, reg);
    }
    pub fn cop2(ctx: *Ctx, op: c_int) void {
        vs_gte_cop2(ctx, op);
    }
};

/// SetGeomOffset: the screen offset in pixels (the registers and the screen record).
pub fn setGeomOffset(ofx: c_int, ofy: c_int) void {
    SetGeomOffset(ofx, ofy);
}

/// MVMVA through the light matrix with the background colour as the translation (the raw CTC2 register values: the matrix's
/// five words, the colour's three); IR1-3 of the vector.
pub fn lightTransform(llm: *const [5]c_uint, bk: *const [3]c_uint, vec: anytype) [3]i16 {
    var ir: [3]i16 = undefined;
    const v: [3]i16 = @as(*align(1) const [3]i16, @ptrCast(vec)).*;
    vs_gte_light_transform(llm, bk, &v, &ir);
    return ir;
}

/// OP: the outer product of the rotation diagonal (raw CTC2 values) and IR (raw MTC2 values), MAC1-3.
pub fn op0Of(r11: c_uint, r22: c_uint, r33: c_uint, ir1: c_uint, ir2: c_uint, ir3: c_uint) [3]c_int {
    var mac: [3]c_int = undefined;
    vs_gte_op0(r11, r22, r33, ir1, ir2, ir3, &mac);
    return mac;
}

/// ApplyMatrixSV: the vector rotated by a MATRIX's rotation, IR1-3 stored as a short vector.
pub inline fn applyMatrixSV(out: anytype, m: anytype, vec: anytype) void {
    const view = viewOf(m, no_screen);
    const r = vs_gte_run(RTV0, &view, @ptrCast(vec), @ptrFromInt(@returnAddress()));
    const o: *align(1) [3]i16 = @ptrCast(out);
    o.* = r.ir;
}

/// ApplyMatrixLV: a long vector rotated by a MATRIX's rotation (v0 and v1 may be the same).
pub fn applyMatrixLV(m: anytype, v0: anytype, v1: anytype) void {
    vs_gte_apply_matrix_lv(@ptrCast(m), @ptrCast(v0), @ptrCast(v1));
}

/// ApplyMatrix: the vector rotated by a MATRIX's rotation, MAC1-3 stored as a vector.
pub inline fn applyMatrix(out: anytype, m: anytype, vec: anytype) void {
    const view = viewOf(m, no_screen);
    rotateTo(out, &view, vec);
}

/// RotAverage4: the four vectors projected (the first three together, as RTPT) and their depths averaged.
pub inline fn average4(view: *const View, a: anytype, b: anytype, cc: anytype, d: anytype) Result4 {
    const vs = [4][3]i16{ @as(*align(1) const [3]i16, @ptrCast(a)).*, @as(*align(1) const [3]i16, @ptrCast(b)).*, @as(*align(1) const [3]i16, @ptrCast(cc)).*, @as(*align(1) const [3]i16, @ptrCast(d)).* };
    return vs_gte_run4(view, &vs, @ptrFromInt(@returnAddress()));
}

/// The scene camera's view for the frame (vs_frame_view, defined in 146C.c beside vs_view): its rotation and
/// translation are what the battle frame sets (SetRotMatrix, SetTransMatrix of vs_view.viewMatrix); the projection
/// is the game's current one (SetGeomOffset, SetGeomScreen), read when the view is taken, so a later offset
/// change (the area map's) is seen.
const vs_frame_view = @import("psx.zig").Global(View, "vs_frame_view");

/// The camera sets the frame's view from a MATRIX (what the registers' SetRotMatrix and SetTransMatrix were).
pub fn setFrame(m: anytype) void {
    const mat: *align(1) const Matrix = @ptrCast(m);
    vs_frame_view.ptr().rot = mat.m;
    vs_frame_view.ptr().t = mat.t;
}

/// The frame's view with the current projection.
pub fn frame() View {
    var view = vs_frame_view.ptr().*;
    view.screen = vs_gte_screen_current();
    return view;
}
