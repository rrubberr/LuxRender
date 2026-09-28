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

FIND_PACKAGE(TBB CONFIG REQUIRED)
IF(NOT TARGET TBB::tbb)
	MESSAGE(FATAL_ERROR "TBB::tbb target not found - build the 'tbb' ext target first.")
ENDIF()
MESSAGE(STATUS "lux2: TBB found (TBB::tbb)")

FIND_PACKAGE(Threads REQUIRED)

FIND_PACKAGE(OpenEXR CONFIG REQUIRED)
if(TARGET OpenEXR::OpenEXR)
	get_target_property(EXR_INC OpenEXR::OpenEXR INTERFACE_INCLUDE_DIRECTORIES)
	MESSAGE(STATUS "OpenEXR include directory: ${EXR_INC}")
endif()
FIND_PACKAGE(Imath CONFIG REQUIRED)
if(TARGET Imath::Imath)
	get_target_property(IMATH_INC Imath::Imath INTERFACE_INCLUDE_DIRECTORIES)
	MESSAGE(STATUS "Imath include directory: ${IMATH_INC}")
endif()

FIND_PACKAGE(PNG CONFIG REQUIRED)
IF(PNG_INCLUDE_DIRS)
	MESSAGE(STATUS "PNG include directory: " ${PNG_INCLUDE_DIRS})
	INCLUDE_DIRECTORIES(BEFORE SYSTEM ${PNG_INCLUDE_DIRS})
ELSE(PNG_INCLUDE_DIRS)
	MESSAGE(STATUS "Warning : could not find PNG headers - building without png support")
endif()

FIND_PACKAGE(libjpeg-turbo CONFIG REQUIRED)
MESSAGE(STATUS "lux2: libjpeg-turbo found (libjpeg-turbo::jpeg-static)")

FIND_PACKAGE(TIFF CONFIG REQUIRED)
IF(TIFF_INCLUDE_DIRS)
	MESSAGE(STATUS "TIFF include directory: " ${TIFF_INCLUDE_DIRS})
	INCLUDE_DIRECTORIES(BEFORE SYSTEM ${TIFF_INCLUDE_DIRS})
ENDIF()
MESSAGE(STATUS "lux2: TIFF found (TIFF::tiff)")

# Bison/Flex for the .lxs parser.
FIND_PACKAGE(BISON REQUIRED)
FIND_PACKAGE(FLEX REQUIRED)

BISON_TARGET(Lux2Parser ${CMAKE_CURRENT_SOURCE_DIR}/core/luxparse.y
	${CMAKE_CURRENT_BINARY_DIR}/luxparse.cpp)
FLEX_TARGET(Lux2Lexer ${CMAKE_CURRENT_SOURCE_DIR}/core/luxlex.l
	${CMAKE_CURRENT_BINARY_DIR}/luxlex.cpp)
ADD_FLEX_BISON_DEPENDENCY(Lux2Lexer Lux2Parser)
