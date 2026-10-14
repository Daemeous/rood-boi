#!/bin/bash
# Builds the Windows programs the native Windows builder downloads (rood-boi-native-tools-<version>.zip):
#   old-gcc/<version>/cc1.exe   the PS1 C compilers the decompilation matches (GCC 2.7.2 "psx" and "cdk",
#                               GCC 2.8.1 "psx"), with decompals/old-gcc's patches
#   bin/mipsel-linux-gnu-*.exe  GNU binutils 2.46.0 for the PS1's MIPS (assembler, linker, objcopy...)
# Run on Ubuntu (WSL is fine) as root: it installs MinGW-w64 and 32-bit gcc, downloads the GNU sources,
# cross-compiles them and writes the zip, plus a zip of every source and patch used (the GPL's source
# offer for the binaries) to the current directory.
#
#   bash build_tools.sh <version>
#
# GCC 2.7.2 predates MinGW: its generator programs are built for Linux first, then only the compiler's
# own objects are rebuilt with MinGW. GCC 2.8.1 knows the i686-pc-mingw32 host and cross-builds as is.
# The Windows cc1 output is byte-identical to decompals' Linux builds (every rood-reverse source checked).
set -euo pipefail
VERSION=${1:?usage: build_tools.sh <version>}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$PWD
W=$(mktemp -d /tmp/rood-native-tools.XXXX)
OLDGCC_COMMIT=0.17   # decompals/old-gcc tag whose patches are used
BINUTILS=2.46.0

export DEBIAN_FRONTEND=noninteractive
apt-get install -y -q gcc-multilib gcc-mingw-w64-i686 gcc-mingw-w64-x86-64 byacc wget xz-utils zip patch > /dev/null

cd "$W"
mkdir -p sources stage/old-gcc stage/bin
wget -q -O sources/old-gcc-$OLDGCC_COMMIT.tar.gz https://github.com/decompals/old-gcc/archive/refs/tags/$OLDGCC_COMMIT.tar.gz
tar xzf sources/old-gcc-$OLDGCC_COMMIT.tar.gz && P=$W/old-gcc-$OLDGCC_COMMIT/patches
wget -q -P sources https://ftp.gnu.org/gnu/gcc/gcc-2.8.1.tar.gz https://ftp.gnu.org/old-gnu/gcc/gcc-2.7.2.tar.gz \
    https://github.com/decompals/old-gcc/releases/download/0.14/b18.tar.gz \
    https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS.tar.xz
cp "$HERE/winshim.c" sources/
i686-w64-mingw32-gcc -O2 -c "$HERE/winshim.c" -o winshim.o

# Links cc1.exe from the current gcc tree with MinGW, keeping the Linux generator programs.
link_windows_cc1() {
    local libs keep=""
    libs=$(grep -E "^LIBS *=" Makefile | sed 's/^LIBS *= *//')
    for g in gen* bi-*; do
        if [ -x "$g" ] && [ ! -d "$g" ]; then case $g in *.*) ;; *) keep="$keep -o $g" ;; esac; fi
    done
    cp "$W/winshim.o" .
    make -j"$(nproc)" $keep cc1 CC=i686-w64-mingw32-gcc HOST_CC="gcc -m32" CFLAGS="-std=gnu89 -O2" \
        HOST_CFLAGS="-std=gnu89 -m32" LIBS="$libs winshim.o" > link.log 2>&1 || { tail -20 link.log; exit 1; }
}

# GCC 2.8.1 (psx): a real cross-host build
echo "GCC 2.8.1-psx"
tar xzf sources/gcc-2.8.1.tar.gz && cd gcc-2.8.1
sed -i -- 's/include <varargs.h>/include <stdarg.h>/g' *.c
patch -s -u -p1 obstack.h -i $P/obstack-2.8.1.h.patch
patch -s -u -p1 config/mips/mips.h -i $P/mips.patch
patch -s -u -p1 < $P/psx.patch
CC="gcc -std=gnu89 -m32" ./configure --target=mips-sony-psx --prefix=/opt/cross --with-endian-little --with-gnu-as \
    --disable-gprof --disable-gdb --host=i686-pc-mingw32 --build=i386-pc-linux > configure.log 2>&1
