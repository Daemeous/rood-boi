#include "../src/hd_textures.cpp"
#include <cassert>
#include <iostream>
#define DEFINE_GL(ret, name, args) ret(APIENTRY *gl_##name) args;
GL_FUNCTIONS(DEFINE_GL)
#undef DEFINE_GL
uint16_t test_vram[1024 * 512];
extern "C" const uint16_t *pc_native_vram(void) { return test_vram; }
void gpu_probe(unsigned x, bool hole)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win = SDL_CreateWindow("HD sampler probe", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(win);
    auto context = SDL_GL_CreateContext(win);
    assert(context);
#define LOAD_GL(ret, name, args) gl_##name = (ret(APIENTRY *) args)SDL_GL_GetProcAddress("gl" #name);
    GL_FUNCTIONS(LOAD_GL)
#undef LOAD_GL
    std::ifstream input(getenv("ROOD_HD_SHADER"));
    std::stringstream shader;
    shader << input.rdbuf();
    std::string fragment = "#version 330 core\n#define ROOD_GL 1\n" + shader.str() +
                           "\nuniform vec2 query; out vec4 result; void main(){vec4 hd; "
                           "result=hd_texel(query,0u,19200u,0u,hd)?hd:vec4(0,0,1,1);}";
    auto compile = [](GLenum type, const char *text) {
        GLuint s = gl_CreateShader(type);
        gl_ShaderSource(s, 1, &text, nullptr);
        gl_CompileShader(s);
        GLint ok;
        gl_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096];
            gl_GetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::cerr << log;
            abort();
        }
        return s;
    };
    GLuint program = gl_CreateProgram();
    gl_AttachShader(program, compile(GL_VERTEX_SHADER, "#version 330 core\nvoid main(){vec2 "
                                                       "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                                                       "gl_Position=vec4(p*2.0-1.0,0,1);}"));
    gl_AttachShader(program, compile(GL_FRAGMENT_SHADER, fragment.c_str()));
    gl_LinkProgram(program);
    GLint ok;
    gl_GetProgramiv(program, GL_LINK_STATUS, &ok);
    assert(ok);
    gl_UseProgram(program);
    GLuint vao;
    gl_GenVertexArrays(1, &vao);
    gl_BindVertexArray(vao);
    gl_Viewport(0, 0, 16, 16);
    for (int enabled : {1, 0, 1}) {
        pc_hd_enabled = enabled;
        pc_hd_gl_bind(program);
        gl_Uniform2f(gl_GetUniformLocation(program, "query"), x + 0.5f, 0.5f);
        gl_DrawArrays(GL_TRIANGLES, 0, 3);
        uint8_t pixel[4];
        gl_ReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        std::cout << "GPU " << enabled << ": " << int(pixel[0]) << "," << int(pixel[1]) << ","
                  << int(pixel[2]) << " error " << gl_GetError() << "\n";
        if (enabled)
            assert(pixel[0] == 240 && pixel[1] == 20 && pixel[2] == 10);
        else
            assert(pixel[0] == 0 && pixel[2] == 255);
    }
    if (hole) {
        gl_Uniform2f(gl_GetUniformLocation(program, "query"), 0.5f, 10.5f);
        gl_DrawArrays(GL_TRIANGLES, 0, 3);
        uint8_t pixel[4];
        gl_ReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        assert(pixel[0] == 0 && pixel[2] == 255);
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(win);
    SDL_Quit();
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    std::string mode = argv[1];
    if (mode == "dds") {
        for (const char *file : {"rgba.dds", "bc1.dds", "bc2.dds", "bc3.dds", "bc7.dds"}) {
            auto s = hd_load_dds(file);
            assert(s && s->w == 4 && s->h == 4);
            auto p = (uint8_t *)s->pixels;
            assert(p[0] >= 250 && p[1] <= 5 && p[2] <= 5 && p[3] >= 250);
            SDL_DestroySurface(s);
        }
        for (const char *file : {"short.dds", "huge.dds", "array.dds", "unknown.dds"})
            assert(!hd_load_dds(file));
        std::cout << "PASS: RGBA8, BC1/2/3/7 DDS pixels and "
                     "malformed/truncated/oversized/array rejection\n";
        return 0;
    }
    // Independent fixtures: whole upload 2x2 words; all indexed texels use
    // entries 1..2.
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x)
            test_vram[y * 1024 + x] = 0x2121;
    for (int i = 0; i < 256; ++i)
        test_vram[300 * 1024 + i] = i + 1;
    init();
    assert(configured);
    pc_hd_upload(0, 0, 2, 2, test_vram);
    pc_hd_use(0, 300 * 64);
    if (mode == "reduced") {
        assert(resident.size() == 1);
        bool bound = false;
        uint32_t hash = words[0].hash, pal = crc(test_vram + 300 * 1024, 16) ^ 0xffffffffu;
        for (int i = 0; i < slots; ++i)
            if (table[i * 4] == hash && table[i * 4 + 1] == pal && table[(slots + i) * 4])
                bound = true;
        assert(bound && pending.back().pixels[0] == 240);
        std::cout << "PASS: reduced palette hashes bound under the full live palette\n";
        return 0;
    }
    assert(page_resident.size() == 1 && pending.size() == 1);
    auto &image = pending.back();
    unsigned stride = image.rect.w * 4;
    // Page and Duck full fixtures use red in the top-left; a Duck rectangle
    // starts at x=4.
    unsigned x = mode == "duckpage" || mode == "duckupload" || mode == "alias" ? 4 : 0;
    unsigned sx = image.rect.w / 256;
    auto at = [&](unsigned u, unsigned v) { return image.pixels.data() + v * sx * stride + u * sx * 4; };
    assert(at(x, 0)[0] == 240 && at(x, 0)[1] == 20 && at(x, 0)[2] == 10 && at(x, 0)[3] == 255);
    if (mode != "page")
        assert(at(0, 10)[3] == 1); // uncovered pixels fall back in the shader,
                                   // never replace original VRAM.
    if (getenv("ROOD_HD_SHADER"))
        gpu_probe(x, mode != "page");
    if (mode == "duckupload") {
        pc_hd_clear(1, 0, 1, 1);
        pc_hd_use(0, 300 * 64);
        assert(pending.back().pixels[(4 * sx) * 4 + 3] == 1); // replaced source word invalidated.
    }
    // A palette mutation must never keep a stale page replacement bound.
    test_vram[300 * 1024 + 1] ^= 0x3ff;
    pc_hd_upload(1, 300, 1, 1, test_vram);
    pc_hd_use(0, 300 * 64);
    auto pal = crc(test_vram + 300 * 1024, 16) ^ 0xffffffffu;
    bool disabled = false;
    for (int i = 0; i < slots; ++i)
        if (table[i * 4] == 0 && table[i * 4 + 1] == pal && table[(slots + i) * 4 + 2] == 1 &&
            !table[(slots + i) * 4 + 3])
            disabled = true;
    assert(disabled);
    std::cout << "PASS: " << mode << " independent hashes, rectangle placement, missing palette fallback\n";
}
