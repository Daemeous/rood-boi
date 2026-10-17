// Depth of field with bokeh (pc/docs/Post_Processing.md, post.h), shared by
// dof.frag (Vulkan) and gl_present.c (OpenGL through gl_shaders.py), so both
// renderers draw the same picture.
//
// The input is the lit image: rgb, alpha the signed blur radius (circle of
// confusion) in its pixels, negative in front of the focal plane, 9999 on
// 2D/UI (the present shader's). Passes (DP(1).x, post.h PC_DOF_*):
//   prefilter  -> half size: colour and signed radius (half-size pixels); a
//                 2 x 2 block keeps the colour of its nearest blur (the
//                 foreground's, else the background's), so a sharp subject's
//                 colour never spreads with the background around it
//   tiles      -> the largest foreground and background radius per tile of
//                 PC_DOF_TILE half-size texels
//   dilate     -> each tile's foreground radius reaching from its neighbours
//   far        -> the background layer: every texel gathers over its own
//                 radius the texels whose blur reaches it (scatter as gather);
//                 sharper or nearer texels never spread into it
//   near       -> the foreground layer (premultiplied): blurred foreground
//                 over whatever is behind it, out to the tile's radius, with
//                 its coverage
//   composite  -> full size: the sharp picture, the background layer where
//                 the pixel's own blur is (upsampled with weights by blur
//                 similarity: no halo of a sharp subject), then the
//                 foreground layer over it; 2D/UI pixels as they are
//   probe      -> 1 x 1: the mean log2 view depth around the focus point
//                 (from the depth target) and the share of 3D there
// The kernel (the bokeh) is the aperture: a circle or 3..9 blades with a
// curvature and a rotation, squeezed horizontally (anamorphic), clipped by
// a second circle toward the frame's edges (cat's eye), its brightness
// across soft / even / rim with an optional colour fringe; bright texels
// weigh more (their bokeh shows).
//
// Coordinates are picture pixels (x right, y down) of each image; DOF_FLIP_Y
// (OpenGL) turns them into image rows counted from the bottom.
// DP(0): output size, main source size
// DP(1): pass, the largest radius (half-size pixels), tile size, dilation (tiles)
// DP(2): blades (< 3: a circle), curvature, rotation (radians), anamorphic squeeze
// DP(3): edge (0 soft, 1 even, 2 rim), edge amount, colour fringe, cat's eye
// DP(4): highlights, samples, bokeh test card, picture aspect (width / height)
// DP(5): keep the 2D/UI mark (a grade pass follows), a foreground layer, no layers (pass through), -
// DP(6): probe point (0..1, y down), probe radius (share of the height), VRAM scale
// DP(7): probe: the display area in VRAM (x, y, w, h)
#ifndef ROOD_DOF_GLSL
#define ROOD_DOF_GLSL

const float DOF_PI = 3.14159265;
const float DOF_GOLDEN = 2.39996323;

// Texel p (picture pixels) of s, clamped to it.
vec4 dof_fetch(sampler2D s, ivec2 p)
{
    ivec2 size = textureSize(s, 0);
    p = clamp(p, ivec2(0), size - 1);
#ifdef DOF_FLIP_Y
    p.y = size.y - 1 - p.y;
#endif
    return texelFetch(s, p, 0);
}
// Bilinear at picture position p (texel units; centres at + 0.5).
vec4 dof_sample(sampler2D s, vec2 p)
{
    vec2 st = p / vec2(textureSize(s, 0));
#ifdef DOF_FLIP_Y
    st.y = 1.0 - st.y;
#endif
    return textureLod(s, st, 0.0);
}

float dof_luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
// Bright texels weigh more: their bokeh shows (the picture is display-referred, so highlights clip at 1).
float dof_highlight(vec3 c) { return 1.0 + DP(4).x * 15.0 * smoothstep(0.65, 1.0, dof_luma(c)); }

