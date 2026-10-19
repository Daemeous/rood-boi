#include "../src/hd_textures.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#define DEFINE_GL(ret, name, args) ret(APIENTRY* gl_##name) args;
GL_FUNCTIONS(DEFINE_GL)
GL_COMPUTE_FUNCTIONS(DEFINE_GL)
#undef DEFINE_GL
int pc_gl_compute;
uint16_t test_vram[1024 * 512];
extern "C" const uint16_t* pc_native_vram(void) { return test_vram; }
int main(int argc, char** argv)
{
    async_loads = false; /* decode on this thread: the checks follow each call */
    init();
    if (argc > 1 && !strcmp(argv[1], "--no-pack")) {
        assert(!configured && files.empty());
        std::cout << "PASS: missing pack or explicit missing override keeps originals\n";
        return 0;
    }
    assert(files.size() == 3);
    test_vram[0] = 0x3210;
    test_vram[1] = 0x7654;
    test_vram[1024] = 0xba98;
    test_vram[1025] = 0xfedc;
    for (int i = 0; i < 256; ++i)
        test_vram[300 * 1024 + i] = i + 1;
    const uint16_t original[] = { 0x3210, 0x7654, 0xba98, 0xfedc };
    pc_hd_upload(0, 0, 2, 2, test_vram);
    assert(matched_uploads == 1);
    assert(words[1025].offset == 0x10001 && words[1025].width == 2 && words[1025].height == 2);
    pc_hd_use(0, 300 * 64);
    assert(resident.size() == 1 && pending.size() == 1 && pending[0].rect.w == 16);
    assert((*pending[0].pixels)[0] == 240 && (*pending[0].pixels)[3] == 255);
    pc_hd_use(128, 300 * 64);
    assert(resident.size() == 2);
    pc_hd_use(256, 0);
    assert(resident.size() == 3);
    /* A page is looked up again only after a write to its own tiles or its
     * palette's: (512, 0) is neither for page 0 / CLUT row 300, (10, 300) is the CLUT. */
    uint64_t looked = used[300 * 64].stamp;
    pc_hd_upload(512, 0, 1, 1, test_vram);
    pc_hd_use(0, 300 * 64);
    assert(used[300 * 64].stamp == looked);
    pc_hd_upload(10, 300, 1, 1, test_vram);
    pc_hd_use(0, 300 * 64);
    assert(used[300 * 64].stamp > looked && resident.size() == 3);
    assert(test_vram[0] == original[0] && test_vram[1025] == original[3]);
    pc_hd_copy(0, 0, 64, 100, 2, 2, 0);
    assert(words[101 * 1024 + 65].offset == 0x10001);
    pc_hd_clear(65, 101, 1, 1);
    assert(!words[101 * 1024 + 65].width && words[100 * 1024 + 64].width);
    test_vram[100 * 1024 + 64] = 7;
    pc_hd_upload(64, 100, 1, 1, test_vram);
    assert(!words[100 * 1024 + 64].width && words[100 * 1024 + 65].width);
    pc_hd_copy(0, 0, 64, 100, 2, 2, 1);
    assert(!words[100 * 1024 + 65].width);
    pc_hd_copy(0, 0, 1023, 511, 2, 2, 0);
    assert(words[0].offset == 0x10001);
    pc_hd_clear(1023, 511, 2, 2);
    assert(!words[0].width);
    /* Bounded atlas: a layer drawn from in the last frames is never cleared;
     * once it has not been, it is the one reused. */
    size_t kept = resident.size();
    full = true;
    pc_hd_frame();
    assert(resident.size() == kept && full);
    for (int i = 0; i < 130; ++i)
        pc_hd_frame();
    assert(resident.empty() && !full && evictions == 1 && table[slots * 4] == 0 && current_layer == 0);
    assert(ram.size() == 3); /* the decoded PNGs stay in RAM: no disk read to place them again */

    /* Layers: a full layer opens the next; with every layer full the least
     * recently drawn one is reused, while the other keeps its image. */
    side = 16;
    max_layers = 2;
    atlas.clear();
    current_layer = -1;
    pending.clear();
    auto px = std::make_shared<std::vector<uint8_t>>(16 * 16 * 4, 7);
    Image a, b, c;
    assert(place(16, 16, px, "a", a) && a.layer == 0);
    assert(place(16, 16, px, "b", b) && b.layer == 1 && atlas.size() == 2);
    assert(!place(16, 16, px, "c", c) && full);
    for (int i = 0; i < 130; ++i) {
        touch(1);
        pc_hd_frame();
    }
    assert(!full && evictions == 2 && current_layer == 0 && atlas[1].cursor_x == 16);
    assert(place(16, 16, px, "c", c) && c.layer == 0 && c.x == 0 && c.y == 0);
    side = 8192;
    max_layers = 8;
    atlas.clear();
    current_layer = -1;
    pending.clear();
    resident.clear();
    table.fill(0);
    used.clear();

    if (argc > 1) {
        assert(SDL_Init(SDL_INIT_VIDEO));
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_Window* win = SDL_CreateWindow("HD sampler probe", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        assert(win);
        auto context = SDL_GL_CreateContext(win);
        assert(context);
#define LOAD_GL(ret, name, args) gl_##name = (ret(APIENTRY*) args)SDL_GL_GetProcAddress("gl" #name);
        GL_FUNCTIONS(LOAD_GL)
#undef LOAD_GL
        std::ifstream input(argv[1]);
        std::stringstream shader;
        shader << input.rdbuf();
        std::string fragment = "#version 330 core\n#define ROOD_GL 1\n" + shader.str()
            + "\nout vec4 result; void main(){vec4 hd; "
              "result=hd_texel(vec2(0.5),0u,19200u,0u,hd)?hd:vec4(0,0,1,1);}";
        auto compile = [](GLenum type, const char* text) {
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
        gl_AttachShader(program,
            compile(GL_VERTEX_SHADER,
                "#version 330 core\nvoid main(){vec2 "
                "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
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
        test_vram[0] = 0x3210;
        test_vram[1] = 0x7654;
        test_vram[1024] = 0xba98;
        test_vram[1025] = 0xfedc;
        pc_hd_upload(0, 0, 2, 2, test_vram);
        pc_hd_use(0, 300 * 64);
        for (int enabled : { 1, 0, 1 }) {
            pc_hd_enabled = enabled;
            pc_hd_gl_bind(program);
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
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(win);
        SDL_Quit();
        std::cout << "PASS: real GPU sampler lookup, PNG colors and enabled/disabled/enabled\n";
    }
    std::cout << "PASS: independent zlib CRCs, 4/8/16-bit palettes, PNG loading, source offsets, partial "
                 "overwrites, wrapped and masked copies, original VRAM, layered atlas reuse, RAM cache, per-tile lookups\n";
}
