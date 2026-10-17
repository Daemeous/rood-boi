#!/usr/bin/env python3
"""Build a fail-fast x86 startup diagnostic from the original C and local disc data."""

import argparse
import pathlib
import subprocess
import re
import os
import shutil
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--battle",
    action="store_true",
    help="also link BATTLE and INITBTL for the startup probe",
)
parser.add_argument(
    "--no-audio",
    action="store_true",
    help="compile the original sound calls out (silent diagnostic build)",
)
parser.add_argument(
    "--arch",
    choices=["32", "64"],
    default="32",
    help="64: the LP64 build (every source -m64 into pc/build/boot64, "
    "report.txt; links pc/build/boot64/rood_battle_probe when all compile)",
)
parser.add_argument(
    "--nrd",
    choices=["auto", "on", "off"],
    help="--arch 64: NVIDIA NRD for the Vulkan GI denoiser, fetched and built "
    "pinned into pc/build/nrd (pc/tools/nrd.py; default $ROOD_NRD, else auto: "
    "without it when it cannot be built)",
)
parser.add_argument(
    "--verbose",
    action="store_true",
    help="--arch 64: also print the warnings of units that compile",
)
parser.add_argument(
    "--target",
    choices=["linux", "windows"],
    default="linux",
    help="windows: also cross-compile every object with MinGW-w64 and link "
    "pc/build/win64/rood.exe (implies --arch 64; the symbol analysis runs on "
    "the ELF twins, pc/docs/Platform_Support.md)",
)
parser.add_argument(
    "--check",
    action="store_true",
    help="only report whether this configuration is up to date (exit 0) or "
    "would rebuild (exit 1); builds nothing",
)
parser.add_argument(
    "--coff",
    action="store_true",
    help="--target windows: compile only the MinGW-w64 objects and run the "
    "symbol analysis on them (no ELF twins, no host gcc); the only mode on a "
    "Windows host (MSYS2)",
)
parsed = parser.parse_args()
win = parsed.target == "windows"
# COFF-only: the analysis reads the PE/COFF objects themselves with MinGW's
# binutils, so the build needs no Linux toolchain (a native Windows host).
coff = win and (parsed.coff or os.name == "nt")
if win:
    parsed.arch = "64"
arch64 = parsed.arch == "64"
# The 64-bit pass compiles every source the full (battle) build does.
battle = parsed.battle or arch64
no_audio = parsed.no_audio

root = pathlib.Path(__file__).resolve().parents[2]
out = root / ("pc/build/win64" if win else "pc/build/boot64" if arch64 else "pc/build/boot")
out.mkdir(parents=True, exist_ok=True)
# One build at a time per output tree: every run recompiles the objects and then
# renames symbols in place (name -> name__overlay), so a second run in the same
# tree reads objects the first already renamed ("Unclassified import:
# _fontTable__title") or half-written ones. The 64-bit Linux and Windows builds
# also share pc/build/sdl3_64 and pc/build/nrd, so they share one lock.
lock = open(root / ("pc/build/build_boot-64.lock" if arch64 else "pc/build/build_boot-32.lock"), "w")
if os.name == "nt":
    import msvcrt
    import time

    try:
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        print(f"Waiting for another build_boot.py using {out.relative_to(root)}...", flush=True)
        while True:
            try:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError:
                time.sleep(1)
else:
    import fcntl

    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print(f"Waiting for another build_boot.py using {out.relative_to(root)}...", flush=True)
        fcntl.flock(lock, fcntl.LOCK_EX)
# Generated sources included by path from src/ ("pc/build/boot/x.inc"); the
# 64-bit pass shadows that path with -iquote so the 32-bit tree is untouched.
generated = out / "include/pc/build/boot" if arch64 else out
generated.mkdir(parents=True, exist_ok=True)


def run(args, **kwargs):
    return subprocess.run(args, cwd=root, check=True, **kwargs)


def run_long(args, rsp):
    """run() for a command line that can pass Windows' 32767-character limit (the final link
    lists every object): the arguments after the program go in a GCC response file, which GCC
    also hands on to the linker."""
    if os.name != "nt":
        return run(args)
    quote = lambda a: '"' + str(a).replace("\\", "\\\\").replace('"', '\\"') + '"'
    rsp.write_text("\n".join(quote(a) for a in args[1:]) + "\n")
    return run([args[0], f"@{rsp}"])


# The symbol links below exist only for their symbol tables: overlays link
# against main with -R, so MIPS relocation overflows are expected. Hide those
# known lines and show anything else the linker says.
_LD_NOISE = re.compile(
    r"relocation truncated to fit|: in function `|additional relocation overflows"
    r"|has a LOAD segment with RWX permissions"
)


def link_symbols(args):
    # The links read only the decomp's build/ tree (its linker scripts and
    # objects): skip one whose inputs and output are as the last one left them.
    output = root / args[args.index("-o") + 1]
    key_file = output.with_name(output.name + ".inputs")
    key = build_stamp.fingerprint([("build", None)], [], []) + " " + toolchain + " " + " ".join(args)
    if output.exists() and key_file.exists() and key_file.read_text() == key + "\n" + str(output.stat().st_mtime_ns):
        return
    key_file.unlink(missing_ok=True)
    result = mips_tool("ld", args)
    shown = [line for line in (result.stdout + result.stderr).splitlines() if not _LD_NOISE.search(line)]
    for line in shown:
        print(line, file=sys.stderr)
    # --noinhibit-exe writes the ELF anyway; a symbol defined twice would leave
    # its address to link order, and the host imports read these tables.
    duplicates = [line for line in shown if "multiple definition" in line]
    if duplicates:
        raise SystemExit(f"{output.name}: symbols defined twice in the reference link:\n" + "\n".join(duplicates))
    if result.returncode:
        raise subprocess.CalledProcessError(result.returncode, result.args)
    key_file.write_text(key + "\n" + str(output.stat().st_mtime_ns))


def mips_tool(tool, args):
    """Run a MIPS binutils tool (ld, objcopy) on paths relative to the root:
    the host's cross binutils, else the decomp's docker image."""
    if shutil.which(f"mipsel-linux-gnu-{tool}"):
        command = [f"mipsel-linux-gnu-{tool}", *args]
    else:
        command = [
            "docker",
            "run",
            "--rm",
            "--cpus=2",
            "-u",
            f"{os.getuid()}:{os.getgid()}",
            "-v",
            f"{root}:/work",
            "-w",
            "/work",
            "rood-reverse-dev:local",
            f"mipsel-linux-gnu-{tool}",
            *args,
        ]
    return subprocess.run(command, cwd=root, capture_output=True, text=True)


flags = [
    "-m32",
    "-fno-pie",
    "-fno-pic",
    "-funsigned-char",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-builtin",
    "-fgnu89-inline",
    "-ffunction-sections",
    "-fdata-sections",
    "-O2",
    "-g",
]
# GCC 14+ rejects the decompilation's K&R-style prototypes and loose pointer
# conversions (GCC 15 also defaults to C23, where f() takes no arguments):
# keep the GNU C17 dialect and GCC 13's warnings-not-errors.
def gcc_major(compiler):
    # "13", "14.2.0", or MinGW's "13-posix"
    version = subprocess.run([compiler, "-dumpversion"], capture_output=True, text=True).stdout
    return int(re.match(r"\d+", version).group())


c_only_flags = ["-std=gnu17"]
if gcc_major("gcc") >= 14:
    c_only_flags.append("-fpermissive")
flags += c_only_flags
if arch64:
    # The executable is non-PIE (native code and data below 4 GB, so a native
    # address fits a 4-byte psx_ptr field), but the code is compiled as PIE:
    # data imports sit at their PS1 addresses (0x80000000..0x801FFFFF), which
    # the small code model cannot address absolutely (R_X86_64_32S is
    # sign-extended); RIP-relative references reach them from code at
    # 0x400000. Truncating pointer casts are errors.
    flags = ["-m64" if f == "-m32" else "-fpie" if f == "-fno-pie" else f
             for f in flags if f != "-fno-pic"] + [
        "-DROOD_PC_LP64",  # selects psx_types.h's 4-byte guest pointers
        "-Werror=pointer-to-int-cast",
        "-Werror=int-to-pointer-cast",
    ]
# Per-file results of the 64-bit pass: (kind, source, returncode, stderr).
arch64_results = []


# --target windows: every object also has a PE/COFF twin (name.obj next to
# name.o) built by MinGW-w64 from the same source. The ELF
# objects drive the analysis below (nm, objdump -r, readelf, gdb's DWARF);
# each symbol edit made to an ELF object is repeated on its twin, and the
# twins are what rood.exe links.
MINGW = "x86_64-w64-mingw32-"
# Ubuntu's MinGW-w64 compilers come in two thread models; the build uses the
# POSIX one. On a Windows host (MSYS2's MINGW64) they are plain gcc and g++.
MINGW_GCC, MINGW_GXX = MINGW + "gcc-posix", MINGW + "g++-posix"
if os.name == "nt":
    MINGW, MINGW_GCC, MINGW_GXX = "", "gcc", "g++"
mingw_permissive = win and gcc_major(MINGW_GCC) >= 14
win_include = out / "win_include"
if win:
    # Vulkan headers (platform-neutral; vk_platform.h handles _WIN32) without
    # the rest of the host's /usr/include.
    win_include.mkdir(parents=True, exist_ok=True)
    for name in ["vulkan", "vk_video"]:
        link = win_include / name
        if not link.exists() and pathlib.Path("/usr/include", name).exists():
            link.symlink_to(pathlib.Path("/usr/include", name))


def twin(path):
    return pathlib.Path(path).with_suffix(".obj")


# The objects the symbol analysis reads, and its binutils: the ELF objects, or
# with --coff the COFF ones (name.obj) and MinGW-w64's tools.
def aobj(path):
    return twin(path) if coff else pathlib.Path(path)


NM, OBJDUMP, READELF = ([MINGW + "nm"], [MINGW + "objdump"], [MINGW + "readelf"]) if coff \
    else (["nm"], ["objdump"], ["readelf"])


def defined_name(symbol, kind):
    """The name an nm line ("<kind> <symbol>", kind uppercase) defines, or None.
    COFF spells a weak definition .weak.<name>.<other symbol>; its absolute
    ("A") form is a weak reference's default, not a definition."""
    if not coff:
        return symbol
    weak = re.fullmatch(r"\.weak\.(\w+)\.\w+", symbol)
    if weak:
        return weak.group(1) if kind != "A" else None
    return None if symbol.startswith(".") else symbol


def win_command(args):
    """A host gcc/g++ command as its MinGW-w64 equivalent (output .obj)."""
    tool = {"gcc": MINGW_GCC, "g++": MINGW_GXX}[args[0]]
    command = [tool]
    # The host's -fpermissive follows the host gcc's version; MinGW's own
    # version decides for the twin (GCC 13 warns that it is C++-only).
    if args[0] == "gcc" and mingw_permissive:
        command.append("-fpermissive")
    rest = iter(args[1:])
    for a in rest:
        if a in ("-m64", "-fpie", "-fno-pie", "-fpermissive"):
            continue
        if a == "-o":
            command += ["-o", str(twin(next(rest)))]
            continue
        command.append(a)
    # Small code model: extern data is addressed RIP-relative, not through
    # .refptr stubs (comdats named after the symbol, which the overlay-local
    # renames below would leave pointing at the wrong copy). Code sits at
    # 0x400000, within reach of the guest addresses (0x1F800000, 0x80000000).
    # Host code (not the game's -nostdinc units) sees windows.h through
    # win_compat.h first.
    # The vendored Lua core (-DROOD_LUA_CORE) needs none of it either: Windows'
    # macros stay away from its identifiers.
    host = "-nostdinc" not in args and not any(a.endswith(".i") for a in args) and "-DROOD_LUA_CORE" not in args
    # GCC's (System V) bitfield and packed-struct layout, which the PS1
    # structures assume, not MSVC's.
    return command[:1] + ["-mcmodel=small", "-mno-ms-bitfields", "-D_WIN32_WINNT=0x0A00", "-isystem", str(win_include)] \
        + (["-include", "pc/src/win_compat.h"] if host else []) + command[1:]


_LONG_TOKEN = re.compile(rb'("(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')|\blong\b(\s+long\b|\s+double\b)?')


def lp64_longs(text):
    """The game units' own `long` as `long long` (preprocessed source). The
    64-bit port is LP64 (pc/docs/LP64_Audit.md): game code casts pointers
    through long, and the SDK's 4-byte longs are already respelled int;
    Windows' long is 4 bytes, so its twins get Linux's 8-byte long back.
    String and character literals are left alone."""
    def replace(match):
        if match.group(1) or match.group(2):
            return match.group(0)
        return b"long long"
    return _LONG_TOKEN.sub(replace, text)


