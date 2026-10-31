/* The in-process shader compiler (shader_compiler.h): glslang's C++ API set
 * up as pc/tools/vk_shaders.py runs the standalone compiler (StandAlone.cpp
 * with -V --target-env vulkan1.3 -I<stage folder> -I<smaa>): the same
 * messages, environment, default resource limits, includer and SPIR-V
 * options, so the output is the build's byte for byte. */
#include "shader_compiler.h"

#include <stdlib.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <sstream>

#include "SPIRV/GlslangToSpv.h"
#include "StandAlone/DirStackFileIncluder.h"
#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"

namespace shader_compiler {

static std::mutex init_lock;
static bool initialised;

bool init()
{
    std::lock_guard<std::mutex> hold(init_lock);
    if (!initialised)
        initialised = glslang::InitializeProcess();
    return initialised;
}

void shutdown()
{
    std::lock_guard<std::mutex> hold(init_lock);
    if (initialised)
        glslang::FinalizeProcess();
    initialised = false;
}

static bool stage_of(const std::string& path, EShLanguage* stage)
{
    size_t dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    if (ext == "vert")
        *stage = EShLangVertex;
    else if (ext == "geom")
        *stage = EShLangGeometry;
    else if (ext == "frag")
        *stage = EShLangFragment;
    else if (ext == "comp")
        *stage = EShLangCompute;
    else
        return false;
    return true;
}

static std::string folder_of(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

/* glslang's "ERROR: file:line: message" lines as "file:line: message";
 * its summary lines ("N compilation errors") are left out. */
static std::string error_lines(const std::string& log)
{
    std::istringstream in(log);
    std::string line, out;
    while (std::getline(in, line)) {
        if (line.compare(0, 7, "ERROR: ") != 0)
            continue;
        line.erase(0, 7);
        if (line.find(": ") == std::string::npos || line.find("compilation errors") != std::string::npos
            || line.find("compilation terminated") != std::string::npos || line.find("Linking ") == 0)
            continue;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r'))
            line.pop_back();
        out += line + "\n";
    }
    return out.empty() ? log : out;
}

/* ---- the layout contract (a small SPIR-V reader: decorations, types, variables) ---- */

Interface reflect(const uint32_t* code, size_t words)
{
    enum { Decorate = 71, MemberDecorate = 72, Variable = 59, TypeInt = 21, TypeFloat = 22,
        TypeVector = 23, TypeMatrix = 24, TypeImage = 25, TypeSampler = 26, TypeSampledImage = 27,
        TypeArray = 28, TypeRuntimeArray = 29, TypeStruct = 30, TypePointer = 32, Constant = 43,
        EntryPoint = 15, TypeAccel = 5341 };
    enum { DecBlock = 2, DecBufferBlock = 3, DecArrayStride = 6, DecMatrixStride = 7, DecLocation = 30,
        DecBinding = 33, DecSet = 34, DecOffset = 35 };
    Interface out;
    if (!code || words < 5 || code[0] != 0x07230203)
        return out;
    struct Info {
        uint32_t op = 0;
        std::vector<uint32_t> w; /* the instruction's operands after the result id */
        int set = -1, binding = -1, location = -1, array_stride = 0;
        bool block = false, buffer_block = false;
        std::vector<int> member_offsets, member_matrix_strides;
    };
    std::vector<Info> ids(code[3]);
    uint32_t model = 0;
    auto id_at = [&](uint32_t id) -> Info* { return id < ids.size() ? &ids[id] : nullptr; };
    for (size_t at = 5; at < words;) {
        uint32_t count = code[at] >> 16, op = code[at] & 0xffff;
        if (!count || at + count > words)
            return out;
        const uint32_t* w = code + at;
        if (op == EntryPoint)
            model = w[1];
        else if (op == Decorate && count >= 3) {
            if (Info* i = id_at(w[1])) {
                uint32_t d = w[2], v = count > 3 ? w[3] : 0;
                if (d == DecSet) i->set = (int)v;
                if (d == DecBinding) i->binding = (int)v;
                if (d == DecLocation) i->location = (int)v;
                if (d == DecBlock) i->block = true;
                if (d == DecBufferBlock) i->buffer_block = true;
                if (d == DecArrayStride) i->array_stride = (int)v;
            }
        } else if (op == MemberDecorate && count >= 4) {
            if (Info* i = id_at(w[1])) {
                uint32_t m = w[2], d = w[3], v = count > 4 ? w[4] : 0;
                std::vector<int>* list = d == DecOffset ? &i->member_offsets : d == DecMatrixStride ? &i->member_matrix_strides : nullptr;
                if (list) {
                    if (list->size() <= m)
                        list->resize(m + 1, 0);
                    (*list)[m] = (int)v;
                }
            }
        } else if ((op == TypeInt || op == TypeFloat || op == TypeVector || op == TypeMatrix || op == TypeImage
                       || op == TypeSampler || op == TypeSampledImage || op == TypeArray || op == TypeRuntimeArray
                       || op == TypeStruct || op == TypePointer || op == TypeAccel) && count >= 2) {
            if (Info* i = id_at(w[1]))
                i->op = op, i->w.assign(w + 2, w + count);
        } else if (op == Constant && count >= 4) {
            if (Info* i = id_at(w[2]))
                i->op = op, i->w.assign(w + 3, w + count);
        }
        at += count;
    }
    std::function<int(uint32_t)> size_of = [&](uint32_t id) -> int {
        Info* t = id_at(id);
        if (!t)
            return 0;
        switch (t->op) {
        case TypeInt:
        case TypeFloat:
            return t->w.empty() ? 4 : (int)t->w[0] / 8;
        case TypeVector:
        case TypeMatrix:
            return t->w.size() < 2 ? 0 : size_of(t->w[0]) * (int)t->w[1];
        case TypeArray: {
            Info* n = t->w.size() < 2 ? nullptr : id_at(t->w[1]);
            int length = n && n->op == Constant && !n->w.empty() ? (int)n->w[0] : 0;
            return length * (t->array_stride ? t->array_stride : size_of(t->w[0]));
        }
        case TypeStruct: {
            int end = 0;
            for (size_t m = 0; m < t->w.size(); m++) {
                int offset = m < t->member_offsets.size() ? t->member_offsets[m] : 0;
                Info* mt = id_at(t->w[m]);
                int size = mt && mt->op == TypeMatrix && m < t->member_matrix_strides.size() && t->member_matrix_strides[m]
                    ? t->member_matrix_strides[m] * (int)mt->w[1] : size_of(t->w[m]);
                end = std::max(end, offset + size);
            }
            return end;
        }
        default:
            return 0;
        }
    };
    /* OpVariable (result type, result id, storage class): read again with the types known. */
    for (size_t at = 5; at < words;) {
        uint32_t count = code[at] >> 16, op = code[at] & 0xffff;
        const uint32_t* w = code + at;
        at += count;
        if (op != Variable || count < 4)
            continue;
        Info* var = id_at(w[2]);
        Info* ptr = id_at(w[1]);
        if (!var || !ptr || ptr->op != TypePointer || ptr->w.size() < 2)
            continue;
        uint32_t storage = w[3], type_id = ptr->w[1];
        Info* type = id_at(type_id);
        int array = 0;
        while (type && (type->op == TypeArray || type->op == TypeRuntimeArray)) {
            Info* n = type->op == TypeArray && type->w.size() >= 2 ? id_at(type->w[1]) : nullptr;
            array = n && n->op == Constant && !n->w.empty() ? (int)n->w[0] : -1;
            type_id = type->w.empty() ? 0 : type->w[0];
            type = id_at(type_id);
        }
        if (!type)
            continue;
        if (storage == 9) { /* PushConstant */
            out.push_bytes = std::max(out.push_bytes, size_of(type_id));
            continue;
        }
        if (storage == 1 || storage == 3) { /* Input, Output */
            if (var->location >= 0 && ((storage == 1 && model == 0) || (storage == 3 && model == 4)))
                (storage == 1 ? out.inputs : out.outputs).push_back(var->location);
            continue;
        }
        if (var->binding < 0)
            continue;
        int kind = 0;
        if (type->op == TypeSampler)
            kind = 1;
        else if (type->op == TypeSampledImage)
            kind = 2;
        else if (type->op == TypeImage && type->w.size() >= 6) {
            uint32_t dim = type->w[1], sampled = type->w[5];
            kind = dim == 6 ? 9 : dim == 5 ? 7 : sampled == 2 ? 4 : 3;
        } else if (type->op == TypeStruct)
            kind = storage == 12 || type->buffer_block ? 6 : 5;
        else if (type->op == TypeAccel)
            kind = 8;
        uint32_t key = (uint32_t)(var->set < 0 ? 0 : var->set) << 16 | (uint32_t)var->binding;
        out.bindings.emplace_back(key, kind + (array ? 100 * array : 0));
    }
    std::sort(out.bindings.begin(), out.bindings.end());
    std::sort(out.inputs.begin(), out.inputs.end());
    std::sort(out.outputs.begin(), out.outputs.end());
    out.valid = true;
    return out;
}

static const char* kind_name(int kind)
{
    static const char* const names[] = { "unknown", "sampler", "combined image sampler", "sampled image",
        "storage image", "uniform buffer", "storage buffer", "texel buffer", "acceleration structure",
        "input attachment" };
    return names[kind % 100 < 10 ? kind % 100 : 0];
}

std::string contract_mismatch(const Interface& builtin, const Interface& replacement)
{
    if (!replacement.valid)
        return "the SPIR-V could not be read";
    if (!builtin.valid)
        return "";
    char line[256];
    for (const auto& [key, kind] : replacement.bindings) {
        auto it = std::lower_bound(builtin.bindings.begin(), builtin.bindings.end(), std::make_pair(key, -1000000));
        if (it == builtin.bindings.end() || it->first != key) {
            snprintf(line, sizeof line, "set %u binding %u (a %s) is not in the pipeline's layout",
                key >> 16, key & 0xffff, kind_name(kind));
            return line;
        }
        if (it->second != kind) {
            snprintf(line, sizeof line, "set %u binding %u is a %s%s in the layout, not a %s%s", key >> 16,
                key & 0xffff, kind_name(it->second), it->second >= 100 ? " array" : "", kind_name(kind),
                kind >= 100 ? " array" : "");
            return line;
        }
    }
    if (replacement.push_bytes > builtin.push_bytes) {
        snprintf(line, sizeof line, "its push constants are %d bytes; the layout has %d", replacement.push_bytes,
            builtin.push_bytes);
        return line;
    }
    for (int location : replacement.inputs)
        if (!std::binary_search(builtin.inputs.begin(), builtin.inputs.end(), location)) {
            snprintf(line, sizeof line, "vertex input location %d is not in the pipeline", location);
            return line;
        }
    for (int location : replacement.outputs)
        if (!std::binary_search(builtin.outputs.begin(), builtin.outputs.end(), location)) {
            snprintf(line, sizeof line, "output location %d has no attachment in the pipeline", location);
            return line;
        }
    return "";
}

Result compile(const std::string& path)
{
    Result r;
    r.files.push_back(path);
    EShLanguage stage = EShLangVertex;
    if (!stage_of(path, &stage)) {
        r.log = path + ":0: not a stage file (.vert, .geom, .frag or .comp)\n";
        return r;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        r.log = path + ":0: cannot be read\n";
        return r;
    }
    std::stringstream text;
    text << file.rdbuf();
    std::string source = text.str();
    if (!init()) {
        r.log = path + ":0: glslang did not start\n";
        return r;
    }
    /* -V: SPIR-V rules for Vulkan; --target-env vulkan1.3: SPIR-V 1.6. */
    const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    /* Includes as the standalone's -I: the stage's folder (pc/shaders/vulkan
     * or $ROOD_SHADERS), then the vendored SMAA. */
    /* The generated folder first (material_graph.cpp: a generated include
     * shadows the source file of its name), then as the standalone's -I. */
    struct Includer : DirStackFileIncluder {
        std::string generated;
        std::set<std::string> extra;
        IncludeResult* includeLocal(const char* header, const char* includer, size_t depth) override
        {
            if (!generated.empty()) {
                std::string p = generated + "/" + header;
                std::ifstream file(p, std::ios_base::binary | std::ios_base::ate);
                if (file) {
                    extra.insert(p);
                    return newIncludeResult(p, file, (int)file.tellg());
                }
            }
            return DirStackFileIncluder::includeLocal(header, includer, depth);
        }
        std::set<std::string> getIncludedFiles() override
        {
            std::set<std::string> all = DirStackFileIncluder::getIncludedFiles();
            all.insert(extra.begin(), extra.end());
            return all;
        }
    } includer;
    const char* generated = getenv("ROOD_SHADERS_GENERATED");
    includer.generated = generated && *generated ? generated : "pc/build/shaders/generated";
    includer.pushExternalDirectory(folder_of(path));
    const char* smaa = getenv("ROOD_SHADER_INCLUDE");
    includer.pushExternalDirectory(smaa && *smaa ? smaa : "pc/third_party/smaa");
    glslang::TShader shader(stage);
    const char* strings[] = { source.c_str() };
    const char* names[] = { path.c_str() };
    shader.setStringsWithLengthsAndNames(strings, nullptr, names, 1);
    shader.setOverrideVersion(0);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
    bool parsed = shader.parse(GetDefaultResources(), 100, false, messages, includer);
    for (const std::string& f : includer.getIncludedFiles())
        r.files.push_back(f);
    if (!parsed) {
        r.log = error_lines(shader.getInfoLog());
        return r;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        r.log = error_lines(program.getInfoLog());
        return r;
    }
    spv::SpvBuildLogger logger;
    glslang::SpvOptions options;
    options.disableOptimizer = false; /* (built without the optimizer: ENABLE_OPT=OFF) */
    options.compilerSignature = "glslang";
    glslang::GlslangToSpv(*program.getIntermediate(stage), r.spirv, &logger, &options);
    r.ok = !r.spirv.empty();
    if (!r.ok)
        r.log = path + ":0: no SPIR-V generated\n" + logger.getAllMessages();
    return r;
}

} // namespace shader_compiler
