//! zig_diff spec: func_800A1C10 -> bone_view_point.boneViewPoint (38C1C.c).
//! Inputs: an actor id 0-15 (0-9 when the bone selects a weapon model) whose
//! D_800F4538 entry is an arena actor or null, with a fallback in D_800F45E0
//! that is valid or null. Each actor's model pointer is an arena block of
//! armatures, and a weapon block's offsets field points at a second model.
//! Bones cover the 0x40 weapon pattern and its edges, endpoints cover 0, 1,
//! 2, -1, INT_MIN and random values. Armature values are edges (signed and
//! unsigned low halves, odd negatives for the /2 truncation). The GTE calls
//! are void or int stubs; func_800A1DE8 returns a fresh arena matrix with
//! deterministic values, or NULL exactly where the real one does (an actor
//! whose weapon bone has no weapon model): the C then returns -1 without
//! reading the matrix (endpoint 0 with that NULL would crash the C, so the
//! spec never builds it).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").boneViewPoint));

pub const classes = [_][:0]const u8{ "no-actor", "endpoint0", "weapon-null", "weapon", "endpoint1", "plain", "fallback" };

const model_words = 1024;
const model_bytes = 64 + 16 * model_words;

const arm_edges = [_]i32{ 0, 1, -1, 2, -2, 3, -3, 0x7FFF, 0x8000, -0x8000, 0xFFFF, -0x10000, 0x10000, 0x12345, -0x12345, 0x7FFFFFFE, -0x7FFFFFFF };

fn newModel(ctx: *zd.Ctx) *v.D_800F4538_unk68 {
    return @ptrCast(@alignCast(ctx.allocBytes(model_bytes, 16)));
}

/// armatures[index].unk0 (armatures start at offset 64, 16 bytes each).
fn setArmature(model: *v.D_800F4538_unk68, index: i32, value: i32) void {
    psx.byteAt(i32, model, 64 + 16 * @as(isize, index)).* = value;
}

pub const stubs = struct {
    pub fn func_800A1DE8(ctx: *zd.Ctx, actorId: c_int, bone: c_int, m: anytype) [*c]v.MATRIX {
        _ = m;
        if (weaponMissing(actorId, bone)) return null;
        const mat = ctx.alloc(v.MATRIX);
        mat.t[0] = ctx.value(i32);
        mat.t[1] = ctx.value(i32);
        mat.t[2] = ctx.value(i32);
        return mat;
    }
};

/// func_800A1DE8 returns NULL here: an actor in the slot, a weapon bone, no weapon model.
fn weaponMissing(actorId: c_int, bone: c_int) bool {
    const id: usize = @intCast(actorId);
    return v.g.D_800F4538()[id] != null and (bone & 0xF0) == 0x40 and v.g.vs_battle_wepModels()[id * 2] == null;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const bone_edges = [_]i32{ 0, 1, 0x0F, 0x10, 0x3E, 0x3F, 0x40, 0x41, 0x4F, 0x50, 0x80, 0xF0, 0x140, 0x14F, 0x2F, 0x7F, 0x3FF };
    const bone: i32 = if (ctx.chance(2)) bone_edges[ctx.range(usize, 0, bone_edges.len - 1)] else ctx.range(i32, 0, 0x3FF);
    const isW = (bone & 0xF0) == 0x40;
    // The weapon path reads vs_battle_wepModels[actorId * 2]: keep it in its 20 entries.
    const id: i32 = if (isW) ctx.range(i32, 0, 9) else ctx.range(i32, 0, 15);
    var endpoint: i32 = ctx.pick(i32, &.{ 0, 1, 2, -1, 0x7FFF_FFFF, -2147483648 });

    const actor = ctx.alloc(v.D_800F4538_t);
    const alt = ctx.alloc(v.D_800F4538_t);
    const model_a = newModel(ctx);
    const model_b = newModel(ctx);
    const model_w = newModel(ctx);
    const weapon = ctx.alloc(v.vs_battle_wepModels_t);

    psx.put(&actor.unk0.unk68, model_a);
    psx.put(&alt.unk0.unk68, model_b);
    psx.byteAt(u32, weapon, 0x1C).* = psx.addr(model_w);

    setArmature(model_a, bone, ctx.pick(i32, &arm_edges));
    setArmature(model_b, bone, ctx.pick(i32, &arm_edges));
    if (isW) setArmature(model_w, bone - 0x3F, ctx.pick(i32, &arm_edges));

    const idx: usize = @intCast(id);
    v.g.D_800F4538()[idx] = if (ctx.chance(8)) null else actor;
    const fallback: ?*v.D_800F45E0_t = if (ctx.chance(4)) null else @ptrCast(alt);
    v.g.D_800F45E0()[idx] = fallback;
    if (id < 10) {
        const wslot: ?*v.vs_battle_wepModels_t = if (ctx.chance(4)) null else weapon;
        v.g.vs_battle_wepModels()[@intCast(id * 2)] = wslot;
    }

    // A NULL matrix at endpoint 0 crashes the C (matrix->t): not an input.
    if (endpoint == 0 and weaponMissing(id, bone)) endpoint = ctx.pick(i32, &.{ 1, 2, -1 });

    const result: [*c]c_ushort = @ptrCast(@alignCast(ctx.allocBytes(8, 2)));
    return .{ id, bone, result, endpoint };
}

pub fn classify(args: Args) usize {
    const id: usize = @intCast(args[0]);
    const bone = args[1];
    const endpoint = args[3];
    const actor_null = v.g.D_800F4538()[id] == null;
    if (actor_null and v.g.D_800F45E0()[id] == null) return 0;
    if (endpoint == 0) return 1;
    if (actor_null) return 6;
    if ((bone & 0xF0) == 0x40) {
        return if (v.g.vs_battle_wepModels()[id * 2] == null) 2 else 3;
    }
    return if (endpoint == 1) 4 else 5;
}
