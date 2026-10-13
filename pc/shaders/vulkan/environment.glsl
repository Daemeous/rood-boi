// The room's sun and sky (pc/src/environment.h pc_env_gpu), shared by the
// path tracer, the probe GI, the haze and the present passes on Vulkan and
// OpenGL (gl_shaders.py PC_GLSL_ENVIRONMENT). The includer defines
// env_sun_dir, env_sun_col, env_zenith, env_horizon and env_ground (vec4s:
// environment.h) and, for the traced functions, rand(). World space, y down.
#ifndef ENVIRONMENT_GLSL
#define ENVIRONMENT_GLSL
bool env_sky_on() { return env_zenith.w > 0.5; }
bool env_sun_on() { return dot(env_sun_col.rgb, vec3(1.0)) > 0.0; }
// Radiance of the sky in direction d (a ray that left the room): zenith ->
// horizon -> ground, the sky around the sun tinted by it. The sun's disc is
// not in it: the sun is sampled directly (env_sun_*), never found by chance.
vec3 env_sky(vec3 d) {
    if (!env_sky_on()) return vec3(0.0);
    float up = -d.y;
    vec3 c = up >= 0.0 ? mix(env_horizon.rgb, env_zenith.rgb, sqrt(up))
                       : mix(env_horizon.rgb, env_ground.rgb, sqrt(min(-up * 4.0, 1.0)));
    float toward = max(dot(d, env_sun_dir.xyz), 0.0);
    return c + env_sun_col.rgb * env_horizon.w * 0.25 * pow(toward, 8.0);
}
// Irradiance-like ambient where nothing traces the sky (shadow maps, no
// GI): the hemisphere's mean radiance around normal n, weighted toward
// upward faces (they see more sky), times the strength (env_ground.w).
vec3 env_ambient(vec3 n) {
    if (!env_sky_on()) return vec3(0.0);
    float up = clamp(-n.y, -1.0, 1.0);
    vec3 sky_mean = (env_zenith.rgb + 2.0 * env_horizon.rgb) / 3.0;
    vec3 ground_mean = (env_ground.rgb + env_horizon.rgb) * 0.5;
    float w = 0.5 + 0.5 * up;
    return mix(ground_mean, sky_mean, w) * (0.35 + 0.65 * w) * env_ground.w;
}
// The sun's light on a surface with normal n, unshadowed (its cosine).
vec3 env_sun_unshadowed(vec3 n) {
    return env_sun_col.rgb * max(dot(n, env_sun_dir.xyz), 0.0);
}
// A direction within the sun's disc (angular radius env_sun_dir.w),
// uniform over its solid angle, from two random numbers.
vec3 env_sun_sample(float u1, float u2) {
    vec3 a = env_sun_dir.xyz;
    float cos_max = cos(env_sun_dir.w);
    float c = 1.0 - u1 * (1.0 - cos_max), s = sqrt(max(1.0 - c * c, 0.0)), phi = 6.2831853 * u2;
    vec3 t = normalize(cross(abs(a.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), a)), b = cross(a, t);
    return normalize(a * c + (t * cos(phi) + b * sin(phi)) * s);
}
// How far a shadow ray toward the sun goes (past any room).
const float ENV_SUN_DISTANCE = 30000.0;
#ifdef ENV_SUN_MAP
// The sun's shadow map where nothing is traced (shadows.c / vk_shadows.c):
// orthographic along the sun, values = distance to a far point along it
// over its radius. The includer defines ENV_SUN_MAP(st) (the stored value
// at map coordinates 0..1), env_sun_matrix (world -> clip), env_sun_light
// (the far point, its radius) and env_sun_texel (one texel in map
// coordinates). 8 rotated taps; the disc's angular size widens them.
float env_sun_map_visibility(vec3 world, vec3 n, float rotation) {
    float cosine = clamp(dot(n, env_sun_dir.xyz), 0.05, 1.0);
    vec3 p = world + n * 2.0;
    vec4 c = env_sun_matrix * vec4(p, 1.0);
    vec2 st = c.xy * 0.5 + 0.5;
    if (any(lessThan(st, vec2(0.0))) || any(greaterThan(st, vec2(1.0)))) return 1.0;
    float dist = length(p - env_sun_light.xyz) / env_sun_light.w;
    vec3 row0 = vec3(env_sun_matrix[0][0], env_sun_matrix[1][0], env_sun_matrix[2][0]); // right / map half size
    float texel_world = env_sun_texel * 2.0 / max(length(row0), 1e-6);
    float bias = (1.5 + 2.0 * min(sqrt(1.0 - cosine * cosine) / cosine, 4.0)) * texel_world / env_sun_light.w;
    float radius = env_sun_texel * (1.0 + 2.0 * env_sun_dir.w / 0.0046);
    radius = min(radius, env_sun_texel * 6.0);
    float lit = 0.0;
    for (int k = 0; k < 8; k++) {
        float r = sqrt((float(k) + 0.5) / 8.0), a = float(k) * 2.39996323 + rotation;
        lit += dist - bias <= ENV_SUN_MAP(st + vec2(cos(a), sin(a)) * r * radius) ? 1.0 : 0.0;
    }
    return lit / 8.0;
}
#endif
#endif
