@echo off
rem MSVC environment for AcroBatya builds: vcvars64 (cl, cmake 3.31, ninja 1.12, ASan runtime)
rem plus clang-format / clang-tidy 19.1.5 from Build Tools (vcvars64 does not put them on PATH).
rem vcvars64 also points VCPKG_ROOT at the vcpkg bundled with Build Tools; AcroBatya uses its own
rem clone (C:\Tools\vcpkg, or OTTISK_VCPKG_ROOT if set), so VCPKG_ROOT is reset afterwards.
rem Not under %LOCALAPPDATA%: the Claude desktop app redirects its processes' writes there (package LocalCache).
rem Usage:  cmd /c "tools\vcenv.cmd && cmake --preset msvc-debug"
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul || (echo vcenv: cl.exe not found after vcvars64.bat 1>&2 & exit /b 1)
set "PATH=%VCINSTALLDIR%Tools\Llvm\x64\bin;%PATH%"
if not defined OTTISK_VCPKG_ROOT set "OTTISK_VCPKG_ROOT=C:\Tools\vcpkg"
set "VCPKG_ROOT=%OTTISK_VCPKG_ROOT%"
rem vcpkg caches default to %LOCALAPPDATA%\vcpkg\{archives,registries} - keep them next to the clone.
if not defined VCPKG_DEFAULT_BINARY_CACHE if exist "%OTTISK_VCPKG_ROOT%\archives\" set "VCPKG_DEFAULT_BINARY_CACHE=%OTTISK_VCPKG_ROOT%\archives"
if not defined X_VCPKG_REGISTRIES_CACHE if exist "%OTTISK_VCPKG_ROOT%\registries\" set "X_VCPKG_REGISTRIES_CACHE=%OTTISK_VCPKG_ROOT%\registries"
exit /b 0
