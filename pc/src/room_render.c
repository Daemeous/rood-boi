/* Replace 2EA3C.s instruction-immediate patches with explicit renderer state.
 * The still-unported assembly drawing entries remain fail-fast imports. */
static short near_depth __attribute__((used)) = 8, far_depth __attribute__((used)) = 0x7ff,
    distance_threshold __attribute__((used)) = 0x200;
static short clip_left __attribute__((used)) = 0, clip_top __attribute__((used)) = 0, clip_right __attribute__((used)) = 320,
    clip_bottom __attribute__((used)) = 224;
void vs_render_setRoomPolygonNearClip(int value);

void vs_render_setRoomTextureLodDepth(int value);

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
