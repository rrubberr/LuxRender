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

PROJECT(lux)
SET(VERSION 1.9)

include(ExternalProject)

##################
# Set up folders #
##################

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

##############################
# Set the default build type #
##############################

# Don't over-configure!
SET(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "limit configs" FORCE)

# CMake needs to know which configuration to generate by default.
IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE Release)
ENDIF(NOT CMAKE_BUILD_TYPE)

MESSAGE(STATUS "Building mode: " ${CMAKE_BUILD_TYPE})
SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "assure config" FORCE) # makes sure type is shown in cmake gui

##############################
# Set the default build args #
##############################

set(
	COMMON_LUX_PROJECT_ARGS
    -DCMAKE_CXX_STANDARD=17
	-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE
    -DCMAKE_PREFIX_PATH=${LUX_BUILD_DIR}
    -DBoost_DIR=${LUX_LIB_DIR}/cmake/Boost-1.90.0
    -DBoost_LIBRARY_DIRS=${LUX_LIB_DIR}
    -DBoost_INCLUDE_DIRS=${LUX_INC_DIR}
    -DBoost_NO_SYSTEM_PATHS=ON
    -DBoost_USE_STATIC_LIBS=ON
)

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#########################
# Find XQuartz on Apple #
#########################

if(APPLE)
    find_package(X11 REQUIRED)
    if(X11_FOUND)
        # Format every path in the list with -I.
        set(APPLE_X11_FLAGS "")
        foreach(inc_path ${X11_INCLUDE_DIR})
            set(APPLE_X11_FLAGS "${APPLE_X11_FLAGS} -I${inc_path}")
        endforeach()
        
        # Format every path in the list with -L.
        set(APPLE_X11_LINKER_FLAGS "")
        foreach(lib_path ${X11_LIB_DIR})
            set(APPLE_X11_LINKER_FLAGS "${APPLE_X11_LINKER_FLAGS} -L${lib_path}")
        endforeach()
    endif()
endif()


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
        ${COMMON_LUX_PROJECT_ARGS}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -DBOOST_BIND_GLOBAL_PLACEHOLDERS -w"
    INSTALL_COMMAND
        /bin/sh -c "cp -av ${LUX_PFX_DIR}/luxrays/src/LuxRays-build/lib/. ${LUX_LIB_DIR}/ && cp -av ${CMAKE_SOURCE_DIR}/src/luxrays/include/luxrays ${LUX_INC_DIR}/"
)

###################
# Build LuxRender #
###################

set(LUX_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${APPLE_X11_FLAGS} -DBOOST_BIND_GLOBAL_PLACEHOLDERS -w -I${LUX_INC_DIR}/Imath -I${LUX_INC_DIR}/OpenEXR")

if(APPLE)
    # Automatically find the Homebrew Qt path
    execute_process(COMMAND brew --prefix qt@6 OUTPUT_VARIABLE QT_BREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(MAC_DEPLOY_QT "${QT_BREW_PREFIX}/bin/macdeployqt")
endif()

# Define the command based on the platform.
if(APPLE)
    set(LUX_INSTALL_CMD
        # 1. Clean and Create Folders
        ${CMAKE_COMMAND} -E rm -rf "${CMAKE_SOURCE_DIR}/release/LuxRender.app" &&
        ${CMAKE_COMMAND} -E copy_directory "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxrender.app" "${CMAKE_SOURCE_DIR}/release/LuxRender.app" &&
        ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ext/build/lib/png.framework" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS/png.framework" &&
        ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS" &&
        ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/Resources" &&
        
        # 2. Copy EVERYTHING into the MacOS folder FIRST
        # This ensures otool-classic (used by macdeployqt) actually finds the files
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxconsole" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS/" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxmerger" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS/" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxcomp" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS/" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/liblux.dylib" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS/" &&
        
        # 3. Copy Icons
        ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/lux/qtgui/icons/macos/luxrender.icns" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/Resources/" &&
        ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/lux/qtgui/icons/macos/luxscene.icns" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/Resources/" &&
        ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/lux/qtgui/icons/macos/luxfilm.icns" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/Resources/" &&
        ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/lux/qtgui/icons/macos/luxqueue.icns" "${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/Resources/" &&

        # 4. Run Deploy Tool
        # We add -always-overwrite to handle any stubborn symlinks
        "${MAC_DEPLOY_QT}" "${CMAKE_SOURCE_DIR}/release/LuxRender.app"
            -libpath="${CMAKE_SOURCE_DIR}/ext/build/lib"
            -libpath="${LUX_PFX_DIR}/LuxRender/src/LuxRender-build" 
            -executable="${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS/luxconsole" 
            -executable="${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS/luxmerger" 
            -executable="${CMAKE_SOURCE_DIR}/release/LuxRender.app/Contents/MacOS/luxcomp"
            -verbose=2
    )
else()
    set(LUX_INSTALL_CMD
        ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/release" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxcomp" "${CMAKE_SOURCE_DIR}/release/" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxconsole" "${CMAKE_SOURCE_DIR}/release/" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxmerger" "${CMAKE_SOURCE_DIR}/release/" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/luxrender" "${CMAKE_SOURCE_DIR}/release/" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/pylux.so" "${CMAKE_SOURCE_DIR}/release/" &&
        ${CMAKE_COMMAND} -E copy "${LUX_PFX_DIR}/LuxRender/src/LuxRender-build/liblux.so" "${CMAKE_SOURCE_DIR}/release/"
    )
endif()

ExternalProject_Add(
    LuxRender
    DEPENDS
        LuxRays
        boost
        OpenImageIO
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/lux
    PREFIX ${LUX_PFX_DIR}/LuxRender
    BUILD_ALWAYS 1
    INSTALL_DIR ${LUX_BUILD_DIR}
    CMAKE_ARGS
        ${COMMON_LUX_PROJECT_ARGS}
        -DCMAKE_PREFIX_PATH=${LUX_BUILD_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
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
    CMAKE_CACHE_ARGS
        "-DCMAKE_CXX_FLAGS:STRING=${LUX_CXX_FLAGS}"
    INSTALL_COMMAND ${LUX_INSTALL_CMD}
)
