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

#############################################################################
##########################      Find LuxRays       ##########################
#############################################################################

FIND_PATH(LUXRAYS_INCLUDE_DIRS NAMES luxrays/luxrays.h PATHS ../luxrays/include ${LuxRays_HOME}/include )
FIND_LIBRARY(LUXRAYS_LIBRARY luxrays PATHS ../luxrays/lib ${LuxRays_HOME}/lib PATH_SUFFIXES "" release relwithdebinfo minsizerel dist )


IF (LUXRAYS_INCLUDE_DIRS AND LUXRAYS_LIBRARY)
	MESSAGE(STATUS "LuxRays include directory: " ${LUXRAYS_INCLUDE_DIRS})
	MESSAGE(STATUS "LuxRays library: " ${LUXRAYS_LIBRARY})
	INCLUDE_DIRECTORIES(SYSTEM ${LUXRAYS_INCLUDE_DIRS})
	
	# Create CMake target for LuxRays
	if(NOT TARGET luxrays)
		add_library(luxrays UNKNOWN IMPORTED)
		set_target_properties(luxrays PROPERTIES
			IMPORTED_LOCATION "${LUXRAYS_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${LUXRAYS_INCLUDE_DIRS}"
		)
	endif()
	
ELSE (LUXRAYS_INCLUDE_DIRS AND LUXRAYS_LIBRARY)
	MESSAGE(FATAL_ERROR "LuxRays not found.")
ENDIF (LUXRAYS_INCLUDE_DIRS AND LUXRAYS_LIBRARY)

#############################################################################
###########################      Find OpenMP       ##########################
#############################################################################

FIND_PACKAGE(OpenMP)
IF (OPENMP_FOUND)
	MESSAGE(STATUS "OpenMP found - compiling with")
	SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
	SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
ELSE(OPENMP_FOUND)
	MESSAGE(WARNING "OpenMP not found - compiling without")
endif(OPENMP_FOUND)

#############################################################################
###########################      Find BISON       ###########################
#############################################################################

IF (NOT BISON_NOT_AVAILABLE)
	FIND_PACKAGE(BISON REQUIRED)
	IF (NOT BISON_FOUND)
		MESSAGE(FATAL_ERROR "bison not found - aborting")
	ENDIF (NOT BISON_FOUND)
ENDIF (NOT BISON_NOT_AVAILABLE)

#############################################################################
###########################      Find FLEX        ###########################
#############################################################################

IF (NOT FLEX_NOT_AVAILABLE)
	FIND_PACKAGE(FLEX REQUIRED)
	IF (NOT FLEX_FOUND)
		MESSAGE(FATAL_ERROR "flex not found - aborting")
	ENDIF (NOT FLEX_FOUND)
ENDIF (NOT FLEX_NOT_AVAILABLE)

#############################################################################
########################### BOOST LIBRARIES SETUP ###########################
#############################################################################

find_package(Boost REQUIRED COMPONENTS
    thread
    program_options
    filesystem
    serialization
    iostreams
    regex
    python
)

#############################################################################
##########################   OPENIMAGEIO LIBRARIES    #######################
#############################################################################

find_package(OpenImageIO CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
find_package(Imath CONFIG REQUIRED)


#############################################################################
########################### PNG   LIBRARIES SETUP ###########################
#############################################################################

FIND_PACKAGE(PNG)
IF(PNG_INCLUDE_DIRS)
	MESSAGE(STATUS "PNG include directory: " ${PNG_INCLUDE_DIRS})
	INCLUDE_DIRECTORIES(BEFORE SYSTEM ${PNG_INCLUDE_DIRS})
ELSE(PNG_INCLUDE_DIRS)
	MESSAGE(STATUS "Warning : could not find PNG headers - building without png support")
ENDIF(PNG_INCLUDE_DIRS)

#############################################################################
########################### FFTW  LIBRARIES SETUP ###########################
#############################################################################

if(NOT TARGET FFTW3::fftw3)
    find_path(FFTW3_INCLUDE_DIR fftw3.h)
    find_library(FFTW3_LIBRARY fftw3)

    if(NOT FFTW3_INCLUDE_DIR OR NOT FFTW3_LIBRARY)
        message(FATAL_ERROR "FFTW3 not found")
    endif()

    add_library(FFTW3::fftw3 UNKNOWN IMPORTED)
    set_target_properties(FFTW3::fftw3 PROPERTIES
        IMPORTED_LOCATION "${FFTW3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FFTW3_INCLUDE_DIR}"
    )
endif()

#############################################################################
############################ THREADING LIBRARIES ############################
#############################################################################

find_package(Threads REQUIRED)

#############################################################################
############################ FIX TIFF CMATH #################################
#############################################################################

find_package(TIFF REQUIRED)
add_library(CMath::CMath INTERFACE IMPORTED GLOBAL)
target_link_libraries(CMath::CMath INTERFACE m)

#############################################################################
############################ NO OPENCL FOR US ###############################
#############################################################################

ADD_DEFINITIONS("-DLUXRAYS_DISABLE_OPENCL")
