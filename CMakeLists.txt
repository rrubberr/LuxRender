###########################################################################
#   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of LuxRender.                                       #
#                                                                         #
#   LuxRender is free software; you can redistribute it and/or modify     #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   any later version.                                                    #
#                                                                         #
#   LuxRender is distributed in the hope that it will be useful,          #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program. If not, see <http://www.gnu.org/licenses/>   #
#                                                                         #
#   This project is based on PBRT; see <http://www.pbrt.org>              #
###########################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
cmake_minimum_required(VERSION 3.25)

if(DEFINED CMAKE_GENERATOR AND NOT CMAKE_GENERATOR STREQUAL "Unix Makefiles")
	message(FATAL_ERROR
		"This project's build directory expects the 'Unix Makefiles' generator, "
		"but '${CMAKE_GENERATOR}' was requested.")
endif()

###################################
# Set the compiler and build type #
###################################

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerSetup.cmake)

##################
# Set up folders #
##################

# This points to LuxRender/ext.
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext)

# This points to LuxRender/ext/build.
set(BUILD_DIR ${EXT_DIR}/build)

# This points to LuxRender/ext/build/bin.
set(BIN_DIR ${BUILD_DIR}/bin)

# This points to LuxRender/ext/build/lib.
set(LIB_DIR ${BUILD_DIR}/lib)

# This points to LuxRender/ext/build/include.
set(INC_DIR ${BUILD_DIR}/include)

# This points to LuxRender/ext/pfx.
set(PFX_DIR ${EXT_DIR}/pfx)

#####################
# Setup the project #
#####################

PROJECT(lux)

include(ExternalProject)

#######################
# Validate Build Type #
#######################

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Release or Debug)" FORCE)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" UPPER_BUILD_TYPE)
if(NOT UPPER_BUILD_TYPE STREQUAL "RELEASE" AND NOT UPPER_BUILD_TYPE STREQUAL "DEBUG")
	message(FATAL_ERROR
		"Invalid build type '${CMAKE_BUILD_TYPE}'. "
		"Only 'Release' and 'Debug' are allowed.")
endif()

# Forwarded to the LuxRender2's build to enable/disable integrator debug.
option(LUX2_INTEGRATOR_DEBUG "Compile lux2 integrator debug instrumentation" OFF)

# Buffers = vertex arenas, film accumulators, pointer tables.
option(LUX_USE_HUGEPAGES "Back lux2 render buffers with huge pages" ON)

##############################
# Set the default build args #
##############################

set(
	COMMON_LUX_PROJECT_ARGS
    -DCMAKE_CXX_STANDARD=20
	-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE
    -DCMAKE_PREFIX_PATH=${BUILD_DIR}
)

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#######################
# Custom Clean Target #
#######################

add_custom_target(clean-all
	COMMAND ${CMAKE_COMMAND} -E remove_directory ${BUILD_DIR}
	COMMAND ${CMAKE_COMMAND} -E remove_directory ${PFX_DIR}
	COMMENT "Emptying the build and pfx directories"
)

##############
# Build Deps #
##############

add_subdirectory(ext)

####################
# Build LuxRender2 #
####################

set(LUX_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -I${INC_DIR}/Imath -I${INC_DIR}/OpenEXR")

# Where the finished binaries are collected.
set(RELEASE_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}" CACHE PATH
    "Directory the LuxRender2 binaries are copied to after the build")

ExternalProject_Add(
    LuxRender2
    DEPENDS
        embree
        enoki
        openexr
        libpng
        libjpeg-turbo
        libtiff
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/lux2
    BINARY_DIR ${PFX_DIR}/LuxRender2/build
    PREFIX ${PFX_DIR}/LuxRender2
    BUILD_ALWAYS 1
    INSTALL_DIR ${BUILD_DIR}
    CMAKE_ARGS
        ${COMMON_LUX_PROJECT_ARGS}
        -DLUX2_RELEASE_DIR=${RELEASE_DIR}
        -DLUX2_INTEGRATOR_DEBUG=${LUX2_INTEGRATOR_DEBUG}
        -DLUX_USE_HUGEPAGES=${LUX_USE_HUGEPAGES}
        -DCMAKE_FIND_ROOT_PATH=${BUILD_DIR}
        -DCMAKE_PREFIX_PATH=${BUILD_DIR}
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
    CMAKE_CACHE_ARGS
        "-DCMAKE_CXX_FLAGS:STRING=${LUX_CXX_FLAGS}"
    INSTALL_COMMAND ${CMAKE_COMMAND}
        -DLIB_DIR=${LIB_DIR}
        -DRELEASE_DIR=${RELEASE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallSharedLibs.cmake
)
