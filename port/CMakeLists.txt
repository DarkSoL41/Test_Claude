cmake_minimum_required(VERSION 3.16)
project(supaplex_amiga_port LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
  # the translated routines are large; silence CRT deprecation warnings
  add_compile_options(/bigobj /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

option(SUPAPLEX_BUILD_TESTS "Build the reference emulator and comparison tools (needs Musashi)" ON)

set(AMIGA_SRC
  src/amiga/amiga.cpp
  src/amiga/paula.cpp
  src/amiga/adf.cpp
  src/amiga/gamefiles.cpp
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

# the game executable (SDL2 front end) and the level editor
find_package(SDL2 REQUIRED)
# program icons (tools/make_icons.py): Windows resources
if(WIN32)
  set(GAME_RC res/supaplex.rc)
  set(EDITOR_RC res/supaplex_editor.rc)
  # the resource compiler does not report the .ico as a dependency: rebuild
  # the resources when the icons change
  set_source_files_properties(res/supaplex.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/supaplex.ico)
  set_source_files_properties(res/supaplex_editor.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/supaplex_editor.ico)
endif()
add_executable(supaplex WIN32 src/platform/main_sdl.cpp ${GAME_RC})
target_link_libraries(supaplex PRIVATE supaplexgame)
add_executable(supaplex_editor WIN32
  src/editor/app.cpp
  src/editor/level.cpp
  src/editor/gfx.cpp
  src/editor/ui.cpp
  src/editor/process.cpp
  ${EDITOR_RC}
)
target_link_libraries(supaplex_editor PRIVATE amigahw)
target_include_directories(supaplex_editor PRIVATE src/platform)
foreach(t supaplex supaplex_editor)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(${t} PRIVATE SDL2::SDL2main)
  endif()
  if(TARGET SDL2::SDL2)
    target_link_libraries(${t} PRIVATE SDL2::SDL2)
  else()
    target_include_directories(${t} PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(${t} PRIVATE ${SDL2_LIBRARIES})
  endif()
endforeach()
if(MINGW)
  target_link_options(supaplex PRIVATE -static-libgcc -static-libstdc++)
  target_link_options(supaplex_editor PRIVATE -static-libgcc -static-libstdc++)
endif()

# checks of the editor's level model against the original levels
add_executable(level_test tests/level_test.cpp src/editor/level.cpp)

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
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/m68k2cpp.py ${CMAKE_CURRENT_SOURCE_DIR}/../tools/m68kdec.py
              ${CMAKE_CURRENT_SOURCE_DIR}/../tools/translate.json ${CMAKE_CURRENT_SOURCE_DIR}/../tools/annotations.json
      COMMENT "Generating instrumented translation")
    add_library(supaplexgame_trace STATIC src/game/runtime.cpp ${TRACE_GEN_FILES})
    target_compile_definitions(supaplexgame_trace PUBLIC SUPAPLEX_TRACE)
    target_include_directories(supaplexgame_trace PRIVATE ${TRACE_GEN_DIR}/..)
    target_link_libraries(supaplexgame_trace PUBLIC amigahw)
    add_executable(difftest_trace tests/difftest_main.cpp)
    target_link_libraries(difftest_trace PRIVATE refemu supaplexgame_trace Threads::Threads)
  endif()
endif()