def compile_twin(args, source, kind="c"):
    if not win or "-c" not in args:
        return True
    result = object_cache.compile(win_command(args))
    arch64_results.append((kind, "win:" + str(source), result.returncode, result.stderr))
    if result.stderr and result.returncode == 0 and parsed.verbose:
        print(result.stderr, end="")
    return result.returncode == 0


def objcopy(args, obj):
    """Apply a symbol edit to an object and to its COFF twin."""
    if not coff:
        run(["objcopy", *args, str(obj)])
    if win:
        run([MINGW + "objcopy", *args, str(twin(obj))])


def compile_object(args, source, kind="c", win_args=None):
    """Compile one object; the 64-bit pass records failures and goes on.
    win_args: the twin's command when it differs (its own preprocessed input)."""
    if not arch64:
        sys.stderr.write(object_cache.compile(args, check=True).stderr)
        return True
    if coff:
        return compile_twin(win_args or args, source, kind)
    result = object_cache.compile(args)
    arch64_results.append((kind, str(source), result.returncode, result.stderr))
    if result.stderr and result.returncode == 0 and parsed.verbose:
        print(result.stderr, end="")
    return result.returncode == 0 and compile_twin(win_args or args, source, kind)


def state_sections(obj):
    """Save states (pc/src/snapshot.c): the object's .data/.bss join the
    sections rood_state_data / rood_state_bss, which a snapshot saves and
    restores whole. For the game's own units and the host units whose statics
    are game state (the SDK layer, CPU rasterizer and VRAM, GTE, CD drive,
    MDEC, XA, the libspu layer). Variables placed in .data.rood_host (host
    configuration read from the environment) stay out. The Windows twins keep
    their sections (no save states there)."""
    if coff:
        return
    renames = []
    for line in query(["readelf", "-SW", str(obj)]).splitlines():
        section = re.match(r"\s*\[\s*\d+\]\s+(\.(data|bss)(\.\S+)?)\s+\w+\s+\w+\s+\w+\s+(\w+)", line)
        if section and int(section.group(4), 16) and "rood_host" not in section.group(1):
            renames.append(f"--rename-section={section.group(1)}=rood_state_{section.group(2)}")
    if renames:
        run(["objcopy", *renames, str(obj)])


# Host units whose statics are game state (state_sections).
STATE_HOST_UNITS = {"runtime", "cd_drive", "mdec", "xa_audio", "audio_libspu", "gte", "libgte", "inline"}


def compile_generated(args):
    """A generated table (game data: saved with the game state): both
    objects must build."""
    if not coff:
        run(args)
        state_sections(args[args.index("-o") + 1])
    if win:
        run(win_command(args))


# SDL3 (vendored, zlib) for the window, input, gamepads and audio, built as a
# static 32-bit library. Its platform backends (Wayland, X11, PulseAudio,
# PipeWire, ALSA, udev, D-Bus) load their libraries at run time, so only
# headers are needed here; CMake is pointed at the 32-bit runtime libraries,
# which have no development symlinks without the :i386 -dev packages.
sdl_source = root / "pc/third_party/SDL3"
sdl_build = root / ("pc/build/sdl3_64" if arch64 else "pc/build/sdl3")
sdl_library = sdl_build / "libSDL3.a"
sdl_flags = ["-I" + str(sdl_source / "include")]
# Shared GLSL for the OpenGL path (the probe GI's compute shaders, written
# once in pc/shaders/vulkan) as C strings.
run([sys.executable, "pc/tools/gl_shaders.py", "--out", str(out / "gl_shaders.h")])
sdl_flags.append("-I" + str(out))
if arch64:
    # The Vulkan renderer (ROOD_RENDERER=vulkan, pc/docs/Vulkan_Renderer.md):
    # its shaders compiled to SPIR-V by the vendored glslang into a header.
    run([sys.executable, "pc/tools/vk_shaders.py", "--out", str(out / "vk_shaders.h")])
    sdl_flags += ["-DROOD_VULKAN", "-I" + str(out)]
    # NVIDIA NRD (not vendored: its license is not CC0): fetched pinned and
    # built once into pc/build/nrd; None builds without it.
    sys.path.insert(0, str(root / "pc/tools"))
    import nrd as nrd_build

    nrd = nrd_build.ensure(parsed.nrd)
    # Windows: the same NRD compiled by MinGW (its SPIR-V comes from the
    # Linux build).
    win_nrd = nrd_build.ensure_windows(nrd) if win else None
    if win and nrd and not win_nrd:
        nrd = None  # both builds with it, or neither
    print("NRD: " + ("linked (" + nrd["libraries"][0] + ")" if nrd else "off (the GI keeps its own filter)"))
else:
    nrd = win_nrd = None
i386 = pathlib.Path("/usr/lib/i386-linux-gnu")
host_libs = pathlib.Path("/usr/lib/x86_64-linux-gnu") if arch64 else i386
sdl_args = [
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_POSITION_INDEPENDENT_CODE=OFF",
    "-DSDL_SHARED=OFF",
    "-DSDL_STATIC=ON",
    "-DSDL_TESTS=OFF",
    "-DSDL_TEST_LIBRARY=OFF",
    "-DSDL_EXAMPLES=OFF",
    "-DSDL_INSTALL=OFF",
    "-DSDL_CAMERA=OFF",
    "-DSDL_GPU=OFF",
    "-DSDL_RENDER_GPU=OFF",
    "-DSDL_VULKAN=" + ("ON" if arch64 else "OFF"),  # the Vulkan renderer is 64-bit only
    "-DSDL_RENDER_VULKAN=OFF",
    "-DSDL_KMSDRM=OFF",
    "-DSDL_X11_XTEST=OFF",  # its header ships only with libxtst-dev
]
for library, variables in [
    ("X11", ["X11_LIB", "X11_X11_LIB"]),
    ("Xext", ["XEXT_LIB"]),
    ("Xcursor", ["XCURSOR_LIB"]),
    ("Xi", ["XI_LIB"]),
    ("Xfixes", ["XFIXES_LIB"]),
    ("Xrandr", ["XRANDR_LIB"]),
    ("Xrender", ["XRENDER_LIB"]),
    ("Xss", ["XSS_LIB"]),
    ("asound", ["ALSA_LIBRARY", "ASOUND_LIB"]),
    ("udev", ["UDEV_LIB"]),
]:
    found = sorted(host_libs.glob(f"lib{library}.so.[0-9]*"))
    if found:
        sdl_args += [f"-D{variable}={found[0]}" for variable in variables]
# D-Bus's arch header comes from the 64-bit -dev package, whose 64-bit integer
# typedefs use long (32 bits here): shadow it with a 32-bit copy.
c_flags_32 = "-m64" if arch64 else "-m32"
arch_deps = pathlib.Path("/usr/lib/x86_64-linux-gnu/dbus-1.0/include/dbus/dbus-arch-deps.h")
if arch_deps.exists() and not arch64:
    shadow = sdl_build / "include32/dbus/dbus-arch-deps.h"
    shadow.parent.mkdir(parents=True, exist_ok=True)
    text = arch_deps.read_text()
    text = text.replace("typedef long dbus_int64_t", "typedef long long dbus_int64_t")
    text = text.replace("typedef unsigned long dbus_uint64_t", "typedef unsigned long long dbus_uint64_t")
    text = text.replace("(val##L))", "(val##LL))").replace("(val##UL))", "(val##ULL))")
    if not shadow.exists() or shadow.read_text() != text:
        shadow.write_text(text)
    c_flags_32 += " -I" + str(sdl_build / "include32")
sdl_args.append(f"-DCMAKE_C_FLAGS={c_flags_32}")
stamp = sdl_build / "rood-configure.txt"
# --coff links only the Windows SDL below.
if not coff and (not (sdl_build / "CMakeCache.txt").exists() or not stamp.exists()
                   or stamp.read_text() != "\n".join(sdl_args)):
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []
    run(["cmake", "-S", str(sdl_source), "-B", str(sdl_build), *generator, *sdl_args], stdout=subprocess.DEVNULL)
    stamp.write_text("\n".join(sdl_args))
if not coff:
    run(["cmake", "--build", str(sdl_build), "--parallel", str(min(os.cpu_count() or 4, 8))], stdout=subprocess.DEVNULL)
win_sdl_library = root / "pc/build/sdl3_win/libSDL3.a"
if win:
    # SDL 3 for Windows (static; its video, audio and input backends are
    # Win32, WASAPI/DirectSound, XInput/RawInput/HIDAPI).
    win_sdl_build = win_sdl_library.parent
    win_sdl_args = [
        "-DCMAKE_SYSTEM_NAME=Windows",
        f"-DCMAKE_C_COMPILER={MINGW_GCC}",
        f"-DCMAKE_CXX_COMPILER={MINGW_GXX}",
        f"-DCMAKE_RC_COMPILER={MINGW}windres",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DSDL_SHARED=OFF",
        "-DSDL_STATIC=ON",
        "-DSDL_TESTS=OFF",
        "-DSDL_TEST_LIBRARY=OFF",
        "-DSDL_EXAMPLES=OFF",
        "-DSDL_INSTALL=OFF",
        "-DSDL_CAMERA=OFF",
        "-DSDL_GPU=OFF",
        "-DSDL_RENDER_GPU=OFF",
        "-DSDL_VULKAN=ON",
        "-DSDL_RENDER_VULKAN=OFF",
    ]
    stamp = win_sdl_build / "rood-configure.txt"
    if not (win_sdl_build / "CMakeCache.txt").exists() or not stamp.exists() or stamp.read_text() != "\n".join(win_sdl_args):
        generator = ["-G", "Ninja"] if shutil.which("ninja") else []
        run(["cmake", "-S", str(sdl_source), "-B", str(win_sdl_build), *generator, *win_sdl_args], stdout=subprocess.DEVNULL)
        stamp.write_text("\n".join(win_sdl_args))
    run(["cmake", "--build", str(win_sdl_build), "--parallel", str(min(os.cpu_count() or 4, 8))], stdout=subprocess.DEVNULL)
# No-op when nothing changed (pc/tools/build_cache.py): the last complete
# build of this configuration in this tree saw the same inputs and left these
# outputs. Otherwise every compile below is checked against the object cache.
from build_cache import ObjectCache, ResultCache, Stamp, tool_identity

stamp_tools = ["gcc", "g++", "nm", "objcopy", "objdump", "readelf", "gdb", "mipsel-linux-gnu-ld", "docker"] \
    + ([MINGW_GCC, MINGW_GXX] + [MINGW + t for t in ["objcopy", "objdump", "nm", "readelf", "windres"]] if win else [])
toolchain = tool_identity(stamp_tools)
build_stamp = Stamp(root, out, {
    "battle": battle, "no_audio": no_audio, "arch": parsed.arch, "target": parsed.target, "coff": coff,
    "nrd": [nrd and nrd["libraries"], win_nrd and win_nrd["libraries"]], "toolchain": toolchain,
})
stamp_trees = [("src", None), ("include", None), ("config", None), ("build", None), ("pc/src", None),
               ("pc/include", None), ("pc/shaders", None), ("pc/third_party", None), ("tools/etc", ".py"),
               ("pc/tests", (".c", ".h"))] \
    + [(f"pc/tools/{tool}.py", None) for tool in ["build_boot", "build_cache", "event_callbacks", "lp64_report",
                                                   "import_types", "nrd", "gl_shaders", "vk_shaders"]]
stamp_files = [root / "data/SLUS_010.40", root / "data/BATTLE/BATTLE.PRG", root / "data/ENDING/ENDING.PRG",
               sdl_library, *(nrd["libraries"] if nrd else []),
               *([win_sdl_library, *(win_nrd["libraries"] if win_nrd else [])] if win else []),
               # the host libraries whose symbols the import classification reads
               "/usr/lib/x86_64-linux-gnu/libstdc++.so.6", "/lib/x86_64-linux-gnu/libc.so.6",
               "/lib/x86_64-linux-gnu/libm.so.6", "/usr/lib/x86_64-linux-gnu/libc_nonshared.a",
               "/usr/lib32/libstdc++.so.6", "/lib/i386-linux-gnu/libc.so.6", "/lib/i386-linux-gnu/libm.so.6",
               "/lib/x86_64-linux-gnu/libgcc_s.so.1", "/lib/i386-linux-gnu/libgcc_s.so.1"]
up_to_date = build_stamp.up_to_date(stamp_trees, stamp_files)
if parsed.check:
    print(f"{out.relative_to(root)}: " + ("up to date" if up_to_date else "would rebuild"))
    raise SystemExit(0 if up_to_date else 1)
if up_to_date and not parsed.verbose:
    print(f"{out.relative_to(root)}: up to date (nothing changed since its last build)")
    raise SystemExit(0)
build_stamp.invalidate()  # until this build completes
object_cache = ObjectCache(root, out, build_stamp, toolchain)
result_cache = ResultCache(out, toolchain)


