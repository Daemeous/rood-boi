//! An effect command: spawns child scripts on the targets over time (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// The P-file's sound-target block (D_800F569C->unkD0), read afresh each time as the C does.
fn targetsOf() *@FieldType(v.D_800F569C_t, "unkD0") {
    return &v.g.D_800F569C().*.?.unkD0;
}

fn targetsCount() u8 {
    const targets: *v.func_800D6894_t = @ptrCast(@alignCast(psx.get(targetsOf().*).?));
    return targets.unk2;
}

/// psx_set of a guest address: the 4-byte field's address on 64-bit, the pointer's bits on 32-bit (stored as
/// the C stores it: by its bits, so an unaligned address does not trap).
fn setAddress(field: anytype, address: u32) void {
    const F = @typeInfo(@TypeOf(field)).pointer.child;
    switch (@typeInfo(F)) {
        .optional, .pointer => @as(*usize, @ptrCast(field)).* = address,
        else => field.raw = address,
    }
}

/// orig: func_800D5A98 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// From its frame on: spawns one child script per P-file target (a delay between them), each told its actor, then a final one once the children are done; 0 after that.
pub fn func_800D5A98(script: ?*v.D_800F53B8_t, state_arg: ?*v.timedSpawnState, kind: c_int) callconv(.c) c_int {
    const Event = extern struct { actors: [6]u8, final: u8, frame: u8, delay: u8 };
    const s = script.?;
    const state = state_arg.?;
    var spawn: v.D_800F53B8_t2 = undefined; // unk21..unk24 stay uninitialised, as in the C
    const event: *align(1) Event = @ptrCast(psx.get(state.data).?);
    var result: c_int = 1;
    const block = psx.get(s.unkD1C.unk30) orelse return result;
    if (event.frame <= block.unk2) {
        if (state.index < targetsCount()) {
            if (state.delay == 0) {
                while (true) {
                    setAddress(&spawn.unk14, psx.rawOf(v.g.D_800F569C().*.?.block8Data));
                    spawn.unk18 = kind;
                    spawn.unk10 = 0;
                    setAddress(&spawn.unk0, psx.rawOf(targetsOf().*) +% 4);
                    setAddress(&spawn.unk4, psx.rawOf(targetsOf().*) +% 4);
                    setAddress(&spawn.unk8, psx.rawOf(targetsOf().*) +% @as(u32, @bitCast(@as(i32, state.index) *% 12 +% 16)));
                    setAddress(&spawn.unkC, psx.rawOf(targetsOf().*) +% @as(u32, @bitCast(@as(i32, state.index) *% 12 +% 16)));
                    psx.put(&spawn.unk1C, s);
                    spawn.unk20 = psx.getBits(s, "unk14_0");
                    const child: *v.D_800F53B8_t = @ptrCast(@alignCast(c.func_800CE83C(@ptrCast(&spawn)).?));
                    psx.setBits(s, "unk14_11", psx.getBits(s, "unk14_11") +% 1);
                    if (psx.get(spawn.unk8).?.unk0.unk0 == 4) {
                        child.unk10[1] = psx.at(&event.actors, @as(*u8, @ptrFromInt(@as(usize, psx.rawOf(spawn.unk8)) + 5)).*).*; // actors[6]: the byte is not checked
                    } else {
                        child.unk10[1] = event.actors[0];
                    }
                    state.index +%= 1;
                    if (state.index == targetsCount()) break;
                    state.delay +%= event.delay;
                    if (state.delay != 0) break;
                }
            }
            if (state.delay != 0) {
                state.delay -%= 1;
            }
        } else if (state.index == targetsCount()) {
            if ((psx.byteAt(u32, s, 0x14).* & 0xF800) == 0) {
                setAddress(&spawn.unk14, psx.rawOf(v.g.D_800F569C().*.?.block8Data));
                spawn.unk18 = kind;
                spawn.unk10 = 0;
                setAddress(&spawn.unk0, psx.rawOf(targetsOf().*) +% 4);
                setAddress(&spawn.unk4, psx.rawOf(targetsOf().*) +% 4);
                setAddress(&spawn.unk8, psx.rawOf(targetsOf().*) +% 16);
                setAddress(&spawn.unkC, psx.rawOf(targetsOf().*) +% 16);
                psx.put(&spawn.unk1C, s);
                spawn.unk20 = psx.getBits(s, "unk14_0");
                const child: *v.D_800F53B8_t = @ptrCast(@alignCast(c.func_800CE83C(@ptrCast(&spawn)).?));
                psx.setBits(s, "unk14_11", psx.getBits(s, "unk14_11") +% 1);
                child.unk10[1] = event.final;
                state.index +%= 1;
            }
        } else {
            result = 0;
        }
    }
    return result;
}
