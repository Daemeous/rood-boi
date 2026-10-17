// A mote's closed form (pc/src/particle_math.h, its C original: keep the two
// the same). The includer defines PARTICLE_EMITTER(e, k) (vec4 k of emitter
// e's record, particle_math.h pc_emitter_gpu), PARTICLE_FLOOR(i) (vec4 i of
// the floor grid's heights), PARTICLE_FLOOR_INFO and PARTICLE_FLOOR_N.
#ifndef PARTICLES_SIM_GLSL
#define PARTICLES_SIM_GLSL
const int SPAWN_BOX = 0, SPAWN_POINT = 1, SPAWN_DISC = 2, SPAWN_SURFACE = 3;
uint particle_hash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float particle_unit(inout uint s) { s = particle_hash(s + 0x9e3779b9u); return float(s >> 8) * (1.0 / 16777216.0); }
float particle_smooth(float x) { x = clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }
float particle_floor_cell(int i, int j, int n) {
    int k = clamp(j, 0, n - 1) * n + clamp(i, 0, n - 1);
    return PARTICLE_FLOOR(k >> 2)[k & 3];
}
float particle_floor(vec2 xz) {
    int n = PARTICLE_FLOOR_N;
    vec2 c = (xz - PARTICLE_FLOOR_INFO.xy) * PARTICLE_FLOOR_INFO.zw - 0.5;
    ivec2 i = ivec2(floor(c));
    vec2 f = c - vec2(i);
    float a = mix(particle_floor_cell(i.x, i.y, n), particle_floor_cell(i.x + 1, i.y, n), f.x);
    float b = mix(particle_floor_cell(i.x, i.y + 1, n), particle_floor_cell(i.x + 1, i.y + 1, n), f.x);
    return mix(a, b, f.y);
}
const vec4 CURL_K[6] = vec4[6](vec4(0.83, 1.31, -0.47, 0.9), vec4(-1.62, 0.38, 1.17, 1.3), vec4(1.12, -0.71, 0.95, 0.7),
    vec4(0.41, 1.77, -1.08, 1.1), vec4(-0.92, 0.56, 1.43, 0.8), vec4(1.36, -1.21, -0.33, 1.5));
const float CURL_P[6] = float[6](0.0, 1.3, 2.1, 0.7, 4.2, 3.3);
vec3 particle_curl(vec3 q, float tau) {
    vec3 g[3] = vec3[3](vec3(0.0), vec3(0.0), vec3(0.0));
    for (int j = 0; j < 6; j++) {
        vec4 k = CURL_K[j];
        g[j >> 1] += ((j & 1) != 0 ? 0.5 : 1.0) * cos(dot(k.xyz, q) + tau * k.w + CURL_P[j]) * k.xyz;
    }
    return 0.5 * vec3(g[2].y - g[1].z, g[0].z - g[2].x, g[1].x - g[0].y);
}
struct Mote { vec3 pos; float age, life, fade, gain; int size, cycle; };
Mote particle_mote(int e, int i, float t) {
    vec4 v0 = PARTICLE_EMITTER(e, 0), v1 = PARTICLE_EMITTER(e, 1), v2 = PARTICLE_EMITTER(e, 2), v3 = PARTICLE_EMITTER(e, 3);
    vec4 v4 = PARTICLE_EMITTER(e, 4), v6 = PARTICLE_EMITTER(e, 6), v7 = PARTICLE_EMITTER(e, 7);
    uint s = particle_hash(uint(v7.x) * 0x9e3779b1u ^ uint(i) * 0x85ebca77u);
    float life = max(v4.x, 0.05) * (1.0 - clamp(v4.y, 0.0, 0.95) * particle_unit(s));
    float tt = max(t, 0.0) + particle_unit(s) * life;
    float k = floor(tt / life);
    float age = clamp(tt - k * life, 0.0, life);
    uint c = particle_hash(s ^ (uint(int(k)) * 0xc2b2ae35u));
    float u0 = particle_unit(c), u1 = particle_unit(c), u2 = particle_unit(c);
    int shape = int(v1.w);
    vec3 p;
    if (shape == SPAWN_POINT) {
        float z = 1.0 - 2.0 * u0, r = sqrt(max(1.0 - z * z, 0.0)), a = 6.2831853 * u1, rr = v1.x * pow(u2, 1.0 / 3.0);
        p = v0.xyz + rr * vec3(r * cos(a), z, r * sin(a));
    } else if (shape == SPAWN_DISC) {
        float rr = v1.x * sqrt(u0), a = 6.2831853 * u1;
        p = v0.xyz + vec3(rr * cos(a), 0.0, rr * sin(a));
    } else {
        vec3 l = (vec3(u0, u1, u2) - 0.5) * v1.xyz;
        float cs = cos(v0.w), sn = sin(v0.w);
        p = v0.xyz + vec3(cs * l.x + sn * l.z, l.y, -sn * l.x + cs * l.z);
        if (shape == SPAWN_SURFACE)
            p.y = (PARTICLE_FLOOR_N > 0 ? particle_floor(p.xz) : v0.y + v1.y * 0.5) - 1.0;
    }
    float g = v2.w, drag = v3.w;
    float fall = drag > 1e-4 ? g * (age / drag - (1.0 - exp(-drag * age)) / (drag * drag)) : 0.5 * g * age * age;
    p += v2.xyz * age;
    p.y += fall;
    if (v3.x > 0.0) p += v3.x * particle_curl(p / max(v3.y, 1.0), t * v3.z);
    Mote m;
    m.pos = p, m.age = age, m.life = life, m.cycle = int(k);
    m.fade = (v4.z > 0.0 ? particle_smooth(age / v4.z) : 1.0) * (v4.w > 0.0 ? particle_smooth((life - age) / v4.w) : 1.0);
    m.gain = 1.0 - clamp(v6.z, 0.0, 1.0) * particle_unit(c);
    int lo = clamp(int(v6.x), 1, 4), hi = clamp(int(v6.y), lo, 4);
    m.size = min(lo + int(particle_unit(c) * float(hi - lo + 1)), hi);
    return m;
}
#endif
