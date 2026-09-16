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

###################################
# Set the compiler and build type #
###################################

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CompilerSetup.cmake)

##################
# Set up folders #
##################

# This points to LuxRender/ext.
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../ext)

# This points to LuxRender/ext/build.
set(BUILD_DIR ${EXT_DIR}/build)

# This points to LuxRender/ext/build/lib.
set(LIB_DIR ${BUILD_DIR}/lib)

# This points to LuxRender/ext/build/include.
set(INC_DIR ${BUILD_DIR}/include)

#####################
# Setup the project #
#####################

PROJECT(lux2)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enables ctest for lux2corecheck.
enable_testing()

#####################
# Find Dependencies #
#####################

# Enoki is header-only.
if(NOT EXISTS ${INC_DIR}/enoki/array.h)
	message(FATAL_ERROR
		"Enoki headers not found in ${INC_DIR}/enoki - "
		"build the 'enoki' target in the top-level build first.")
endif()
INCLUDE_DIRECTORIES(BEFORE SYSTEM "${INC_DIR}")

FIND_PACKAGE(embree CONFIG REQUIRED)
IF(TARGET embree::embree)
	SET(LUX2_EMBREE_TARGET embree::embree)
ELSEIF(TARGET embree)
	SET(LUX2_EMBREE_TARGET embree)
ELSE()
	MESSAGE(FATAL_ERROR "No Embree target (embree::embree or embree) found.")
ENDIF()
MESSAGE(STATUS "lux2: Embree found (${LUX2_EMBREE_TARGET})")

FIND_PACKAGE(Threads REQUIRED)

#####################
# Configure LuxRays #
#####################

# Configure the LuxRays version header.
set(LUXRAYS_VERSION_MAJOR 2)
set(LUXRAYS_VERSION_MINOR 6)
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/luxrays/include/luxrays/cfg.h.in
	${CMAKE_BINARY_DIR}/luxrays/include/luxrays/cfg.h
	@ONLY
)

#############################
# Build luxrays2 static lib #
#############################

# Collect all luxrays source files from the snapshot.
file(GLOB_RECURSE LUXRAYS2_SRC CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/luxrays/src/*.cpp)

add_library(luxrays2 STATIC ${LUXRAYS2_SRC})
target_include_directories(luxrays2 SYSTEM PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/luxrays/include
	${CMAKE_BINARY_DIR}/luxrays/include)

# luxrays uses pthread for threading primitives.
target_link_libraries(luxrays2 PRIVATE
	Threads::Threads
)

# Guards for OpenEXR/Imath includes (luxrays color/spds headers).
if(EXISTS ${INC_DIR}/Imath)
	target_include_directories(luxrays2 SYSTEM PUBLIC ${INC_DIR}/Imath)
endif()
if(EXISTS ${INC_DIR}/OpenEXR)
	target_include_directories(luxrays2 SYSTEM PUBLIC ${INC_DIR}/OpenEXR)
endif()

######################
# Set compiler flags #
######################

# Same native/vectorization policy as src/lux.
ADD_DEFINITIONS(-march=native -ftree-vectorize -funroll-loops -DHAVE_PTHREAD_H)
add_compile_options(-w)

# Where the finished binaries are collected.
if(NOT DEFINED LUX2_RELEASE_DIR)
	set(LUX2_RELEASE_DIR ${CMAKE_BINARY_DIR})
endif()

#######################
# Build lux2 binaries #
#######################

# lux2core type headers.
set(LUX2_CORE_HEADERS
	core/lux2.h
	core/vecp.h
	core/geometry.h
	core/spectrum.h
	core/ray.h
	core/rng.h
)

# Build lux2corecheck.
ADD_EXECUTABLE(lux2corecheck
	tests/core_check.cpp
	${LUX2_CORE_HEADERS}
)

TARGET_INCLUDE_DIRECTORIES(lux2corecheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

# Run the check automatically after each build of the check target.
add_test(NAME lux2corecheck COMMAND lux2corecheck)

# lux2 foundation sources.
set(LUX2_FOUNDATION_SRC
	core/paramset.cpp
	core/context2.cpp
	core/scene.cpp
	core/dynload.cpp
)

set(LUX2_FOUNDATION_HEADERS
	core/scene.h
	core/paramset.h
	core/context2.h
	core/graphicsstate.h
	core/transform_stack.h
	core/transform.h
	core/dynload.h
)

# Build lux2foundationcheck.
ADD_EXECUTABLE(lux2foundationcheck
	tests/foundation_check.cpp
	${LUX2_FOUNDATION_SRC}
	${LUX2_FOUNDATION_HEADERS}
)

TARGET_INCLUDE_DIRECTORIES(lux2foundationcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME lux2foundationcheck COMMAND lux2foundationcheck)

# luxrays2check tests luxrays scalar types to verify core APIs work.
ADD_EXECUTABLE(luxrays2check
	tests/luxrays_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(luxrays2check PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(luxrays2check PRIVATE
	luxrays2
)
add_test(NAME luxrays2check
	COMMAND luxrays2check
	${CMAKE_CURRENT_SOURCE_DIR}/../../test/cornell_box/Scene/00001/floor_0000_m000.ply
)

ADD_EXECUTABLE(lux2console
	console/lux2console.cpp
	${LUX2_CORE_HEADERS}
)

TARGET_INCLUDE_DIRECTORIES(lux2console PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

TARGET_LINK_LIBRARIES(lux2console PRIVATE
	${LUX2_EMBREE_TARGET}
	Threads::Threads
)

SET_TARGET_PROPERTIES(lux2console PROPERTIES
	BUILD_WITH_INSTALL_RPATH TRUE
	INSTALL_RPATH "$ORIGIN"
)

# Collect the binary next to the regular LuxRender binaries.
ADD_CUSTOM_COMMAND(TARGET lux2console POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${LUX2_RELEASE_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lux2console> "${LUX2_RELEASE_DIR}/"
)
