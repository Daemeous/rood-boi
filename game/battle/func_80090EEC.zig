//! Room-driven scene effects: drifts the room's effect values and runs the scene effect selected by D_800F1DCA (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;

/// The scratchpad word at 0x1F800000 (the packet cursor), as a guest pointer.
fn scratchPointer() ?*const anyopaque {
    return @ptrFromInt(@as(usize, @as(*const u32, @ptrFromInt(0x1F800000)).*));
}

/// orig: func_80090EEC (src/BATTLE/BATTLE.PRG/2842C.c)
/// Drifts the room's effect values on their timers, then runs the scene effect selected by D_800F1DCA.
pub fn func_80090EEC() callconv(.c) void {
    var masks = [4]c_int{ 0xF, 0x1F, 0x3F, 0x7F };
    const room = psx.get(v.g.vs_battle_roomData().sectionB);

    if (room != null and v.g.D_800F1DCA().* != 0) {
        const r = room.?;
        const d = v.g.D_800F1D98();

        if (v.g.D_800F1BA4().* == 0 and v.g.D_800F1DCB().* != 0) {
            var changed: c_int = 0;
            if ((@as(c_int, v.g.D_800F1CDC().*) & psx.at(&masks, r.unk3A).*) == 0) {
                if ((c.rand() & 7) < 3) {
                    d[7] = psx.conv(i16, @as(c_int, d[7]) *% -1);
                }
                d[5] = psx.conv(i16, @as(c_int, d[5]) +% d[7]);
                d[5] = psx.conv(i16, @as(c_int, d[5]) & 0xFFF);
                d[5] = psx.conv(i16, c.vs_battle_clamp(d[5], @as(c_int, r.unk36), @as(c_int, r.unk38)));
                changed = 1;
            }
            if (((@as(c_int, v.g.D_800F1CDC().*) +% 30) & psx.at(&masks, r.unk46).*) == 0) {
                if ((c.rand() & 7) < 3) {
                    d[9] = psx.conv(i16, @as(c_int, d[9]) *% -1);
                }
                d[4] = psx.conv(i16, @as(c_int, d[4]) +% d[9]);
                d[4] = psx.conv(i16, c.vs_battle_clamp(d[4], @as(c_int, r.unk42), @as(c_int, r.unk44)));
                changed = 1;
            }
            if (((@as(c_int, v.g.D_800F1CDC().*) +% 60) & psx.at(&masks, r.unk40).*) == 0) {
                if ((c.rand() & 7) < 3) {
                    d[8] = psx.conv(i16, @as(c_int, d[8]) *% -1);
                }
                v.g.D_800F1DC4().* = psx.conv(i16, @as(c_int, v.g.D_800F1DC4().*) +% v.g.D_800F1DA8().*);
                v.g.D_800F1DC4().* = psx.conv(i16, c.vs_battle_clamp(v.g.D_800F1DC4().*, @as(c_int, r.unk3C), @as(c_int, r.unk3E)));
                c.func_8009121C();
                changed = 1;
            }
            if (changed != 0) {
                c.func_80090B28();
            }
        }

        switch (@as(c_int, v.g.D_800F1DCA().*)) {
            1 => c.func_8008F440(),
            2 => {
                const start = scratchPointer();
                c.func_8008FDC4();
                if (psx.weak(Anchor, "pc_hw_anchor_packets")) |hook| {
                    hook(start, scratchPointer(), psx.conv(c_int, c.PC_ANCHOR_SCENE));
                }
            },
            3 => {
                const start = scratchPointer();
                c.func_80090434();
                if (psx.weak(Anchor, "pc_hw_anchor_packets")) |hook| {
                    hook(start, scratchPointer(), psx.conv(c_int, c.PC_ANCHOR_COVER));
                }
            },
            4 => {
                const start = scratchPointer();
                c.func_8008FAC8();
                if (psx.weak(Anchor, "pc_hw_anchor_packets")) |hook| {
                    hook(start, scratchPointer(), psx.conv(c_int, c.PC_ANCHOR_SCENE));
                }
            },
            else => {},
        }
    }
}
