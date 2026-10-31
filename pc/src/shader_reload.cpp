/* Shader hot reload's service (shader_reload.h).
 *
 * Polling: every shader_reload_poll_ms, each stage file in the source folder
 * and each file a stage's last compile read (its includes) is looked at
 * (modification time and size). A file is acted on once it differs from the
 * version last compiled AND has stayed the same for one poll, so an editor
 * saving in several writes (or a save caught mid-write) is compiled once,
 * complete.
 *
 * Compiling: one job at a time on an SDL thread; a change compiles the
 * stages that read a changed file. What a stage reads is first found from
 * its #include lines (followed into the includes, at start: no compile),
 * then taken from each compile (glslang's own list). A job whose inputs
 * changed again while it ran is dropped (a newer job follows), so a stale
 * result is never applied after a newer one.
 *
 * Applying (pc_shader_reload_poll, between frames): for each stage that
 * compiled, its code by name becomes the new SPIR-V (vk_shader_override; the
 * override is dropped when the SPIR-V is the embedded one again) and the
 * pipelines made from changed stages are made again (vk_pipelines_rebuild).
 * A stage that failed keeps the code it had and its errors are kept until it
 * compiles; `generation` counts applied jobs. */
#include "shader_reload.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "light_prefabs.h" /* prefab::Json: RPC params, json_quote */
#include "shader_compiler.h"
#include "vk_core.h"
#include "vk_hooks.h" /* pc_vk_on */

extern "C" {
int pc_shader_reload = 1;
int pc_shader_reload_poll_ms = 500;
}

