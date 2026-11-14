cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)   # lets us pick the static MSVC runtime

project(GhostNet LANGUAGES CXX)

set(GHOSTNET_VERSION "0.1.0" CACHE STRING "Version stamped into the binaries")
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

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /D_CRT_SECURE_NO_WARNINGS)
  add_compile_options(/guard:cf /sdl)
  add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
else()
  add_compile_options(-Wall -Wextra)
  if(WIN32)
    add_link_options(-static)
  endif()
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
  if(WIN32)
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

# ---- The game (portable C++17: Windows, Linux, macOS) ------------------------
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/*.cpp)
add_executable(GhostNet ${GAME_SOURCES})
target_compile_definitions(GhostNet PRIVATE GHOSTNET_VERSION="${GHOSTNET_VERSION}")
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
  gn_resource(GhostNet-Setup GhostNet-Setup "GhostNet Setup - downloads and verifies GhostNet from GitHub")
endif()

# ---- Tests ------------------------------------------------------------------
enable_testing()
add_executable(installer_logic_tests tests/installer_logic_tests.cpp)
target_include_directories(installer_logic_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/installer)
add_test(NAME installer_logic COMMAND installer_logic_tests)
