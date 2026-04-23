###########################################################################
#   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of Lux.                                             #
#                                                                         #
#   Lux is free software; you can redistribute it and/or modify           #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   Lux is distributed in the hope that it will be useful,                #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program.  If not, see <http://www.gnu.org/licenses/>. #
#                                                                         #
#   Lux website: http://www.luxrender.net                                 #
###########################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
	cmake_minimum_required(VERSION 3.10)

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

PROJECT(lux)
SET(VERSION 1.9)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

SET(LUX_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} 
	"${LUX_CMAKE_DIR}"
	"${LUX_CMAKE_DIR}/SpecializedConfig"
)
option(LUX_DOCUMENTATION "Generate project documentation" OFF)

# Don't overconfigure
SET(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "limit configs" FORCE)

# CMake needs to know which configuration to generate by default.
IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE Release)
ENDIF(NOT CMAKE_BUILD_TYPE)

MESSAGE(STATUS "Building mode: " ${CMAKE_BUILD_TYPE})
SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "assure config" FORCE) # makes sure type is shown in cmake gui

# Export symbols on Linux.
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(CMAKE_EXE_LINKER_FLAGS -Wl,-rpath,$ORIGIN,--version-script='${CMAKE_SOURCE_DIR}/cmake/exportmaps/linux_symbol_exports.map')
	set(CMAKE_SHARED_LINKER_FLAGS -Wl,-rpath,$ORIGIN,--version-script='${CMAKE_SOURCE_DIR}/cmake/exportmaps/linux_symbol_exports.map')
ENDIF()

#############################################################################
######################    FIND PROJECT DEPENDENCIES    ######################
#############################################################################

INCLUDE(Dependencies)

#############################################################################
############################    DOCUMENTATION    ############################
#############################################################################

IF(LUX_DOCUMENTATION)
	INCLUDE(documentation)
ENDIF(LUX_DOCUMENTATION)

#Generate the config.h file
CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)
ADD_DEFINITIONS(-DLUX_USE_CONFIG_H)

#############################################################################
#########################      COMPILER FLAGS     ###########################
#############################################################################

# Dade - default compiler options
# Removed "-O3" from definitions in order to have CMake Release/Debug options to work
ADD_DEFINITIONS(-ftree-vectorize -funroll-loops -Wall -fPIC -DHAVE_PTHREAD_H)

FIND_PACKAGE(AVX 2.0 EXACT)
	
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
	IF(AVX_FOUND)
		MESSAGE(STATUS "AVX2 detected. Adding compiler definitions.")
		ADD_DEFINITIONS(-mavx2 -march=haswell)
	ELSE()
		MESSAGE(STATUS "No AVX2 support. Reverting to SSE2")
		ADD_DEFINITIONS(-msse2 -mfpmath=sse -march=nocona)
	ENDIF()
# The QBVH accelerator needs to be compiled with much reduced optimizations
# otherwise gcc produces incorrect code and ruins the render on 64bits machines
SET_SOURCE_FILES_PROPERTIES(accelerators/qbvhaccel.cpp COMPILE_FLAGS "-O1")

#############################################################################
#####################            LINKER INFO           ######################
#############################################################################

LINK_DIRECTORIES(${LINK_DIRECTORIES} ${Boost_LIBRARY_DIRS} )

SET(LUX_LIBRARY luxShared)
ADD_DEFINITIONS(-DLUX_DLL) # for controlling visibility
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH "$ORIGIN")

set(LUX_DEPS
	OpenImageIO::OpenImageIO
	OpenEXR::OpenEXR
	Imath::Imath
	Boost::thread
	Boost::program_options
	Boost::filesystem
	Boost::serialization
	Boost::iostreams
	Boost::regex
	Boost::python
	TIFF::TIFF
	PNG::PNG
	JPEG::JPEG
	FFTW3::fftw3
	${SYS_LIBRARIES}
)

#############################################################################
#########################      LIBRARY BUILD      ###########################
#############################################################################

INCLUDE(liblux)

#############################################################################
############################   PYTHON LIBRARY    ############################
#############################################################################

INCLUDE(pylux)

#############################################################################
#############################    Binaries       #############################
#############################################################################

INCLUDE(luxconsole)
INCLUDE(luxmerger)
INCLUDE(luxcomp)
INCLUDE(luxrender)
