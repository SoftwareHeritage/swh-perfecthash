cmake_minimum_required(VERSION 3.15...3.29)
project(${SKBUILD_PROJECT_NAME}
	VERSION ${SKBUILD_PROJECT_VERSION}
       	LANGUAGES C CXX)
include(FindPkgConfig)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PYBIND11_FINDPYTHON ON)
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)
pkg_search_module(CMPH cmph)

if (NOT CMPH_MODULE_NAME)
  find_path(CMPH_INCLUDEDIR cmph.h
    HINTS
      cmph/include
  )
  find_library(CMPH_LIBRARY NAMES cmph libcmph
    HINTS
      cmph/lib
  )
endif()

if (NOT CMPH_INCLUDEDIR)
   message(FATAL_ERROR "cmph not found")
else()
   message(STATUS "cmph include dir: ${CMPH_INCLUDEDIR}")
   message(STATUS "cmph lib: ${CMPH_LDFLAGS}")
endif()

include_directories(${CMPH_INCLUDEDIR})

# Add a library using FindPython's tooling (pybind11 also provides a helper like
# this)
python_add_library(_shard MODULE src/_shard/bindings.cpp src/_shard/shard.c WITH_SOABI)
target_link_libraries(_shard PRIVATE pybind11::headers ${CMPH_LDFLAGS})

# This is passing in the version as a define just as an example
target_compile_definitions(_shard PRIVATE VERSION_INFO=${PROJECT_VERSION})

# The install directory is the output (wheel) directory
install(TARGETS _shard DESTINATION swh/shard)
