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

# the game executable (SDL2 front end)
find_package(SDL2 REQUIRED)
add_executable(supaplex WIN32 src/platform/main_sdl.cpp)
target_link_libraries(supaplex PRIVATE supaplexgame)
if(TARGET SDL2::SDL2main)
  target_link_libraries(supaplex PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2)
  target_link_libraries(supaplex PRIVATE SDL2::SDL2)
else()
  target_include_directories(supaplex PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(supaplex PRIVATE ${SDL2_LIBRARIES})
endif()
if(MINGW)
  target_link_options(supaplex PRIVATE -static-libgcc -static-libstdc++)
endif()

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

  # debugging variant: generated code instrumented with the original addresses
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    set(TRACE_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/trace_gen)
    set(TRACE_GEN_FILES ${TRACE_GEN_DIR}/gen_phil_00.cpp ${TRACE_GEN_DIR}/gen_phil_01.cpp ${TRACE_GEN_DIR}/gen_dispatch.cpp)
    add_custom_command(
      OUTPUT ${TRACE_GEN_FILES}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/m68k2cpp.py
              ${CMAKE_CURRENT_SOURCE_DIR}/../tools/translate.json --trace-out ${TRACE_GEN_DIR}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/m68k2cpp.py ${CMAKE_CURRENT_SOURCE_DIR}/../tools/translate.json
      COMMENT "Generating instrumented translation")
    add_library(supaplexgame_trace STATIC src/game/runtime.cpp ${TRACE_GEN_FILES})
    target_compile_definitions(supaplexgame_trace PUBLIC SUPAPLEX_TRACE)
    target_include_directories(supaplexgame_trace PRIVATE ${TRACE_GEN_DIR}/..)
    target_link_libraries(supaplexgame_trace PUBLIC amigahw)
    add_executable(difftest_trace tests/difftest_main.cpp)
    target_link_libraries(difftest_trace PRIVATE refemu supaplexgame_trace Threads::Threads)
  endif()
endif()
