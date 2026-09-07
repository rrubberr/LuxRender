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

##############
# Find Bison #
##############

FIND_PACKAGE(BISON REQUIRED)

#############
# Find Flex #
#############

FIND_PACKAGE(FLEX REQUIRED)

##############
# Find Boost #
##############

FIND_PACKAGE(Boost CONFIG REQUIRED COMPONENTS
    thread
    program_options
    filesystem
    serialization
    iostreams
    regex
    python
)

IF(Boost_FOUND)
	MESSAGE(STATUS "Boost include directory: " ${Boost_INCLUDE_DIRS})
	include_directories(${Boost_INCLUDE_DIRS})
ENDIF(Boost_FOUND)

####################
# Find OpenImageIO #
####################

FIND_PACKAGE(OpenImageIO CONFIG REQUIRED)
if(TARGET OpenImageIO::OpenImageIO)
    get_target_property(OIIO_INC OpenImageIO::OpenImageIO INTERFACE_INCLUDE_DIRECTORIES)
    MESSAGE(STATUS "OpenImageIO include directory: ${OIIO_INC}")
endif()
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

############
# Find PNG #
############

FIND_PACKAGE(PNG CONFIG REQUIRED)
IF(PNG_INCLUDE_DIRS)
	MESSAGE(STATUS "PNG include directory: " ${PNG_INCLUDE_DIRS})
	INCLUDE_DIRECTORIES(BEFORE SYSTEM ${PNG_INCLUDE_DIRS})
ELSE(PNG_INCLUDE_DIRS)
	MESSAGE(STATUS "Warning : could not find PNG headers - building without png support")
endif()

##############
# Find Expat #
##############

FIND_PACKAGE(EXPAT REQUIRED)
IF(EXPAT_FOUND)
	MESSAGE(STATUS "Expat include directory: " ${EXPAT_INCLUDE_DIR})
	INCLUDE_DIRECTORIES(${EXPAT_INCLUDE_DIR})
endif()

#############
# Find FFTW #
#############

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
MESSAGE(STATUS "FFTW3 include directory: " ${FFTW3_INCLUDE_DIR})

################
# Find Threads #
################

FIND_PACKAGE(Threads REQUIRED)
if(TARGET Threads::Threads)
    get_target_property(THREADS_INC Threads::Threads INTERFACE_INCLUDE_DIRECTORIES)
endif()

#############
# Find TIFF #
#############

FIND_PACKAGE(TIFF CONFIG REQUIRED)
IF(TIFF_FOUND)
	MESSAGE(STATUS "TIFF include directory: " ${TIFF_INCLUDE_DIRS})
ENDIF(TIFF_FOUND)

###############
# Find Embree #
###############

FIND_PACKAGE(embree CONFIG REQUIRED)
IF(embree_FOUND)
	MESSAGE(STATUS "Embree found")
	if(TARGET embree::embree)
		get_target_property(EMBREE_INC embree::embree INTERFACE_INCLUDE_DIRECTORIES)
	endif()
	# The target is embree::embree
ELSE(embree_FOUND)
	MESSAGE(STATUS "Warning : could not find Embree headers.")
ENDIF(embree_FOUND)

############
# Find TBB #
############

IF(LUX_USE_TBB)
	FIND_PACKAGE(TBB CONFIG REQUIRED)
	IF(TBB_FOUND)
		MESSAGE(STATUS "TBB found")
	ELSE(TBB_FOUND)
		MESSAGE(STATUS "Warning : could not find TBB.")
	ENDIF(TBB_FOUND)
ENDIF()
