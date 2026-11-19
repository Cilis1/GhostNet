cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0091 NEW)   # lets us pick the static MSVC runtime

project(GhostNet LANGUAGES C CXX)

set(GHOSTNET_VERSION "0.7.0" CACHE STRING "Version stamped into the binaries")
set(GHOSTNET_REPO "Cilis1/GhostNet" CACHE STRING "GitHub owner/repo the installer is pinned to")
set(GHOSTNET_SIGNING_PUBKEY_HEX "" CACHE STRING "Optional ECDSA P-256 public key (X||Y hex) the installer requires")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Self-contained executables: no Visual C++ redistributable, no MinGW DLLs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(GHOSTNET_HEADLESS "Build the game on raylib's software 'Memory' platform: no window, renders screenshots (CI)" OFF)

# Warnings for OUR code only (third-party C code such as raylib keeps its own flags).
function(gn_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /D_CRT_SECURE_NO_WARNINGS /sdl)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

if(MSVC)
  add_compile_options(/guard:cf)
  add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
elseif(WIN32)
  add_link_options(-static)
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _v "${GHOSTNET_VERSION}")
set(GN_V1 ${CMAKE_MATCH_1})
set(GN_V2 ${CMAKE_MATCH_2})
set(GN_V3 ${CMAKE_MATCH_3})
if(NOT GN_V1)
  set(GN_V1 0)
  set(GN_V2 0)
  set(GN_V3 0)
endif()
set(GN_RES_DIR "${CMAKE_SOURCE_DIR}/res")
set(GN_REPO "${GHOSTNET_REPO}")

function(gn_resource target name desc)
  if(WIN32 AND NOT GHOSTNET_NO_RC)   # GHOSTNET_NO_RC: cross-compile checks without a resource compiler
    set(GN_NAME "${name}")
    set(GN_DESC "${desc}")
    set(GN_VERSION "${GHOSTNET_VERSION}")
    configure_file(${CMAKE_SOURCE_DIR}/res/ghostnet.rc.in ${CMAKE_BINARY_DIR}/${name}.rc @ONLY)
    target_sources(${target} PRIVATE ${CMAKE_BINARY_DIR}/${name}.rc)
    if(MSVC)
      # MSVC's linker embeds the manifest (asInvoker) itself.
      target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/res/asinvoker.manifest)
    else()
      set_source_files_properties(${CMAKE_BINARY_DIR}/${name}.rc PROPERTIES COMPILE_DEFINITIONS GHOSTNET_RC_MANIFEST)
    endif()
  endif()
endfunction()

