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
set(CMPH_REQUIRED_VERSION 2.0.2)

pkg_search_module(CMPH cmph=${CMPH_REQUIRED_VERSION})

if (NOT CMPH_MODULE_NAME)
  message(STATUS "cmph not found; it will be downloaded and compiled")
  include(ExternalProject)
  ExternalProject_Add(
    cmph
    URL  https://downloads.sourceforge.net/project/cmph/v${CMPH_REQUIRED_VERSION}/cmph-${CMPH_REQUIRED_VERSION}.tar.gz
    URL_HASH SHA1=143ddd4a9ba0b0dad8f0d0e573a4a3af463030c1
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/cmph-${CMPH_REQUIRED_VERSION}

    BUILD_IN_SOURCE ON
    # don't ask, don't tell...
    CONFIGURE_COMMAND autoreconf -i && ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/cmph-${CMPH_REQUIRED_VERSION} --with-pic
    BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/cmph-${CMPH_REQUIRED_VERSION}/lib/libcmph.a
  )
  ExternalProject_Get_Property(cmph install_dir)
  set(CMPH_INCLUDEDIR "${install_dir}/include")
  set(CMPH_STATIC_LIB "${install_dir}/lib/libcmph.a")
  file(MAKE_DIRECTORY ${CMPH_INCLUDEDIR})

  add_library(libcmph STATIC IMPORTED GLOBAL)
  add_dependencies(libcmph cmph)

  set_target_properties(libcmph PROPERTIES IMPORTED_LOCATION ${CMPH_STATIC_LIB})
  set_target_properties(libcmph PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMPH_INCLUDEDIR})
  set(CMPH_LIBRARIES libcmph)
endif()

message(STATUS "cmph include dir: ${CMPH_INCLUDEDIR}")
message(STATUS "cmph lib: ${CMPH_LIBRARIES}")

# Add a library using FindPython's tooling (pybind11 also provides a helper like
# this)
python_add_library(_shard MODULE src/_shard/bindings.cpp src/_shard/shard.c WITH_SOABI)
target_link_libraries(_shard PRIVATE pybind11::headers ${CMPH_LIBRARIES})

# This is passing in the version as a define just as an example
target_compile_definitions(_shard PRIVATE VERSION_INFO=${PROJECT_VERSION})

# The install directory is the output (wheel) directory
install(TARGETS _shard DESTINATION swh/shard)
