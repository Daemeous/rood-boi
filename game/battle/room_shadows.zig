//! The shadows of the room's moving platforms: pc/src/room_shadows.inc, compiled into
//! src/BATTLE/BATTLE.PRG/146C.c.
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// The scratchpad block, addressed by number as the C does.
fn scratch() *v.vs_scratch_t {
    return @ptrFromInt(0x1F800000);
}

const ShadowNative = *const fn (c_int, [*]const c_int, c_int, ?[*]c_uint, ?[*]c_uint) callconv(.c) ?[*]c_uint;
const ShadowsEnd = *const fn (?*const anyopaque, ?*const anyopaque) callconv(.c) void;

/// orig: func_8008CEB0 (src/BATTLE/BATTLE.PRG/146C.c)
/// Draws a flat subtractive square on the floor under each of the room's moving platforms (section 12) that is above the floor; the native renderer gets each square too.
pub fn func_8008CEB0() callconv(.c) void {
    // uninitialised in the original (the GTE reads the vector's pad word): zeroed
    var vertices = std.mem.zeroes([4]v.SVECTOR);
    var interpolation: c_int = undefined;
    var flags: c_int = undefined;

    c.SetRotMatrix(psx.cptr(c.MATRIX, &scratch().viewMatrix));
    c.SetTransMatrix(psx.cptr(c.MATRIX, &scratch().viewMatrix));

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
        if (c.func_8009195C(@as(c_int, psx.getBits(object, "unk0_0"))) == null) continue;
        const cell = c.func_8008B764(psx.conv(c_uint, @divTrunc(object.unkA8, 0x80000)), psx.conv(c_uint, @divTrunc(object.unkB0, 0x80000)), 0).?;
        const cell_view: *v._mpdRoomSection3 = @ptrCast(cell);
        if (psx.getBits(cell_view, "unk0_17") != 0 and psx.conv(c_uint, @as(c_int, psx.getBits(cell_view, "unk0_0")) -% 15) >= 4) continue;
        var x: c_int = @divTrunc(object.unkA8, 4096);
        var height: c_int = c.func_8008DC7C(x, @divTrunc(object.unkB0, 4096));
        height <<= 17; // sign-extends the 15-bit height
        height >>= 17;
        x = object.unkAC;
        var altitude: c_int = -%@divTrunc(x, 4096);
        if (altitude >= height) continue;
        altitude = -%@divTrunc(x, 4096);
        var shade: c_int = @divTrunc(height -% altitude, 2);
        if (shade >= 224) shade = 223;
        shade = 255 -% shade;
        var prim: *v.POLY_F4 = @ptrCast(@alignCast(psx.get(scratch().unk0).?));
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
        const depth = c.RotAverage4(
            psx.cptr(c.SVECTOR, &vertices[0]),
            psx.cptr(c.SVECTOR, &vertices[1]),
            psx.cptr(c.SVECTOR, &vertices[2]),
            psx.cptr(c.SVECTOR, &vertices[3]),
            @ptrCast(@alignCast(&prim.x0)),
            @ptrCast(@alignCast(&prim.x1)),
            @ptrCast(@alignCast(&prim.x2)),
            @ptrCast(@alignCast(&prim.x3)),
            &interpolation,
            &flags,
        );
        if (depth > 4 and depth < 2048) {
            const tag: *v.P_TAG = @ptrCast(prim);
            psx.setBits(tag, "len", 5); // setPolyF4
            tag.code = 0x28;
            tag.code = tag.code | 0x02; // setSemiTrans(prim, 1)
            prim.r0 = psx.conv(u8, @divTrunc(shade, 2) +% 16);
            prim.g0 = psx.conv(u8, @divTrunc(shade, 2) +% 16);
            prim.b0 = psx.conv(u8, @divTrunc(shade, 2) +% 16);
            c.AddPrim(otEntry(depth), prim);
            prim = psx.at(prim, 1);
            psx.put(&scratch().unk0, prim);
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
