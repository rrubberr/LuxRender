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

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

SET(LUX_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} 
	"${LUX_CMAKE_DIR}"
	"${LUX_CMAKE_DIR}/SpecializedConfig"
)
option(LUX_DOCUMENTATION "Generate project documentation" OFF)
option(LUX_USE_TBB "Use Intel TBB instead of Boost.Thread for the render path" ON)
IF(LUX_USE_TBB)
	ADD_DEFINITIONS(-DLUX_USE_TBB)
ENDIF()


############################
# Use LTO on Clang configs #
############################

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   OR CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
    message(STATUS "Enabling ThinLTO (-flto=thin)")
    add_compile_options(-flto=thin)
    add_link_options(-flto=thin)
endif()

#####################
# Find Dependencies #
#####################

INCLUDE(Dependencies)

##########################
# Generate documentation #
##########################

IF(LUX_DOCUMENTATION)
	INCLUDE(documentation)
ENDIF(LUX_DOCUMENTATION)

#Generate the config.h file
CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)
ADD_DEFINITIONS(-DLUX_USE_CONFIG_H)

#####################
# Configure LuxRays #
#####################

# Configure the LuxRays version header.
SET(LUXRAYS_VERSION_MAJOR 1)
SET(LUXRAYS_VERSION_MINOR 9)
CONFIGURE_FILE(
	"${CMAKE_SOURCE_DIR}/luxrays/include/luxrays/cfg.h.in"
	"${CMAKE_BINARY_DIR}/luxrays/include/luxrays/cfg.h"
	@ONLY
)

# LuxRays headers are included via the "luxrays/..." prefix.
INCLUDE_DIRECTORIES(BEFORE SYSTEM "${CMAKE_SOURCE_DIR}/luxrays/include")
# The generated cfg.h lives in the build tree.
INCLUDE_DIRECTORIES(BEFORE SYSTEM "${CMAKE_BINARY_DIR}/luxrays/include")

####################
# Set linker flags #
####################

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-Wl,--ignore-data-address-equality")
check_cxx_source_compiles("int main() { return 0; }"
	HAVE_LINKER_IGNORE_DATA_EQ)
set(CMAKE_REQUIRED_FLAGS "")
if(HAVE_LINKER_IGNORE_DATA_EQ)
	add_link_options("-Wl,--ignore-data-address-equality")
endif()


ADD_DEFINITIONS(-DLUX_DLL) # for controlling visibility

# Control how the linker handles relative paths
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

set(CMAKE_INSTALL_RPATH "$ORIGIN")

######################
# Set compiler flags #
######################

# Dade - default compiler options.
# Removed "-O3" from definitions in order to have CMake Release/Debug options to work.
ADD_DEFINITIONS(-march=native -ftree-vectorize -funroll-loops -DHAVE_PTHREAD_H)
add_compile_options(-w)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")

#############################
# Build LuxRender libraries #
#############################

INCLUDE(liblux)
INCLUDE(pylux)

############################
# Build LuxRender binaries #
############################

INCLUDE(luxconsole)
INCLUDE(luxmerger)
INCLUDE(luxcomp)
INCLUDE(luxrender)
