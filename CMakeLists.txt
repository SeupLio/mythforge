cmake_minimum_required(VERSION 3.20)
project(MythForge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(MYTHFORGE_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(MYTHFORGE_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "MYTHFORGE_SANITIZE is only supported by GCC/Clang")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

add_subdirectory(native)