def query(args):
    """The output of a read-only nm/objdump/readelf of one file (its last
    argument), cached by the file's content."""
    return result_cache.run(args, root).decode()


def finish(*outputs):
    """A complete build: the next one with the same inputs is a no-op."""
    build_stamp.write(stamp_trees, stamp_files, [p for p in outputs if p and pathlib.Path(p).exists()])
    result_cache.prune()


# Keep the full data symbol manifest reproducible; upstream intentionally strips
# its exported ELF. This local-only reference contains no host executable code.
ldargs = [
    "-nostdlib",
    "--build-id=none",
    "--unresolved-symbols=ignore-all",
    "--noinhibit-exe",
    "-L",
    "build/config/SLUS_010.40",
    "-T",
    "link.ld",
    "-T",
    "undefined_funcs_auto.txt",
    "-T",
    "undefined_syms_auto.txt",
    "-o",
    str(out.relative_to(root) / "main-symbols.elf"),
]
link_symbols(ldargs)
if battle:
    # The overlays resolve main's symbols with -R. Main's ELF also keeps
    # absolute symbols that are other overlays' data (exports.txt retains
    # vs_battle_mapCompletionFlags, BATTLE's, for the MENU links): BATTLE's
    # objects define those themselves, so -R reads a copy without them.
    slus_elf = root / "build/data/SLUS_010.40.elf"
    main_symbols = out / "slus-symbols.elf"
    foreign = sorted(parts[7] for parts in map(str.split, query([*READELF, "-sW", str(slus_elf)]).splitlines())
                     if len(parts) >= 8 and parts[6] == "ABS" and parts[3] != "FILE")
    stamp = main_symbols.with_name(main_symbols.name + ".inputs")
    key = f"{slus_elf.stat().st_mtime_ns} {' '.join(foreign)}"
    if not (main_symbols.exists() and stamp.exists() and stamp.read_text() == key):
        stamp.unlink(missing_ok=True)
        result = mips_tool("objcopy", [*[f"--strip-symbol={n}" for n in foreign],
                                       str(slus_elf.relative_to(root)), str(main_symbols.relative_to(root))])
        if result.returncode:
            raise SystemExit(f"objcopy {slus_elf.name}: {result.stderr.strip()}")
        stamp.write_text(key)
    for config, name in [
        ("BATTLE/BATTLE.PRG", "battle-symbols"),
        ("BATTLE/INITBTL.PRG", "init-symbols"),
    ]:
        args = [
            "-nostdlib",
            "--build-id=none",
            "--unresolved-symbols=ignore-all",
            "--noinhibit-exe",
            "-L",
            f"build/config/{config}",
            "-T",
            "link.ld",
            "-R",
            str(main_symbols.relative_to(root)),
            "-o",
            str(out.relative_to(root) / (name + ".elf")),
        ]
        link_symbols(args)
objects = []
sources = [
    ("src/SLUS_010.40/main.c", "main"),
    ("src/TITLE/TITLE.PRG/22C.c", "title"),
    ("src/TITLE/TITLE.PRG/titleData.c", "title_data"),
    ("pc/src/boot_sdk.c", "sdk"),
    ("src/SLUS_010.40/25AC.c", "sound_driver"),
    # The game's VLC table builder for the intro movie (DecDCTvlcBuild).
    ("src/TITLE/TITLE.PRG/libpress/BUILD.c", "title_vlc_build"),
]
if battle:
    # Only explicitly identified callback tables are rehomed. Verify their exact targets
    # against the original, privately supplied overlay before accepting a build.
    overlay = (root / "data/BATTLE/BATTLE.PRG").read_bytes()
    for address, targets in [
        (0x800EC25C, [0x800CBBCC, 0x800CC128, 0x800CC204, 0x800CC600]),
        (
            0x800F1760,
            [0x800E3EAC, 0x800E3C30] * 2
            + [0x800E3FF4, 0x800E3C3C] * 2
            + [0x800E40F0, 0x800E3C48] * 2,
        ),
        (
            0x800F1790,
            [
                0x800DBA4C,
                0x800DBA88,
                0x800DBB98,
                0x800DA5CC,
                0x800DA6A4,
                0x800DA738,
                0x800DAA6C,
                0x800DAAD8,
                0x800DBC0C,
                0,
            ],
        ),
        (
            0x800E85CC,
            [
                0x80091C6C,
                0x80091FE8,
                0x80091E10,
                0x80092540,
                0x80092540,
                0x80092540,
                0x800923F8,
            ],
        ),
        (
            0x800E85E8,
            [
                0x80092C68,
                0x80092E14,
                0x80092F6C,
                0x80092F74,
                0x80092F98,
                0x80092FBC,
                0x8009352C,
                0x800935B8,
                0x80092E7C,
                0x800935F8,
                0x8009361C,
                0x800936F0,
                0x80093640,
            ],
        ),
    ]:
        actual = struct.unpack_from(
            "<" + "I" * len(targets), overlay, address - 0x80068800
        )
        if list(actual) != targets:
            raise RuntimeError(
                f"Callback table {address:#x} differs from native mapping"
            )
    # The event table lives in guest data, but its targets must be native C.
    # Generate only this bounded table; never reinterpret arbitrary data as code.
    from event_callbacks import generate_event_callbacks, generate_effect_callbacks

    symbol_text = run(
        [*READELF, "-sW", str(out / "battle-symbols.elf")],
        capture_output=True,
        text=True,
    ).stdout
    (generated / "event_callbacks.inc").write_text(
        generate_event_callbacks(
            overlay,
            symbol_text,
            (root / "src/BATTLE/BATTLE.PRG/4A0A8.c").read_text(),
            (root / "src/BATTLE/BATTLE.PRG/4A0A8.h").read_text(),
        )
    )
    (generated / "effect_callbacks.inc").write_text(
        generate_effect_callbacks(
            overlay,
            symbol_text,
            (root / "src/BATTLE/BATTLE.PRG/5BF94.c").read_text(),
        )
    )
    sources += [
        ("src/EFFECT/EFFPURGE.BIN/0.c", "effect_dispatch"),
        ("src/GIM/SCREFF2.PRG/0.c", "screff2_main"),
        ("src/GIM/SCREFF2.PRG/5E8.c", "screff2_border"),
        ("pc/src/battle_helpers.c", "battle_helpers"),
        ("pc/src/battle_gte.c", "battle_gte"),
        ("pc/src/battle_movement.c", "battle_movement"),
        ("pc/src/battle_motion.c", "battle_motion"),
        ("pc/src/actor_prepare.c", "actor_prepare"),
        ("pc/src/actor_vertices.c", "actor_vertices"),
        ("pc/src/actor_packets.c", "actor_packets"),
        ("pc/src/actor_blend.c", "actor_blend"),
        ("pc/src/fixed_font.c", "fixed_font"),
        ("pc/src/weapon_packets.c", "weapon_packets"),
        ("pc/src/actor_shadow.c", "actor_shadow"),
        ("pc/src/actor_update.c", "actor_update"),
        ("pc/src/actor_cleanup.c", "actor_cleanup"),
        ("pc/src/navigation.c", "navigation"),
        ("pc/src/room_render.c", "room_render"),
        ("pc/src/room_geometry.c", "room_geometry"),
        ("pc/src/room_lighting.c", "room_lighting"),
        ("pc/src/screff2_warp.c", "screff2_warp"),
        ("pc/src/effect_particles.c", "effect_particles"),
        ("pc/src/map_view.c", "map_view"),
        ("pc/src/dev_state.c", "dev_state"),
        ("pc/src/room_warp.c", "room_warp"),
        ("pc/src/fast_travel.c", "fast_travel"),
        ("pc/src/weapon_slots.c", "weapon_slots"),
        ("pc/src/button_roles.c", "button_roles"),
        ("pc/src/text_lines.c", "text_lines"),
        ("pc/src/ui/menu_bridge.c", "ui_menu_bridge"),
        ("pc/src/ui/equip_bridge.c", "ui_equip_bridge"),
        ("pc/src/ui/ashley_bridge.c", "ui_ashley_bridge"),
        ("pc/src/ui/abilities_bridge.c", "ui_abilities_bridge"),
        ("pc/src/ui/magick_bridge.c", "ui_magick_bridge"),
        ("pc/src/ui/items_bridge.c", "ui_items_bridge"),
        ("pc/src/ui/inventory_ops.c", "ui_inventory_ops"),
        ("pc/src/ui/container_ops.c", "ui_container_ops"),
        ("pc/src/ui/loot_bridge.c", "ui_loot_bridge"),
        ("pc/src/ui/map_bridge.c", "ui_map_bridge"),
        ("pc/src/ui/map_pad.c", "ui_map_pad"),
        ("pc/src/ui/data_bridge.c", "ui_data_bridge"),
        ("pc/src/ui/container_bridge.c", "ui_container_bridge"),
        ("pc/src/ui/manual_bridge.c", "ui_manual_bridge"),
        ("pc/src/ui/score_bridge.c", "ui_score_bridge"),
        ("pc/src/ui/workshop_bridge.c", "ui_workshop_bridge"),
        ("pc/src/ui/game_options.c", "ui_game_options"),
    ]
    sources += [
        (p.relative_to(root).as_posix(), "battle_" + p.stem)
        for p in sorted((root / "src/BATTLE/BATTLE.PRG").glob("*.c"))
    ]
    sources += [
        (p.relative_to(root).as_posix(), "init_" + p.stem)
        for p in sorted((root / "src/BATTLE/INITBTL.PRG").glob("*.c"))
    ]
    # The main menu overlay (loaded at 0x800F9800 like the effect plugins);
    # its data stays in guest RAM where the original loader places it.
    sources += [
        (p.relative_to(root).as_posix(), "mainmenu_" + p.stem)
        for p in sorted((root / "src/MENU/MAINMENU.PRG").glob("*.c"))
    ]
    # Submenu overlays MENU0-F share the 0x80102800 slot; their global names
    # are distinct, so they link side by side. Their data is defined in C.
    sources += [
        (p.relative_to(root).as_posix(), p.parent.name.split(".")[0].lower() + "_" + p.stem)
        for p in sorted((root / "src/MENU").glob("MENU?.PRG/*.c"))
    ]
    # The ending (credits, score and rank screens; main.c's
    # vs_main_showEndingAndReturnToTitle) loads at 0x80068800 over BATTLE.
    # MENUF reuses some of its global names, so its globals are prefixed
    # ending_ after compiling; its data stays in guest RAM like MAINMENU's.
    sources += [
        (p.relative_to(root).as_posix(), "ending_" + p.stem)
        for p in sorted((root / "src/ENDING/ENDING.PRG").glob("*.c"))
    ]
    sources += [
        (p.relative_to(root).as_posix(), "gpu_" + p.stem)
        for p in sorted((root / "src/SLUS_010.40/libgpu").glob("P*.c"))
    ]
if battle:
    # Effect plug-ins (PLGnnn.BIN) all load at 0x800F9800 and reuse function
    # names, so each object's own symbols are prefixed plgnnn_ after compiling;
    # their data resolves against that PLG's ELF (the loader puts the image in
    # guest RAM). pc/src/effects/plgNNN.c holds native-only translations.
    plg_sources = sorted((root / "src/EFFECT").glob("PLG*.BIN/*.c"))
    sources += [
        (p.relative_to(root).as_posix(), p.parent.name.split(".")[0].lower() + "_" + p.stem)
        for p in plg_sources
    ]
    sources += [
        (p.relative_to(root).as_posix(), p.stem.lower() + "_native")
        for p in sorted((root / "pc/src/effects").glob("plg*.c"))
    ]

# Include path and defines of the game sources (src/ and the pc/src files
# built like them). pc/include comes first: its SDK wrappers give the 64-bit
# build the PS1's 32-bit long (pc/include/psx_sdk_long_begin.h).
game_cpp_flags = [
    "-nostdinc",
    "-Ipc/include",
    "-Iinclude/psx",
    "-Isrc/include",
    *(["-iquote", str(out.relative_to(root) / "include"), "-DROOD_PC_LP64"] if arch64 else []),
    "-I.",
    "-D_LANGUAGE_C",
    "-DOBJDIFF",
    "-DROOD_PC",
    "-DROOD_PC_BOOT_DIAGNOSTIC",
    # psx_ptr/psx_get/PSX_SIZE/PSX_ASSERT_* in every game unit; on 32 bits
    # they expand to the original pointers and literals.
    "-include",
    "pc/src/psx_types.h",
]
# PS1 SDK layouts (packets, OT entries, GTE and environment types) must hold
# on both architectures: a regression stops the build, --arch 64 included.
for layout_pass in [[], ["-DPSX_SDK_LAYOUT_LIBDS"]]:
    if not coff:
        run(["gcc", *flags, *game_cpp_flags, *layout_pass, "-fsyntax-only", "pc/src/psx_sdk_layout.c"])
    if win:  # and under Windows' LLP64
        run(win_command(["gcc", *flags, *game_cpp_flags, *layout_pass, "-fsyntax-only", "pc/src/psx_sdk_layout.c"]))

