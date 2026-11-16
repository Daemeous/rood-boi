//! Room-driven scene effects: drifts the room's effect values and runs the scene effect selected by vs_field_roomWeatherKind (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;

/// The packet cursor.
fn scratchPointer() ?*const anyopaque {
    return common.scratch.get().unk0;
}

/// orig: vs_field_updateRoomWeather (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Drifts the room's effect values on their timers, then runs the scene effect selected by vs_field_roomWeatherKind.
pub fn vs_field_updateRoomWeather() callconv(.c) void {
    var masks = [4]c_int{ 0xF, 0x1F, 0x3F, 0x7F };
    const room = psx.get(v.g.vs_battle_roomData().sectionB);

    if (room != null and v.g.vs_field_roomWeatherKind().* != 0) {
        const r = room.?;
        const d = v.g.D_800F1D98();

        if (v.g.vs_field_areRoomUpdatesPaused().* == 0 and v.g.vs_field_isWeatherVariationEnabled().* != 0) {
            var changed: c_int = 0;
            if ((@as(c_int, v.g.vs_field_roomAnimationTicks().*) & psx.at(&masks, r.unk3A).*) == 0) {
                if ((c.rand() & 7) < 3) {
                    d[7] = psx.conv(i16, @as(c_int, d[7]) *% -1);
                }
                d[5] = psx.conv(i16, @as(c_int, d[5]) +% d[7]);
                d[5] = psx.conv(i16, @as(c_int, d[5]) & 0xFFF);
                d[5] = psx.conv(i16, c.vs_battle_clamp(d[5], @as(c_int, r.unk36), @as(c_int, r.unk38)));
                changed = 1;
            }
            if (((@as(c_int, v.g.vs_field_roomAnimationTicks().*) +% 30) & psx.at(&masks, r.unk46).*) == 0) {
                if ((c.rand() & 7) < 3) {
                    d[9] = psx.conv(i16, @as(c_int, d[9]) *% -1);
                }
                d[4] = psx.conv(i16, @as(c_int, d[4]) +% d[9]);
                d[4] = psx.conv(i16, c.vs_battle_clamp(d[4], @as(c_int, r.unk42), @as(c_int, r.unk44)));
                changed = 1;
            }
            if (((@as(c_int, v.g.vs_field_roomAnimationTicks().*) +% 60) & psx.at(&masks, r.unk40).*) == 0) {
                if ((c.rand() & 7) < 3) {
                    d[8] = psx.conv(i16, @as(c_int, d[8]) *% -1);
                }
                v.g.vs_field_weatherIntensity().* = psx.conv(i16, @as(c_int, v.g.vs_field_weatherIntensity().*) +% v.g.D_800F1DA8().*);
                v.g.vs_field_weatherIntensity().* = psx.conv(i16, c.vs_battle_clamp(v.g.vs_field_weatherIntensity().*, @as(c_int, r.unk3C), @as(c_int, r.unk3E)));
                c.vs_field_applyWeatherIntensity();
                changed = 1;
            }
            if (changed != 0) {
                c.func_80090B28();
            }
        }

        switch (@as(c_int, v.g.vs_field_roomWeatherKind().*)) {
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
