//! The event script's frame (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BF850 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the event script state machine (D_800F4C34): 0 idle; 1 waits for the start (D_800F4BB8);
/// 2 opens the script (the room's, a door's, or loads an event file: 3 waits for the load); else
/// runs the threads, the camera and the screen effects, and ends the script when its text is done.
/// Returns 0 when no script runs, else 2.
pub fn func_800BF850() callconv(.c) c_int {
    const state = v.g.D_800F4C34();
    if (state.* == 0) return 0;
    if (state.* == 1) {
        if (v.g.D_800F4BB8().* != 0) {
            state.* = 2;
            v.g.D_800F4BB8().* = 0;
            c.func_800BBBE8();
            c.func_8007CEC0();
            var i: usize = 0;
            while (i < 17) : (i += 1) v.g.D_800F4B70()[i] = 0;
            return 2;
        }
        return 0;
    }
    if (state.* == 2) {
        v.g.D_800F4C6A().* = 0;
        v.g.D_800F4C54().* = 0;
        switch (@as(i16, @bitCast(v.g.D_800F4BE0().*))) {
            1 => {
                c.func_800BF5F0(@ptrCast(v.g.D_800F4C30().*));
                state.* = 4;
            },
            2 => {
                const doors: [*c]u8 = @ptrCast(v.g._mpdRoomDoorSection().*);
                const offsets: [*c]align(1) i16 = @ptrCast(doors);
                c.func_800BF5F0(psx.at(doors, psx.at(offsets, v.g._evtFile().*).*));
                state.* = 4;
            },
            0 => {
                c.func_800C00E8(v.g._evtFile().*, v.g.D_800F4C30().*);
                state.* = 3;
            },
            else => {},
        }
        return 2;
    }
    if (state.* == 3) {
        if (v.g.D_800F4BBC().*.?[0] == 4) {
            c.vs_main_freeCdQueueSlot(@ptrCast(@alignCast(v.g.D_800F4BBC().*)));
            if (v.g.D_800F4C54().* != 0) {
                c.vs_main_memcpy(v.g.D_800F4C30().*, v.g.D_800F4C24().*, 0x1800);
                c.vs_main_freeHeapR(v.g.D_800F4C24().*);
                v.g.D_800F4C54().* = 0;
            }
            c.func_800BF5F0(@ptrCast(v.g.D_800F4C30().*));
            state.* = 4;
        }
        return 2;
    }
    c.func_800BBE94();
    if (v.g.D_800F4C22().* == 0) {
        const threads: [*c][*c]u8 = @ptrCast(v.g.D_800F4C38());
        if (c.func_800BFBB8(threads, 0) != 0) {
            if (v.g.D_800F4BB6().* == 0 and v.g.D_800F4C54().* == 0 and v.g.D_800F4BE2().* == 0) {
                c.func_800BC1CC(-1, -1);
            }
            v.g.D_800F4C22().* = 1;
        }
        var i: c_short = 1;
        while (i < 4) : (i += 1) {
            if (v.g.D_800F4C38()[@intCast(i)] != null) _ = c.func_800BFBB8(threads + @as(usize, @intCast(i)), i);
        }
    }
    c.func_800BC9E0();
    c.func_800BDAB4();
    c.vs_battle_applyCameraState();
    c.func_800BB9B0();
    c.func_800BBAFC();
    if (v.g.D_800F4BE4().* != 0) c.func_800BB874();
    if (v.g.D_800F4C22().* != 0 and psx.conv(u8, c.func_800BD610()) == 0xFF) {
        if (v.g.D_800F4C54().* != 0) {
            state.* = 3;
            v.g.D_800F4BE0().* = 0;
        } else {
            c.func_800BFD9C();
            state.* = 0;
        }
        v.g.D_800F4C22().* = 0;
    }
    return 2;
}
