#!/bin/sh
# Builds build/dsound.dll with clang + lld-link + llvm-dlltool (LLVM 15+).
# No Visual Studio, Windows SDK or MinGW needed: the code declares the few
# Win32 functions it uses itself and uses no C runtime.
# Works on Linux, WSL, or Git Bash with LLVM for Windows on PATH.
set -e
cd "$(dirname "$0")"
mkdir -p build
llvm-dlltool -m i386:x86-64 -d src/kernel32.def -l build/kernel32.lib
llvm-dlltool -m i386:x86-64 -d src/user32.def -l build/user32.lib
clang --target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector \
      -mno-stack-arg-probe -Wall -c src/main.c -o build/main.obj
clang --target=x86_64-pc-windows-msvc -c src/stubs.S -o build/stubs.obj
lld-link /nologo /dll /out:build/dsound.dll /entry:DllMain /nodefaultlib /def:src/dsound.def \
         /subsystem:windows build/main.obj build/stubs.obj build/kernel32.lib build/user32.lib
cp dist/OreSweep.ini build/OreSweep.ini
echo "Built build/dsound.dll"