namespace {

struct Sig {
    long long time = -1; /* ns; -1: missing */
    long long size = -1;
    bool operator==(const Sig& o) const { return time == o.time && size == o.size; }
    bool operator!=(const Sig& o) const { return !(*this == o); }
};
struct Watched {
    Sig polled; /* at the last poll */
    Sig used;   /* the version the last job started from */
};
struct Error {
    std::string file, message;
    int line = 0;
};
struct Job {
    std::vector<std::string> stages; /* paths */
    std::map<std::string, Sig> inputs; /* what the stages read, as the job started */
    std::vector<shader_compiler::Result> results;
    bool all_or_nothing = false;
    Uint64 started = 0;
    SDL_Thread* thread = nullptr;
    std::atomic<bool> done { false };
};

std::string dir;
int state; /* 0 not started, 1 on, -1 off (no sources, or settings off at the start) */
Uint64 last_poll;
std::map<std::string, Watched> watched;
std::map<std::string, std::vector<std::string>> reads; /* stage -> the files its last compile read */
std::map<std::string, std::vector<Error>> errors; /* stage -> its compile's errors */
std::vector<std::string> last_stages; /* the names the last applied job compiled */
Job* job;
bool scanned, reload_wanted, reset_wanted, redraw;
int generation;
double last_ms = -1;

std::string source_dir()
{
    const char* env = getenv("ROOD_SHADERS");
    return env && *env ? env : "pc/shaders/vulkan";
}

/* Generated includes (material_graph.cpp): a file here shadows the source
 * file of its name (the compiler's includer reads it first). */
std::string generated_dir()
{
    const char* env = getenv("ROOD_SHADERS_GENERATED");
    return env && *env ? env : "pc/build/shaders/generated";
}
std::vector<std::string> generated_files()
{
    std::vector<std::string> out;
    int count = 0;
    std::string folder = generated_dir();
    char** names = SDL_GlobDirectory(folder.c_str(), "*.glsl", 0, &count);
    for (int i = 0; names && i < count; i++)
        if (!strchr(names[i], '/'))
            out.push_back(folder + "/" + names[i]);
    SDL_free(names);
    std::sort(out.begin(), out.end());
    return out;
}
std::string base_name(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
bool is_stage(const std::string& name)
{
    size_t dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    return ext == "vert" || ext == "geom" || ext == "frag" || ext == "comp";
}

/* The stage files in `folder`, sorted. */
std::vector<std::string> stage_files(const std::string& folder)
{
    std::vector<std::string> out;
    int count = 0;
    char** names = SDL_GlobDirectory(folder.c_str(), "*", 0, &count);
    for (int i = 0; names && i < count; i++)
        if (is_stage(names[i]) && !strchr(names[i], '/'))
            out.push_back(folder + "/" + names[i]);
    SDL_free(names);
    std::sort(out.begin(), out.end());
    return out;
}

/* "a/./b" and "a//b" as "a/b": an include is the same file however reached. */
std::string tidy(std::string path)
{
    for (size_t at; (at = path.find("/./")) != std::string::npos;)
        path.erase(at, 2);
    for (size_t at; (at = path.find("//")) != std::string::npos;)
        path.erase(at, 1);
    return path;
}

Sig sig_of(const std::string& path)
{
    SDL_PathInfo info;
    Sig s;
    if (SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
        s.time = (long long)info.modify_time, s.size = (long long)info.size;
    return s;
}

/* scaled.frag -> scaled_frag (vk_shaders.h's names). */
std::string stage_name(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = base.rfind('.');
    return base.substr(0, dot) + "_" + base.substr(dot + 1);
}

/* `file:line: message` lines (shader_compiler's log) as errors. */
std::vector<Error> parse_errors(const std::string& log, const std::string& stage)
{
    std::vector<Error> out;
    size_t start = 0;
    while (start < log.size()) {
        size_t end = log.find('\n', start);
        std::string line = log.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? log.size() : end + 1;
        if (line.empty())
            continue;
        Error e { stage, line, 0 };
        /* The last ":<digits>: " splits the file from the message. */
        for (size_t colon = line.find(':'); colon != std::string::npos; colon = line.find(':', colon + 1)) {
            size_t digits = colon + 1;
            while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9')
                ++digits;
            if (digits > colon + 1 && line.compare(digits, 2, ": ") == 0) {
                e.file = line.substr(0, colon);
                e.line = atoi(line.c_str() + colon + 1);
                e.message = line.substr(digits + 2);
                break;
            }
        }
        out.push_back(e);
    }
    if (out.empty())
        out.push_back(Error { stage, "compile failed", 0 });
    return out;
}

int run_job(void* data)
{
    Job* j = (Job*)data;
    for (const std::string& stage : j->stages)
        j->results.push_back(shader_compiler::compile(stage));
    j->done = true;
    return 0;
}

void start_job(std::vector<std::string> stages, bool all_or_nothing)
{
    job = new Job;
    job->stages = std::move(stages);
    job->all_or_nothing = all_or_nothing;
    job->started = SDL_GetTicksNS();
    /* The versions compiled now: a change after this is a new job's. */
    for (const std::string& stage : job->stages) {
        Sig s = sig_of(stage);
        job->inputs[stage] = s;
        watched[stage].used = s;
        for (const std::string& f : reads[stage]) {
            Sig fs = sig_of(f);
            job->inputs[f] = fs;
            watched[f].used = fs;
        }
    }
    job->thread = SDL_CreateThread(run_job, "shader reload", job);
    if (!job->thread) {
        run_job(job); /* no thread: compile here */
    }
}

/* What the stages read now becomes watched (a file new to the watch starts
 * at its current version). keep: added to what was known (a failed compile
 * stops reading at its error). */
void learn_reads(const std::string& stage, const std::vector<std::string>& files, bool keep = false)
{
    std::vector<std::string>& r = reads[stage];
    if (!keep)
        r.clear();
    for (const std::string& f : files) {
        std::string path = tidy(f);
        if (path == stage || std::find(r.begin(), r.end(), path) != r.end())
            continue;
        r.push_back(path);
        if (!watched.count(path)) {
            Sig s = sig_of(path);
            watched[path] = Watched { s, s };
        }
    }
}

/* "" when `spirv` keeps the layout contract of the built-in `name` (a stage
 * with no built-in code has none), else why not. */
std::string contract_breach(const std::string& name, const std::vector<uint32_t>& spirv)
{
    static std::map<std::string, shader_compiler::Interface> builtin;
    auto it = builtin.find(name);
    if (it == builtin.end()) {
        size_t bytes = 0;
        const uint32_t* code = vk_shader_builtin(name.c_str(), &bytes);
        shader_compiler::Interface contract;
        if (code)
            contract = shader_compiler::reflect(code, bytes / 4);
        it = builtin.emplace(name, contract).first;
    }
    return shader_compiler::contract_mismatch(it->second, shader_compiler::reflect(spirv.data(), spirv.size()));
}

/* Changed stages' code set and their pipelines made again. Returns false
 * (with the overrides as they were) when the pipelines could not be made. */
bool apply_code(const std::vector<std::pair<std::string, const std::vector<uint32_t>*>>& code, std::string* why)
{
    struct Previous {
        std::string name;
        std::vector<uint32_t> code; /* empty: no override */
    };
    std::vector<Previous> previous;
    std::vector<std::string> changed;
    for (const auto& [name, spirv] : code) {
        size_t embedded_bytes = 0, current_bytes = 0;
        const uint32_t* embedded = vk_shader_builtin(name.c_str(), &embedded_bytes);
        const uint32_t* current = vk_shader_code(name.c_str(), embedded, &current_bytes);
        current_bytes = current == embedded ? embedded_bytes : current_bytes;
        size_t bytes = spirv->size() * 4;
        if (current && current_bytes == bytes && !memcmp(current, spirv->data(), bytes))
            continue; /* the same SPIR-V (a comment, say): nothing to remake */
        Previous p { name, {} };
        if (current && current != embedded)
            p.code.assign(current, current + current_bytes / 4);
        previous.push_back(std::move(p));
        bool is_embedded = embedded && embedded_bytes == bytes && !memcmp(embedded, spirv->data(), bytes);
        vk_shader_override(name.c_str(), is_embedded ? nullptr : spirv->data(), bytes);
        changed.push_back(name);
    }
    if (changed.empty())
        return true;
    std::vector<const char*> names;
    for (const std::string& n : changed)
        names.push_back(n.c_str());
    char error[256];
    if (vk_pipelines_rebuild(names.data(), (int)names.size(), error, sizeof error) >= 0)
        return true;
    for (const Previous& p : previous)
        vk_shader_override(p.name.c_str(), p.code.empty() ? nullptr : p.code.data(), p.code.size() * 4);
    *why = error;
    return false;
}

void finish_job()
{
    SDL_WaitThread(job->thread, nullptr);
    Job* j = job;
    job = nullptr;
    /* What the stages read is known whatever happens next. */
    for (size_t i = 0; i < j->stages.size(); i++)
        learn_reads(j->stages[i], j->results[i].files, !j->results[i].ok);
    /* An input changed while it compiled: dropped, the newer version's job follows. */
    for (const auto& [path, s] : j->inputs)
        if (sig_of(path) != s) {
            delete j;
            return;
        }
    bool clean = true;
    for (size_t i = 0; i < j->stages.size(); i++) {
        const std::string name = stage_name(j->stages[i]);
        shader_compiler::Result& r = j->results[i];
        if (r.ok) {
            /* The pipeline's layout (descriptor bindings, push constants, attachments) is the C
             * code's, made for the built-in stage: a stage that needs more is refused. */
            std::string why = contract_breach(name, r.spirv);
            if (!why.empty()) {
                r.ok = false;
                errors[name] = { Error { j->stages[i], "does not fit its pipeline: " + why, 0 } };
                clean = false;
                continue;
            }
            errors.erase(name);
        } else {
            errors[name] = parse_errors(r.log, j->stages[i]);
            clean = false;
        }
    }
    std::vector<std::pair<std::string, const std::vector<uint32_t>*>> code;
    std::vector<std::string> names;
    for (size_t i = 0; i < j->stages.size(); i++) {
        names.push_back(stage_name(j->stages[i]));
        if (j->results[i].ok && (clean || !j->all_or_nothing))
            code.emplace_back(names.back(), &j->results[i].spirv);
    }
    if (!code.empty()) {
        std::string why;
        if (apply_code(code, &why)) {
            ++generation;
            last_stages = names;
            last_ms = (double)(SDL_GetTicksNS() - j->started) * 1e-6;
            redraw = true;
            fprintf(stderr, "[shaders] reloaded %d stage%s in %.0f ms (generation %d)\n", (int)code.size(),
                code.size() == 1 ? "" : "s", last_ms, generation);
        } else {
            errors[code[0].first] = { Error { code[0].first, "the pipelines could not be made: " + why, 0 } };
            fprintf(stderr, "[shaders] %s\n", why.c_str());
        }
    }
    for (const auto& [name, list] : errors)
        fprintf(stderr, "[shaders] %s: %s:%d: %s\n", name.c_str(), list[0].file.c_str(), list[0].line,
            list[0].message.c_str());
    delete j;
}

/* The files `path` includes, recursively, as glslang finds them (the
 * including file's folder, then the vendored SMAA): a superset is fine (an
 * include in a disabled #if block only compiles a stage more). */
void include_lines(const std::string& path, std::vector<std::string>& out)
{
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io)
        return;
    std::string text;
    char buffer[4096];
    for (size_t n; (n = SDL_ReadIO(io, buffer, sizeof buffer)) > 0;)
        text.append(buffer, n);
    SDL_CloseIO(io);
    size_t slash = path.find_last_of('/');
    std::string folder = slash == std::string::npos ? "." : path.substr(0, slash);
    const char* smaa = getenv("ROOD_SHADER_INCLUDE");
    for (size_t at = text.find("#include"); at != std::string::npos; at = text.find("#include", at + 8)) {
        size_t open = text.find('"', at), end = text.find('\n', at);
        size_t close = open == std::string::npos ? open : text.find('"', open + 1);
        if (open == std::string::npos || close == std::string::npos || (end != std::string::npos && close > end))
            continue;
        std::string name = text.substr(open + 1, close - open - 1);
        for (const std::string& base : { generated_dir(), folder, std::string(smaa && *smaa ? smaa : "pc/third_party/smaa") }) {
            std::string candidate = tidy(base + "/" + name);
            if (sig_of(candidate).time < 0)
                continue;
            if (std::find(out.begin(), out.end(), candidate) == out.end()) {
                out.push_back(candidate);
                include_lines(candidate, out);
            }
            break;
        }
    }
}

/* A compile still running at exit ends before glslang's own statics go. */
void wait_at_exit()
{
    if (job)
        SDL_WaitThread(job->thread, nullptr);
}

bool start()
{
    dir = source_dir();
    std::vector<std::string> stages = stage_files(dir);
    if (stages.empty() || !pc_shader_reload || !shader_compiler::init()) {
        state = -1;
        return false;
    }
    state = 1;
    atexit(wait_at_exit);
    for (const std::string& s : stages) {
        Sig sig = sig_of(s);
        watched[s] = Watched { sig, sig };
    }
    /* What each stage reads, from its #include lines (no compile; each compile refines it). */
    for (const std::string& s : stages) {
        std::vector<std::string> files;
        include_lines(s, files);
        learn_reads(s, files);
    }
    scanned = true;
    fprintf(stderr, "[shaders] hot reload: watching %d stages in %s\n", (int)stages.size(), dir.c_str());
    return true;
}

/* Stage files that a changed (settled) file affects; marks nothing. */
std::vector<std::string> affected(const std::set<std::string>& changed)
{
    std::vector<std::string> out;
    /* A generated file affects the stages reading a file of its name (the stub
     * it shadows, until a compile records the generated one). */
    std::set<std::string> shadowed;
    std::string gen = generated_dir() + "/";
    for (const std::string& c : changed)
        if (c.rfind(gen, 0) == 0)
            shadowed.insert(base_name(c));
    for (const std::string& stage : stage_files(dir)) {
        bool hit = changed.count(stage) > 0;
        for (const std::string& f : reads[stage])
            hit |= changed.count(f) > 0 || shadowed.count(base_name(f)) > 0;
        if (hit)
            out.push_back(stage);
    }
    return out;
}

/* A running compile waited for and its result dropped (its inputs stay
 * marked as compiled: nothing recompiles until they change again). */
void drop_job()
{
    if (!job)
        return;
    SDL_WaitThread(job->thread, nullptr);
    delete job;
    job = nullptr;
}

/* Every override dropped: the built-in code, and the original pipelines. A
 * compile running meanwhile, and saves not compiled yet, are dropped: the
 * reset sticks until the next save. */
void reset_all()
{
    drop_job();
    /* Saves not compiled yet are taken as seen too: the built-in shaders until the next save. */
    for (auto& [path, w] : watched)
        w.used = w.polled = sig_of(path);
    std::vector<std::string> names;
    for (int i = 0; vk_shader_override_name(i); i++)
        names.push_back(vk_shader_override_name(i));
    std::vector<std::vector<uint32_t>> kept;
    for (const std::string& n : names) {
        size_t bytes = 0;
        const uint32_t* code = vk_shader_code(n.c_str(), nullptr, &bytes);
        kept.emplace_back(code, code + bytes / 4);
    }
    vk_shader_overrides_clear();
    std::vector<const char*> list;
    for (const std::string& n : names)
        list.push_back(n.c_str());
    char why[256];
    if (!list.empty() && vk_pipelines_rebuild(list.data(), (int)list.size(), why, sizeof why) < 0) {
        for (size_t i = 0; i < names.size(); i++) /* as it was */
            vk_shader_override(names[i].c_str(), kept[i].data(), kept[i].size() * 4);
        fprintf(stderr, "[shaders] reset: %s\n", why);
        return;
    }
    errors.clear();
    ++generation;
    redraw = true;
    fprintf(stderr, "[shaders] reset: %d stage%s back to the built-in code\n", (int)names.size(),
        names.size() == 1 ? "" : "s");
}

std::string status_json()
{
    std::string s = "{\"on\":" + std::string(state > 0 ? "true" : "false") + ",\"dir\":" + prefab::json_quote(dir)
                  + ",\"generation\":" + std::to_string(generation);
    bool pending = job != nullptr || reload_wanted || reset_wanted;
    for (const auto& [path, w] : watched)
        pending |= w.polled != w.used;
    s += ",\"pending\":" + std::string(pending ? "true" : "false") + ",\"scanned\":" + (scanned ? "true" : "false");
    char ms[32];
    snprintf(ms, sizeof ms, "%.1f", last_ms);
    s += ",\"last_ms\":" + std::string(last_ms < 0 ? "null" : ms) + ",\"errors\":[";
    bool first = true;
    for (const auto& [name, list] : errors)
        for (const Error& e : list) {
            s += (first ? "" : ",") + std::string("{\"stage\":") + prefab::json_quote(name) + ",\"file\":"
               + prefab::json_quote(e.file) + ",\"line\":" + std::to_string(e.line)
               + ",\"message\":" + prefab::json_quote(e.message) + "}";
            first = false;
        }
    s += "],\"overridden\":[";
    for (int i = 0; vk_shader_override_name(i); i++)
        s += (i ? "," : "") + prefab::json_quote(vk_shader_override_name(i));
    s += "],\"last_stages\":[";
    for (size_t i = 0; i < last_stages.size(); i++)
        s += (i ? "," : "") + prefab::json_quote(last_stages[i]);
    int recipes = 0, replaced = 0, pipelines = 0, modules = 0;
    vk_pipelines_hot_counts(&recipes, &replaced, &pipelines, &modules);
    s += "],\"pipelines\":{\"recipes\":" + std::to_string(recipes) + ",\"replaced\":" + std::to_string(replaced)
       + ",\"live\":" + std::to_string(pipelines) + ",\"live_modules\":" + std::to_string(modules)
       + "},\"poll_ms\":" + std::to_string(pc_shader_reload_poll_ms) + "}";
    return s;
}

} // namespace

extern "C" int pc_shader_reload_available(void)
{
    static int available = -1;
    if (available < 0)
        available = !stage_files(source_dir()).empty();
    return available;
}

extern "C" void pc_shader_reload_settle(void)
{
    /* The next library starts its own service (and the built-in shaders it was built with): dropped,
     * and glslang's process state goes with this library. */
    drop_job();
    if (state > 0)
        shader_compiler::shutdown();
    state = -1;
}

/* A generated include was written (material_graph.cpp): poll at once. */
extern "C" void pc_shader_reload_request(void) { last_poll = 0; }

extern "C" int pc_shader_reload_redraw(void)
{
    bool was = redraw;
    redraw = false;
    return was;
}

extern "C" void pc_shader_reload_poll(void)
{
    if (state < 0 || !pc_vk_on() || vk_is_recording())
        return;
    if (job && job->done)
        finish_job();
    if (reset_wanted && state > 0) {
        reset_wanted = false;
        reset_all();
    }
    if (!pc_shader_reload)
        return;
    if (state == 0 && !start())
        return;
    Uint64 now = SDL_GetTicks();
    int every = pc_shader_reload_poll_ms < 100 ? 100 : pc_shader_reload_poll_ms > 5000 ? 5000 : pc_shader_reload_poll_ms;
    if (now - last_poll < (Uint64)every && !reload_wanted)
        return;
    last_poll = now;
    /* New stage files, and generated includes, are watched from their first version on (a change). */
    for (const std::string& stage : stage_files(dir))
        if (!watched.count(stage))
            watched[stage] = Watched {};
    for (const std::string& f : generated_files())
        if (!watched.count(f))
            watched[f] = Watched {};
    std::set<std::string> settled;
    for (auto& [path, w] : watched) {
        Sig s = sig_of(path);
        bool steady = s == w.polled;
        w.polled = s;
        if (steady && s != w.used && s.time >= 0)
            settled.insert(path);
    }
    if (job)
        return; /* one job at a time */
    if (reload_wanted) {
        reload_wanted = false;
        start_job(stage_files(dir), true);
        return;
    }
    if (settled.empty())
        return;
    std::vector<std::string> stages = affected(settled);
    for (const std::string& path : settled)
        watched[path].used = watched[path].polled; /* an include no stage reads: nothing to compile */
    if (!stages.empty())
        start_job(stages, false);
}

extern "C" void pc_shader_reload_summary(char* out, int size)
{
    std::string s;
    if (state > 0)
        s = "Watching " + dir + (job || reload_wanted || reset_wanted ? " (compiling...)" : "") + "\n";
    else
        s = state < 0 ? "Off: no stage files in " + source_dir() + " (or shader_reload 0 at start)\n"
                      : "Not started yet (the first Vulkan frame starts it)\n";
    char line[160];
    if (last_ms >= 0)
        snprintf(line, sizeof line, "Reloads: %d, the last %.0f ms (compile and apply)\n", generation, last_ms);
    else
        snprintf(line, sizeof line, "Reloads: %d\n", generation);
    s += line;
    std::string names;
    for (int i = 0; vk_shader_override_name(i); i++)
        names += (i ? ", " : "") + std::string(vk_shader_override_name(i));
    s += "Edited stages in use: " + (names.empty() ? std::string("none (the built-in shaders)") : names) + "\n";
    int recipes = 0, replaced = 0, pipelines = 0, modules = 0;
    vk_pipelines_hot_counts(&recipes, &replaced, &pipelines, &modules);
    snprintf(line, sizeof line, "Pipelines replaced: %d of %d that can be", replaced, recipes);
    s += line;
    snprintf(out, (size_t)size, "%s", s.c_str());
}

extern "C" int pc_shader_reload_errors(char* first, int size)
{
    int count = 0;
    for (const auto& [name, list] : errors) {
        if (!count && size > 0)
            snprintf(first, (size_t)size, "%s:%d: %s", list[0].file.c_str(), list[0].line, list[0].message.c_str());
        count += (int)list.size();
    }
    return count;
}

static int rpc_reply(const std::string& text, char* out, int size)
{
    if ((int)text.size() >= size) {
        snprintf(out, (size_t)size, "shaders: the reply is too long (%d bytes)", (int)text.size());
        return -1;
    }
    memcpy(out, text.c_str(), text.size() + 1);
    return 0;
}

extern "C" int pc_shader_reload_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, (size_t)size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    const prefab::Json* a = params.get("action");
    std::string action = a && a->type == prefab::Json::String ? a->string : "status";
    if (action == "status")
        return rpc_reply(status_json(), out, size);
    if (state <= 0) {
        snprintf(out, (size_t)size, "shaders: hot reload is off (%s)",
            state < 0 ? "no stage files in the source folder, or shader_reload 0" : "not started yet");
        return -1;
    }
    if (action == "reload") {
        reload_wanted = true; /* every stage, applied if all compile (the next poll) */
        return rpc_reply(status_json(), out, size);
    }
    if (action == "reset") {
        reset_wanted = true; /* between frames (the next poll): a frame may be recording now */
        return rpc_reply(status_json(), out, size);
    }
    snprintf(out, (size_t)size, "shaders: unknown action %s (status, reload, reset)", action.c_str());
    return -1;
}