overlay_statics = {}  # overlay file on the disc -> {"data", "bss"} sections present
win_overlay_statics = {}  # the same for the COFF twins
game_objects = []  # objects of the game's (-nostdinc) units
for source, name in sources:
    cpp_args = [
        "-E",
        *game_cpp_flags,
        *(["-DROOD_PC_NO_AUDIO"] if no_audio else []),
        *[
            f"-D{n}=pc_psx_{n}"
            for n in ["open", "read", "write", "close", "rename"]
        ],
        f"-DMENUBGSZ={(root / 'build/assets/TITLE/TITLE.PRG/_titleScreenBg.rleRgb555.img.bin').stat().st_size}",
        source,
    ]
    cpp = None if coff else (subprocess.run if arch64 else run)(
        ["gcc", *cpp_args],
        capture_output=True,
        **({"cwd": root} if arch64 else {}),
    )
    win_pre = None
    if win and (coff or not cpp.returncode):
        # The twin is preprocessed by MinGW: the compiler's own macros
        # (__UINTPTR_TYPE__ is unsigned long long under LLP64) end up in the
        # source.
        win_cpp = subprocess.run([MINGW_GCC, "-mno-ms-bitfields", "-D_WIN32_WINNT=0x0A00", *cpp_args],
                                 capture_output=True, cwd=root)
        if win_cpp.returncode:
            arch64_results.append(("c", "win:" + source, win_cpp.returncode, win_cpp.stderr.decode(errors="replace")))
            continue
        win_pre = out / (name + ".win.i")
        win_pre.write_bytes(lp64_longs(result_cache.run([sys.executable, "-m", "tools.etc.vsStringTransformer"], root,
                                                        files=sorted((root / "tools/etc").glob("vsString*.py")),
                                                        input=win_cpp.stdout)))
    if not coff and arch64 and cpp.returncode:  # record it and go on, like a compile error
        arch64_results.append(("c", source, cpp.returncode, cpp.stderr.decode(errors="replace")))
        continue
    pre = out / (name + ".i")
    if not coff:
        encoded = result_cache.run([sys.executable, "-m", "tools.etc.vsStringTransformer"], root,
                                   files=sorted((root / "tools/etc").glob("vsString*.py")), input=cpp.stdout)
        pre.write_bytes(encoded)
    obj = out / (name + ".o")
    # main.c and MAINMENU statics that only other code calls (by address on
    # the PS1) must survive compilation to be exposed below.
    keep = ["-fkeep-static-functions"] if name == "main" or name.startswith("mainmenu_") else []
    if not compile_object(["gcc", *flags, *keep, "-c", str(pre), "-o", str(obj)], source,
                          win_args=win_pre and ["gcc", *flags, *keep, "-c", str(win_pre), "-o", str(obj)]):
        continue
    # Overlay (not executable) C: its .data/.bss go to sections named after the
    # overlay file so the runtime can restart them when the file is reloaded.
    parts = pathlib.PurePosixPath(source).parts
    if parts[0] == "src" and parts[1] != "SLUS_010.40":
        overlay_file = "/".join(parts[1:3])
        key = re.sub(r"\W", "_", overlay_file).lower()
        renames = []
        for line in [] if coff else query(["readelf", "-SW", str(obj)]).splitlines():
            section = re.match(r"\s*\[\s*\d+\]\s+(\.(data|bss)(\.\S+)?)\s+\w+\s+\w+\s+\w+\s+(\w+)", line)
            if section and int(section.group(4), 16):
                kind = section.group(2)
                renames.append(f"--rename-section={section.group(1)}=rood_ovl_{key}_{kind}")
                overlay_statics.setdefault(overlay_file, set()).add(kind)
        if renames:
            run(["objcopy", *renames, str(obj)])
        if win:
            # COFF: .data$<name> (GCC on MinGW puts zero-initialised statics
            # there too). ld sorts grouped .data$ sections by name, so
            # .data$rood_ovl_<key>_data_m sits between the _a and _z markers
            # of overlay_statics_win.c.
            renames = []
            headers = query([MINGW + "objdump", "-h", str(twin(obj))])
            for section, kind, size in re.findall(r"^\s*\d+\s+(\.(data|bss)(?:\$\S+)?)\s+([0-9a-f]+)\s", headers, re.M):
                if int(size, 16) and kind == "bss":
                    # The default PE script does not sort .bss$ sections the
                    # same way; markers there are unverified.
                    raise RuntimeError(f"{twin(obj).name}: overlay statics in {section} (expected .data$)")
                if int(size, 16):
                    renames.append(f"--rename-section={section}=.{kind}$rood_ovl_{key}_{kind}_m")
                    win_overlay_statics.setdefault(overlay_file, set()).add(kind)
            if renames:
                run([MINGW + "objcopy", *renames, str(twin(obj))])
    else:
        state_sections(obj)
    objects.append(obj)
    game_objects.append(obj)
for name in ["runtime", "window", "controller", "gamepad"]:
    obj = out / (name + ".o")
    if compile_object(["gcc", *flags, *sdl_flags, "-c", f"pc/src/boot_{name}.c", "-o", str(obj)], f"pc/src/boot_{name}.c") \
            and name in STATE_HOST_UNITS:
        state_sections(obj)
    objects.append(obj)
for name in ["audio_libspu", "audio_output", "input_record", "cd_drive", "xa_audio", "mdec", "memory_card", "dev_rpc", "charge_skip", "camera_stick", "gl_present", "gpu_hw", "render", "room_native", "gameplay_walls", "room_effects", "actor_native", "effect_native", "scene_capture", "floor_grid", "shadows", "ptgi", "glows", "overlay_statics", "ui_upscale", "antialias", "ui_text", "button_prompts", "ui_bubbles", "ui_emotes", "room_catalog", "fast_travel_rules", "ui/item_names", "ui/manual_parse", "ui/manual_cache", "ui/data_plan", "ui/score_format", "ui/score_nav", "ui/mon_parse", "ui/score_mon", "snapshot"] + (["vk_core", "vk_hw", "vk_present", "vk_shadows"] if arch64 else []):
    obj = out / (name.replace("/", "_") + ".o")
    # ui_text rasterizes fonts with the stb_truetype that Dear ImGui vendors.
    extra = ["-Ipc/third_party/imgui"] if name == "ui_text" else []
    if compile_object(["gcc", *flags, *sdl_flags, *extra, "-c", f"pc/src/{name}.c", "-o", str(obj)], f"pc/src/{name}.c") \
            and name in STATE_HOST_UNITS:
        state_sections(obj)
    objects.append(obj)
if not arch64:
    # Gamepad mapping test, and --hardware to report a connected gamepad.
    run(["gcc", "-m32", "-O2", "-fno-pie", "-no-pie", "-DROOD_CONTROLLER_HARDWARE", "-Ipc/src", *sdl_flags,
        "pc/tests/controller_probe.c", str(out / "controller.o"), str(out / "gamepad.o"), str(sdl_library),
        "-lm", "-ldl", "-lpthread", "-o", str(out / "controller_probe")])
    run([str(out / "controller_probe")])
    run(["gcc", "-m32", "-O2", "-fno-pie", "-no-pie", "pc/tests/memory_card_probe.c", str(out / "memory_card.o"),
        "-o", str(out / "memory_card_probe")])
    run([str(out / "memory_card_probe")])
    # Movie decoding: synthetic MDEC/XA cases, then TITLE.STR frames from the disc.
    run(["gcc", "-m32", "-O2", "-fno-pie", "-no-pie", "pc/tests/movie_probe.c", str(out / "mdec.o"),
        str(out / "xa_audio.o"), str(out / "title_vlc_build.o"), "-lm", "-o", str(out / "movie_probe")])
    run([str(out / "movie_probe")])
# Software SPU (C++). Without g++-multilib the 32-bit C++ config header is
# missing; fall back to the 64-bit one, which only differs in word-size macros
# the compiler itself provides. Install g++-multilib to avoid the fallback.
audio_vendor = root / "pc/third_party/psycross-audio"
cxx_flags = ["-m64", "-O2", "-std=c++17", "-fpie", "-g"] if arch64 else ["-m32", "-O2", "-std=c++17", "-fno-pie", "-fno-pic", "-g"]
probe_cxx = None if coff else subprocess.run(
    ["g++", *cxx_flags, "-x", "c++", "-fsyntax-only", "-"],
    input=b"#include <vector>\n",
    capture_output=True,
)
if probe_cxx and probe_cxx.returncode:
    fallback = out / "cxx32"
    (fallback / "bits").mkdir(parents=True, exist_ok=True)
    version = subprocess.run(["g++", "-dumpversion"], capture_output=True, text=True).stdout.strip()
    shutil.copy(f"/usr/include/x86_64-linux-gnu/c++/{version}/bits/c++config.h", fallback / "bits")
    cxx_flags += ["-I" + str(fallback), f"-I/usr/include/x86_64-linux-gnu/c++/{version}"]
for source in sorted((audio_vendor / "src/audio").glob("*.cpp")) + [root / "pc/src/audio_spu.cpp"]:
    obj = out / ("spu_" + source.stem + ".o")
    compile_object(
        [
            "g++",
            *cxx_flags,
            "-I" + str(audio_vendor / "include"),
            "-I" + str(audio_vendor / "src/audio"),
            "-Ipc/src",
            "-c",
            str(source),
            "-o",
            str(obj),
        ],
        source.relative_to(root),
        "c++",
    )
    objects.append(obj)
# Dear ImGui (vendored, MIT) and the developer overlay.
imgui = root / "pc/third_party/imgui"
for source in [
    imgui / "imgui.cpp",
    imgui / "imgui_draw.cpp",
    imgui / "imgui_tables.cpp",
    imgui / "imgui_widgets.cpp",
    imgui / "backends/imgui_impl_opengl3.cpp",
    imgui / "backends/imgui_impl_sdl3.cpp",
    root / "pc/third_party/ImGuizmo/src/ImGuizmo.cpp",
    root / "pc/src/dev_overlay.cpp",
    root / "pc/src/lights.cpp",
    root / "pc/src/light_actors.cpp",
    root / "pc/src/light_prefabs.cpp",
    root / "pc/src/fixture_catalog.cpp",
    root / "pc/src/fixture_editor.cpp",
    root / "pc/src/emissive.cpp",
    root / "pc/src/emissive_editor.cpp",
    root / "pc/src/materials.cpp",
    root / "pc/src/room_settings.cpp",
    root / "pc/src/lighting_layers.cpp",
    root / "pc/src/lighting_editor.cpp",
    root / "pc/src/environment.cpp",
    root / "pc/src/atmosphere.cpp",
    root / "pc/src/post_profile.cpp",
    root / "pc/src/fog_sim.cpp",
    root / "pc/src/particles.cpp",
    root / "pc/src/particle_assets.cpp",
    root / "pc/src/particles_editor.cpp",
    root / "pc/src/room_browser.cpp",
    root / "pc/src/project_settings.cpp",
    root / "pc/src/lighting_quality.cpp",
    root / "pc/src/editor_view.cpp",
    root / "pc/src/editor_overlays.cpp",
    root / "pc/src/editor_undo.cpp",
    root / "pc/src/editor_selection.cpp",
    root / "pc/src/editor_picking.cpp",
    root / "pc/src/editor_scene.cpp",
    root / "pc/src/editor_clipboard.cpp",
    root / "pc/src/editor_schema.cpp",
    root / "pc/src/editor_details.cpp",
    root / "pc/src/editor_math.cpp",
    root / "pc/src/editor_transform.cpp",
    root / "pc/src/editor_handles.cpp",
    root / "pc/src/editor_light_handles.cpp",
    root / "pc/src/editor_volume_handles.cpp",
    root / "pc/src/editor_colour.cpp",
    root / "pc/src/library_model.cpp",
    root / "pc/src/library_thumbs.cpp",
    root / "pc/src/editor_library.cpp",
    root / "pc/src/editor_fit_window.cpp",
    root / "pc/src/probe_overlay.cpp",
    root / "pc/src/ui_upscale_panel.cpp",
    root / "pc/src/ui/ui_shell.cpp",
    root / "pc/src/ui/ui_settings.cpp",
    root / "pc/src/ui/ui_theme.cpp",
    root / "pc/src/ui/menu_light.cpp",
    root / "pc/src/ui/menu_light_kind.cpp",
    root / "pc/src/ui/ui_tint.cpp",
    root / "pc/src/ui/ui_draw.cpp",
    root / "pc/src/ui/ui_equip_draw.cpp",
    root / "pc/src/ui/ui_ashley_draw.cpp",
    root / "pc/src/ui/ui_system_draw.cpp",
    root / "pc/src/ui/ui_abilities_draw.cpp",
    root / "pc/src/ui/ui_magick_draw.cpp",
    root / "pc/src/ui/ui_items_draw.cpp",
    root / "pc/src/ui/ui_loot.cpp",
    root / "pc/src/ui/ui_loot_draw.cpp",
    root / "pc/src/ui/ui_map.cpp",
    root / "pc/src/ui/ui_map_draw.cpp",
    root / "pc/src/ui/ui_data.cpp",
    root / "pc/src/ui/ui_data_draw.cpp",
    root / "pc/src/ui/ui_container.cpp",
    root / "pc/src/ui/ui_container_draw.cpp",
    root / "pc/src/ui/ui_workshop.cpp",
    root / "pc/src/ui/ui_workshop_draw.cpp",
    root / "pc/src/ui/ui_manual.cpp",
    root / "pc/src/ui/ui_manual_draw.cpp",
    root / "pc/src/ui/ui_score.cpp",
    root / "pc/src/ui/ui_score_draw.cpp",
    root / "pc/src/prompt_glyphs.cpp",
    root / "pc/src/fast_travel_prompt.cpp",
] + ([imgui / "backends/imgui_impl_vulkan.cpp", root / "pc/src/vk_overlay.cpp"] if arch64 else []):
    obj = out / ("imgui_" + source.stem + ".o")
    # The object cache decides what recompiles; the 64-bit pass reports
    # every unit (a cached compile replays its warnings).
    compile_object(["g++", *cxx_flags, "-I" + str(imgui), "-I" + str(root / "pc/third_party/ImGuizmo/src"),
        "-I" + str(root / "pc/src"),
        # imgui_impl_vulkan loads its entry points (vk_overlay.cpp hands it the loader)
        *(["-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES"] if arch64 else []),
        *sdl_flags, "-c", str(source), "-o", str(obj)], source.relative_to(root), "c++")
    objects.append(obj)
