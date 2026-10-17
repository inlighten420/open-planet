@echo off
rem Builds geotile.exe with the Visual Studio C++ compiler. No CMake needed.
setlocal
set HERE=%~dp0
set CORE=%HERE%..\..\Core
if not defined VCINSTALLDIR call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%HERE%bin" mkdir "%HERE%bin"
if not exist "%HERE%obj\lib" mkdir "%HERE%obj\lib"

rem Third-party C libraries (zstd, miniz) are built once
set Z=%CORE%\third_party\zstd
set M=%HERE%third_party\miniz
if not exist "%HERE%obj\lib\miniz.obj" (
  cl /nologo /O2 /MT /c /w /DZSTD_DISABLE_ASM /DZSTD_MULTITHREAD=0 /I"%Z%" /Fo"%HERE%obj\lib\\" ^
    "%Z%\common\*.c" "%Z%\compress\*.c" "%Z%\decompress\*.c" "%Z%\dictBuilder\*.c" "%M%\miniz.c" >nul || exit /b 1
)

cl /nologo /O2 /MT /EHsc /std:c++17 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I"%CORE%\GeoTiles" /I"%CORE%" /I"%Z%" /I"%M%" /I"%HERE%third_party\json" ^
  /Fo"%HERE%obj\\" /Fe"%HERE%bin\geotile.exe" "%HERE%src\*.cpp" "%CORE%\GeoTiles\*.cpp" "%CORE%\GeoRoads\*.cpp" "%CORE%\GeoGround\*.cpp" "%CORE%\GeoMesh\*.cpp" "%HERE%obj\lib\*.obj" || exit /b 1
