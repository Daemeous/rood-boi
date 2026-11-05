//! Room group submission (D2: 2EA3C.s; oracle pc/src/room_geometry.c).
const c = @import("c_room_geometry");
const v = @import("views_room_geometry");
const psx = @import("common").psx;

const NativeBegin = *const fn (?*const anyopaque, c_uint, [*c]const c_uint) callconv(.c) c_int;
const NativeGroup = *const fn (?*const anyopaque, c_uint) callconv(.c) void;
const NativeEnd = *const fn ([*c]c_uint, [*c]c_uint) callconv(.c) [*c]c_uint;
const NativeGroupCut = *const fn (f32, f32, c_int) callconv(.c) void;
const LiveCameraActive = *const fn () callconv(.c) c_int;
const EditorFacing = *const fn () callconv(.c) c_uint;
const EditorHidden = *const fn (?*const anyopaque, c_uint) callconv(.c) c_int;
const WallsFrame = *const fn (?*const anyopaque, c_uint, c_uint) callconv(.c) c_int;
const WallsGroup = *const fn (c_uint, c_int, [*c]f32, [*c]f32, [*c]c_int) callconv(.c) c_int;
const OcclusionFrame = *const fn (c_int) callconv(.c) void;
const PhotoWhole = *const fn () callconv(.c) c_int;
const SceneRoomData = *const fn () callconv(.c) ?*const anyopaque;
const HwRoomPackets = *const fn (?*const anyopaque, ?*const anyopaque) callconv(.c) void;

/// orig: func_8009723C (pc/src/room_geometry.c)
/// Submits the room's groups (a count, then 0x40-byte groups) into the
/// packet buffer at scratchpad 0x1F800000, honouring the facing mask, the
/// hidden flags and the native-room, free-camera and occlusion hooks.
pub fn drawRoomGroups(data: [*]u8, mode: c_uint) callconv(.c) void {
    const scratch: [*]c_uint = @ptrFromInt(0x1f800000);
    var packet: [*]c_uint = @ptrFromInt(@as(usize, scratch[0]));
    const limit: [*]c_uint = @ptrFromInt(@intFromPtr(packet) +% 0x10000);
    const first = packet;
    const ot: [*]c_uint = @ptrFromInt(@as(usize, scratch[1]));
    const count: c_uint = @as(*align(1) const c_uint, @ptrCast(data)).*;
    var group: [*]u8 = data + 4;
    if (count > 4096) c.pc_unimplemented("room group count");
    const native = v.g.native();
    native.* = if (psx.weak(NativeBegin, "pc_room_native_begin")) |begin| begin(data, mode, first) else 0;
    v.g.translucent_only().* = @intFromBool(native.* == 2);
    const free_camera: c_int = @intFromBool(if (psx.weak(LiveCameraActive, "pc_live_camera_active")) |active| active() != 0 else false);
    const editor_facing = psx.weak(EditorFacing, "pc_room_editor_facing");
    const free_facing: c_uint = if (free_camera != 0 and editor_facing != null) editor_facing.?() else 0x10000;
    const photo_whole = psx.weak(PhotoWhole, "pc_room_photo_whole");
    const hidden_flags: c_uint = if (free_camera != 0 and photo_whole != null and photo_whole.?() != 0) 0x100 else 0x1100;
    const record = if (psx.weak(SceneRoomData, "pc_scene_room_data")) |room_data|
        @intFromPtr(data) == @intFromPtr(room_data()) and count <= 4096
    else
        false;
    if (record) {
        v.g.drawn_data().* = data;
        v.g.drawn_count().* = count;
        v.g.drawn_facing().* = if (free_camera != 0) free_facing else scratch[4];
        var i: c_uint = 0;
        while (i < count) : (i +%= 1) v.g.drawn_groups()[i] = 0; // count <= 4096 here
    }
    const smart = record and if (psx.weak(WallsFrame, "pc_gameplay_walls_frame")) |frame|
        frame(if (free_camera != 0) null else data, scratch[4], mode) != 0
    else
        false;
    var i: c_uint = 0;
    while (i < count) : ({
        i +%= 1;
        group += 0x40;
    }) {
        const flags: c_uint = @as(*align(1) const c_uint, @ptrCast(group)).*;
        var facing: c_uint = scratch[4];
        if (free_camera != 0) facing = free_facing;
        if (@intFromPtr(packet) >= @intFromPtr(limit)) break;
        if (flags & hidden_flags != 0 or ((flags >> 15) & mode & 1) != 0) continue;
        if (free_camera != 0) {
            if (psx.weak(EditorHidden, "pc_room_editor_hidden")) |hidden| {
                if (hidden(data, i) != 0) continue;
            }
        }
        const kept: c_int = @intFromBool((facing >> 16) != 0 or ((flags ^ 255) & facing & 255) == 0);
        var cut: f32 = 0;
        var cut_previous: f32 = 0;
        var ghost: c_int = 0;
        if (smart) {
            if (psx.weak(WallsGroup, "pc_gameplay_walls_group").?(i, kept, &cut, &cut_previous, &ghost) == 0) continue;
        } else if (kept == 0) continue;
        const gp: c_uint = (flags & 0xffff6000) | (if (flags & 0x800 != 0) @as(c_uint, 0) else 3);
        const matrix: [*c]c.MATRIX = @ptrCast(@alignCast(group + 0x20));
        c.SetRotMatrix(matrix);
        c.SetTransMatrix(matrix);
        const geometry_offset: c_uint = @as(*align(1) const c_uint, @ptrCast(group + 0x1c)).*;
        const geometry: [*c]c_uint = @ptrCast(@alignCast(data + geometry_offset));
        if (record) v.g.drawn_groups()[i] = 1; // record: count <= 4096
        if (native.* != 0) {
            if (psx.weak(NativeGroupCut, "pc_room_native_group_cut")) |group_cut| {
                if (cut > 0 or cut_previous > 0) group_cut(cut, cut_previous, ghost);
            }
            psx.weak(NativeGroup, "pc_room_native_group").?(group, gp);
        }
        packet = @ptrCast(@alignCast(c.pc_room_group_packets(geometry, packet, ot, gp, mode, limit)));
    }
    v.g.translucent_only().* = 0;
    if (record) {
        if (psx.weak(OcclusionFrame, "pc_occlusion_frame")) |frame| frame(free_camera);
    }
    if (native.* != 0) packet = @ptrCast(@alignCast(psx.weak(NativeEnd, "pc_room_native_end").?(packet, ot)));
    native.* = 0;
    scratch[0] = psx.addr(packet);
    if (psx.weak(HwRoomPackets, "pc_hw_room_packets")) |room_packets| room_packets(first, packet);
}