if arch64:
    # The NRD adapter (vk_nrd.cpp; a stub reporting "unavailable" without NRD).
    obj = out / "vk_nrd.o"
    compile_object(["g++", *cxx_flags, *sdl_flags, *(["-DROOD_NRD", "-I" + nrd["include"]] if nrd else []),
        "-c", "pc/src/vk_nrd.cpp", "-o", str(obj)], "pc/src/vk_nrd.cpp", "c++")
    objects.append(obj)
# Lua 5.4 (vendored, MIT; pc/third_party/lua) for plugins (pc/docs/Extensions.md):
# the core and its safe libraries only (no io, os, package or debug, no
# standalone interpreters), compiled as C++ so a Lua error is a C++
# exception and the bindings may keep std::string across Lua calls; with
# the pattern matcher's budget hook (rood_luauser.h, ROOD-NOTES.md).
lua_source = root / "pc/third_party/lua/src"
LUA_CORE = ["lapi", "lcode", "lctype", "ldebug", "ldo", "ldump", "lfunc", "lgc", "llex", "lmem", "lobject",
            "lopcodes", "lparser", "lstate", "lstring", "ltable", "ltm", "lundump", "lvm", "lzio", "lauxlib",
            "lbaselib", "lcorolib", "lmathlib", "lstrlib", "ltablib", "lutf8lib"]
for name in LUA_CORE:
    obj = out / ("lua_" + name + ".o")
    compile_object(["g++", *cxx_flags, "-DROOD_LUA_CORE", "-include", "pc/src/lua/rood_luauser.h",
                    "-I" + str(lua_source), "-x", "c++", "-c", str(lua_source / (name + ".c")), "-o", str(obj)],
                   f"pc/third_party/lua/src/{name}.c", "c++")
    objects.append(obj)
# The extension system (pc/src/lua): runtime, API, RPC, console.
for source in sorted((root / "pc/src/lua").glob("*.cpp")):
    obj = out / (source.stem + ".o")
    compile_object(["g++", *cxx_flags, "-I" + str(lua_source), "-I" + str(imgui), "-Ipc/src", "-Ipc/src/lua",
                    *sdl_flags, "-c", str(source), "-o", str(obj)], source.relative_to(root), "c++")
    objects.append(obj)
vendor = root / "pc/third_party/psycross"
for name, compiler, source, extra in [
    ("inline", "gcc", "src/psx/INLINE_C.C", ["-x", "c", "-std=gnu11"]),
    ("gte", "g++", "src/gte/PsyX_GTE.cpp", ["-nostdinc++"]),
    (
        "libgte",
        "gcc",
        "src/psx/LIBGTE.C",
        [
            "-x",
            "c",
            "-std=gnu11",
            "-fgnu89-inline",
            "-DInitGeom=psycross_InitGeom",
            "-DCompMatrixLV=psycross_CompMatrixLV",
        ],
    ),
]:
    obj = out / (name + ".o")
    if compile_object(
        [
            compiler,
            *[f for f in flags if compiler != "g++" or f not in ["-fgnu89-inline", *c_only_flags]],
            *extra,
            "-DUSE_PGXP=0",
            "-DUSE_EXTENDED_PRIM_POINTERS=0",
            "-I" + str(vendor / "include"),
            "-c",
            str(vendor / source),
            "-o",
            str(obj),
        ],
        f"pc/third_party/psycross/{source}",
        "c" if compiler == "gcc" else "c++",
    ) and name in STATE_HOST_UNITS:
        state_sections(obj)
    objects.append(obj)
if arch64:
    from lp64_report import write_report

    report = write_report(out, arch64_results)
    print(f"64-bit pass: {report}")
    if any(code for _, _, code, _ in arch64_results):
        if win:  # a previous rood.exe would otherwise look freshly built
            raise SystemExit(f"Windows build: units failed to compile (see {report})")
        raise SystemExit(0)  # measurement only until every unit compiles
# SDK/function imports are explicit fail-fast traps. Data imports retain original
# addresses into the private disc image. Never emit a callable MIPS address.
addresses = {}
for config in ["SLUS_010.40", "TITLE/TITLE.PRG", "BATTLE/BATTLE.PRG"]:
    for p in [
        root / f"config/{config}/symbol_addrs.txt",
        root / f"build/config/{config}/undefined_syms_auto.txt",
    ]:
        if p.exists():
            for name, value in re.findall(
                r"^(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", p.read_text(), re.M
            ):
                addresses.setdefault(name, int(value, 16))
# Full reference ELF distinguishes data objects from functions, including assets.
functions = set()
function_addresses = {}
for elf in [out / "main-symbols.elf", root / "build/data/TITLE/TITLE.PRG.elf"] + (
    [
        out / "battle-symbols.elf",
        out / "init-symbols.elf",
        root / "build/data/MENU/MAINMENU.PRG.elf",
    ]
    + sorted((root / "build/data/MENU").glob("MENU?.PRG.elf"))
    if battle
    else []
):
    symbols = query([*READELF, "-sW", str(elf)])
    for line in symbols.splitlines():
        parts = line.split()
        if (
            len(parts) >= 8
            and parts[3] in ("OBJECT", "FUNC", "NOTYPE")
            and parts[6] != "UND"
        ):
            name = parts[7]
            if parts[3] == "FUNC":
                functions.add(name)
                function_addresses[name] = int(parts[1], 16)
            else:
                addresses[name] = int(parts[1], 16)
ending_elf = root / "build/data/ENDING/ENDING.PRG.elf"
if battle:
    # ENDING's names only fill gaps: where an earlier overlay uses one name
    # for other data (MENU9's _score), ENDING's objects get name__ending.
    for line in query([*READELF, "-sW", str(ending_elf)]).splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[3] in ("OBJECT", "FUNC", "NOTYPE") and parts[6] != "UND":
            if parts[3] == "FUNC":
                functions.add(parts[7])
                function_addresses.setdefault(parts[7], int(parts[1], 16))
            else:
                addresses.setdefault(parts[7], int(parts[1], 16))
# Overlays are linked side by side, but on the PS1 each was loaded alone, so two
# overlays may import different data under one name (TITLE's _fontTable is not
# BATTLE's). Give an object its own overlay's address for such a name by
# renaming the reference to name__overlay; everything else keeps one address.
overlay_elfs = {"main": out / "main-symbols.elf", "title": root / "build/data/TITLE/TITLE.PRG.elf"}
if battle:
    overlay_elfs.update(
        {
            "battle": out / "battle-symbols.elf",
            "init": out / "init-symbols.elf",
            "mainmenu": root / "build/data/MENU/MAINMENU.PRG.elf",
        }
    )
    for menu in sorted((root / "build/data/MENU").glob("MENU?.PRG.elf")):
        overlay_elfs[menu.name.split(".")[0].lower()] = menu
    for plg in sorted((root / "build/data/EFFECT").glob("PLG*.BIN.elf")):
        overlay_elfs[plg.name.split(".")[0].lower()] = plg
    overlay_elfs["ending"] = ending_elf
overlay_data = {}
for key, elf in overlay_elfs.items():
    overlay_data[key] = {}
    for line in query([*READELF, "-sW", str(elf)]).splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[3] in ("OBJECT", "NOTYPE") and parts[6] != "UND":
            overlay_data[key][parts[7]] = int(parts[1], 16)
def object_overlay(obj):
    stem = obj.stem
    for key in sorted(overlay_data, key=len, reverse=True):
        if stem == key or stem.startswith(key + "_"):
            return key
    return None
# Original code calls some static functions by address: overlays call main's
# (e.g. vs_main_getcurrentMusicSlot, func_80046578) and BATTLE calls the
# resident MAINMENU overlay's (func_800FA188). Expose exactly those local
# functions of the host main and MAINMENU objects that another object imports
# and nothing defines, preserving their C, instead of linking fail-fast traps.
exposing = [out / "main.o"] + sorted(o for o in objects if o.stem.startswith("mainmenu_"))
imported, defined_elsewhere = set(), set()
for obj in objects:
    for line in query([*NM, str(aobj(obj))]).splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "U":
            imported.add(parts[1])
        elif len(parts) == 3 and parts[1].isupper() and defined_name(parts[2], parts[1]):
            defined_elsewhere.add(defined_name(parts[2], parts[1]))
local_owner = {}
for obj in exposing:
    for line in query([*NM, "--defined-only", str(aobj(obj))]).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] == "t":
            local_owner.setdefault(parts[2], []).append(obj)
for obj in exposing:
    exposed = sorted(n for n in (imported - defined_elsewhere)
                     if local_owner.get(n) == [obj])
    if exposed:
        objcopy([f"--globalize-symbol={n}" for n in exposed], obj)
        print(f"{obj.name}: exposed static functions {', '.join(exposed)}")
# ENDING's globals (MENUF also defines _renderRiskbreakerRank and the
# renderTextures.h functions) become ending_<name>; main calls the entry point.
ending_objects = [obj for obj in objects if object_overlay(obj) == "ending"]
ending_globals = sorted({line.split()[2] for obj in ending_objects for line in
    query([*NM, "--defined-only", "-g", str(aobj(obj))]).splitlines()
    if len(line.split()) == 3} - {"vs_ending_exec"})
for obj in ending_objects:
    objcopy([f"--redefine-sym={n}=ending_{n}" for n in ending_globals], obj)
natively_defined = set()
for obj in objects:
    for line in query([*NM, "--defined-only", "-g", str(aobj(obj))]).splitlines():
        parts = line.split()
        if len(parts) == 3 and defined_name(parts[2], parts[1]):
            natively_defined.add(defined_name(parts[2], parts[1]))
plg_functions = {}  # plg key -> {original name: prefixed name}
for obj in objects:
    key = object_overlay(obj)
    if not key or not key.startswith("plg"):
        continue
    own = [line.split() for line in query([*NM, "--defined-only", "-g", str(aobj(obj))]).splitlines()]
    names = [parts[2] for parts in own if len(parts) == 3]
    if names:
        objcopy([f"--redefine-sym={n}={key}_{n}" for n in names], obj)
        for parts in own:
            if len(parts) == 3 and parts[1] == "T":
                plg_functions.setdefault(key, {})[parts[2]] = f"{key}_{parts[2]}"
