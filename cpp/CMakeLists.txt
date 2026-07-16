cmake_minimum_required(VERSION 3.20)
project(WorldClock LANGUAGES CXX RC)
add_executable(WorldClock WIN32 src/main.cpp resources/world_clock.rc)
target_compile_features(WorldClock PRIVATE cxx_std_20)
target_compile_definitions(WorldClock PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
target_link_libraries(WorldClock PRIVATE gdiplus comctl32 shell32 comdlg32)
if(MSVC)
  target_compile_options(WorldClock PRIVATE /W4 /permissive- /utf-8 /EHsc)
elseif(MINGW)
  target_compile_options(WorldClock PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-Os;-ffunction-sections;-fdata-sections>
  )
  target_link_options(WorldClock PRIVATE
    -municode -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s;-Wl,--gc-sections>
  )
endif()
set_target_properties(WorldClock PROPERTIES OUTPUT_NAME "世界时钟")