// The aperture's radius toward angle a (1: the circle through the blades' tips).
float dof_aperture(float a)
{
    float n = DP(2).x;
    if (n < 3.0)
        return 1.0;
    float seg = 2.0 * DOF_PI / n;
    float t = mod(a - DP(2).z, seg) - 0.5 * seg;
    return mix(cos(0.5 * seg) / cos(t), 1.0, DP(2).y);
}
// Kernel sample i of n: an offset in units of the blur radius, inside the aperture (rho: its share of the
// way to the aperture's edge, 0..1). A golden-angle spiral, turned per pixel by `turn`.
vec2 dof_kernel(int i, float n, float turn, out float rho)
{
    rho = sqrt((float(i) + 0.5) / n);
    float a = float(i) * DOF_GOLDEN + turn;
    return rho * dof_aperture(a) * vec2(cos(a), sin(a));
}
// Cat's eye: off axis the lens barrel clips the aperture. The clipping circle's offset for picture
// position uv (0..1): the cat's eye amount at the corners.
vec2 dof_cats_eye(vec2 uv)
{
    vec2 c = (uv - 0.5) * vec2(DP(4).w, 1.0);
    return c * (2.0 / length(vec2(DP(4).w, 1.0))) * DP(3).w;
}
// The brightness across a bokeh disc at x (0 its centre, 1 its edge).
float dof_edge(float x)
{
    float s = DP(3).y;
    if (DP(3).x < 0.5)
        return 1.0 - 0.85 * s * x * x; // soft: bright middle, fading edge
    if (DP(3).x > 1.5)
        return 1.0 - 0.6 * s + 2.4 * s * x * x * x * x; // rim: a bright ring (soap bubble)
    return 1.0;
}
// Per channel radius scale: red reaches a little further than blue (a colour fringe at the edges).
vec3 dof_fringe() { return 1.0 + DP(3).z * vec3(0.08, 0.0, -0.08); }
// A fixed per-pixel turn of the kernel: sampling noise instead of a pattern.
float dof_turn(vec2 p) { return fract(52.9829189 * fract(dot(floor(p), vec2(0.06711056, 0.00583715)))) * DOF_GOLDEN; }

// The bokeh test card (Details > Post-processing): black with small white discs, all far and blurred
// by the largest radius, to show the kernel's shape (and its cat's eye toward the edges).
vec4 dof_test_card(vec2 p, vec2 size)
{
    vec2 cell = vec2(size.x / 4.0, size.y / 3.0);
    vec2 d = abs(mod(p, cell) - 0.5 * cell);
    float dot_radius = max(size.y * 0.004, 1.0);
    // As bright as the dot's blur spreads it: the bokeh shows at about full brightness.
    float spread = max(DP(1).y / dot_radius, 1.0);
    return vec4(vec3(length(d) < dot_radius ? 0.7 * spread * spread : 0.0), DP(1).y);
}

vec4 dof_prefilter(sampler2D lit, vec2 p)
{
    ivec2 b = ivec2(floor(p)) * 2;
    vec4 t[4];
    float nearest = 1e9, farthest = -1e9;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        t[i] = dof_fetch(lit, b + ivec2(i & 1, i >> 1));
        if (t[i].a > 9000.0)
            continue;
        nearest = min(nearest, t[i].a);
        farthest = max(farthest, t[i].a);
        n++;
    }
    if (DP(4).z != 0.0)
        return dof_test_card(p, DP(0).xy);
    if (n == 0)
        return vec4(0.0, 0.0, 0.0, 9999.0);
    // The foreground's radius when there is any (it spreads over the rest), else the background's.
    float coc = nearest < -1.0 ? nearest : farthest;
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < 4; i++) {
        if (t[i].a > 9000.0)
            continue;
        float w = 1.0 / (1.0 + abs(t[i].a - coc));
        sum += t[i].rgb * w;
        weight += w;
    }
    return vec4(sum / weight, coc * 0.5);
}