if battle:
    # Native effect entry points, parallel to EFFPURGE's _effectEntryPoints:
    # entry i of effect e is plg<nnn>_<name> when that PLG is compiled, else 0
    # (the effect then stops with a named diagnostic).
    effpurge = (root / "src/EFFECT/EFFPURGE.BIN/0.c").read_text()
    def c_array(name):
        start = effpurge.index(name + "[] = {")
        return effpurge[start: effpurge.index("};", start)].split("{", 1)[1]
    indices = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", c_array("_effectEntryPointIndices"))]
    entry_names = re.findall(r"\w+", c_array("_effectEntryPoints"))
    plg_files = [v.strip() for v in c_array("_plgFiles").split(",") if v.strip()]
    # Byte-identical plug-ins share one decompiled source: the matching build
    # copies e.g. PLG045 to PLG290 (config/EFFECT/Makefile duplicate rules).
    makefile = (root / "config/EFFECT/Makefile").read_text().replace("\\\n", " ")
    for copies, original in re.findall(r"\$\(call PLGBINS,([\d ]+)\):\s*\$\(call PLGBINS,(\d+)\)", makefile):
        for copy in copies.split():
            if "plg" + original in plg_functions:
                plg_functions["plg" + copy] = plg_functions["plg" + original]
    entries = ["0"] * len(entry_names)
    native, missing_plgs = 0, set()
    # Effects with an entry that has no native function. The original table
    # also holds NULL entries on purpose (the PS1 stores NULL and skips the
    # slot): those stay 0 and do not make the effect unresolved.
    unresolved = [0] * len(plg_files)
    for effect, plg in enumerate(plg_files):
        number = re.match(r"PLG\((\d+)\)", plg)
        for i in range(indices[effect], indices[effect + 1]):
            if entry_names[i] == "NULL":
                continue
            symbol = number and plg_functions.get("plg" + number.group(1), {}).get(entry_names[i])
            if symbol:
                entries[i] = "(void*)" + symbol
                native += 1
            elif number:
                missing_plgs.add(number.group(1))
                unresolved[effect] = 1
    table = out / "plg_entries.c"
    table.write_text(
        "/* Generated by build_boot.py from EFFPURGE's _effectEntryPoints. */\n"
        + "".join(f"extern char {e[7:]}[];\n" for e in sorted(set(entries)) if e != "0")
        + f"void* pc_plg_entry_points[{len(entries)}] = {{ " + ", ".join(entries) + " };\n"
        + f"unsigned char pc_plg_effect_unresolved[{len(unresolved)}] = {{ "
        + ", ".join(map(str, unresolved)) + " };\n"
        + f"unsigned pc_plg_effect_count = {len(unresolved)};\n"
    )
    compile_generated(["gcc", *flags, "-c", str(table), "-o", str(out / "plg_entries.o")])
    objects.append(out / "plg_entries.o")
    nulls = sum(1 for e in entry_names if e == "NULL")
    print(f"PLG entry points: {native} of {len(entries) - nulls} native ({nulls} NULL in the original); "
          f"PLGs without C: {len(missing_plgs)}")
# Calls: i386 calls are PC32/PLT32 (data is R_386_32); on x86-64 data is
# also PC32 (RIP-relative), so only PLT32 marks a call.
call_relocation = r"R_X86_64_PLT32\s+(\w+)" if arch64 else r"R_386_(?:PC32|PLT32)\s+(\w+)"


def called_names(obj):
    """The names an object calls or jumps to. COFF has one relocation type for
    calls and RIP-relative data (IMAGE_REL_AMD64_REL32), so there the
    instruction holding the relocation decides: a call or a jump."""
    if not coff:
        return set(re.findall(call_relocation, query([*OBJDUMP, "-r", str(obj)])))
    names, instruction = set(), ""
    for line in query([*OBJDUMP, "-dr", "--no-show-raw-insn", str(twin(obj))]).splitlines():
        relocation = re.match(r"\s+[0-9a-f]+: (IMAGE_REL_AMD64_\w+)\s+([\w.$@?]+)", line)
        if relocation:
            if relocation.group(1) == "IMAGE_REL_AMD64_REL32" and re.fullmatch(r"call\w*|j[a-z]+", instruction):
                names.add(relocation.group(2))
            continue
        decoded = re.match(r"\s+[0-9a-f]+:\s+(\S+)", line)
        if decoded:
            instruction = decoded.group(1)
    return names


for obj in objects:
    key = object_overlay(obj)
    if key is None:
        continue
    renames = []
    called = called_names(obj)
    for line in query([*NM, "-u", str(aobj(obj))]).splitlines():
        name = line.split()[-1]
        own = overlay_data[key].get(name)
        # PLG ELFs list every symbol (functions too) as NOTYPE, so for them
        # also take names the global map lacks, but never calls or data the
        # port defines natively.
        local_only = key.startswith("plg") and name not in addresses and name not in called \
            and name not in natively_defined and name not in functions
        if own is not None and (local_only or (name in addresses and addresses[name] != own)) \
            and name not in called:
            renames.append(name)
            addresses[f"{name}__{key}"] = own
    if renames:
        objcopy([f"--redefine-sym={n}={n}__{key}" for n in renames], obj)
        print(f"{obj.name}: overlay-local data imports {', '.join(renames)}")
defined = set()
text_defined = set()
undefined = set()
calls = set()
for obj in objects:
    for line in query([*NM, str(aobj(obj))]).splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "U":
            undefined.add(parts[1])
        elif len(parts) == 3 and parts[1].isupper() and defined_name(parts[2], parts[1]):
            defined.add(defined_name(parts[2], parts[1]))
            if parts[1] == "T":
                text_defined.add(defined_name(parts[2], parts[1]))
    calls.update(called_names(obj))
libc = {
    "__divdi3",
    "__udivdi3",
    "__moddi3",
    "__umoddi3",
    "rename",
    "clock_gettime",
    "time",
    "ioctl",
    "open",
    "read",
    "close",
    "__errno_location",
    "memset",
    "memcpy",
    "memcmp",
    "strcmp",
    "strlen",
    "strcpy",
    "strcat",
    "sprintf",
    "printf",
    "fprintf",
    "fopen",
    "fclose",
    "fread",
    "fwrite",
    "fseek",
    "ftell",
    "perror",
    "exit",
    "abort",
    "mmap",
    "munmap",
    "strtoul",
    "__isoc23_strtoul",
    "puts",
    "stderr",
    "stdout",
    "__stack_chk_fail",
    "__stack_chk_fail_local",
    "snprintf",
    "getenv",
    "signal",
    "fflush",
    "fputc",
    "fgetc",
    "calloc",
    "nanosleep",
    "__assert_fail",
}
# Host libraries the audio objects use (C++ runtime, threads, dlopen, math).
# Windows' system libraries, for rood.exe's link and --coff's scan.
WIN_SYSTEM_LIBS = ["setupapi", "winmm", "imm32", "version", "ole32", "oleaut32", "uuid", "gdi32", "user32",
                   "shell32", "advapi32", "cfgmgr32", "ws2_32", "dbghelp"]
if coff:
    # The MinGW-w64 runtime that g++ -static links (with the console startup
    # objects: crt2.o has atexit), the system libraries rood.exe names, SDL and NRD.
    def mingw_library(name):
        return subprocess.run([MINGW_GXX, f"-print-file-name={name}"], capture_output=True,
                              text=True).stdout.strip()
    host_libraries = [mingw_library(f"lib{n}.a") for n in [
        "stdc++", "gcc", "gcc_eh", "mingw32", "mingwex", "msvcrt", "moldname", "pthread", "winpthread",
        "kernel32", *WIN_SYSTEM_LIBS]] + [mingw_library(n) for n in ["crt2.o", "crtbegin.o", "crtend.o"]] \
        + [str(win_sdl_library)] + (win_nrd["libraries"] if win_nrd else [])
else:
    gcc_lib = "/usr/lib/gcc/x86_64-linux-gnu/" + subprocess.run(["gcc", "-dumpversion"], capture_output=True, text=True).stdout.strip()
    host_libraries = ([
        "/usr/lib/x86_64-linux-gnu/libstdc++.so.6",
        "/lib/x86_64-linux-gnu/libc.so.6",
        "/lib/x86_64-linux-gnu/libm.so.6",
        "/usr/lib/x86_64-linux-gnu/libc_nonshared.a",  # atexit, stat, ...
        gcc_lib + "/libgcc.a",
        "/lib/x86_64-linux-gnu/libgcc_s.so.1",
    ] if arch64 else [
        "/usr/lib32/libstdc++.so.6",
        "/lib/i386-linux-gnu/libc.so.6",
        "/lib/i386-linux-gnu/libm.so.6",
        gcc_lib + "/32/libgcc.a",
        "/lib/i386-linux-gnu/libgcc_s.so.1",
    ]) + [str(sdl_library)] + (nrd["libraries"] if nrd else [])
for library in host_libraries:
    if os.path.exists(library):
        shared = ".so." in os.path.basename(library)  # libc.so.6, libgcc_s.so.1: their dynamic symbols
        for line in run(["nm", "-D", "--defined-only", library] if shared else [*NM, "--defined-only", library],
                        capture_output=True, text=True).stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3:
                libc.add(parts[2].split("@")[0])
libc.update({"_GLOBAL_OFFSET_TABLE_", "__dso_handle", "__data_start", "_end", "__executable_start", "etext",
             "__start_rood_state_data", "__stop_rood_state_data", "__start_rood_state_bss",
             "__stop_rood_state_bss"})  # linker-provided (snapshot.c: code hash, state sections)
libc.update({"pc_overlay_statics", "pc_overlay_statics_count"})  # generated before linking
if coff:  # provided by the PE linker (ld's default script for PE+)
    libc.update({"__ImageBase", "__image_base__", "__CTOR_LIST__", "__DTOR_LIST__", "_tls_index",
                 "__RUNTIME_PSEUDO_RELOC_LIST__", "__RUNTIME_PSEUDO_RELOC_LIST_END__", "__data_start__",
                 "__data_end__", "__bss_start__", "__bss_end__", "__end__", "_end"})
missing = undefined - defined - libc
# Function-pointer tables in BATTLE data hold guest code addresses. Rehome a
# table natively when every word is a named function (or null): native C
# where it exists, otherwise a fail-fast trap. Each word is checked against the
# private overlay first. Tables with mid-function entries stay explicit.
generated_tables = {}
slus_image = (root / "data/SLUS_010.40").read_bytes()
images = [(root / "build/src/SLUS_010.40", slus_image, 0x80010000 - 0x800)]
if battle:
    images.append((root / "build/src/BATTLE/BATTLE.PRG", overlay, 0x80068800))
for data_dir, image, image_base in images:
    tables = {}
    order = []  # labels in file order, to rejoin tables splat split
    for data in sorted(data_dir.glob("*data.s")):
        current = None
        for line in data.read_text().splitlines():
            label = re.match(r"\s*dlabel (\w+)", line)
            if label:
                current = label.group(1)
                tables[current] = []
                order.append(current)
                continue
            if re.match(r"\s*enddlabel", line):
                current = None
                continue
            if current is None or tables[current] is None or not line.strip():
                continue
            word = re.search(r"\.word (\w+)", line)
            tables[current] = tables[current] + [word.group(1)] if word else None
    def function_words(words):
        return words is not None and all(e == "0x00000000" or e in functions for e in words)
    for name in sorted(missing):
        entries = tables.get(name)
        if not entries or name.startswith("jtbl_") or name not in addresses:
            continue
        if not 0 <= addresses[name] - image_base < len(image):
            continue
        # splat splits a table wherever code names an entry's address (e.g.
        # D_800301E0[0xD0]); rejoin contiguous function-only labels up to the
        # next label native code references by name.
        if function_words(entries):
            index = order.index(name) + 1
            end = addresses[name] + 4 * len(entries)
            while index < len(order):
                follow = order[index]
                if follow in missing or addresses.get(follow) != end:
                    break
                if not function_words(tables[follow]) or not tables[follow]:
                    break
                entries = entries + tables[follow]
                end += 4 * len(tables[follow])
                index += 1
        # splat can leave unrelated trailing words under a table's label; use
        # the leading run of function/null entries (the rest stays in guest RAM).
        for cut, e in enumerate(entries):
            if not (e == "0x00000000" or e in functions):
                if e.startswith("0x"):
                    entries = entries[:cut]
                break
        if not entries or not all(e == "0x00000000" or e in functions for e in entries):
            continue
        if all(e == "0x00000000" for e in entries):
            continue
        offset = addresses[name] - image_base
        actual = struct.unpack_from("<" + "I" * len(entries), image, offset)
        expected = [0 if e == "0x00000000" else function_addresses[e] for e in entries]
        if list(actual) != expected:
            raise RuntimeError(f"Function table {name} differs from the overlay")
        generated_tables[name] = entries
for entries in generated_tables.values():
    for e in entries:
        if e != "0x00000000" and e not in defined:
            missing.add(e)
            calls.add(e)
