# CMakeList.txt : CMake project for PixelSenseEngine.
# A dependency-free CLI that validates media edit operations and executes
# them through FFmpeg. The Node server drives it and reads its JSON output.

add_executable (PixelSenseEngine
  "src/main.cpp"
  "src/Json.h"
  "src/Media.h"
  "src/Media.cpp"
  "src/Process.h"
  "src/Process.cpp"
  "src/Operations.h"
  "src/Operations.cpp"
)

set_property(TARGET PixelSenseEngine PROPERTY CXX_STANDARD 20)
set_property(TARGET PixelSenseEngine PROPERTY CXX_STANDARD_REQUIRED ON)

if (MSVC)
  string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  target_compile_options(PixelSenseEngine PRIVATE /W4 /utf-8)
  target_compile_definitions(PixelSenseEngine PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(PixelSenseEngine PRIVATE -Wall -Wextra)
endif()

install(TARGETS PixelSenseEngine RUNTIME DESTINATION bin)
