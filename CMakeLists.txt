###########################################################################
#   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of LuxRender.                                       #
#                                                                         #
#   LuxRender is free software; you can redistribute it and/or modify     #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   LuxRender is distributed in the hope that it will be useful,          #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program.  If not, see <http://www.gnu.org/licenses/>. #
#                                                                         #
#   LuxRender website: https://github.com/rrubberr/LuxRender              #
###########################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
	cmake_minimum_required(VERSION 3.10)

PROJECT(lux)
SET(VERSION 1.9)

include(ExternalProject)

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

# This points to LuxRender/ext.
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext)

# This points to LuxRender/ext/build.
set(LUX_BUILD_DIR ${EXT_DIR}/build)

# This points to LuxRender/ext/build/bin.
set(LUX_BIN_DIR ${LUX_BUILD_DIR}/bin)

# This points to LuxRender/ext/build/lib.
set(LUX_LIB_DIR ${LUX_BUILD_DIR}/lib)

# This points to LuxRender/ext/build/include.
set(LUX_INC_DIR ${LUX_BUILD_DIR}/include)

#This points to LuxRender/ext/build/pfx.
set(LUX_PFX_DIR ${LUX_BUILD_DIR}/pfx)

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Don't over-configure!
SET(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "limit configs" FORCE)


##############
# Build Deps #
##############

add_subdirectory(ext)

#################
# Build LuxRays #
#################

ExternalProject_Add(
    LuxRays
    DEPENDS
        boost
        OpenImageIO
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/luxrays
    PREFIX ${LUX_PFX_DIR}/luxrays
    INSTALL_DIR ${LUX_BUILD_DIR}
    CMAKE_ARGS
        -DCMAKE_PREFIX_PATH=${LUX_BUILD_DIR}
        -DCMAKE_CXX_STANDARD=14
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        "-DCMAKE_CXX_FLAGS=-DBOOST_BIND_GLOBAL_PLACEHOLDERS -w"
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBoost_DIR=${LUX_LIB_DIR}/cmake/Boost-1.90.0
        -DBoost_LIBRARY_DIRS=${LUX_LIB_DIR}
        -DBoost_INCLUDE_DIRS=${LUX_INC_DIR}
        -DBoost_NO_SYSTEM_PATHS=ON
        -DBoost_USE_STATIC_LIBS=ON
    INSTALL_COMMAND
        /bin/sh -c "cp -av ${LUX_PFX_DIR}/luxrays/src/LuxRays-build/lib/. ${LUX_LIB_DIR}/ && cp -av ${CMAKE_SOURCE_DIR}/src/luxrays/include/luxrays ${LUX_INC_DIR}/"
)

###################
# Build LuxRender #
###################

ExternalProject_Add(
    LuxRender
    DEPENDS
        LuxRays
        boost
        OpenImageIO
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/lux
    PREFIX ${LUX_PFX_DIR}/LuxRender
    INSTALL_DIR ${LUX_BUILD_DIR}
    CMAKE_ARGS
        -DCMAKE_PREFIX_PATH=${LUX_BUILD_DIR}
        -DCMAKE_CXX_STANDARD=14
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -DBOOST_BIND_GLOBAL_PLACEHOLDERS -w"
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBoost_DIR=${LUX_LIB_DIR}/cmake/Boost-1.90.0
        -DBoost_LIBRARY_DIRS=${LUX_LIB_DIR}
        -DBoost_INCLUDE_DIRS=${LUX_INC_DIR}
        -DBoost_NO_SYSTEM_PATHS=ON
        -DBoost_USE_STATIC_LIBS=ON
        "-DCMAKE_EXE_LINKER_FLAGS=-static"
        "-DCMAKE_FIND_LIBRARY_SUFFIXES=.a"
        "-DTIFF_LIBRARY=${LUX_LIB_DIR}/libtiff.a"
        "-DJPEG_LIBRARY=${LUX_LIB_DIR}/libjpeg.a"
        "-DPNG_LIBRARY=${LUX_LIB_DIR}/libpng16.a"
        "-Dminizip-ng_LIBRARY=${LUX_LIB_DIR}/libminizip.a"
        -DJPEG_INCLUDE_DIR=${LUX_INC_DIR}
        -DLuxRays_HOME=${LUX_BUILD_DIR}
        -DPython3_ROOT_DIR=${LUX_BUILD_DIR}
        -DPython3_FIND_STRATEGY=LOCATION
        -DPython3_FIND_REGISTRY=NEVER
        -DPython3_FIND_FRAMEWORK=NEVER
        "-DCMAKE_EXE_LINKER_FLAGS=-L${LUX_LIB_DIR} -lpystring"
        "-DCMAKE_SHARED_LINKER_FLAGS=-L${LUX_LIB_DIR} -lpystring"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/release &&
        ${CMAKE_COMMAND} -E copy ${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxcomp ${CMAKE_SOURCE_DIR}/release/ &&
        ${CMAKE_COMMAND} -E copy ${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxconsole ${CMAKE_SOURCE_DIR}/release/ &&
        ${CMAKE_COMMAND} -E copy ${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxmerger ${CMAKE_SOURCE_DIR}/release/ &&
        ${CMAKE_COMMAND} -E copy ${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxrender ${CMAKE_SOURCE_DIR}/release/ &&
        ${CMAKE_COMMAND} -E copy ${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/pylux.so ${CMAKE_SOURCE_DIR}/release/ &&
        ${CMAKE_COMMAND} -E copy ${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/liblux.so ${CMAKE_SOURCE_DIR}/release/
)