missing -= set(generated_tables)
externs = sorted({e for t in generated_tables.values() for e in t if e != "0x00000000"})
(out / "function_tables.c").write_text(
    "/* Generated by build_boot.py from verified function tables. */\n"
    + "".join(f"extern char {e}[];\n" for e in externs)
    + "".join(
        f"void* {name}[] = {{ "
        + ", ".join("0" if e == "0x00000000" else f"(void*){e}" for e in entries)
        + " };\n"
        for name, entries in generated_tables.items()
    )
)
compile_generated(["gcc", *flags, "-c", str(out / "function_tables.c"), "-o", str(out / "function_tables.o")])
objects.append(out / "function_tables.o")
def lp64_native_imports():
    """64-bit: data imports whose declared type changes layout under LP64
    (pointers, 8-byte longs) get native definitions instead of their PS1
    addresses, where 4-byte pointer slots sit back to back (an 8-byte store
    would clobber the next import). Their types come from the objects' DWARF
    (pc/tools/import_types.py in gdb). Zero-initialised ones start at zero;
    initialised pointer tables get the image's guest addresses zero-extended.
    Imports inside an overlay's file restart with it (overlay statics), as the
    PS1's reload of the file would reset them. Returns the moved names."""
    import concurrent.futures
    import json
    import tempfile

    data = {n for n in missing if n in addresses and n not in calls and n not in functions}
    def declared_name(name):
        base, _, key = name.rpartition("__")
        return base if base and key in overlay_data else name
    wanted = {}  # object -> names it references
    users = {}  # import -> overlay keys of the objects that reference it
    for obj in objects:
        names = [line.split()[-1] for line in
                 query([*NM, "-u", str(aobj(obj))]).splitlines()]
        for n in names:
            users.setdefault(n, set()).add(object_overlay(obj))
        names = sorted({declared_name(n) for n in names if n in data})
        if names:
            wanted[obj] = names
    scratch = pathlib.Path(tempfile.mkdtemp(prefix="import_types", dir=out))
    def query_types(item):
        obj, names = item
        key = result_cache.key(["import_types", *names], [aobj(obj), root / "pc/tools/import_types.py"])
        cached = result_cache.get(key)
        if cached is not None:
            return obj, json.loads(cached)
        request, reply = scratch / (obj.stem + ".in.json"), scratch / (obj.stem + ".out.json")
        request.write_text(json.dumps(names))
        subprocess.run(["gdb", "-batch", "-nx", str(aobj(obj)), "-ex",
                        f"python import sys; sys.argv = ['import_types', {str(request)!r}, {str(reply)!r}]",
                        "-x", str(root / "pc/tools/import_types.py")],
                       cwd=root, check=True, capture_output=True)
        result_cache.put(key, reply.read_bytes())
        return obj, json.loads(reply.read_text())
    declarations = {}  # declared name -> [(object, info)]
    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        for obj, found in pool.map(query_types, wanted.items()):
            for name, info in found.items():
                declarations.setdefault(name, []).append((obj, info))
    shutil.rmtree(scratch)

    def changes(info):
        element = info.get("element")
        if info.get("size", 0) == 0 and element:
            return element["size"] != element["ps1_size"]
        return info.get("size") != info.get("ps1_size")
    image_ranges = [(base, image) for _, image, base in images]
    # ENDING loads over BATTLE (both at 0x80068800): its regions read its image.
    ending_image = (root / "data/ENDING/ENDING.PRG").read_bytes() if battle else b""
    def image_bytes(address, size, owner=None):
        for base, image in ([(0x80068800, ending_image)] if owner == "ending" else image_ranges):
            if base <= address and address + size <= base + len(image):
                return image[address - base: address - base + size]
        return None
    import bisect
    # Extents end at the next data import: symbols nothing imports (or that C
    # defines natively) do not bound an array (vs_battle_commonWepBuffs[2]
    # is followed by an unused label one word in).
    battle_end = 0x80068800 + len(overlay) if battle else 0
    # ENDING loads over BATTLE (both at 0x80068800), so the imports only its
    # objects use there are a separate group: their extents, regions and
    # restart are ENDING's.
    def group(name):
        if not 0x80068800 <= addresses[name] < 0x80068800 + len(ending_image):
            return None
        keys = users.get(name, set())
        if "ending" in keys and len(keys) > 1:
            raise RuntimeError(f"LP64 import {name} is shared by ENDING and {keys - {'ending'}}")
        return "ending" if keys == {"ending"} else None
    groups = {n: group(n) for n in data}
    ordered = {g: sorted({addresses[n] for n in data if groups[n] == g}) for g in set(groups.values())}
    # Each moved import covers its PS1 extent (up to the next known symbol, or
    # its declared size). Overlapping or adjacent ones form one region: splat
    # names interior elements separately (vs_battle_actors[1] is also
    # D_800F192C), so a region is one native block where every 4-byte PS1
    # pointer word becomes 8 bytes and each name sits at twice its PS1 offset.
    def pointer_words(name):
        infos = [info for _, info in declarations.get(declared_name(name), [])]
        return any(changes(i) for i in infos) and all(i.get("pointer_words") for i in infos if changes(i))
    pointer_word_addresses = {(groups[n], addresses[n]) for n in data if pointer_words(n)}
    spans, problems = [], []
    for name in sorted(data):
        infos = declarations.get(declared_name(name), [])
        changed = [info for _, info in infos if changes(info)]
        if not changed:
            continue
        if len({(info.get("size"), info.get("ps1_size")) for _, info in infos}) > 1:
            print(f"LP64 import {name}: declarations disagree: "
                  + ", ".join(f"{obj.stem}: {info.get('type')}" for obj, info in infos))
        if not all(info.get("pointer_words") for info in changed):
            problems.append(f"{name} ({changed[0]['type']}: not only pointers)")
            continue
        address = addresses[name]
        # An incomplete array runs to the next import that is not itself a
        # pointer word splat named inside it (vs_battle_actors[] spans
        # D_800F192C = vs_battle_actors[1]).
        following = [a for a in ordered[groups[name]][bisect.bisect_right(ordered[groups[name]], address):]
                     if (groups[name], a) not in pointer_word_addresses]
        extent = (following[0] - address) if following else 4
        complete = [info["ps1_size"] for info in changed if info.get("size")]
        # Complete types: their PS1 size; incomplete arrays: up to the next import.
        size = max(complete) if complete else extent
        spans.append([address, address + size, [name], groups[name] or ""])
    regions = []
    for span in sorted(spans, key=lambda span: (span[3], span[0], span[1])):
        if regions and span[3] == regions[-1][3] and span[0] <= regions[-1][1]:
            regions[-1][1] = max(regions[-1][1], span[1])
            regions[-1][2] += span[2]
        else:
            regions.append(span)
    moved = {name for region in regions for name in region[2]}
    definitions = []
    for start, end, names, owner in regions:
        # Other imports inside would keep reading the guest copy (symbols
        # nothing imports do not matter).
        inside = sorted(n for n in data - moved if start <= addresses[n] < end and (groups[n] or "") == owner)
        # A 4-byte integer alias of a pointer word (MENU3 tests D_800F4594,
        # which is vs_battle_wepModels[3], for zero) maps to the low half of
        # the 8-byte slot: the same value for addresses below 4 GB.
        words = [n for n in inside if addresses[n] % 4 == 0 and all(
            info.get("size") == 4 for _, info in declarations.get(declared_name(n), [(None, {})]))]
        names = names + words
        moved.update(words)
        inside = [n for n in inside if n not in words]
        if inside:
            problems.append(f"region {start:#x}-{end:#x} ({', '.join(names)}) holds other imports "
                            + ", ".join(inside))
            continue
        if (end - start) % 4 or start % 4:
            problems.append(f"region {start:#x}-{end:#x} ({', '.join(names)}) is not word aligned")
            continue
        region = f"rood_lp64_{start:08x}" + (f"_{owner}" if owner else "")
        attributes = "__attribute__((used, aligned(8)"
        if owner == "ending":
            attributes += ', section("rood_ovl_ending_ending_prg_data")'
            overlay_statics.setdefault("ENDING/ENDING.PRG", set()).add("data")
            win_overlay_statics.setdefault("ENDING/ENDING.PRG", set()).add("data")
        elif battle and 0x80068800 <= start < battle_end:
            attributes += ', section("rood_ovl_battle_battle_prg_data")'
            overlay_statics.setdefault("BATTLE/BATTLE.PRG", set()).add("data")
            win_overlay_statics.setdefault("BATTLE/BATTLE.PRG", set()).add("data")
        attributes += "))"
        count = (end - start) // 4
        content = image_bytes(start, end - start, owner)
        if content is None or not any(content):
            definitions.append(f"{attributes} unsigned long long {region}[{count}];")
        else:
            words = struct.unpack_from(f"<{count}I", content)
            definitions.append(f"{attributes} unsigned long long {region}[{count}] = {{ "
                               + ", ".join(f"{w:#x}" for w in words) + " };")
        for name in names:
            offset = 2 * (addresses[name] - start)
            definitions.append(f'__asm__(".globl {name}\\n.type {name}, @object\\n'
                               f'.set {name}, {region} + {offset}");')
    # The mover's view of every data import, for pc/tools/lp64_alias_scan.py
    # (an import that is a field of another import must live where it does).
    (out / "lp64_imports.json").write_text(json.dumps({
        name: {
            "address": addresses[name],
            "group": groups[name] or "",
            "moved": name in moved,
            "users": sorted(k or "main" for k in users.get(name, ())),
            "declarations": [dict(info, object=obj.stem) for obj, info in declarations.get(declared_name(name), [])],
        } for name in sorted(data)}, indent=1, sort_keys=True) + "\n")
    if problems:
        raise RuntimeError("LP64 data imports that cannot move natively: " + "; ".join(problems))
    # Guest data that holds the address of a moved import would now point at a stale copy.
    moved_addresses = {addresses[n]: n for n in moved}
    for _, image, base in images:
        for offset in range(0x800 if base < 0x80010000 else 0, len(image) - 3, 4):
            word = struct.unpack_from("<I", image, offset)[0]
            if word in moved_addresses:
                print(f"LP64 import {moved_addresses[word]}: its PS1 address appears in guest data at {base + offset:#x}")
    source = out / "native_imports.c"
    source.write_text("/* Generated by build_boot.py --arch 64: data imports with an LP64 layout. */\n"
                      + "\n".join(definitions) + "\n")
    obj = out / "native_imports.o"
    if not coff:
        run(["gcc", *flags, "-c", str(source), "-o", str(obj)])
    state_sections(obj)  # game data (save states); overlay regions keep their rood_ovl_ sections
    if win:
        # PE: no .type directive; overlay data goes to its grouped section.
        text = source.read_text()
        text = re.sub(r"\\n\.type \w+, @object", "", text)
        text = re.sub(r'section\("(rood_ovl_\w+_data)"\)', r'section(".data$\1_m")', text)
        win_source = out / "native_imports_win.c"
        win_source.write_text(text)
        run(win_command(["gcc", *flags, "-c", str(win_source), "-o", str(obj)]))
    objects.append(obj)
    print(f"LP64: {len(moved)} pointer-bearing data imports defined natively in {len(regions)} regions")
    return moved


native_imports = set()
if arch64:
    native_imports = lp64_native_imports()
assign = []
traps = []
for name in sorted(missing):
    if name in native_imports:
        continue
    if name in addresses and name not in calls and name not in functions:
        address = addresses[name]
        if not (
            0x80000000 <= address < 0x80200000 or 0x1F800000 <= address < 0x1F800400
        ):
            raise RuntimeError(
                f"Data import {name} at {address:#x} is outside mapped guest memory"
            )
        assign.append(f"{name} = 0x{address:08x};")
    elif name in calls or name in functions:
        traps.append(name)
    else:
        raise RuntimeError(
            f"Unclassified import: {name}; refusing to fake data with a function"
        )
# A script assignment silently replaces an object's definition: an import
# must only name what no linked object defines.
shadowed = sorted({line.split(" = ")[0] for line in assign} & defined)
if shadowed:
    raise RuntimeError("Data imports that linked objects also define: " + ", ".join(shadowed))
(out / "imports.ld").write_text("\n".join(assign) + "\n")
(out / "traps.c").write_text(
    "extern void pc_unimplemented(const char*) __attribute__((noreturn));\n"
    + "".join(f'void {n}(void) {{ pc_unimplemented("{n}"); }}\n' for n in traps)
)
compile_generated(["gcc", *flags, "-c", str(out / "traps.c"), "-o", str(out / "traps.o")])
# Bounds of each overlay's native statics (linker-provided __start_/__stop_ of
# the sections renamed above), for overlay_statics.c. Byte-identical PLG copies
# (config/EFFECT/Makefile duplicate rules) load the same statics.
statics_files = {f: f for f in (win_overlay_statics if coff else overlay_statics)}
makefile = (root / "config/EFFECT/Makefile").read_text().replace("\\\n", " ")
for copies, original in re.findall(r"\$\(call PLGBINS,([\d ]+)\):\s*\$\(call PLGBINS,(\d+)\)", makefile):
    for copy in copies.split():
        if f"EFFECT/PLG{original}.BIN" in statics_files:
            statics_files[f"EFFECT/PLG{copy}.BIN"] = f"EFFECT/PLG{original}.BIN"