# configure checked Linux's headers and functions; MinGW lacks these
for d in HAVE_SYS_RESOURCE_H HAVE_SYS_TIMES_H HAVE_BCMP HAVE_BCOPY HAVE_BZERO HAVE_GETRLIMIT HAVE_SETRLIMIT \
         HAVE_INDEX HAVE_RINDEX HAVE_KILL HAVE_SYSCONF; do
    sed -i "s/^#define $d 1/\/* #undef $d *\//" auto-config.h
done
touch insn-config.h
link_windows_cc1
mkdir -p "$W/stage/old-gcc/2.8.1-psx" && cp cc1.exe "$W/stage/old-gcc/2.8.1-psx/"
cd "$W"

# GCC 2.7.2 (psx) and the CDK's 2.7.2: Linux build first, then the compiler's objects again with MinGW
build_272() {
    local name=$1 dir=$2
    echo "GCC $name"
    chmod +x config.sub configure 2>/dev/null || true
    CC="gcc -std=gnu89 -m32" ./configure --target=mips-sony-psx --prefix=/opt/cross --with-endian-little \
        --with-gnu-as --disable-gprof --disable-gdb --host=i386-pc-linux --build=i386-pc-linux > configure.log 2>&1
    make -j"$(nproc)" cc1 CC="gcc -std=gnu89 -m32" CFLAGS="-std=gnu89 -m32 -static" > linux.log 2>&1 \
        || { tail -20 linux.log; exit 1; }
    rm -f $(grep -E -- "-o cc1( |$)" linux.log | tail -1 | tr ' ' '\n' | grep '\.o$' | sort -u) cc1
    link_windows_cc1
    mkdir -p "$W/stage/old-gcc/$name" && cp cc1.exe "$W/stage/old-gcc/$name/"
    cd "$W"
}
tar xzf sources/gcc-2.7.2.tar.gz && cd gcc-2.7.2
sed -i -- 's/include <varargs.h>/include <stdarg.h>/g' *.c
patch -s -u -p1 obstack.h -i $P/obstack-2.7.2.h.patch
patch -s -u -p1 configure -i $P/configure.patch
patch -s -u -p1 config.sub -i $P/config.sub.patch 2> /dev/null || true
patch -s -u -p1 config/mips/mips.h -i $P/mipsel-2.7.patch
patch -s -u -p1 < $P/psx-2.5.7.patch
build_272 2.7.2-psx gcc-2.7.2
tar xzf sources/b18.tar.gz && cd cdk-gcc-b18
patch -s -u -p1 Makefile.in -i $P/Makefile-2.7.2-cdk.in.patch
patch -s -u -p1 obstack.h -i $P/obstack-2.7.2-cdk.h.patch
patch -s -u -p1 config/mips/mips.h -i $P/mipsel-2.7-cdk.patch
patch -s -u -p1 < $P/psx-2.7.2-cdk.patch
build_272 2.7.2-cdk cdk-gcc-b18

# binutils for mipsel, static Windows programs
echo "binutils $BINUTILS"
tar xJf sources/binutils-$BINUTILS.tar.xz && mkdir binutils-build && cd binutils-build
../binutils-$BINUTILS/configure --host=x86_64-w64-mingw32 --target=mipsel-linux-gnu --prefix=/ --disable-nls \
    --disable-werror --disable-gdb --disable-gdbserver --disable-sim --disable-gprof --disable-gprofng \
    --disable-shared --enable-static --without-zstd > configure.log 2>&1
make -j"$(nproc)" LDFLAGS="-static -s" > make.log 2>&1 || { tail -20 make.log; exit 1; }
make install DESTDIR="$W/binutils-install" > install.log 2>&1
cp "$W"/binutils-install/bin/mipsel-linux-gnu-*.exe "$W/stage/bin/"
cd "$W"

# licences and the source offer
cp gcc-2.8.1/COPYING stage/COPYING.GPLv2
cp binutils-$BINUTILS/COPYING3 stage/COPYING.GPLv3
cp "$HERE/README.txt" stage/README.txt
cp "$HERE/build_tools.sh" "$HERE/winshim.c" sources/
(cd stage && zip -q -r "$OUT/rood-boi-native-tools-$VERSION.zip" .)
(cd sources && zip -q -r "$OUT/rood-boi-native-tools-$VERSION-sources.zip" .)
rm -rf "$W"
ls -la "$OUT"/rood-boi-native-tools-$VERSION*.zip
