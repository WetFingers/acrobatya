@echo off
rem MSVC environment for Ottisk builds: vcvars64 (cl, cmake 3.31, ninja 1.12, ASan runtime)
rem plus clang-format / clang-tidy 19.1.5 from Build Tools (vcvars64 does not put them on PATH).
rem vcvars64 also points VCPKG_ROOT at the vcpkg bundled with Build Tools; Ottisk uses its own
rem clone (%LOCALAPPDATA%\vcpkg, or OTTISK_VCPKG_ROOT if set), so VCPKG_ROOT is reset afterwards.
rem Usage:  cmd /c "tools\vcenv.cmd && cmake --preset msvc-debug"
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul || (echo vcenv: cl.exe not found after vcvars64.bat 1>&2 & exit /b 1)
set "PATH=%VCINSTALLDIR%Tools\Llvm\x64\bin;%PATH%"
if not defined OTTISK_VCPKG_ROOT set "OTTISK_VCPKG_ROOT=%LOCALAPPDATA%\vcpkg"
set "VCPKG_ROOT=%OTTISK_VCPKG_ROOT%"
exit /b 0
