//! Scrolls an actor's animated texture parts: moves each part's source window
//! inside its bounds (wrapping or bouncing) and queues a move packet
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_battlePacketBegin (src/BATTLE/BATTLE.PRG/40564.c)
/// Reads the packet cursor from scratchpad slot 0 (a file-scope static inline).
fn packetBegin() *v.DR_MOVE {
    const p: u32 = @as(*const u32, @ptrFromInt(0x1F800000)).*;
    return @ptrFromInt(@as(usize, p));
}

/// orig: vs_battlePacketEnd (src/BATTLE/BATTLE.PRG/40564.c)
/// Stores the packet cursor past `p` into scratchpad slot 0 (a file-scope static inline).
fn packetEnd(p: *v.DR_MOVE) void {
    @as(*u32, @ptrFromInt(0x1F800000)).* = psx.addr(psx.at(p, 1));
}

/// orig: func_800AB4F0 (src/BATTLE/BATTLE.PRG/40564.c)
/// Steps the four scrolling texture parts of `actor` and copies each moved window with a move packet.
pub fn func_800AB4F0(actor: ?*anyopaque) callconv(.c) void {
    const base: [*]u8 = @ptrCast(actor.?);
    const page: usize = base[0x5BB];
    const table = v.g.D_800E8F30();
    const originX: c_int = @as(c_int, @as(*align(1) const i16, @ptrCast(table + page * 4)).*) << 6;
    const originY: c_int = (@as(c_int, @as(*align(1) const i16, @ptrCast(table + page * 4 + 2)).*) << 8) +%
        (@as(c_int, v.g.D_800E8F90()[page]) << 6);
    // Uninitialised in the original (the caller's register): 0 here, so the
    // first part's `bounds[2] != height` holds, as it does unless that register
    // happens to equal the part's width.
    var height: c_int = 0;
    var i: usize = 0;
    while (i < 4) : (i += 1) {
        const bounds = base + 0x61C + i * 8;
        if (bounds[2] != 0) {
            const state = base + 0x181C + i * 8;
            state[5] = psx.conv(u8, @as(c_int, state[5]) +% @divTrunc(v.g.vs_gametime_tickspeed().*, 2));
            if (state[4] < state[5]) {
                state[5] = 0;
                const part = base + 0x5F4 + i * 4;
                if (bounds[2] != 0 and @as(c_int, bounds[2]) != height) {
                    var width: c_int = part[2];
                    const px: c_int = state[0];
                    const dx: c_int = @as(i8, @bitCast(state[2]));
                    const py: c_int = state[1];
                    const dy: c_int = @as(i8, @bitCast(state[3]));
                    const sx: c_int = bounds[0];
                    height = part[3];
                    var x: c_int = px +% dx;
                    var y: c_int = py +% dy;
                    const limitX: c_int = sx +% bounds[2];
                    const limitY: c_int = @as(c_int, bounds[1]) +% bounds[3];
                    if (limitX < x +% width or x < sx) {
                        if (bounds[6] & 1 != 0) {
                            x -%= dx *% 2;
                            state[2] = psx.conv(u8, -%@as(c_int, state[2]));
                        } else x = sx;
                    }
                    if (limitY < y +% height or y < @as(c_int, bounds[1])) {
                        if (bounds[6] & 1 != 0) {
                            y -%= @as(c_int, @as(i8, @bitCast(state[3]))) *% 2;
                            state[3] = psx.conv(u8, -%@as(c_int, state[3]));
                        } else y = bounds[1];
                    }
                    state[0] = psx.conv(u8, x);
                    state[1] = psx.conv(u8, y);
                    var destX: c_int = undefined;
                    if (@as(*align(1) const c_int, @ptrCast(base + 0x5AC)).* & 0x8000000 == 0) {
                        x = @divTrunc(x, 4);
                        width = @divTrunc(width, 4);
                        destX = @as(c_int, part[0]) >> 2;
                    } else {
                        x = @divTrunc(x, 2);
                        width = @divTrunc(width, 2);
                        destX = @as(c_int, part[0]) >> 1;
                    }
                    var rect: v.RECT = undefined;
                    rect.x = psx.conv(i16, originX +% x);
                    const packet = packetBegin();
                    rect.y = psx.conv(i16, originY +% y);
                    rect.w = psx.conv(i16, width);
                    rect.h = psx.conv(i16, height);
                    c.SetDrawMove(psx.cptr(c.DR_MOVE, packet), psx.cptr(c.RECT, &rect), originX +% destX, originY +% part[1]);
                    c.func_8007A824(psx.cptr(c.DR_MOVE, packet));
                    packetEnd(packet);
                }
            }
        }
    }
}
