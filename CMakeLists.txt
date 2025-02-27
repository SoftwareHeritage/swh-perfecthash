cmake_minimum_required(VERSION 3.15...3.29)
project(${SKBUILD_PROJECT_NAME}
	VERSION ${SKBUILD_PROJECT_VERSION}
       	LANGUAGES C CXX)
include(FindPkgConfig)

set(PYBIND11_FINDPYTHON ON)
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)
pkg_search_module(CMPH REQUIRED cmph)
link_directories(${CMPH_INCLUDE_DIR})

# Add a library using FindPython's tooling (pybind11 also provides a helper like
# this)
python_add_library(_shard MODULE src/_shard/bindings.cpp src/_shard/shard.c WITH_SOABI)
target_link_libraries(_shard PRIVATE pybind11::headers cmph ${CMPH_INCLUDE_DIR})

# This is passing in the version as a define just as an example
target_compile_definitions(_shard PRIVATE VERSION_INFO=${PROJECT_VERSION})

# The install directory is the output (wheel) directory
install(TARGETS _shard DESTINATION swh/shard)
