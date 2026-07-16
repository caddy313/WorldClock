@echo off
setlocal
set "CONDA_ROOT=%CONDA_PREFIX%"
if not defined CONDA_ROOT for /f "delims=" %%i in ('conda info --base 2^>nul') do set "CONDA_ROOT=%%i"
if exist "%CONDA_ROOT%\Library\bin\x86_64-w64-mingw32-g++.exe" goto mingw

where cmake >nul 2>nul || (echo CMake not found. Install Visual Studio 2022 C++ Desktop workload. & exit /b 1)
cmake -S "%~dp0." -B "%~dp0build" -A x64 || exit /b 1
cmake --build "%~dp0build" --config Release || exit /b 1
echo Built: %~dp0build\Release\世界时钟.exe
exit /b 0

:mingw
set "PATH=%CONDA_ROOT%\Library\bin;%CONDA_ROOT%\Scripts;%PATH%"
"%CONDA_ROOT%\Library\bin\cmake.exe" -S "%~dp0." -B "%~dp0build-mingw" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="%CONDA_ROOT%/Library/bin/x86_64-w64-mingw32-g++.exe" -DCMAKE_RC_COMPILER="%CONDA_ROOT%/Library/bin/x86_64-w64-mingw32-windres.exe" || exit /b 1
"%CONDA_ROOT%\Library\bin\cmake.exe" --build "%~dp0build-mingw" || exit /b 1
echo Built: %~dp0build-mingw\世界时钟.exe
