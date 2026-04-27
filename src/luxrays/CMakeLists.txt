################################################################################
# Copyright 1998-2015 by authors (see AUTHORS.txt)
#
#   This file is part of LuxRender.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
	cmake_minimum_required(VERSION 3.10)

#############################
# Set the compiler on macOS #
#############################

if(APPLE)
    # 1. Use the system Clang
    find_program(CLANG_CC NAMES clang)
    find_program(CLANG_CXX NAMES clang++)
    
    set(CMAKE_C_COMPILER ${CLANG_CC})
    set(CMAKE_CXX_COMPILER ${CLANG_CXX})
    message(STATUS "Using Apple Clang: ${CMAKE_C_COMPILER}")

    execute_process(COMMAND sysctl -n hw.ncpu OUTPUT_VARIABLE NPROC_VALUE OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
    set(NPROC_VALUE "$(nproc)")
endif()

##########################
# Set the CMake policies #
##########################

IF(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
    cmake_policy(SET CMP0074 NEW)
	cmake_policy(SET CMP0111 OLD)
    cmake_policy(SET CMP0144 OLD)
	cmake_policy(SET CMP0148 OLD)
	if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
    endif()
ENDIF(COMMAND cmake_policy)

#####################
# Setup the project #
#####################

project(LuxRays)

##############################
# Set the default build type #
##############################

# Don't overconfigure.
SET(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "limit configs" FORCE)

# CMake needs to know which configuration to generate by default.
IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE Release)
ENDIF(NOT CMAKE_BUILD_TYPE)

MESSAGE(STATUS "Building mode: " ${CMAKE_BUILD_TYPE})
SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "assure config" FORCE) # makes sure type is shown in cmake gui


################################################################################
#
# Include necessary submodules
#
################################################################################

set(CMAKE_MODULE_PATH
	"${LuxRays_SOURCE_DIR}"
	"${LuxRays_SOURCE_DIR}/cmake"
	"${LuxRays_SOURCE_DIR}/cmake/Utils"
	"${LuxRays_SOURCE_DIR}/cmake/Packages"
	"${LuxRays_SOURCE_DIR}/cmake/SpecializedConfig"
)

# CMake needs to know which configuration to generate by default.
IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE Release)
ENDIF(NOT CMAKE_BUILD_TYPE)

SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "assure config" FORCE) # makes sure type is shown in cmake gui

message(STATUS "Building mode: " ${CMAKE_BUILD_TYPE})

INCLUDE(PlatformSpecific)
INCLUDE(Configuration)
INCLUDE(KernelPreprocess)

# Install CMake modules
#add_subdirectory(CMake)

SET(LuxRays_INCLUDE_DIR "${LuxRays_SOURCE_DIR}/include")
include_directories("${LuxRays_INCLUDE_DIR}")

# Find dependencies
include(Dependencies)

if (NOT Boost_FOUND)
	MESSAGE(FATAL_ERROR "--> Could not locate required Boost files - Please check ${BOOST_SEARCH_PATH}")
endif()

################################################################################
#
# Build options
#
################################################################################

ADD_DEFINITIONS("-DLUXRAYS_DISABLE_OPENCL")

################################################################################
#
# LuxRays libraries
#
################################################################################

add_subdirectory(src/luxrays)

################################################################################
#
# For non win32 we'll have to copy everything to a single dir
#
################################################################################

INCLUDE(AssembleBinDirs)