vec4 dof_tiles(sampler2D image, vec2 p)
{
    int tile = int(DP(1).z);
    ivec2 b = ivec2(floor(p)) * tile;
    float fg = 0.0, bg = 0.0;
    for (int y = 0; y < tile; y++)
        for (int x = 0; x < tile; x++) {
            float c = dof_fetch(image, b + ivec2(x, y)).a;
            if (c > 9000.0)
                continue;
            fg = max(fg, -c);
            bg = max(bg, c);
        }
    return vec4(fg, bg, 0.0, 1.0);
}

vec4 dof_dilate(sampler2D tiles, vec2 p)
{
    int k = int(DP(1).w);
    ivec2 b = ivec2(floor(p));
    float tile = DP(1).z;
    vec2 m = dof_fetch(tiles, b).rg;
    for (int y = -k; y <= k; y++)
        for (int x = -k; x <= k; x++) {
            float r = dof_fetch(tiles, b + ivec2(x, y)).r;
            if (float(max(abs(x), abs(y)) - 1) * tile <= r) // its blur reaches this tile
                m.r = max(m.r, r);
        }
    return vec4(m, 0.0, 1.0);
}

vec4 dof_far(sampler2D image, vec2 p)
{
    vec4 c0 = dof_fetch(image, ivec2(p));
    float radius = c0.a;
    if (radius > 9000.0 || radius < 0.5)
        return c0;
    int n = int(DP(4).y);
    vec2 cat = dof_cats_eye(p / DP(0).xy);
    vec3 fringe = dof_fringe(), sum = vec3(0.0), weight = vec3(0.0);
    float turn = dof_turn(p), squeeze = max(DP(2).w, 1.0);
    for (int i = 0; i < n; i++) {
        float rho;
        vec2 k = dof_kernel(i, float(n), turn, rho);
        if (DP(3).w > 0.0 && length(k - cat) > 1.0)
            continue;
        k.x /= squeeze;
        // The texel whose bokeh would cover this one at offset k (a point reflection, so odd blade
        // counts show the right way up).
        vec4 t = dof_fetch(image, ivec2(floor(p - k * radius)));
        if (t.a > 9000.0 || t.a < 0.0)
            continue; // 2D/UI, or the foreground (its own layer)
        float reach = rho * radius;
        vec3 cover = clamp(t.a * fringe - reach + 1.0, 0.0, 1.0);
        float w = dof_edge(min(reach / max(t.a, 0.5), 1.0)) * dof_highlight(t.rgb);
        sum += t.rgb * cover * w;
        weight += cover * w;
    }
    return vec4(vec3(weight.r > 0.0 ? sum.r / weight.r : c0.r, weight.g > 0.0 ? sum.g / weight.g : c0.g,
                     weight.b > 0.0 ? sum.b / weight.b : c0.b), radius);
}

vec4 dof_near(sampler2D image, sampler2D tiles, vec2 p)
{
    float radius = dof_fetch(tiles, ivec2(floor(p / DP(1).z))).r;
    if (radius < 1.0)
        return vec4(0.0);
    int n = int(DP(4).y);
    vec2 cat = dof_cats_eye(p / DP(0).xy);
    vec3 fringe = dof_fringe(), sum = vec3(0.0), weight = vec3(0.0);
    float turn = dof_turn(p), squeeze = max(DP(2).w, 1.0), cover_sum = 0.0;
    for (int i = 0; i < n; i++) {
        float rho;
        vec2 k = dof_kernel(i, float(n), turn, rho);
        if (DP(3).w > 0.0 && length(k - cat) > 1.0)
            continue;
        k.x /= squeeze;
        vec4 t = dof_fetch(image, ivec2(floor(p - k * radius)));
        float c = -t.a;
        if (t.a > 9000.0 || c < 1.0)
            continue; // 2D/UI, the background, or near enough to sharp
        float reach = rho * radius;
        vec3 cover = clamp(c * fringe - reach + 1.0, 0.0, 1.0);
        float w = dof_edge(min(reach / c, 1.0)) * dof_highlight(t.rgb);
        sum += t.rgb * cover * w;
        weight += cover * w;
        // Coverage: a texel spreads over the area of its disc, each sample stands for radius^2 / n of it.
        cover_sum += cover.g / (c * c);
    }
    float alpha = clamp(cover_sum * radius * radius / float(n), 0.0, 1.0);
    if (alpha <= 0.0)
        return vec4(0.0);
    vec3 c = vec3(weight.r > 0.0 ? sum.r / weight.r : 0.0, weight.g > 0.0 ? sum.g / weight.g : 0.0,
        weight.b > 0.0 ? sum.b / weight.b : 0.0);
    return vec4(c * alpha, alpha);
}

