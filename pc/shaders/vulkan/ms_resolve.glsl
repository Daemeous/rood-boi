#ifndef ROOD_MS_RESOLVE_GLSL
#define ROOD_MS_RESOLVE_GLSL
// MSAA resolve (ms_resolve.frag; the OpenGL path embeds this file too). The
// colour is the average of the samples: the anti-aliased picture, with the
// PS1's baked light in it. The G-buffer the present pass lights from cannot
// be averaged (normals of two surfaces, "lit" flags, depths), so each pixel
// keeps one representative surface: among the samples of lit 3D surfaces if
// there are any (else all), the one most samples agree with in depth (the
// pixel's main surface; nearer on a tie). Its depth, normal and vertex
// colour describe the pixel; the albedo is the sum of the lit samples'
// albedo over all samples, so the dynamic light the present pass adds
// (albedo x light at the representative surface) fades with the lit
// surfaces' coverage: a silhouette against the void or the HUD is lit in
// proportion. Pixels whose samples share one depth (interiors: most of the
// picture) take sample 0's G-buffer, with only the colour averaged, so
// interiors resolve exactly and cheaply.
// Inputs: color_ms, normal_ms, albedo_ms, shade_ms, depth_ms (sampler2DMS).
void ms_resolve(ivec2 p, int samples, out vec4 color, out vec4 normal_out, out vec4 albedo_out, out vec4 shade_out,
    out float depth_out) {
    float z[8];
    bool lit[8];
    bool any_lit = false, uniform_depth = true;
    vec4 sum = vec4(0.0);
    for (int i = 0; i < samples; i++) {
        z[i] = texelFetch(depth_ms, p, i).r;
        uniform_depth = uniform_depth && z[i] == z[0];
        sum += texelFetch(color_ms, p, i);
    }
    color = sum / float(samples);
    if (uniform_depth) { // one surface (most pixels): sample 0 describes it
        normal_out = texelFetch(normal_ms, p, 0);
        albedo_out = texelFetch(albedo_ms, p, 0);
        shade_out = texelFetch(shade_ms, p, 0);
        depth_out = z[0];
        return;
    }
    for (int i = 0; i < samples; i++) {
        lit[i] = texelFetch(normal_ms, p, i).a > 0.5 && z[i] > 0.0;
        any_lit = any_lit || lit[i];
    }
    int best = 0, best_votes = -1;
    for (int i = 0; i < samples; i++) {
        if (any_lit && !lit[i]) continue;
        int votes = 0;
        for (int j = 0; j < samples; j++)
            votes += abs(z[j] - z[i]) <= 0.01 * z[i] + 1e-6 ? 1 : 0;
        if (votes > best_votes || (votes == best_votes && z[i] < z[best])) {
            best = i;
            best_votes = votes;
        }
    }
    vec4 albedo = texelFetch(albedo_ms, p, best);
    if (any_lit) {
        vec3 lit_sum = vec3(0.0);
        for (int i = 0; i < samples; i++)
            if (lit[i]) lit_sum += texelFetch(albedo_ms, p, i).rgb;
        albedo.rgb = lit_sum / float(samples);
    }
    normal_out = texelFetch(normal_ms, p, best);
    albedo_out = albedo;
    shade_out = texelFetch(shade_ms, p, best);
    depth_out = z[best];
}
#endif
