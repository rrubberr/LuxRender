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
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Enables ctest for lux2corecheck.
enable_testing()

#####################
# Find Dependencies #
#####################

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

######################
# Set compiler flags #
######################

ADD_DEFINITIONS(-march=native -ftree-vectorize -funroll-loops -DHAVE_PTHREAD_H)
add_compile_options(-w)

# Where the finished binaries are collected.
if(NOT DEFINED LUX2_RELEASE_DIR)
	set(LUX2_RELEASE_DIR ${CMAKE_BINARY_DIR})
endif()

####################
# Build liblux2.so #
####################

file(GLOB LUX2_CORE_SRC CONFIGURE_DEPENDS
	core/*.cpp
	shapes/*.cpp
	textures/*.cpp
	materials/*.cpp
	samplers/*.cpp
	filters/*.cpp
	cameras/*.cpp
	lights/*.cpp
	film/*.cpp
	integrators/*.cpp
	tonemaps/*.cpp
)

# Bison/Flex generate these at build time.
list(APPEND LUX2_CORE_SRC
	${BISON_Lux2Parser_OUTPUTS}
	${FLEX_Lux2Lexer_OUTPUTS}
)

ADD_LIBRARY(lux2 SHARED
	${LUX2_CORE_SRC}
)

TARGET_INCLUDE_DIRECTORIES(lux2 PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
)

# Share the libtbb used by Embree.
TARGET_LINK_LIBRARIES(lux2 PUBLIC
	${LUX2_EMBREE_TARGET} TBB::tbb
	OpenEXR::OpenEXR OpenEXR::Iex Imath::Imath PNG::PNG
	libjpeg-turbo::jpeg-static TIFF::tiff)

###############
# Build tests #
###############

# Test check executables (defined in tests/CMakeLists.txt).
add_subdirectory(tests)


#####################
# Build lux2console #
#####################

ADD_EXECUTABLE(lux2console
	console/lux2console.cpp
)

TARGET_INCLUDE_DIRECTORIES(lux2console PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

TARGET_LINK_LIBRARIES(lux2console PRIVATE
	lux2
	${LUX2_EMBREE_TARGET}
	Threads::Threads
)

SET_TARGET_PROPERTIES(lux2console PROPERTIES
	BUILD_WITH_INSTALL_RPATH TRUE
	INSTALL_RPATH "$ORIGIN"
)

####################
# Collect binaries #
####################

ADD_CUSTOM_COMMAND(TARGET lux2console POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${LUX2_RELEASE_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lux2console> "${LUX2_RELEASE_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lux2> "${LUX2_RELEASE_DIR}/"
)