statics_rows, statics_externs = [], []
for path, owner in sorted(statics_files.items()):
    key = re.sub(r"\W", "_", owner).lower()
    bounds = []
    for kind in ["data", "bss"]:
        if kind in (win_overlay_statics if coff else overlay_statics)[owner]:
            start, stop = f"__start_rood_ovl_{key}_{kind}", f"__stop_rood_ovl_{key}_{kind}"
            statics_externs += [start, stop]
            bounds += [start, stop]
        else:
            bounds += ["0", "0"]
    statics_rows.append(f'    {{ "{path}", {", ".join(bounds)} }},\n')
(out / "overlay_statics_table.c").write_text(
    "/* Generated by build_boot.py: native statics of each overlay file. */\n"
    + "".join(f"extern char {n}[];\n" for n in sorted(set(statics_externs)))
    + "const struct { const char* path; char *data_start, *data_end, *bss_start, *bss_end; }"
    + " pc_overlay_statics[] = {\n"
    + "".join(statics_rows)
    + "    { 0 } };\n"
    + f"const int pc_overlay_statics_count = {len(statics_rows)};\n"
)
if not coff:
    run(["gcc", *flags, "-c", str(out / "overlay_statics_table.c"), "-o", str(out / "overlay_statics_table.o")])
if win:
    # PE has no __start_/__stop_ section symbols: zero-length markers in
    # .data$..._a and _z bracket each overlay's sorted .data$..._m sections.
    markers, rows = [], []
    for path, owner in sorted(statics_files.items()):
        if owner not in win_overlay_statics:
            continue
        key = re.sub(r"\W", "_", owner).lower()
        bounds = []
        for kind in ["data", "bss"]:
            if kind in win_overlay_statics[owner]:
                start, stop = f"rood_ovl_{key}_{kind}_start", f"rood_ovl_{key}_{kind}_stop"
                markers += [f'__attribute__((section(".{kind}$rood_ovl_{key}_{kind}_a"))) char {start}[0];',
                            f'__attribute__((section(".{kind}$rood_ovl_{key}_{kind}_z"))) char {stop}[0];']
                bounds += [start, stop]
            else:
                bounds += ["0", "0"]
        rows.append(f'    {{ "{path}", {", ".join(bounds)} }},\n')
    (out / "overlay_statics_win.c").write_text(
        "/* Generated by build_boot.py --target windows: native statics of each overlay file. */\n"
        + "\n".join(sorted(set(markers))) + "\n"
        + "const struct { const char* path; char *data_start, *data_end, *bss_start, *bss_end; }"
        + " pc_overlay_statics[] = {\n" + "".join(rows) + "    { 0 } };\n"
        + f"const int pc_overlay_statics_count = {len(rows)};\n")
    run(win_command(["gcc", *flags, "-c", str(out / "overlay_statics_win.c"), "-o", str(out / "overlay_statics_table.o")]))
    # The MinGW runtime provides the unwinder (libgcc_eh); it is never a
    # trap (Lua's errors are C++ exceptions).
    (out / "traps_win.c").write_text(
        "extern void pc_unimplemented(const char*) __attribute__((noreturn));\n"
        + "".join(f'void {n}(void) {{ pc_unimplemented("{n}"); }}\n' for n in traps if not n.startswith("_Unwind_")))
    run(win_command(["gcc", *flags, "-c", str(out / "traps_win.c"), "-o", str(out / "traps.o")]))
    # PS1 SDK functions named like Windows runtime ones are renamed in the
    # game's objects: kernel32's EnterCriticalSection (used by the C++ and
    # thread runtimes), and libsn's __main, which would replace the MinGW
    # one that main() calls to run the C++ constructors. (rand and srand
    # override the C library's as they do on Linux.)
    for obj in [*game_objects, out / "traps.o"]:
        symbols = query([MINGW + "nm", str(twin(obj))]).split()
        clashes = [n for n in ["EnterCriticalSection", "__main"] if n in symbols]
        if clashes:
            run([MINGW + "objcopy", *[f"--redefine-sym={n}=psx_{n}" for n in clashes], str(twin(obj))])
    # A function or variable declared weak in a header and defined by a unit that includes it
    # is a weak definition in COFF (a weak external aliasing .weak.<name>.<other symbol>). Where
    # other objects also hold weak references to it, ld can resolve the name to one of their
    # defaults (0) instead: the Room Browser's pc_room_warp_* and pc_rooms_* were all null.
    # Each such name gets a strong definition at its weak definition's address.
    weak_defs, strong = {}, set()
    for obj in [*objects, out / "traps.o", out / "overlay_statics_table.o"]:
        for line in query([MINGW + "nm", str(twin(obj))]).splitlines():
            parts = line.split()
            if len(parts) != 3 or parts[1] not in "TDBR":
                continue
            weak = re.fullmatch(r"\.weak\.(\w+)\.\w+", parts[2])
            if weak:
                weak_defs.setdefault(weak.group(1), parts[2])
            elif not parts[2].startswith("."):
                strong.add(parts[2])
    (out / "weak_defs.ld").write_text("".join(f'{name} = "{alias}";\n' for name, alias in
                                              sorted(weak_defs.items()) if name not in strong))
    exe = out / "rood.exe"
    run_long([
        MINGW_GXX,
        "-static",
        "-mconsole",
        # Native code and data below 4 GB at a fixed address (psx_ptr fields
        # hold native addresses; RIP-relative code reaches the guest RAM).
        "-Wl,--image-base=0x400000",
        "-Wl,--disable-dynamicbase",
        "-Wl,--disable-high-entropy-va",
        "-Wl,--gc-sections",
        # Reproducible: no link time in the PE header, so an incremental and a clean build match.
        "-Wl,--no-insert-timestamp",
        # Files open in binary mode unless "t" is given, as on Linux.
        subprocess.run([MINGW_GCC, "-print-file-name=binmode.o"], capture_output=True,
                       text=True).stdout.strip(),
        *[str(twin(o)) for o in objects],
        str(twin(out / "traps.o")),
        str(twin(out / "overlay_statics_table.o")),
        str(out / "imports.ld"),
        str(out / "weak_defs.ld"),
        str(win_sdl_library),
        *(win_nrd["libraries"] if win_nrd else []),
        *[f"-l{n}" for n in WIN_SYSTEM_LIBS],
        "-o", str(exe),
    ], out / "link.rsp")
    print(f"Built {exe}: {len(assign)} data imports, {len(traps)} fail-fast function imports")
    # The linked objects, for pc/tools/decl_mismatch.py --windows (it reads their COFF twins)
    (out / "objects.txt").write_text("".join(f"{o.relative_to(root)}\n" for o in objects))
    finish(exe, out / "objects.txt", report, out / "report.json", out / "lp64_imports.json")
    raise SystemExit(0)
# The linked objects, for pc/tools/decl_mismatch.py (stale .o files in the
# directory are not part of the program).
(out / "objects.txt").write_text("".join(f"{o.relative_to(root)}\n" for o in objects))
run(
    [
        "gcc",
        # 32 bits: native code below 16 MB, where 24-bit OT links can reach
        # it. 64 bits: links are guest-only; non-PIE keeps native addresses
        # below 4 GB (psx_ptr fields can hold them).
        *(["-m64", "-no-pie"] if arch64 else ["-m32", "-no-pie", "-Wl,-Ttext-segment=0x00400000"]),
        "-Wl,--gc-sections",
        *[str(o) for o in objects],
        str(out / "traps.o"),
        str(out / "overlay_statics_table.o"),
        str(out / "imports.ld"),
        str(sdl_library),
        *(nrd["libraries"] if nrd else []),
        *([] if arch64 else ["-L/usr/lib32"]),
        "-l:libstdc++.so.6",
        "-l:libgcc_s.so.1",  # C++ unwinding (Lua errors are exceptions: pc/docs/Extensions.md)
        "-lm",
        "-lpthread",
        "-ldl",
        "-o",
        str(out / ("rood_battle_probe" if battle else "rood_boot")),
    ]
)
print(
    f"Built {out / ('rood_battle_probe' if battle else 'rood_boot')}: {len(assign)} data imports, {len(traps)} fail-fast function imports"
)

if arch64:
    finish(out / "rood_battle_probe", out / "objects.txt", report, out / "report.json", out / "lp64_imports.json")
    raise SystemExit(0)  # the probes below are 32-bit host programs
# Exercise the same geometry objects linked into the boot executable.
probe = out / "gte_probe.i"
probe.write_bytes(
    run(
        [
            "gcc",
            "-E",
            "-nostdinc",
            "-Ipc/include",
            "-Iinclude/psx",
            "-Isrc/include",
            "-I.",
            "-D_LANGUAGE_C",
            *(["-DPC_ROOM_TEST"] if battle else []),
            "pc/tests/gte_probe.c",
        ],
        capture_output=True,
    ).stdout
)
run(
    [
        "gcc",
        "-m32",
        "-fno-builtin",
        "-fno-pie",
        "-no-pie",
        "-Wl,--gc-sections",
        str(probe),
        *[
            str(out / (n + ".o"))
            for n in ["sdk", "gte", "libgte", "inline"]
            + (["room_geometry", "room_render", "actor_vertices", "battle_gte"] if battle else [])
        ],
        "-o",
        str(out / "gte_probe"),
    ]
)
run([str(out / "gte_probe")])

if battle:
    run(
        [
            "gcc",
            "-m32",
            "pc/tests/battle_helpers_probe.c",
            "pc/src/battle_helpers.c",
            "-o",
            str(out / "battle_helpers_probe"),
        ]
    )
    run([str(out / "battle_helpers_probe")])

if battle:
    run(
        [
            "gcc",
            "-m32",
            "-ffunction-sections",
            "-fdata-sections",
            "-Wl,--gc-sections",
            "pc/tests/navigation_probe.c",
            "pc/src/navigation.c",
            "-o",
            str(out / "navigation_probe"),
        ]
    )
    run([str(out / "navigation_probe")])

if battle:
    run(
        [
            "gcc",
            "-m32",
            "pc/tests/actor_update_probe.c",
            "pc/src/actor_update.c",
            "-o",
            str(out / "actor_update_probe"),
        ]
    )
    run([str(out / "actor_update_probe")])

if battle:
    run(
        [
            "gcc",
            "-m32",
            "-ffunction-sections",
            "-fdata-sections",
            "-Wl,--gc-sections",
            "pc/tests/route_continuation_probe.c",
            "pc/src/battle_movement.c",
            "-o",
            str(out / "route_continuation_probe"),
        ]
    )
    run([str(out / "route_continuation_probe")])

if battle:
    run(
        [
            "gcc",
            *flags,
            "-no-pie",
            "-Wl,--gc-sections",
            "-Ipc/include",
            "-Iinclude/psx",
            "-Isrc/include",
            "-I.",
            "-D_LANGUAGE_C",
            "-DROOD_PC",
            "pc/tests/effect_particles_probe.c",
            *[
                str(out / (n + ".o"))
                for n in ["effect_particles", "gte", "libgte", "inline"]
            ],
            "-o",
            str(out / "effect_particles_probe"),
        ]
    )
    run([str(out / "effect_particles_probe")])

if battle:
    for name in [
        "weapon_packets",
        "actor_packets",
        "actor_shadow",
        "actor_blend",
        "fixed_font",
        "room_lighting",
    ]:
        run(
            [
                "gcc",
                "-m32",
                "-fno-pie",
                "-no-pie",
                "-Wl,--gc-sections",
                "-Ipc/include",
                "-Iinclude/psx",
                "-Isrc/include",
                "-I.",
                "-D_LANGUAGE_C",
                "-DROOD_PC",
                f"pc/tests/{name}_probe.c",
                *[
                    str(out / (n + ".o"))
                    for n in [name, "sdk", "gte", "libgte", "inline"]
                ],
                "-o",
                str(out / (name + "_probe")),
            ]
        )
        run([str(out / (name + "_probe"))])

if battle:
    run(
        [
            "gcc",
            "-m32",
            "-fno-pie",
            "-no-pie",
            "-Wl,--gc-sections",
            "pc/tests/screff2_warp_probe.c",
            *[
                str(out / (n + ".o"))
                for n in ["screff2_warp", "gte", "libgte", "inline"]
            ],
            "-o",
            str(out / "screff2_warp_probe"),
        ]
    )
    run([str(out / "screff2_warp_probe")])


if battle:
    run(
        [
            "gcc",
            "-m32",
            "-fno-pie",
            "-no-pie",
            "pc/tests/actor_cleanup_probe.c",
            "pc/src/actor_cleanup.c",
            "-o",
            str(out / "actor_cleanup_probe"),
        ]
    )
    run([str(out / "actor_cleanup_probe")])
finish(out / ("rood_battle_probe" if battle else "rood_boot"), out / "objects.txt")