vec4 dof_composite(sampler2D lit, sampler2D far_layer, sampler2D near_layer, vec2 p)
{
    vec4 c = dof_fetch(lit, ivec2(p));
    if (c.a > 9000.0)
        return vec4(c.rgb, DP(5).x != 0.0 ? 9999.0 : 1.0);
    if (DP(5).z != 0.0)
        return vec4(c.rgb, 1.0); // no layers (they could not be made): as it is
    vec3 colour = c.rgb;
    float coc = c.a;
    if (DP(4).z != 0.0) // the test card: all of it far, at the largest blur
        coc = DP(1).y * 2.0;
    if (coc > 0.5) {
        // The background layer, from the four half-size texels around, each weighed by how close its blur
        // is to this pixel's (a sharp subject's texels do not tint the blurred background beside it).
        vec2 h = p * 0.5 - 0.5;
        ivec2 b = ivec2(floor(h));
        vec2 f = h - vec2(b);
        vec3 sum = vec3(0.0);
        float weight = 0.0;
        for (int i = 0; i < 4; i++) {
            ivec2 o = ivec2(i & 1, i >> 1);
            vec4 t = dof_fetch(far_layer, b + o);
            if (t.a > 9000.0)
                continue;
            float bw = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
            float d = abs(max(t.a, 0.0) * 2.0 - coc) / max(coc, 1.0);
            float w = bw * (max(1.0 - 2.0 * d, 0.0) + 1e-3);
            sum += t.rgb * w;
            weight += w;
        }
        if (weight > 0.0)
            colour = mix(colour, sum / weight, smoothstep(0.5, 2.0, coc));
    }
    if (DP(5).y != 0.0) {
        vec4 n = dof_sample(near_layer, p * 0.5);
        colour = colour * (1.0 - n.a) + n.rgb;
    }
    return vec4(colour, 1.0);
}

vec4 dof_probe(sampler2D depth)
{
    ivec4 rect = ivec4(DP(7));
    int scale = int(DP(6).w);
    vec2 size = vec2(rect.zw * scale);
    vec2 radius = DP(6).z * vec2(float(rect.w) / float(max(rect.z, 1)), 1.0);
    float sum = 0.0, n = 0.0;
    for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++) {
            vec2 u = DP(6).xy + vec2(i, j) * 0.5 * radius;
            ivec2 q = clamp(ivec2(floor(u * size)), ivec2(0), ivec2(size) - 1);
            q = ivec2((rect.x * scale + q.x) % (1024 * scale), (rect.y * scale + q.y) % (512 * scale));
            float d = texelFetch(depth, q, 0).r;
            if (d * 65536.0 >= 0.5 && d < 1.0) {
                sum += log2(d * 65536.0);
                n += 1.0;
            }
        }
    return vec4(n > 0.0 ? sum / n : 0.0, n / 25.0, 0.0, 1.0);
}

// One pass at uv (0..1 over the output, y down).
vec4 dof_main(sampler2D src0, sampler2D src1, sampler2D src2, vec2 uv)
{
    int pass = int(DP(1).x);
    vec2 p = uv * DP(0).xy;
    if (pass == 0)
        return dof_prefilter(src0, p);
    if (pass == 1)
        return dof_tiles(src0, p);
    if (pass == 2)
        return dof_dilate(src0, p);
    if (pass == 3)
        return dof_far(src0, p);
    if (pass == 4)
        return dof_near(src0, src1, p);
    if (pass == 5)
        return dof_composite(src0, src1, src2, p);
    return dof_probe(src0);
}
#endif
