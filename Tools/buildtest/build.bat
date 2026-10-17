@echo off
rem Builds and runs buildtest.exe (Core/GeoBuild.s checks) with the Visual Studio C++ compiler. No CMake needed.
setlocal
set HERE=%~dp0
set CORE=%HERE%..\..\Core
if not defined VCINSTALLDIR call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%HERE%bin" mkdir "%HERE%bin"
if not exist "%HERE%obj" mkdir "%HERE%obj"
cl /nologo /O2 /MT /EHsc /std:c++17 /W4 /utf-8 /I"%CORE%" /Fo"%HERE%obj\\" /Fe"%HERE%bin\buildtest.exe" "%HERE%src\*.cpp" "%CORE%\GeoBody\*.cpp" "%CORE%\GeoBuild\*.cpp" || exit /b 1
"%HERE%bin\buildtest.exe"
