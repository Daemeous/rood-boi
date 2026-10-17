// Files for the extension system (pc/docs/Extensions.md): whole-file reads
// with a size limit, atomic writes (a .tmp then pc_replace_file), mkdir -p,
// stat, sorted directory lists, and the two path checks every plugin path
// goes through (safe_relative: no "..", no absolute or drive paths, plain
// characters only; inside: no link out of the folder).
#include "rl_internal.h"
#include "platform.h"

#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rl {

bool read_file(const std::string& path, std::string* out, size_t limit, std::string* why)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && !S_ISREG(st.st_mode)) { /* a FIFO (from a tarball) blocks open */
        if (why)
            *why = S_ISDIR(st.st_mode) ? "a folder" : "not a regular file";
        return false;
    }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (why)
            *why = "not found";
        return false;
    }
    std::string text;
    char buffer[65536];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
        if (text.size() + n > limit) {
            std::fclose(f);
            if (why)
                *why = "larger than " + std::to_string(limit / 1024) + " KB";
            return false;
        }
        text.append(buffer, n);
    }
    bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) {
        if (why)
            *why = "read error";
        return false;
    }
    *out = std::move(text);
    return true;
}

bool make_dirs(const std::string& path)
{
    std::string part;
    for (size_t k = 0; k <= path.size(); k++) {
        if (k == path.size() || path[k] == '/') {
            if (!part.empty() && part != ".") {
                struct stat st;
                if (stat(part.c_str(), &st) != 0 && pc_mkdir(part.c_str()) != 0)
                    return false;
            }
        }
        if (k < path.size())
            part += path[k];
    }
    return true;
}

bool write_file(const std::string& path, const std::string& text, std::string* why)
{
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && !make_dirs(path.substr(0, slash))) {
        if (why)
            *why = "cannot create " + path.substr(0, slash);
        return false;
    }
    std::string temp = path + ".tmp";
    std::remove(temp.c_str()); /* a link left there must not be written through */
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) {
        if (why)
            *why = "cannot write " + temp;
        return false;
    }
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        std::remove(temp.c_str());
        if (why)
            *why = "cannot write " + path;
        return false;
    }
    return true;
}

bool file_stat(const std::string& path, long long* mtime_ns, long long* size, bool* is_dir)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    if (mtime_ns)
        *mtime_ns = PC_MTIME_NS(st);
    if (size)
        *size = (long long)st.st_size;
    if (is_dir)
        *is_dir = S_ISDIR(st.st_mode);
    return true;
}

std::vector<std::string> list_dir(const std::string& path)
{
    std::vector<std::string> names;
    DIR* d = opendir(path.c_str());
    if (!d)
        return names;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name != "." && name != "..")
            names.push_back(name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

bool inside(const std::string& root, const std::string& full, std::string* why)
{
#ifndef _WIN32
    auto refuse = [&](const char* reason) {
        if (why)
            *why = reason;
        return false;
    };
    char real_root[PATH_MAX], real[PATH_MAX];
    struct stat st;
    if (!realpath(root.c_str(), real_root)) /* not there yet (fine: made below it) or a dangling link */
        return lstat(root.c_str(), &st) != 0 ? true : refuse("a link that leads nowhere");
    /* The deepest part of full that exists (a link counts, even a dangling one). */
    std::string probe = full;
    while (lstat(probe.c_str(), &st) != 0) {
        size_t slash = probe.find_last_of('/');
        if (slash == std::string::npos || slash < root.size()) {
            probe = root;
            break;
        }
        probe.resize(slash);
    }
    if (!realpath(probe.c_str(), real))
        return refuse("a link that leads nowhere");
    size_t n = std::strlen(real_root);
    if (std::strncmp(real, real_root, n) != 0 || (real[n] != '/' && real[n] != 0))
        return refuse("a link that leads outside its folder");
#else
    (void)root, (void)full, (void)why; /* links need privileges there; not checked */
#endif
    return true;
}

bool safe_relative(const std::string& path, std::string* why)
{
    auto refuse = [&](const char* reason) {
        if (why)
            *why = reason;
        return false;
    };
    if (path.empty() || path.size() > 200)
        return refuse("empty or longer than 200 characters");
    if (path[0] == '/')
        return refuse("an absolute path");
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        std::string part = path.substr(start, end - start);
        if (part.empty())
            return refuse("an empty path component");
        if (part == "." || part == "..")
            return refuse("'.' and '..' are not allowed");
        for (char c : part)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
                return refuse("only letters, digits, _ - . and / are allowed");
        if (part.back() == '.')
            return refuse("a name ending with '.' (Windows drops it)");
        /* Windows' device names, in any folder and with any extension (reading CON or COM1 blocks) */
        std::string base = part.substr(0, part.find('.'));
        for (char& c : base)
            c = (char)std::toupper((unsigned char)c);
        bool device = base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
            || (base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0)
                && std::isdigit((unsigned char)base[3]));
        if (device)
            return refuse("a Windows device name (CON, PRN, AUX, NUL, COM1, LPT1, ...)");
        start = end + 1;
    }
    return true;
}

} // namespace rl
