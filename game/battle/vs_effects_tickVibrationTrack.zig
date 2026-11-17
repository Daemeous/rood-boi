//! An effect command: plays a sound at its frame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickVibrationTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame: plays block 11's sound (vs_input_startVibrationPattern) when its target condition holds (0: always; 1, 2: the unk0 / unk18 target is actor 0; 3: any of the P-file's sound targets is), then the next command; else 1.
pub fn vs_effects_tickVibrationTrack(actor_arg: ?*v.vs_effects_EffectInstance, event_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const actor = actor_arg.?;
    const event = event_arg.?;
    var result: c_int = 1;
    const data: [*]u16 = @ptrCast(@alignCast(psx.get(event.keys).?));
    const packed_word: u16 = psx.at(data, event.keyIndex).*;
    var play = false;
    if (@as(c_int, packed_word & 511) != psx.get(actor.context.unk30).?.frame) {
        return result;
    }
    switch (packed_word >> 14) {
        1 => {
            if (actor.context.unk0.unk0 == 4 and psx.conv(u8, actor.context.unk0.unk4.unk0) == 0) play = true;
        },
        2 => {
            if (actor.context.unk18.unk0 == 4 and psx.conv(u8, actor.context.unk18.unk4.unk0) == 0) play = true;
        },
        0 => play = true,
        3 => {
            const targets: *v.soundEventTargets = @ptrCast(@alignCast(psx.get(v.g.vs_effects_package().*.?.unkD0).?));
            var i: c_int = 0;
            while (i < targets.count) : (i += 1) { // i < a u8 count
                const target = psx.at(&targets.target, i);
                if (target.unk0 == 4 and psx.conv(u8, target.unk4.unk0) == 0) {
                    play = true;
                    break;
                }
            }
        },
        else => unreachable, // a u16 >> 14 is 0..3
    }
    if (play) {
        const sound: [*]u8 = @ptrCast(psx.get(v.g.vs_effects_package().*.?.block11Data).?);
        const offsets: [*]align(1) i16 = @ptrCast(sound + 4);
        const offset: isize = offsets[(psx.at(data, event.keyIndex).* >> 9) & 31];
        _ = c.vs_input_startVibrationPattern(@ptrFromInt(@intFromPtr(sound) +% @as(usize, @bitCast(offset))), 255, 1);
    }
    result = c.vs_effects_advanceTimelineKey(@ptrCast(event));
    return result;
}
