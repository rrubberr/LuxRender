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

include(FindPkgMacros)
getenv_path(LuxRays_DEPENDENCIES_DIR)

################################################################################
#
# Core dependencies
#
################################################################################

# Find threading library
find_package(Threads REQUIRED)

if(NOT APPLE)
    # Apple has these available hardcoded and matched in macos repo, see Config_OSX.cmake

    find_package(TIFF REQUIRED)
    include_directories(BEFORE SYSTEM ${TIFF_INCLUDE_DIR})
    find_package(JPEG REQUIRED)
    include_directories(BEFORE SYSTEM ${JPEG_INCLUDE_DIR})
    find_package(PNG REQUIRED)
    include_directories(BEFORE SYSTEM ${PNG_PNG_INCLUDE_DIR})
	# Find Python Libraries
	find_package(PythonLibs)
endif()

include_directories(${PYTHON_INCLUDE_DIRS})

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

include_directories(${Boost_INCLUDE_DIRS})

# OpenMP
if(NOT APPLE)
	find_package(OpenMP)
	if (OPENMP_FOUND)
		MESSAGE(STATUS "OpenMP found - compiling with")
   		set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
   		set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
	else()
		MESSAGE(WARNING "OpenMP not found - compiling without")
	endif()
endif()

# Find BISON
IF (NOT BISON_NOT_AVAILABLE)
	find_package(BISON)
	IF (NOT BISON_FOUND)
		MESSAGE(WARNING "bison not found - try compilation using already generated files")
		SET(BISON_NOT_AVAILABLE 1)
	ENDIF (NOT BISON_FOUND)
ENDIF (NOT BISON_NOT_AVAILABLE)

# Find FLEX
IF (NOT FLEX_NOT_AVAILABLE)
	find_package(FLEX)
	IF (NOT FLEX_FOUND)
		MESSAGE(WARNING "flex not found - try compilation using already generated files")
		SET(FLEX_NOT_AVAILABLE 1)
	ENDIF (NOT FLEX_FOUND)
ENDIF (NOT FLEX_NOT_AVAILABLE)
