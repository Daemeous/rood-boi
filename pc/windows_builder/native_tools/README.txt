rood-boi native Windows build tools
====================================

Windows programs that rood-boi's native Windows builder uses to build the PlayStation
decompilation (rood-reverse, by ser-pounce and its other contributors:
https://github.com/ser-pounce/rood-reverse) without Linux or WSL:

  old-gcc\2.7.2-psx\cc1.exe   GCC 2.7.2 for the PlayStation, with the patches of
  old-gcc\2.7.2-cdk\cc1.exe   decompals/old-gcc (https://github.com/decompals/old-gcc):
  old-gcc\2.8.1-psx\cc1.exe   the compilers the decompilation is matched against.
                              Their output is byte-identical to decompals' Linux builds.
  bin\mipsel-linux-gnu-*.exe  GNU binutils 2.46.0 for the PlayStation's MIPS CPU.

They are cross-compiled from the original GNU sources with MinGW-w64 by build_tools.sh.
GCC is free software under the GNU General Public License, version 2 (COPYING.GPLv2);
binutils under version 3 (COPYING.GPLv3). The complete corresponding source code (the GNU
source archives, the decompals patches, the small Windows compatibility file winshim.c and
the build script) is in rood-boi-native-tools-<version>-sources.zip, published next to this
file on the same release page.

They contain nothing from the game.
