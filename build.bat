@echo off
rem Builds build\dsound.dll with LLVM (clang, lld-link, llvm-dlltool). No Visual Studio needed.
setlocal
cd /d "%~dp0"

where clang >nul 2>nul
if errorlevel 1 if exist "%ProgramFiles%\LLVM\bin\clang.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
where clang >nul 2>nul
if errorlevel 1 (
    echo LLVM not found. Install it with:  winget install LLVM.LLVM
    goto :fail
)

if not exist build mkdir build
llvm-dlltool -m i386:x86-64 -d src\kernel32.def -l build\kernel32.lib || goto :fail
llvm-dlltool -m i386:x86-64 -d src\user32.def -l build\user32.lib || goto :fail
clang --target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -Wall -c src\main.c -o build\main.obj || goto :fail
clang --target=x86_64-pc-windows-msvc -c src\stubs.S -o build\stubs.obj || goto :fail
lld-link /nologo /dll /out:build\dsound.dll /entry:DllMain /nodefaultlib /def:src\dsound.def /subsystem:windows build\main.obj build\stubs.obj build\kernel32.lib build\user32.lib || goto :fail
copy /y dist\OreSweep.ini build\OreSweep.ini >nul

echo.
echo Built build\dsound.dll and build\OreSweep.ini
echo Copy both into ...\steamapps\common\Whiskerwood\Whiskerwood\Binaries\Win64\
pause
exit /b 0

:fail
echo.
echo Build failed.
pause
exit /b 1
