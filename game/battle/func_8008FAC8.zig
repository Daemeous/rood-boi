//! Falling particle quads: projects each live particle, builds its quad from two projected points and links it into the ordering table (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_146C");
const c = @import("c_146C");
const sprite_item = @import("common").sprite_item;

fn xy(x: c_int, y: c_int) c_int {
    return (x & 0xFFFF) | (y << 16);
}

/// orig: func_8008FAC8 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Projects every live particle, builds its quad from two projected points and links it into the ordering table.
pub fn func_8008FAC8() callconv(.c) void {
    const scratch = v.g.vs_scratch();
    const view = gte.viewOf(&v.g.vs_view().viewMatrix, gte.screenCurrent());
    const base: ?*v.D_800F1DD4_t = v.g.vs_field_weatherParticles().*;
    var i: c_int = 0;
    while (i < @as(c_int, v.g.vs_field_weatherParticleCount().*)) : (i +%= 1) {
        const particle: *v.D_800F1DD4_t = psx.at(base.?, i);
        if (particle.unkC != 0) continue;
        if (v.g.vs_field_areRoomUpdatesPaused().* == 0) {
            particle.unk2 = psx.conv(i16, @as(c_int, particle.unk2) -% (@as(c_int, particle.unkF) *% v.g.vs_gametime_tickspeed().*));
            if (@as(c_int, particle.unk2) < -1024) c.func_8008FA14(psx.cptr(c.D_800F1DD4_t, particle));
        }
        var sxy1: c_int = 0;
        var sxy2: c_int = 0;
        var dx: c_int = undefined;
        var dy: c_int = undefined;
        v.g.pc_gte_anchored().* +%= 1;
        const first = gte.project(&view, particle);
        v.g.pc_gte_anchored().* -%= 1;
        sxy1 = first.sxy;
        var depth: c_int = @intCast(first.otz);
        const x1: c_int = psx.conv(i16, sxy1);
        const y1: c_int = psx.conv(i16, sxy1 >> 16);

        const widen: c_int = if (v.g.pc_gte_widescreen().* != 0) 54 else 0;
        const y1m: c_uint = psx.conv(c_uint, @as(c_int, psx.conv(u16, y1)) -% 1);
        if (y1m >= 239 or x1 <= (0 -% widen) or x1 >= (@as(c_int, 320) +% widen) or depth <= 64 or depth >= 2048) continue;

        particle.unk2 = psx.conv(i16, @as(c_int, particle.unk2) +% 192);
        v.g.pc_gte_anchored().* +%= 1;
        const second = gte.project(&view, particle);
        v.g.pc_gte_anchored().* -%= 1;
        sxy2 = second.sxy;
        depth = @intCast(second.otz);
        particle.unk2 = psx.conv(i16, @as(c_int, particle.unk2) -% 192);
        const x2: c_int = psx.conv(i16, sxy2);
        const y2: c_int = psx.conv(i16, sxy2 >> 16);

        dx = @divTrunc(x2 -% x1, 32);
        dy = @divTrunc(y2 -% y1, 32);
        // A semi-transparent shaded quad: a streak from the first point to the second, widened at its head.
        const ot: ?*anyopaque = @ptrFromInt(@intFromPtr(psx.get(scratch.unk4).?) +% @as(usize, @bitCast(@as(isize, depth) *% 4)));
        sprite_item.shaded(ot, 4, true, &[4]c_int{ 32 | (8 << 8) | (32 << 16), 192 | (64 << 8) | (64 << 16), 0, 32 | (8 << 8) | (32 << 16) }, &[4]c_int{
            xy(psx.conv(i16, x1 +% dx +% dy), psx.conv(i16, y1 +% dy -% dx)),
            sxy1,
            sxy2,
            xy(psx.conv(i16, x1 +% dx -% dy), psx.conv(i16, dx +% (y1 +% dy))),
        });
        _ = c.pc_insert_tpage(depth, 32);
    }
}
