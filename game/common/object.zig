//! The battle object's (D_800F4538_t) whole-word views of its bitfield runs. Each is anchored on a named
//! field, so a layout change moves them with it. `a` is a pointer to any view of D_800F4538_t.
const psx = @import("psx.zig");

/// The header (vs_actors_ModelInstance) of an object, or the pointer itself when `a` already points to one.
inline fn header(a: anytype) @TypeOf(if (@hasField(@typeInfo(@TypeOf(a)).pointer.child, "unk0")) &a.unk0 else a) {
    return if (comptime @hasField(@typeInfo(@TypeOf(a)).pointer.child, "unk0")) &a.unk0 else a;
}

/// unk0's flag run: bytes 8..B, the movement and drawing flags.
/// The actor's pose steps due this frame (the animation's speed: 1, 2 hasted).
pub inline fn animationSteps(a: anytype) *u8 {
    return &a.framesPerStep;
}

pub inline fn flags8(a: anytype) *align(1) c_uint {
    return psx.bitsAt(c_uint, &header(a)._bits_skip, 0);
}
/// unk0's second word of the run: bytes C..F.
pub inline fn flagsC(a: anytype) *align(1) c_uint {
    return psx.fieldAt(c_uint, &header(a)._bits_skip, 4);
}
/// The state flag run after the bone matrices.
pub inline fn flags5AC(a: anytype) *align(1) c_uint {
    return psx.bitsAt(c_uint, &a._bits_unk5AC_0, 0);
}
/// The movement-mode run that follows it.
pub inline fn flags5B0(a: anytype) *align(1) c_uint {
    return psx.fieldAt(c_uint, &a._bits_unk5AC_0, 4);
}
/// unkC54's flag run.
pub inline fn flags119C(a: anytype) *align(1) c_uint {
    return psx.bitsAt(c_uint, &a.pose._bits_unk548_0, 0);
}

/// The room an object takes in its actor's heap block: the struct rounded up to 0x100 bytes (VS_OBJECT_REGION).
pub fn region(comptime T: type) usize {
    return (@sizeOf(T) + 0xFF) & ~@as(usize, 0xFF);
}
