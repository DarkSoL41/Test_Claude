cmake_minimum_required(VERSION 3.16)
project(supaplex_amiga_port LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SUPAPLEX_BUILD_TESTS "Build the reference emulator and comparison tools (needs Musashi)" ON)

set(AMIGA_SRC
  src/amiga/amiga.cpp
  src/amiga/paula.cpp
  src/amiga/adf.cpp
)

add_library(amigahw STATIC ${AMIGA_SRC})
target_include_directories(amigahw PUBLIC src)

# the game: translated 68000 routines + runtime
file(GLOB GAME_GENERATED src/game/generated/*.cpp)
add_library(supaplexgame STATIC
  src/game/runtime.cpp
  ${GAME_GENERATED}
)
target_link_libraries(supaplexgame PUBLIC amigahw)

if(SUPAPLEX_BUILD_TESTS)
  add_library(musashi STATIC
    third_party/musashi/m68kcpu.c
    third_party/musashi/m68kops.c
    third_party/musashi/m68kdasm.c
    third_party/musashi/softfloat/softfloat.c
  )
  target_include_directories(musashi PUBLIC third_party/musashi)
  if(NOT MSVC)
    target_compile_options(musashi PRIVATE -w)
  endif()

  add_library(refemu STATIC tests/refemu.cpp)
  target_link_libraries(refemu PUBLIC amigahw musashi)

  add_executable(refrun tests/refrun_main.cpp)
  target_link_libraries(refrun PRIVATE refemu)

  find_package(Threads REQUIRED)
  add_executable(difftest tests/difftest_main.cpp)
  target_link_libraries(difftest PRIVATE refemu supaplexgame Threads::Threads)
endif()
