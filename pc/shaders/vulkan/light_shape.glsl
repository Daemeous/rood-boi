// A spot's edge and a rect's barn doors (lights.cpp packs them; light_shape.h
// documents the slots and holds the OpenGL shaders' copy of these two
// functions; keep them the same).
// type (col.w): 1 spot: dir.w cos(cone / 2), right.x cos(inner / 2);
//               2 rect: dir.w and right.w its barn doors (length x cos / sin of their angle).
// The doors: per axis of the rect, the share of its width a point p in front
// sees past the two doors on that axis; the factor is both axes' product.
float barn_doors(vec3 centre, vec4 dir, vec4 right, vec4 up, vec3 p) {
    if (dir.w <= 0.0) return 1.0;
    vec3 v = p - centre;
    float z = max(dot(v, dir.xyz), 1e-3), w = max(length(right.xyz), 1e-3), h = max(length(up.xyz), 1e-3);
    float tx = dot(v, right.xyz) / (w * z), ty = dot(v, up.xyz) / (h * z);
    float fx = clamp(1.0 - (max(dir.w * tx - right.w, 0.0) + max(-dir.w * tx - right.w, 0.0)) / (2.0 * w), 0.0, 1.0);
    float fy = clamp(1.0 - (max(dir.w * ty - right.w, 0.0) + max(-dir.w * ty - right.w, 0.0)) / (2.0 * h), 0.0, 1.0);
    return fx * fy;
}
// The factor on a light's falloff at p (l: unit direction toward the light,
// centre: the light's centre): a spot's edge, a rect's front face times its
// doors, 1 for a point light.
float light_shape(float type, vec3 centre, vec4 dir, vec4 right, vec4 up, vec3 p, vec3 l) {
    float c = dot(-l, dir.xyz);
    if (type > 1.5) return max(c, 0.0) * barn_doors(centre, dir, right, up, p);
    if (type > 0.5) return smoothstep(dir.w, right.x, c);
    return 1.0;
}
