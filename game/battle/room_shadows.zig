//! The shadows of the room's moving platforms: pc/src/room_shadows.inc, compiled into
//! pc/src/game/BATTLE/BATTLE.PRG/146C.c.
const std = @import("std");
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_146C");
const c = @import("c_146C");
const sprite_item = @import("common").sprite_item;

/// The scene's view.
fn view() *v.vs_view_t {
    return v.g.vs_view();
}

fn scratch() *v.vs_scratch_t {
    return v.g.vs_scratch();
}

const ShadowNative = *const fn (c_int, [*]const c_int, c_int, ?[*]c_uint, ?[*]c_uint) callconv(.c) ?[*]c_uint;
const ShadowsEnd = *const fn (?*const anyopaque, ?*const anyopaque) callconv(.c) void;

/// orig: vs_field_drawMovingPlatformShadows (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Draws a flat subtractive square on the floor under each of the room's moving platforms (section 12) that is above the floor; the native renderer gets each square too.
pub fn vs_field_drawMovingPlatformShadows() callconv(.c) void {
    // uninitialised in the original (the GTE reads the vector's pad word): zeroed
    var vertices = std.mem.zeroes([4]v.SVECTOR);

    const camera = gte.viewOf(&view().viewMatrix, gte.screenCurrent());

    if (psx.get(v.g.vs_battle_roomData().section12) == null) return;
    var object = psx.get(v.g.vs_battle_roomData().section12).?;
    const packets_start = psx.get(scratch().unk0);

    const count: c_int = psx.conv(c_int, @as(usize, v.g.vs_battle_roomData().header.section12Len) / @sizeOf(v._mpdRoomSection12));
    var i: c_int = 0;
    while (i < count) : ({
        i +%= 1;
        object = psx.at(object, 1);
    }) {
        if (psx.getBits(object, "unk0_9") != 0) continue;
        if (c.vs_field_findRoomGeometryGroup(@as(c_int, psx.getBits(object, "unk0_0"))) == null) continue;
        const cell = c.vs_field_collisionCellAt(psx.conv(c_uint, @divTrunc(object.unkA8, 0x80000)), psx.conv(c_uint, @divTrunc(object.unkB0, 0x80000)), 0).?;
        const cell_view: *v._mpdRoomSection3 = @ptrCast(cell);
        if (psx.getBits(cell_view, "unk0_17") != 0 and psx.conv(c_uint, @as(c_int, psx.getBits(cell_view, "unk0_0")) -% 15) >= 4) continue;
        var x: c_int = @divTrunc(object.unkA8, 4096);
        var height: c_int = c.vs_field_floorHeightAt(x, @divTrunc(object.unkB0, 4096));
        height <<= 17; // sign-extends the 15-bit height
        height >>= 17;
        x = object.unkAC;
        var altitude: c_int = -%@divTrunc(x, 4096);
        if (altitude >= height) continue;
        altitude = -%@divTrunc(x, 4096);
        var shade: c_int = @divTrunc(height -% altitude, 2);
        if (shade >= 224) shade = 223;
        shade = 255 -% shade;
        const half: c_int = 32 +% @divTrunc(shade, 8);
        vertices[0].vx = psx.conv(i16, @divTrunc(object.unkA8, 4096) -% half);
        vertices[0].vy = psx.conv(i16, height);
        vertices[0].vz = psx.conv(i16, @divTrunc(object.unkB0, 4096) -% half);
        vertices[1].vx = psx.conv(i16, @divTrunc(object.unkA8, 4096) +% half);
        vertices[1].vy = psx.conv(i16, height);
        vertices[1].vz = psx.conv(i16, @divTrunc(object.unkB0, 4096) -% half);
        vertices[2].vx = psx.conv(i16, @divTrunc(object.unkA8, 4096) -% half);
        vertices[2].vy = psx.conv(i16, height);
        vertices[2].vz = psx.conv(i16, @divTrunc(object.unkB0, 4096) +% half);
        vertices[3].vx = psx.conv(i16, @divTrunc(object.unkA8, 4096) +% half);
        vertices[3].vy = psx.conv(i16, height);
        vertices[3].vz = psx.conv(i16, @divTrunc(object.unkB0, 4096) +% half);
        const average = gte.average4(&camera, &vertices[0], &vertices[1], &vertices[2], &vertices[3]);
        const depth: c_int = @intCast(average.otz);
        if (depth > 4 and depth < 2048) {
            // The square: a flat semi-transparent quad (setPolyF4, setSemiTrans) of the four screen words, a draw item at the slot of its depth.
            const grey: c_int = @divTrunc(shade, 2) +% 16;
            const xy = [4]c_int{ average.sxy[0], average.sxy[1], average.sxy[2], average.sxy[3] };
            sprite_item.flat(otEntry(depth), 4, true, (grey & 0xFF) | ((grey & 0xFF) << 8) | ((grey & 0xFF) << 16), &xy);
            c.pc_insert_tpage(depth, 64);
            if (psx.weak(ShadowNative, "pc_platform_shadow_native")) |native| {
                const centre = [3]c_int{ @divTrunc(object.unkA8, 4096), height, @divTrunc(object.unkB0, 4096) };
                const end = native(i, &centre, shade, @ptrCast(@alignCast(otEntry(depth))), @ptrCast(@alignCast(psx.get(scratch().unk0))));
                psx.put(&scratch().unk0, @as(?*anyopaque, @ptrCast(end)));
            }
        }
    }
    if (psx.weak(ShadowsEnd, "pc_platform_shadows_end")) |end_hook| end_hook(packets_start, psx.get(scratch().unk0));
}

/// `(u_long*)ot + depth`: the ordering table entry `depth` words in.
fn otEntry(depth: c_int) ?*anyopaque {
    const base: usize = @intFromPtr(psx.get(scratch().unk4));
    return @ptrFromInt(base +% @as(usize, @bitCast(@as(isize, depth) *% 4)));
}
