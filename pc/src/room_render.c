/* Replace 2EA3C.s instruction-immediate patches with explicit renderer state.
 * The still-unported assembly drawing entries remain fail-fast imports. */
static short near_depth = 8, far_depth = 0x7ff, distance_threshold = 0x200;
static short clip_left = 0, clip_top = 0, clip_right = 320, clip_bottom = 224;
void func_80098160(int value) { near_depth = value; }
void func_8009817C(int value) { far_depth = value; }
void func_80098194(int value) { distance_threshold = value; }
void func_800981B8(int left, int top, int right, int bottom)
{
    clip_left = left;
    clip_top = top;
    clip_right = right;
    clip_bottom = bottom;
}
/* The same rules for the native room (room_native.c, render.h). */
void pc_room_render_rules(int* near_bucket, int* far_bucket, int* lod_bucket)
{
    *near_bucket = near_depth;
    *far_bucket = far_depth;
    *lod_bucket = distance_threshold;
}
int pc_room_depth_bucket(unsigned depth, int bias)
{
    int bucket = (int)(depth >> 2);
    if (bucket < near_depth)
        return -1;
    bucket += bias;
    if (bucket < near_depth || bucket >= far_depth)
        return -1;
    return bucket;
}
int pc_room_within_distance(unsigned depth)
{
    return depth < (unsigned)(int)distance_threshold;
}
int pc_room_quad_visible(const unsigned xy[4])
{
    int left = 1, right = 1, top = 1, bottom = 1;
    for (int i = 0; i < 4; ++i) {
        int x = (short)xy[i], y = (short)(xy[i] >> 16);
        left &= x < clip_left;
        right &= x >= clip_right;
        top &= y < clip_top;
        bottom &= y >= clip_bottom;
    }
    return !(left || right || top || bottom);
}