# ---- The game rules: pure C++17, no I/O ------------------------------------
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/core/*.cpp)
add_library(ghostnet_core STATIC ${CORE_SOURCES})
target_include_directories(ghostnet_core PUBLIC ${CMAKE_SOURCE_DIR}/src/core)
gn_warnings(ghostnet_core)

# ---- raylib (zlib licence), fetched at a pinned version and hash -------------
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(USE_EXTERNAL_GLFW "OFF" CACHE STRING "" FORCE)                 # always the bundled, pinned GLFW
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)         # GLFW on the static runtime too
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
if(GHOSTNET_HEADLESS)
  set(PLATFORM "Memory" CACHE STRING "" FORCE)
endif()
FetchContent_Declare(raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/6.2.tar.gz
  URL_HASH SHA256=df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(raylib)

# ---- Fonts (SIL OFL, see res/fonts), compiled into the exe ------------------
set(GN_FONTS Orbitron-Bold Orbitron-Black JetBrainsMono-Regular JetBrainsMono-Bold)
list(JOIN GN_FONTS "," GN_FONT_LIST)
set(GN_FONT_CPP ${CMAKE_BINARY_DIR}/gen/fonts_data.cpp)
set(GN_FONT_FILES)
foreach(f ${GN_FONTS})
  list(APPEND GN_FONT_FILES ${CMAKE_SOURCE_DIR}/res/fonts/${f}.ttf)
endforeach()
add_custom_command(OUTPUT ${GN_FONT_CPP}
  COMMAND ${CMAKE_COMMAND} -DOUT=${GN_FONT_CPP} -DDIR=${CMAKE_SOURCE_DIR}/res/fonts "-DNAMES=${GN_FONT_LIST}" -P ${CMAKE_SOURCE_DIR}/cmake/embed_fonts.cmake
  DEPENDS ${GN_FONT_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_fonts.cmake
  COMMENT "Embedding fonts")

# ---- The game: a windowed raylib front end over the core --------------------
file(GLOB GUI_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gui/*.cpp)
add_executable(GhostNet WIN32 ${GUI_SOURCES} ${GN_FONT_CPP})
target_link_libraries(GhostNet PRIVATE ghostnet_core raylib)
target_compile_definitions(GhostNet PRIVATE GHOSTNET_VERSION="${GHOSTNET_VERSION}")
if(GHOSTNET_HEADLESS)
  target_compile_definitions(GhostNet PRIVATE GHOSTNET_HEADLESS=1)
endif()
gn_warnings(GhostNet)
if(MSVC)
  target_link_options(GhostNet PRIVATE /ENTRY:mainCRTStartup)   # keep int main() in a GUI-subsystem exe
endif()
gn_resource(GhostNet GhostNet "GHOSTNET - a hacking game about owning servers")

# ---- The safe downloader / installer (Windows only) -------------------------
if(WIN32)
  add_executable(GhostNet-Setup src/installer/main.cpp)
  target_compile_definitions(GhostNet-Setup PRIVATE
    GHOSTNET_VERSION="${GHOSTNET_VERSION}"
    GHOSTNET_REPO="${GHOSTNET_REPO}"
    GHOSTNET_SIGNING_PUBKEY_HEX="${GHOSTNET_SIGNING_PUBKEY_HEX}"
    UNICODE _UNICODE)
  target_link_libraries(GhostNet-Setup PRIVATE winhttp bcrypt ole32 shell32 uuid)
  if(NOT MSVC)
    target_link_options(GhostNet-Setup PRIVATE -municode)
  endif()
  gn_warnings(GhostNet-Setup)
  gn_resource(GhostNet-Setup GhostNet-Setup "GhostNet Setup - downloads and verifies GhostNet from GitHub")
endif()

# ---- Tests ------------------------------------------------------------------
enable_testing()
add_executable(installer_logic_tests tests/installer_logic_tests.cpp)
target_include_directories(installer_logic_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/installer)
gn_warnings(installer_logic_tests)
add_test(NAME installer_logic COMMAND installer_logic_tests)

# The automated playtester plays the real rules end to end (see tests/sim_bot.cpp).
add_executable(ghostnet_sim tests/sim_bot.cpp)
target_link_libraries(ghostnet_sim PRIVATE ghostnet_core)
gn_warnings(ghostnet_sim)
add_test(NAME sim_careful COMMAND ghostnet_sim 7 12 careful)
add_test(NAME sim_chaos COMMAND ghostnet_sim 11 40 chaos)
add_test(NAME sim_subnet COMMAND ghostnet_sim 3 70 subnet)
foreach(seed 7 11 23 42)
  add_test(NAME sim_deep_${seed} COMMAND ghostnet_sim ${seed} 70 deep)
endforeach()
# Act IV: beat the deep job, open the Ledger, climb; kit trials prove the doctrines matter, and the 0.7
# Kessler trials (first visit, patience, slow, wall) prove persistent codes reward patience but not a bare deck.
foreach(seed 3 7 19)
  add_test(NAME sim_tower_${seed} COMMAND ghostnet_sim ${seed} 110 tower)
endforeach()

# The headless build can also play its own UI with scripted clicks and typing.
if(GHOSTNET_HEADLESS)
  add_test(NAME gui_autoplay COMMAND GhostNet --autoplay)
endif()
